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

typedef enum
{
	GPK_DBUS_ERROR_FAILED,
	GPK_DBUS_ERROR_INTERNAL_ERROR,
	GPK_DBUS_ERROR_NO_PACKAGES_FOUND,
	GPK_DBUS_ERROR_FORBIDDEN,
	GPK_DBUS_ERROR_CANCELLED,
	GPK_DBUS_ERROR_LAST
} GpkDbusError;

GQuark		 gpk_dbus_error_quark			(void);
GType		 gpk_dbus_error_get_type		(void) G_GNUC_CONST;
GType		 gpk_dbus_get_type			(void) G_GNUC_CONST;
GpkDbus		*gpk_dbus_new				(void);

void		 gpk_dbus_install_local_file		(GpkDbus	*dbus,
							 guint32	 xid,
							 guint32	 timestamp,
							 const gchar	*full_path,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_provide_file		(GpkDbus	*dbus,
							 guint32	 xid,
							 guint32	 timestamp,
							 const gchar	*full_path,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_package_name		(GpkDbus	*dbus,
							 guint32	 xid,
							 guint32	 timestamp,
							 const gchar	*package_name,
							 DBusGMethodInvocation *context);
#if 0
void		 gpk_dbus_install_package_names		(GpkDbus	*dbus,
							 guint32	 xid,
							 guint32	 timestamp,
							 gchar		**package_names,
							 DBusGMethodInvocation *context);
#endif
void		 gpk_dbus_install_mime_type		(GpkDbus	*dbus,
							 guint32	 xid,
							 guint32	 timestamp,
							 const gchar	*mime_type,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_gstreamer_codecs	(GpkDbus	*dbus,
							 guint32	 xid,
							 guint32	 timestamp,
							 GPtrArray	*codecs,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_font			(GpkDbus	*dbus,
							 guint32	 xid,
							 guint32	 timestamp,
							 const gchar	*font_desc,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_fonts			(GpkDbus	*dbus,
							 guint32	 xid,
							 guint32	 timestamp,
							 gchar		**font_descs,
							 DBusGMethodInvocation *context);
void		 gpk_dbus_install_catalog		(GpkDbus	*dbus,
							 guint32	 xid,
							 guint32	 timestamp,
							 const gchar	*catalog_file,
							 DBusGMethodInvocation *context);
#if 0
gboolean	 gpk_dbus_is_package_installed		(GpkDbus	*dbus,
							 const gchar	*package_name,
							 gboolean	*installed,
							 GError		**error);
#endif

/* org.freedesktop.PackageKit.Query */
gboolean	 gpk_dbus_is_installed			(GpkDbus	*dbus,
							 const gchar	*package_name,
							 const gchar	*interaction,
							 gboolean	*installed,
							 GError		**error);
gboolean	 gpk_dbus_search_file			(GpkDbus	*dbus,
							 const gchar	*file_name,
							 const gchar	*interaction,
							 gboolean	*installed,
							 gchar		**package_name,
							 GError		**error);

/* org.freedesktop.PackageKit.Modify */
void		 gpk_dbus_install_provide_files		(GpkDbus	*dbus,
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

G_END_DECLS

#endif /* __GPK_DBUS_H */
