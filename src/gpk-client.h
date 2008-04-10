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

#ifndef __GPK_CLIENT_H
#define __GPK_CLIENT_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GPK_TYPE_CLIENT		(gpk_client_get_type ())
#define GPK_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_CLIENT, GpkClient))
#define GPK_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_CLIENT, GpkClientClass))
#define GPK_IS_CLIENT(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_CLIENT))
#define GPK_IS_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_CLIENT))
#define GPK_CLIENT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_CLIENT, GpkClientClass))
#define GPK_CLIENT_ERROR	(gpk_client_error_quark ())
#define GPK_CLIENT_TYPE_ERROR	(gpk_client_error_get_type ())

typedef struct GpkClientPrivate GpkClientPrivate;

typedef struct
{
	 GObject		 parent;
	 GpkClientPrivate	*priv;
} GpkClient;

typedef struct
{
	GObjectClass	parent_class;
} GpkClientClass;

GType		 gpk_client_get_type			(void) G_GNUC_CONST;
GpkClient	*gpk_client_new				(void);

gboolean	 gpk_client_install_file		(GpkClient	*gclient,
							 const gchar	*file_rel)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 gpk_client_install_package		(GpkClient	*gclient,
							 const gchar	*package)
							 G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

#endif /* __GPK_CLIENT_H */
