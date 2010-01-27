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

#ifndef __GPK_X11_H
#define __GPK_X11_H

#include <glib-object.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

#define GPK_TYPE_X11		(gpk_x11_get_type ())
#define GPK_X11(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_X11, GpkX11))
#define GPK_X11_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_X11, GpkX11Class))
#define GPK_IS_X11(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_X11))
#define GPK_IS_X11_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_X11))
#define GPK_X11_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_X11, GpkX11Class))
#define GPK_X11_ERROR		(gpk_x11_error_quark ())
#define GPK_X11_TYPE_ERROR	(gpk_x11_error_get_type ())

typedef struct GpkX11Private GpkX11Private;

typedef struct
{
	 GObject		 parent;
	 GpkX11Private		*priv;
} GpkX11;

typedef struct
{
	GObjectClass		parent_class;
} GpkX11Class;

GType		 gpk_x11_get_type		  	(void);
GpkX11		*gpk_x11_new				(void);
gboolean	 gpk_x11_set_xid			(GpkX11		*x11,
							 guint32	 xid);
gboolean	 gpk_x11_set_window			(GpkX11		*x11,
							 GdkWindow	*window);
guint32		 gpk_x11_get_user_time			(GpkX11		*x11);
gchar		*gpk_x11_get_title			(GpkX11		*x11);

G_END_DECLS

#endif /* __GPK_X11_H */
