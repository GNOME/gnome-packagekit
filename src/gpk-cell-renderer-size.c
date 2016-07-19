/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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

#include "gpk-cell-renderer-size.h"

struct _GpkCellRendererSize
{
	GtkCellRendererText	 parent_instance;
	guint			 value;
	gchar			*markup;
};

enum {
	PROP_0,
	PROP_VALUE
};

G_DEFINE_TYPE (GpkCellRendererSize, gpk_cell_renderer_size, GTK_TYPE_CELL_RENDERER_TEXT)

static gpointer parent_class = NULL;

static void
gpk_cell_renderer_size_get_property (GObject *object, guint param_id,
				    GValue *value, GParamSpec *pspec)
{
	GpkCellRendererSize *cru = GPK_CELL_RENDERER_SIZE (object);

	switch (param_id) {
	case PROP_VALUE:
		g_value_set_uint (value, cru->value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
gpk_cell_renderer_size_set_property (GObject *object, guint param_id,
				    const GValue *value, GParamSpec *pspec)
{
	GpkCellRendererSize *cru = GPK_CELL_RENDERER_SIZE (object);

	switch (param_id) {
	case PROP_VALUE:
		cru->value = g_value_get_uint (value);
		g_free (cru->markup);
		cru->markup = g_format_size (cru->value);
		g_object_set (cru, "markup", cru->markup, NULL);

		/* if the size is zero, we hide the markup */
		g_object_set (cru, "visible", (cru->value != 0), NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
gpk_cell_renderer_finalize (GObject *object)
{
	GpkCellRendererSize *cru;
	cru = GPK_CELL_RENDERER_SIZE (object);
	g_free (cru->markup);
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gpk_cell_renderer_size_class_init (GpkCellRendererSizeClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	object_class->finalize = gpk_cell_renderer_finalize;

	parent_class = g_type_class_peek_parent (class);

	object_class->get_property = gpk_cell_renderer_size_get_property;
	object_class->set_property = gpk_cell_renderer_size_set_property;

	g_object_class_install_property (object_class, PROP_VALUE,
					 g_param_spec_uint ("value", "VALUE",
					 "VALUE", 0, G_MAXUINT, 0, G_PARAM_READWRITE));
}

static void
gpk_cell_renderer_size_init (GpkCellRendererSize *cru)
{
}

GtkCellRenderer *
gpk_cell_renderer_size_new (void)
{
	return g_object_new (GPK_TYPE_CELL_RENDERER_SIZE, NULL);
}

