/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gpk-helper-media-change.h"
#include "gpk-enum.h"
#include "gpk-common.h"

#include "egg-debug.h"

static void     gpk_helper_media_change_finalize	(GObject	  *object);

#define GPK_HELPER_MEDIA_CHANGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_HELPER_MEDIA_CHANGE, GpkHelperMediaChangePrivate))

struct GpkHelperMediaChangePrivate
{
	GtkWindow		*window;
	guint			 idle_id;
	PkMediaTypeEnum		 type;
	gchar			*media_id;
	gchar			*media_text;
};

enum {
	GPK_HELPER_MEDIA_CHANGE_EVENT,
	GPK_HELPER_MEDIA_CHANGE_LAST_SIGNAL
};

static guint signals [GPK_HELPER_MEDIA_CHANGE_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkHelperMediaChange, gpk_helper_media_change, G_TYPE_OBJECT)

/**
 * gpk_helper_media_change_show_idle_cb:
 *
 * Return value: if we agreed
 **/
gboolean
gpk_helper_media_change_show_idle_cb (GpkHelperMediaChange *helper)
{
	const gchar *name = NULL;
	gchar *message = NULL;
	GtkWidget *dialog;
	GtkResponseType response;

	name = gpk_media_type_enum_to_localised_text (helper->priv->type);
	/* TRANSLATORS: dialog body, explains to the user that they need to insert a disk to continue. The first replacement is DVD, CD etc */
	message = g_strdup_printf (_("Additional media is required. Please insert the %s labeled '%s' to continue."), name, helper->priv->media_text);

	dialog = gtk_message_dialog_new (helper->priv->window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 /* TRANSLATORS: this is the window title when a new cd or dvd is required */
					 GTK_MESSAGE_INFO, GTK_BUTTONS_CANCEL, _("A media change is required"));
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);

	/* TRANSLATORS: this is button text */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Continue"), GTK_RESPONSE_YES);

	/* set icon name */
	gtk_window_set_icon_name (GTK_WINDOW (dialog), GPK_ICON_SOFTWARE_INSTALLER);

	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));

	/* yes / no */
	if (response == GTK_RESPONSE_YES) {
		g_signal_emit (helper, signals [GPK_HELPER_MEDIA_CHANGE_EVENT], 0, response);
	} else {
		g_signal_emit (helper, signals [GPK_HELPER_MEDIA_CHANGE_EVENT], 0, GTK_RESPONSE_NO);
	}
	g_free (message);

	/* never repeat */
	helper->priv->media_id = 0;
	return FALSE;
}

/**
 * gpk_helper_media_change_show:
 *
 * Return value: if we agreed
 **/
gboolean
gpk_helper_media_change_show (GpkHelperMediaChange *helper, PkMediaTypeEnum type, const gchar *media_id, const gchar *media_text)
{
	g_return_val_if_fail (GPK_IS_HELPER_MEDIA_CHANGE (helper), FALSE);
	g_return_val_if_fail (helper->priv->media_id == 0, FALSE);

	helper->priv->type = type;
	helper->priv->media_id = g_strdup (media_id);
	helper->priv->media_text = g_strdup (media_text);
	helper->priv->idle_id = g_idle_add ((GSourceFunc) gpk_helper_media_change_show_idle_cb, helper);
	return TRUE;
}

/**
 * gpk_helper_media_change_set_parent:
 **/
gboolean
gpk_helper_media_change_set_parent (GpkHelperMediaChange *helper, GtkWindow *window)
{
	g_return_val_if_fail (GPK_IS_HELPER_MEDIA_CHANGE (helper), FALSE);
	g_return_val_if_fail (window != NULL, FALSE);

	helper->priv->window = window;
	return TRUE;
}

/**
 * gpk_helper_media_change_class_init:
 * @klass: The GpkHelperMediaChangeClass
 **/
static void
gpk_helper_media_change_class_init (GpkHelperMediaChangeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_helper_media_change_finalize;
	g_type_class_add_private (klass, sizeof (GpkHelperMediaChangePrivate));
	signals [GPK_HELPER_MEDIA_CHANGE_EVENT] =
		g_signal_new ("event",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkHelperMediaChangeClass, event),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * gpk_helper_media_change_init:
 **/
static void
gpk_helper_media_change_init (GpkHelperMediaChange *helper)
{
	helper->priv = GPK_HELPER_MEDIA_CHANGE_GET_PRIVATE (helper);
	helper->priv->window = NULL;
	helper->priv->idle_id = 0;
}

/**
 * gpk_helper_media_change_finalize:
 **/
static void
gpk_helper_media_change_finalize (GObject *object)
{
	GpkHelperMediaChange *helper;

	g_return_if_fail (GPK_IS_HELPER_MEDIA_CHANGE (object));

	helper = GPK_HELPER_MEDIA_CHANGE (object);
	if (helper->priv->idle_id != 0)
		g_source_remove (helper->priv->idle_id);
	g_free (helper->priv->media_id);
	g_free (helper->priv->media_text);

	G_OBJECT_CLASS (gpk_helper_media_change_parent_class)->finalize (object);
}

/**
 * gpk_helper_media_change_new:
 **/
GpkHelperMediaChange *
gpk_helper_media_change_new (void)
{
	GpkHelperMediaChange *helper;
	helper = g_object_new (GPK_TYPE_HELPER_MEDIA_CHANGE, NULL);
	return GPK_HELPER_MEDIA_CHANGE (helper);
}

