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
#include "gpk-cell-renderer-info.h"

struct _GpkCellRendererInfo
{
	GtkCellRendererPixbuf	 parent_instance;
	GPtrArray		*array;
	PkInfoEnum		 value;
	const gchar		*icon_name;
	GPtrArray		*ignore;
};

enum {
	PROP_0,
	PROP_VALUE,
	PROP_IGNORE_VALUES
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

static gboolean
gpk_cell_renderer_should_show (GpkCellRendererInfo *cru)
{
	guint i;
	GPtrArray *array;
	PkInfoEnum info;

	/* are we in the ignore array */
	array = cru->ignore;
	for (i = 0; i < array->len; i++) {
		info = GPOINTER_TO_UINT (g_ptr_array_index (array, i));
		if (info == cru->value)
			return FALSE;
	}
	return TRUE;
}

static void
gpk_cell_renderer_info_set_property (GObject *object, guint param_id,
				     const GValue *value, GParamSpec *pspec)
{
	const gchar *text;
	g_auto(GStrv) split = NULL;
	gboolean ret;
	guint i;
	PkInfoEnum info;
	GpkCellRendererInfo *cru = GPK_CELL_RENDERER_INFO (object);

	switch (param_id) {
	case PROP_VALUE:
		cru->value = g_value_get_uint (value);
		ret = gpk_cell_renderer_should_show (cru);
		if (!ret) {
			g_object_set (cru, "icon-name", NULL, NULL);
		} else {
			cru->icon_name = gpk_info_status_enum_to_icon_name ((GpkInfoStatusEnum) cru->value);
			g_object_set (cru, "icon-name", cru->icon_name, NULL);
		}
		break;
	case PROP_IGNORE_VALUES:
		/* split up ignore values */
		text = g_value_get_string (value);
		split = g_strsplit (text, ",", -1);
		for (i=0; split[i] != NULL; i++) {
			info = pk_info_enum_from_string (split[i]);
			g_ptr_array_add (cru->ignore, GUINT_TO_POINTER (info));
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
gpk_cell_renderer_finalize (GObject *object)
{
	GpkCellRendererInfo *cru;
	cru = GPK_CELL_RENDERER_INFO (object);
	g_ptr_array_free (cru->ignore, TRUE);
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gpk_cell_renderer_info_class_init (GpkCellRendererInfoClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	object_class->finalize = gpk_cell_renderer_finalize;

	parent_class = g_type_class_peek_parent (class);

	object_class->get_property = gpk_cell_renderer_info_get_property;
	object_class->set_property = gpk_cell_renderer_info_set_property;

	g_object_class_install_property (object_class, PROP_VALUE,
					 g_param_spec_uint ("value", "VALUE",
					 "VALUE", 0, G_MAXUINT, PK_INFO_ENUM_UNKNOWN, G_PARAM_READWRITE));
	g_object_class_install_property (object_class, PROP_IGNORE_VALUES,
					 g_param_spec_string ("ignore-values", "IGNORE-VALUES",
					 "IGNORE-VALUES", "unknown", G_PARAM_WRITABLE));
}

static void
gpk_cell_renderer_info_init (GpkCellRendererInfo *cru)
{
	cru->value = PK_INFO_ENUM_UNKNOWN;
	cru->ignore = g_ptr_array_new ();
}

GtkCellRenderer *
gpk_cell_renderer_info_new (void)
{
	return g_object_new (GPK_TYPE_CELL_RENDERER_INFO, NULL);
}

