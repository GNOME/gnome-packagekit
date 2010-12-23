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

#include "gpk-cell-renderer-uri.h"

enum {
	PROP_0,
	PROP_URI,
	PROP_CLICKED
};

enum {
	CLICKED,
	LAST_SIGNAL
};

G_DEFINE_TYPE (GpkCellRendererUri, gpk_cell_renderer_uri, GTK_TYPE_CELL_RENDERER_TEXT)

static gpointer parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

static gboolean
gpk_cell_renderer_uri_is_clicked (GpkCellRendererUri *cru)
{
	gpointer value;
	g_return_val_if_fail (cru != NULL, FALSE);
	if (cru->uri == NULL)
		return FALSE;
	value = g_hash_table_lookup (cru->clicked, cru->uri);
	return (value != NULL);
}

static void
gpk_cell_renderer_uri_set_clicked (GpkCellRendererUri *cru, gboolean clicked)
{
	g_return_if_fail (cru != NULL);
	if (clicked)
		g_hash_table_insert (cru->clicked, g_strdup (cru->uri), GINT_TO_POINTER (1));
	else
		g_hash_table_remove (cru->clicked, cru->uri);
}

static gboolean
gpk_cell_renderer_uri_activate (GtkCellRenderer *cell, GdkEvent *event,
			        GtkWidget *widget, const gchar *path,
			        const GdkRectangle *background_area,
			        const GdkRectangle *cell_area, GtkCellRendererState flags)
{
	GpkCellRendererUri *cru = GPK_CELL_RENDERER_URI (cell);

	/* nothing to do */
	if (cru->uri == NULL)
		return TRUE;

	gpk_cell_renderer_uri_set_clicked (cru, TRUE);

	g_debug ("emit: %s", cru->uri);
	g_signal_emit (cell, signals [CLICKED], 0, cru->uri);
	return TRUE;
}

