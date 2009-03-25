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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __GPK_SMART_ICON_H
#define __GPK_SMART_ICON_H

#include <glib-object.h>
#include <gtk/gtkstatusicon.h>

G_BEGIN_DECLS

#define GPK_TYPE_SMART_ICON		(gpk_smart_icon_get_type ())
#define GPK_SMART_ICON(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_SMART_ICON, GpkSmartIcon))
#define GPK_SMART_ICON_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_SMART_ICON, GpkSmartIconClass))
#define GPK_IS_SMART_ICON(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_SMART_ICON))
#define GPK_IS_SMART_ICON_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_SMART_ICON))
#define GPK_SMART_ICON_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_SMART_ICON, GpkSmartIconClass))
#define GPK_SMART_ICON_ERROR		(gpk_smart_icon_error_quark ())
#define GPK_SMART_ICON_TYPE_ERROR	(gpk_smart_icon_error_get_type ())

typedef struct GpkSmartIconPrivate GpkSmartIconPrivate;

typedef struct
{
	 GtkStatusIcon		 parent;
	 GpkSmartIconPrivate	*priv;
} GpkSmartIcon;

typedef struct
{
	GtkStatusIconClass	 parent_class;
} GpkSmartIconClass;

GType		 gpk_smart_icon_get_type		(void);
GpkSmartIcon	*gpk_smart_icon_new			(void);
gboolean	 gpk_smart_icon_set_icon_name		(GpkSmartIcon		*sicon,
							 const gchar		*icon_name);

G_END_DECLS

#endif /* __GPK_SMART_ICON_H */
