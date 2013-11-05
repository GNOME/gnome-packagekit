/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-string.h"

#include "gpk-animated-icon.h"

G_DEFINE_TYPE (GpkAnimatedIcon, gpk_animated_icon, GTK_TYPE_IMAGE)

static gpointer parent_class = NULL;

/**
 * gpk_animated_icon_free_pixbufs:
 **/
static gboolean
gpk_animated_icon_free_pixbufs (GpkAnimatedIcon *icon)
{
	guint i;

	g_return_val_if_fail (GPK_IS_ANIMATED_ICON (icon), FALSE);

	/* none loaded */
	if (icon->frames == NULL) {
		g_debug ("nothing to free");
		return FALSE;
	}

	/* free each frame */
	for (i=0; i<icon->number_frames; i++)
		g_object_unref (icon->frames[i]);
	g_free (icon->frames);
	icon->frames = NULL;
	return TRUE;
}

/**
 * gpk_animated_icon_set_icon_name:
 **/
gboolean
gpk_animated_icon_set_icon_name (GpkAnimatedIcon *icon, GtkIconSize size, const gchar *name)
{
	g_return_val_if_fail (GPK_IS_ANIMATED_ICON (icon), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	/* stop existing animation */
	gpk_animated_icon_enable_animation (icon, FALSE);

	/* set static image */
	gtk_image_set_from_icon_name (GTK_IMAGE (icon), name, size);
	return TRUE;
}

/**
 * gpk_animated_icon_set_filename_tile:
 **/
gboolean
gpk_animated_icon_set_filename_tile (GpkAnimatedIcon *icon, GtkIconSize size, const gchar *name)
{
	gboolean ret;
	gint w, h;
	gint rows, cols;
	gint r, c, i;
	GdkPixbuf *pixbuf;

	g_return_val_if_fail (GPK_IS_ANIMATED_ICON (icon), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	/* have we already set the same icon */
	if (g_strcmp0 (icon->filename, name) == 0) {
		g_debug ("already set the same icon name %s, ignoring", name);
		return FALSE;
	}

	/* stop existing animation */
	gpk_animated_icon_enable_animation (icon, FALSE);

	/* save new value */
	g_free (icon->filename);
	icon->filename = g_strdup (name);

	/* do we need to unload */
	if (icon->frames != NULL) {
		gpk_animated_icon_free_pixbufs (icon);
	}

	g_debug ("loading from %s", name);
	ret = gtk_icon_size_lookup (size, &w, &h);
	if (!ret) {
		g_warning ("Can't get icon size info");
		return FALSE;
	}

	pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), name, w, 0, NULL);
	/* can't load from gnome-icon-theme */
	if (pixbuf == NULL) {
		g_debug ("can't load animation %s", name);
		return FALSE;
	}

	/* silly hack until we get some metadata */
	if (g_strcmp0 (name, "process-working") == 0) {
		w = gdk_pixbuf_get_width (pixbuf) / 8;
		h = gdk_pixbuf_get_height (pixbuf) / 4;
	}

	/* we failed to get a valid pixbuf */
	if (w == 0 || h == 0) {
		g_object_unref (pixbuf);
		return FALSE;
	}

	cols = gdk_pixbuf_get_width (pixbuf) / w;
	rows = gdk_pixbuf_get_height (pixbuf) / h;

	icon->frame_counter = 0;
	icon->number_frames = rows * cols;
	icon->frames = g_new (GdkPixbuf*, icon->number_frames);

	for (i = 0, r = 0; r < rows; r++)
		for (c = 0; c < cols; c++, i++) {
		icon->frames[i] = gdk_pixbuf_new_subpixbuf (pixbuf, c * w, r * h, w, h);
	}

	g_object_unref (pixbuf);

	/* enable new animation */
	gpk_animated_icon_enable_animation (icon, TRUE);

	return TRUE;
}

/**
 * gpk_animated_icon_visible_notify_cb:
 **/
static void
gpk_animated_icon_visible_notify_cb (GObject *object, GParamSpec *param, GpkAnimatedIcon *icon)
{
	gboolean ret;
	g_object_get (object, "visible", &ret, NULL);
	if (!ret && icon->animation_id != 0) {
		g_debug ("disabling animation as hidden");
		gpk_animated_icon_enable_animation (icon, FALSE);
	}
}

