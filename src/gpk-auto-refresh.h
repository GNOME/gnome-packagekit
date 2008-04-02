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

#ifndef __GPK_AUTO_REFRESH_H
#define __GPK_AUTO_REFRESH_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GPK_TYPE_AUTO_REFRESH		(gpk_auto_refresh_get_type ())
#define GPK_AUTO_REFRESH(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_AUTO_REFRESH, GpkAutoRefresh))
#define GPK_AUTO_REFRESH_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_AUTO_REFRESH, GpkAutoRefreshClass))
#define PK_IS_AUTO_REFRESH(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_AUTO_REFRESH))
#define PK_IS_AUTO_REFRESH_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_AUTO_REFRESH))
#define GPK_AUTO_REFRESH_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_AUTO_REFRESH, GpkAutoRefreshClass))
#define GPK_AUTO_REFRESH_ERROR		(gpk_auto_refresh_error_quark ())
#define GPK_AUTO_REFRESH_TYPE_ERROR	(gpk_auto_refresh_error_get_type ())

typedef struct GpkAutoRefreshPrivate GpkAutoRefreshPrivate;

typedef struct
{
	 GObject		 parent;
	 GpkAutoRefreshPrivate	*priv;
} GpkAutoRefresh;

typedef struct
{
	GObjectClass	parent_class;
} GpkAutoRefreshClass;

GType		 gpk_auto_refresh_get_type		(void) G_GNUC_CONST;
GpkAutoRefresh	*gpk_auto_refresh_new			(void);
gboolean	 gpk_auto_refresh_get_on_battery	(GpkAutoRefresh *arefresh);

G_END_DECLS

#endif /* __GPK_AUTO_REFRESH_H */
