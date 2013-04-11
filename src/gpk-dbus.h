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

#ifndef __GPK_DBUS_H
#define __GPK_DBUS_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

G_BEGIN_DECLS

#define GPK_TYPE_DBUS		(gpk_dbus_get_type ())
#define GPK_DBUS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_DBUS, GpkDbus))
#define GPK_DBUS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_DBUS, GpkDbusClass))
#define PK_IS_DBUS(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_DBUS))
#define PK_IS_DBUS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_DBUS))
#define GPK_DBUS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_DBUS, GpkDbusClass))
#define GPK_DBUS_ERROR		(gpk_dbus_error_quark ())
#define GPK_DBUS_TYPE_ERROR	(gpk_dbus_error_get_type ())

typedef struct GpkDbusPrivate GpkDbusPrivate;

typedef struct
{
	 GObject		 parent;
	 GpkDbusPrivate	*priv;
} GpkDbus;

typedef struct
{
	GObjectClass	parent_class;
} GpkDbusClass;

GQuark		 gpk_dbus_error_quark			(void);
GType		 gpk_dbus_error_get_type		(void);
GType		 gpk_dbus_get_type			(void);
GpkDbus		*gpk_dbus_new				(void);

guint		 gpk_dbus_get_idle_time			(GpkDbus	*dbus);

/* org.freedesktop.PackageKit.Query */
void		 gpk_dbus_is_installed			(GpkDbus	*dbus,
							 const gchar	*package_name,
							 const gchar	*interaction,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_search_file			(GpkDbus	*dbus,
							 const gchar	*file_name,
							 const gchar	*interaction,
							 DBusGMethodInvocation *context);

/* org.freedesktop.PackageKit.Modify */
void		 gpk_dbus_install_provide_files		(GpkDbus	*dbus,
							 guint32	 xid,
							 gchar		**files,
							 const gchar	*interaction,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_catalogs		(GpkDbus	*dbus,
							 guint32	 xid,
							 gchar		**files,
							 const gchar	*interaction,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_package_files		(GpkDbus	*dbus,
							 guint32	 xid,
							 gchar		**files,
							 const gchar	*interaction,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_package_names		(GpkDbus	*dbus,
							 guint32	 xid,
							 gchar		**packages,
							 const gchar	*interaction,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_mime_types		(GpkDbus	*dbus,
							 guint32	 xid,
							 gchar		**mime_types,
							 const gchar	*interaction,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_fontconfig_resources	(GpkDbus 	*dbus,
							 guint32	 xid,
							 gchar		**fonts,
							 const gchar	*interaction,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_gstreamer_resources	(GpkDbus 	*dbus,
							 guint32	 xid,
							 gchar		**codecs,
							 const gchar	*interaction,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_resources		(GpkDbus 	*dbus,
							 guint32	 xid,
							 const gchar	*type,
							 gchar		**resources,
							 const gchar	*interaction,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_remove_package_by_files	(GpkDbus	*dbus,
							 guint32	 xid,
							 gchar		**files,
							 const gchar	*interaction,
							 DBusGMethodInvocation *context);

void		 gpk_dbus_install_printer_drivers	(GpkDbus 	*dbus,
							 guint32	 xid,
							 gchar		**device_ids,
							 const gchar	*interaction,
							 DBusGMethodInvocation *context);
G_END_DECLS

#endif /* __GPK_DBUS_H */