/**
 * gpk_animated_icon_update:
 **/
static gboolean
gpk_animated_icon_update (GpkAnimatedIcon *icon)
{
	static guint rate_limit = 0;

	/* debug so we can catch polling */
	if (rate_limit++ % 20 == 0)
		g_debug ("polling check");

	/* have we loaded a file */
	if (icon->frames == NULL) {
		g_warning ("no frames to process");
		icon->animation_id = 0;
		return FALSE;
	}

	/* set new */
	gtk_image_set_from_pixbuf (GTK_IMAGE (icon), icon->frames[icon->frame_counter]);

	/* advance counter, wrapping around */
	icon->frame_counter = (icon->frame_counter + 1) % icon->number_frames;

	return TRUE;
}

/**
 * gpk_animated_icon_set_frame_delay:
 **/
gboolean
gpk_animated_icon_set_frame_delay (GpkAnimatedIcon *icon, guint delay_ms)
{
	g_return_val_if_fail (GPK_IS_ANIMATED_ICON (icon), FALSE);

	g_debug ("frame delay set to %ims", delay_ms);
	icon->frame_delay = delay_ms;

	/* do we have to change a running icon? */
	if (icon->animation_id != 0) {
		g_source_remove (icon->animation_id);
		icon->animation_id = g_timeout_add (icon->frame_delay, (GSourceFunc) gpk_animated_icon_update, icon);
		g_source_set_name_by_id (icon->animation_id, "[GpkAnimatedIcon] update from delay change");
	}

	return TRUE;
}

/**
 * gpk_animated_icon_enable_animation:
 **/
gboolean
gpk_animated_icon_enable_animation (GpkAnimatedIcon *icon, gboolean enabled)
{
	g_return_val_if_fail (GPK_IS_ANIMATED_ICON (icon), FALSE);

	if (!enabled) {
		if (icon->animation_id == 0) {
			g_debug ("ignoring stop on stopped icon");
			return FALSE;
		}

		g_source_remove (icon->animation_id);
		icon->animation_id = 0;
		return TRUE;
	}

	/* don't double queue */
	if (icon->animation_id != 0) {
		g_debug ("ignoring start on started icon");
		return FALSE;
	}

	/* start */
	icon->frame_counter = 0;
	icon->animation_id = g_timeout_add (icon->frame_delay, (GSourceFunc) gpk_animated_icon_update, icon);
	g_source_set_name_by_id (icon->animation_id, "[GpkAnimatedIcon] start update");
	gpk_animated_icon_update (icon);
	return TRUE;
}

/**
 * gpk_animated_icon_destroy:
 * @object: The object to destroy
 **/
static void
gpk_animated_icon_destroy (GtkWidget *object)
{
	GpkAnimatedIcon *icon;
	icon = GPK_ANIMATED_ICON (object);

	/* avoid going pop after unref when spinning */
	if (icon->animation_id != 0) {
		g_source_remove (icon->animation_id);
	}
	g_free (icon->filename);
	icon->filename = NULL;
	gpk_animated_icon_free_pixbufs (icon);

	GTK_WIDGET_CLASS (parent_class)->destroy (object);
}

static void
gpk_animated_icon_class_init (GpkAnimatedIconClass *class)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
	widget_class->destroy = gpk_animated_icon_destroy;
	parent_class = g_type_class_peek_parent (class);
}

/**
 * gpk_animated_icon_init:
 **/
static void
gpk_animated_icon_init (GpkAnimatedIcon *icon)
{
	g_return_if_fail (GPK_IS_ANIMATED_ICON (icon));
	icon->frames = NULL;
	icon->filename = NULL;
	icon->animation_id = 0;
	icon->frame_counter = 0;
	icon->number_frames = 0;
	icon->frame_delay = 200;

	/* disable polling if we are hidden */
	g_signal_connect (GTK_WIDGET (icon), "notify::visible", G_CALLBACK (gpk_animated_icon_visible_notify_cb), icon);
}

/**
 * gpk_animated_icon_new:
 **/
GtkWidget *
gpk_animated_icon_new (void)
{
	return g_object_new (GPK_TYPE_ANIMATED_ICON, NULL);
}