static void
gpk_cell_renderer_uri_get_property (GObject *object, guint param_id,
				    GValue *value, GParamSpec *pspec)
{
	gboolean ret;
	GpkCellRendererUri *cru = GPK_CELL_RENDERER_URI (object);

	switch (param_id) {
	case PROP_URI:
		g_value_set_string (value, cru->uri);
		break;
	case PROP_CLICKED:
		ret = gpk_cell_renderer_uri_is_clicked (cru);
		g_value_set_boolean (value, ret);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
gpk_cell_renderer_uri_set_property (GObject *object, guint param_id,
				    const GValue *value, GParamSpec *pspec)
{
	gboolean ret;
	GpkCellRendererUri *cru = GPK_CELL_RENDERER_URI (object);

	switch (param_id) {
	case PROP_URI:
		if (cru->uri!=NULL) {
			g_free (cru->uri);
		}
		cru->uri = g_strdup (g_value_get_string (value));
		break;
	case PROP_CLICKED:
		ret = g_value_get_boolean (value);
		gpk_cell_renderer_uri_set_clicked (cru, ret);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

/* we can't hardcode colours, so just make blue-er and purple-er */
static void
gpk_cell_renderer_uri_set_link_rgba (GdkRGBA *color, gboolean visited)
{
	const gdouble color_half = 0.5;
	const gdouble offset = 1.0 / 3;

	if (visited) {
		if (color->red < color_half && color->blue < color_half) {
			color->red += offset;
			color->blue += offset;
			return;
		}
		if (color->green > color_half) {
			color->green -= offset;
			return;
		}
	} else {
		if (color->blue < color_half) {
			color->blue += offset;
			return;
		}
		if (color->red > color_half && color->green > color_half) {
			color->red -= offset;
			color->green -= offset;
			return;
		}
	}

	g_debug ("cannot get color for %f,%f,%f", color->red, color->blue, color->green);
}

static void
gpk_cell_renderer_uri_render (GtkCellRenderer *cell,
			      cairo_t *cr,
			      GtkWidget *widget,
			      const GdkRectangle *background_area,
			      const GdkRectangle *cell_area,
			      GtkCellRendererState flags)
{
	gboolean ret;
	GtkStyleContext *style;
	GdkRGBA rgba;
	GdkColor *color;
	gchar *color_string;
	GpkCellRendererUri *cru = GPK_CELL_RENDERER_URI (cell);

	ret = gpk_cell_renderer_uri_is_clicked (cru);

	/* get a copy of the widget color */
	style = gtk_widget_get_style_context (GTK_WIDGET (widget));

	/* set colour */
	if (cru->uri == NULL) {
		gtk_style_context_get_color (style, 0, &rgba);
		color_string = gdk_rgba_to_string (&rgba);
		g_object_set (G_OBJECT (cru), "foreground", color_string, NULL);
		g_object_set (G_OBJECT (cru), "underline", PANGO_UNDERLINE_NONE, NULL);
	} else if (ret) {
		/* if we defined this in our theme, else find a compromise */
		gtk_style_context_get_style (style,
					     "visited-link-color", &color,
                                             NULL);
		if (color == NULL) {
			gtk_style_context_get_color (style, 0, &rgba);
			gpk_cell_renderer_uri_set_link_rgba (&rgba, TRUE);

			color_string = gdk_rgba_to_string (&rgba);
		} else {
			color_string = gdk_color_to_string (color);
			gdk_color_free (color);
		}

		g_object_set (G_OBJECT (cru), "foreground", color_string, NULL);
		g_object_set (G_OBJECT (cru), "underline", PANGO_UNDERLINE_SINGLE, NULL);
	} else {
		/* if we defined this in our theme, else find a compromise */
		gtk_style_context_get_style (style,
					     "link-color", &color,
                                             NULL);
		if (color == NULL) {
			gtk_style_context_get_color (style, 0, &rgba);
			gpk_cell_renderer_uri_set_link_rgba (&rgba, FALSE);

			color_string = gdk_rgba_to_string (&rgba);
		} else {
			color_string = gdk_color_to_string (color);
			gdk_color_free (color);
		}

		g_object_set (G_OBJECT (cru), "foreground", color_string, NULL);
		g_object_set (G_OBJECT (cru), "underline", PANGO_UNDERLINE_SINGLE, NULL);
	}

	g_free (color_string);

	/* can we select the text or click it? */
	if (cru->uri == NULL) {
		g_object_set (G_OBJECT(cru), "editable", TRUE, NULL);
		g_object_set (G_OBJECT(cru), "mode", GTK_CELL_RENDERER_MODE_EDITABLE, NULL);
	} else  {
		g_object_set (G_OBJECT(cru), "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE, NULL);
	}

	GTK_CELL_RENDERER_CLASS (parent_class)->render (cell, cr, widget, background_area, cell_area, flags);
}

/**
 * gpk_cell_renderer_finalize:
 * @object: The object to finalize
 **/
static void
gpk_cell_renderer_finalize (GObject *object)
{
	GpkCellRendererUri *cru;
	cru = GPK_CELL_RENDERER_URI (object);
	g_hash_table_unref (cru->clicked);
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gpk_cell_renderer_uri_class_init (GpkCellRendererUriClass *class)
{
	GtkCellRendererClass *cell_renderer_class;
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	object_class->finalize = gpk_cell_renderer_finalize;

	parent_class = g_type_class_peek_parent (class);

	cell_renderer_class = GTK_CELL_RENDERER_CLASS (class);
	cell_renderer_class->activate = gpk_cell_renderer_uri_activate;
	cell_renderer_class->render = gpk_cell_renderer_uri_render;

	object_class->get_property = gpk_cell_renderer_uri_get_property;
	object_class->set_property = gpk_cell_renderer_uri_set_property;

	g_object_class_install_property (object_class, PROP_URI,
					 g_param_spec_string ("uri", "URI",
					 "URI", NULL, G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_CLICKED,
					 g_param_spec_boolean ("clicked", "Clicked",
					 "If the URI has been clicked", FALSE, G_PARAM_READWRITE));

	signals [CLICKED] =
		g_signal_new ("clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkCellRendererUriClass, clicked), NULL, NULL,
			      g_cclosure_marshal_VOID__STRING, G_TYPE_NONE, 1, G_TYPE_STRING);
}

/**
 * gpk_cell_renderer_uri_init:
 **/
static void
gpk_cell_renderer_uri_init (GpkCellRendererUri *cru)
{
	cru->uri = NULL;
	cru->clicked = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

/**
 * gpk_cell_renderer_uri_new:
 **/
GtkCellRenderer *
gpk_cell_renderer_uri_new (void)
{
	return g_object_new (GPK_TYPE_CELL_RENDERER_URI, NULL);
}

