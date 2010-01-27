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

#ifndef GPK_CELL_RENDERER_INFO_H
#define GPK_CELL_RENDERER_INFO_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

#define GPK_TYPE_CELL_RENDERER_INFO		(gpk_cell_renderer_info_get_type())
#define GPK_CELL_RENDERER_INFO(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), GPK_TYPE_CELL_RENDERER_INFO, GpkCellRendererInfo))
#define GPK_CELL_RENDERER_INFO_CLASS(cls)	(G_TYPE_CHECK_CLASS_CAST((cls), GPK_TYPE_CELL_RENDERER_INFO, GpkCellRendererInfoClass))
#define GPK_IS_CELL_RENDERER_INFO(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GPK_TYPE_CELL_RENDERER_INFO))
#define GPK_IS_CELL_RENDERER_INFO_CLASS(cls)	(G_TYPE_CHECK_CLASS_TYPE((cls), GPK_TYPE_CELL_RENDERER_INFO))
#define GPK_CELL_RENDERER_INFO_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GPK_TYPE_CELL_RENDERER_INFO, GpkCellRendererInfoClass))

G_BEGIN_DECLS

typedef struct _GpkCellRendererInfo		GpkCellRendererInfo;
typedef struct _GpkCellRendererInfoClass	GpkCellRendererInfoClass;
typedef struct _GpkCellRendererInfoPrivate	GpkCellRendererInfoPrivate;

struct _GpkCellRendererInfo
{
	GtkCellRendererPixbuf		 parent;
	GpkCellRendererInfoPrivate	*priv;
};

struct _GpkCellRendererInfoClass
{
	GtkCellRendererPixbufClass parent_class;
};

GType		 gpk_cell_renderer_info_get_type	(void);
GtkCellRenderer	*gpk_cell_renderer_info_new		(void);

G_END_DECLS

#endif /* GPK_CELL_RENDERER_INFO_H */

