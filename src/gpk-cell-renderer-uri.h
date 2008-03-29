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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef PK_CELL_RENDERER_URI_H
#define PK_CELL_RENDERER_URI_H

#include <glib-object.h>
#include <gtk/gtk.h>

#define PK_TYPE_CELL_RENDERER_URI		(pk_cell_renderer_uri_get_type	())
#define PK_CELL_RENDERER_URI(obj)		(G_TYPE_CHECK_INSTANCE_CAST	((obj), PK_TYPE_CELL_RENDERER_URI, PkCellRendererUri))
#define PK_CELL_RENDERER_URI_CLASS(cls)		(G_TYPE_CHECK_CLASS_CAST	((cls), PK_TYPE_CELL_RENDERER_URI, PkCellRendererUriClass))
#define PK_IS_CELL_RENDERER_URI(obj)		(G_TYPE_CHECK_INSTANCE_TYPE	((obj), PK_TYPE_CELL_RENDERER_URI))
#define PK_IS_CELL_RENDERER_URI_CLASS(cls)	(G_TYPE_CHECK_CLASS_TYPE	((cls), PK_TYPE_CELL_RENDERER_URI))
#define PK_CELL_RENDERER_URI_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS	((obj), PK_TYPE_CELL_RENDERER_URI, PkCellRendererUriClass))

G_BEGIN_DECLS

typedef struct _PkCellRendererUri		PkCellRendererUri;
typedef struct _PkCellRendererUriClass		PkCellRendererUriClass;

struct _PkCellRendererUri
{
	GtkCellRendererText parent;
	gchar *uri;
	gboolean clicked;
};

struct _PkCellRendererUriClass
{
	GtkCellRendererTextClass parent_class;
	void (*clicked)		(PkCellRendererUri	*cru,
				 const gchar		*uri);
};

GType		 pk_cell_renderer_uri_get_type		(void);
GtkCellRenderer	*pk_cell_renderer_uri_new		(void);

G_END_DECLS

#endif /* PK_CELL_RENDERER_URI_H */

