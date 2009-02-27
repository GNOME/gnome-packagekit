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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"

#include "gpk-enum.h"
#include "gpk-cell-renderer-info.h"

enum {
	PROP_0,
	PROP_VALUE
};

G_DEFINE_TYPE (GpkCellRendererInfo, gpk_cell_renderer_info, GTK_TYPE_CELL_RENDERER_PIXBUF)

static gpointer parent_class = NULL;

static void
gpk_cell_renderer_info_get_property (GObject *object, guint param_id,
				        GValue *value, GParamSpec *pspec)
{
	GpkCellRendererInfo *cru = GPK_CELL_RENDERER_INFO (object);

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
gpk_cell_renderer_info_set_property (GObject *object, guint param_id,
				    const GValue *value, GParamSpec *pspec)
{
	GpkCellRendererInfo *cru = GPK_CELL_RENDERER_INFO (object);

	switch (param_id) {
	case PROP_VALUE:
		cru->value = g_value_get_uint (value);
		if (cru->value == PK_INFO_ENUM_UNKNOWN) {
			g_object_set (cru, "visible", FALSE, NULL);
		} else if (cru->value == PK_INFO_ENUM_FINISHED) {
			// just ignore
		} else {
			cru->icon_name = gpk_info_enum_to_icon_name (cru->value);
			g_object_set (cru, "visible", TRUE, NULL);
			g_object_set (cru, "icon-name", cru->icon_name, NULL);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

/**
 * gpk_cell_renderer_finalize:
 * @object: The object to finalize
 **/
static void
gpk_cell_renderer_finalize (GObject *object)
{
	GpkCellRendererInfo *cru;
	cru = GPK_CELL_RENDERER_INFO (object);
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gpk_cell_renderer_info_class_init (GpkCellRendererInfoClass *class)
{
	GtkCellRendererClass *cell_renderer_class;
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	object_class->finalize = gpk_cell_renderer_finalize;

	parent_class = g_type_class_peek_parent (class);

	cell_renderer_class = GTK_CELL_RENDERER_CLASS (class);

	object_class->get_property = gpk_cell_renderer_info_get_property;
	object_class->set_property = gpk_cell_renderer_info_set_property;

	g_object_class_install_property (object_class, PROP_VALUE,
					 g_param_spec_uint ("value", "VALUE",
					 "VALUE", 0, G_MAXUINT, PK_INFO_ENUM_UNKNOWN, G_PARAM_READWRITE));
}

/**
 * gpk_cell_renderer_info_init:
 **/
static void
gpk_cell_renderer_info_init (GpkCellRendererInfo *cru)
{
	cru->value = PK_INFO_ENUM_UNKNOWN;
	cru->icon_name = NULL;
}

/**
 * gpk_cell_renderer_info_new:
 **/
GtkCellRenderer *
gpk_cell_renderer_info_new (void)
{
	return g_object_new (GPK_TYPE_CELL_RENDERER_INFO, NULL);
}

