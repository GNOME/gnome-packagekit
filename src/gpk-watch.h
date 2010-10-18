/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __GPK_WATCH_H
#define __GPK_WATCH_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GPK_TYPE_WATCH		(gpk_watch_get_type ())
#define GPK_WATCH(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_WATCH, GpkWatch))
#define GPK_WATCH_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_WATCH, GpkWatchClass))
#define GPK_IS_WATCH(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_WATCH))
#define GPK_IS_WATCH_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_WATCH))
#define GPK_WATCH_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_WATCH, GpkWatchClass))
#define GPK_WATCH_ERROR		(gpk_watch_error_quark ())
#define GPK_WATCH_TYPE_ERROR	(gpk_watch_error_get_type ())

typedef struct GpkWatchPrivate GpkWatchPrivate;

typedef struct
{
	 GObject		 parent;
	 GpkWatchPrivate	*priv;
} GpkWatch;

typedef struct
{
	GObjectClass	parent_class;
} GpkWatchClass;

GType		 gpk_watch_get_type		  	(void);
GpkWatch	*gpk_watch_new				(void);

G_END_DECLS

#endif /* __GPK_WATCH_H */
