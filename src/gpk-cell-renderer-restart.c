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
#include <packagekit-glib2/packagekit.h>

#include "gpk-enum.h"
#include "gpk-cell-renderer-restart.h"

struct _GpkCellRendererRestart
{
	GtkCellRendererPixbuf	 parent_instance;
	PkRestartEnum		 value;
	const gchar		*icon_name;
};

enum {
	PROP_0,
	PROP_VALUE
};

G_DEFINE_TYPE (GpkCellRendererRestart, gpk_cell_renderer_restart, GTK_TYPE_CELL_RENDERER_PIXBUF)

static gpointer parent_class = NULL;

static void
gpk_cell_renderer_restart_get_property (GObject *object, guint param_id,
				        GValue *value, GParamSpec *pspec)
{
	GpkCellRendererRestart *cru = GPK_CELL_RENDERER_RESTART (object);

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
gpk_cell_renderer_restart_set_property (GObject *object, guint param_id,
				    const GValue *value, GParamSpec *pspec)
{
	GpkCellRendererRestart *cru = GPK_CELL_RENDERER_RESTART (object);

	switch (param_id) {
	case PROP_VALUE:
		cru->value = g_value_get_uint (value);
		cru->icon_name = gpk_restart_enum_to_icon_name (cru->value);
		g_object_set (cru, "icon-name", cru->icon_name, NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
gpk_cell_renderer_finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gpk_cell_renderer_restart_class_init (GpkCellRendererRestartClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	object_class->finalize = gpk_cell_renderer_finalize;

	parent_class = g_type_class_peek_parent (class);

	object_class->get_property = gpk_cell_renderer_restart_get_property;
	object_class->set_property = gpk_cell_renderer_restart_set_property;

	g_object_class_install_property (object_class, PROP_VALUE,
					 g_param_spec_uint ("value", "VALUE",
					 "VALUE", 0, G_MAXUINT, PK_RESTART_ENUM_NONE, G_PARAM_READWRITE));
}

static void
gpk_cell_renderer_restart_init (GpkCellRendererRestart *cru)
{
	cru->value = PK_RESTART_ENUM_NONE;
}

GtkCellRenderer *
gpk_cell_renderer_restart_new (void)
{
	return g_object_new (GPK_TYPE_CELL_RENDERER_RESTART, NULL);
}

