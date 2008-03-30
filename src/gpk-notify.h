/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#ifndef __GPK_NOTIFY_H
#define __GPK_NOTIFY_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GPK_TYPE_NOTIFY		(gpk_notify_get_type ())
#define GPK_NOTIFY(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_NOTIFY, GpkNotify))
#define GPK_NOTIFY_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_NOTIFY, GpkNotifyClass))
#define GPK_IS_NOTIFY(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_NOTIFY))
#define GPK_IS_NOTIFY_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_NOTIFY))
#define GPK_NOTIFY_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_NOTIFY, GpkNotifyClass))
#define GPK_NOTIFY_ERROR	(gpk_notify_error_quark ())
#define GPK_NOTIFY_TYPE_ERROR	(gpk_notify_error_get_type ())

typedef struct GpkNotifyPrivate GpkNotifyPrivate;

typedef struct
{
	 GObject		 parent;
	 GpkNotifyPrivate	*priv;
} GpkNotify;

typedef struct
{
	GObjectClass	parent_class;
} GpkNotifyClass;

GType		 gpk_notify_get_type		  	(void);
GpkNotify	*gpk_notify_new				(void);

G_END_DECLS

#endif /* __GPK_NOTIFY_H */
