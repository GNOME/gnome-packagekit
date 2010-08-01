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

#ifndef __GPK_CHECK_UPDATE_H
#define __GPK_CHECK_UPDATE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GPK_TYPE_CHECK_UPDATE		(gpk_check_update_get_type ())
#define GPK_CHECK_UPDATE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_CHECK_UPDATE, GpkCheckUpdate))
#define GPK_CHECK_UPDATE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_CHECK_UPDATE, GpkCheckUpdateClass))
#define GPK_IS_CHECK_UPDATE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_CHECK_UPDATE))
#define GPK_IS_CHECK_UPDATE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_CHECK_UPDATE))
#define GPK_CHECK_UPDATE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_CHECK_UPDATE, GpkCheckUpdateClass))
#define GPK_CHECK_UPDATE_ERROR		(gpk_check_update_error_quark ())
#define GPK_CHECK_UPDATE_TYPE_ERROR	(gpk_check_update_error_get_type ())

typedef struct GpkCheckUpdatePrivate GpkCheckUpdatePrivate;

typedef struct
{
	 GObject		 parent;
	 GpkCheckUpdatePrivate	*priv;
} GpkCheckUpdate;

typedef struct
{
	GObjectClass	parent_class;
} GpkCheckUpdateClass;

GType		 gpk_check_update_get_type		  	(void);
GpkCheckUpdate	*gpk_check_update_new				(void);
void		 gpk_check_update_set_status_icon		(GpkCheckUpdate	*check_update,
								 GtkStatusIcon	*status_icon);

G_END_DECLS

#endif /* __GPK_CHECK_UPDATE_H */
