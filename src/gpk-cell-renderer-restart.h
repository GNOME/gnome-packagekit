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

#ifndef GPK_CELL_RENDERER_RESTART_H
#define GPK_CELL_RENDERER_RESTART_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

#define GPK_TYPE_CELL_RENDERER_RESTART			(gpk_cell_renderer_restart_get_type())
#define GPK_CELL_RENDERER_RESTART(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), GPK_TYPE_CELL_RENDERER_RESTART, GpkCellRendererRestart))
#define GPK_CELL_RENDERER_RESTART_CLASS(cls)		(G_TYPE_CHECK_CLASS_CAST((cls), GPK_TYPE_CELL_RENDERER_RESTART, GpkCellRendererRestartClass))
#define GPK_IS_CELL_RENDERER_RESTART(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GPK_TYPE_CELL_RENDERER_RESTART))
#define GPK_IS_CELL_RENDERER_RESTART_CLASS(cls)		(G_TYPE_CHECK_CLASS_TYPE((cls), GPK_TYPE_CELL_RENDERER_RESTART))
#define GPK_CELL_RENDERER_RESTART_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GPK_TYPE_CELL_RENDERER_RESTART, GpkCellRendererRestartClass))

G_BEGIN_DECLS

typedef struct _GpkCellRendererRestart		GpkCellRendererRestart;
typedef struct _GpkCellRendererRestartClass	GpkCellRendererRestartClass;

struct _GpkCellRendererRestart
{
	GtkCellRendererPixbuf	 parent;
	PkRestartEnum		 value;
	const gchar		*icon_name;
};

struct _GpkCellRendererRestartClass
{
	GtkCellRendererPixbufClass parent_class;
};

GType		 gpk_cell_renderer_restart_get_type	(void);
GtkCellRenderer	*gpk_cell_renderer_restart_new		(void);

G_END_DECLS

#endif /* GPK_CELL_RENDERER_RESTART_H */

