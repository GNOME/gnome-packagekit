/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __GPK_UPDATE_VIEWER_H
#define __GPK_UPDATE_VIEWER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GPK_TYPE_UPDATE_VIEWER		(gpk_update_viewer_get_type ())
#define GPK_UPDATE_VIEWER(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_UPDATE_VIEWER, GpkUpdateViewer))
#define GPK_UPDATE_VIEWER_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_UPDATE_VIEWER, GpkUpdateViewerClass))
#define GPK_IS_UPDATE_VIEWER(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_UPDATE_VIEWER))
#define GPK_IS_UPDATE_VIEWER_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_UPDATE_VIEWER))
#define GPK_UPDATE_VIEWER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_UPDATE_VIEWER, GpkUpdateViewerClass))

typedef struct GpkUpdateViewerPrivate GpkUpdateViewerPrivate;

typedef struct
{
	GObject		 parent;
	GpkUpdateViewerPrivate *priv;
} GpkUpdateViewer;

typedef struct
{
	GObjectClass	parent_class;
	void		(* action_help)			(GpkUpdateViewer	*update_viewer);
	void		(* action_close)		(GpkUpdateViewer	*update_viewer);
} GpkUpdateViewerClass;

GType		 gpk_update_viewer_get_type		(void);
GpkUpdateViewer	*gpk_update_viewer_new			(void);
void		 gpk_update_viewer_show			(GpkUpdateViewer	*update_viewer);

G_END_DECLS

#endif	/* __GPK_UPDATE_VIEWER_H */
