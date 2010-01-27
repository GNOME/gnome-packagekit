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

#ifndef GPK_CELL_RENDERER_SIZE_H
#define GPK_CELL_RENDERER_SIZE_H

#include <glib-object.h>
#include <gtk/gtk.h>

#define GPK_TYPE_CELL_RENDERER_SIZE		(gpk_cell_renderer_size_get_type())
#define GPK_CELL_RENDERER_SIZE(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), GPK_TYPE_CELL_RENDERER_SIZE, GpkCellRendererSize))
#define GPK_CELL_RENDERER_SIZE_CLASS(cls)	(G_TYPE_CHECK_CLASS_CAST((cls), GPK_TYPE_CELL_RENDERER_SIZE, GpkCellRendererSizeClass))
#define GPK_IS_CELL_RENDERER_SIZE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GPK_TYPE_CELL_RENDERER_SIZE))
#define GPK_IS_CELL_RENDERER_SIZE_CLASS(cls)	(G_TYPE_CHECK_CLASS_TYPE((cls), GPK_TYPE_CELL_RENDERER_SIZE))
#define GPK_CELL_RENDERER_SIZE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GPK_TYPE_CELL_RENDERER_SIZE, GpkCellRendererSizeClass))

G_BEGIN_DECLS

typedef struct _GpkCellRendererSize		GpkCellRendererSize;
typedef struct _GpkCellRendererSizeClass	GpkCellRendererSizeClass;

struct _GpkCellRendererSize
{
	GtkCellRendererText	 parent;
	guint			 value;
	gchar			*markup;
};

struct _GpkCellRendererSizeClass
{
	GtkCellRendererTextClass parent_class;
};

GType		 gpk_cell_renderer_size_get_type	(void);
GtkCellRenderer	*gpk_cell_renderer_size_new		(void);

G_END_DECLS

#endif /* GPK_CELL_RENDERER_SIZE_H */

