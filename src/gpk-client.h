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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __GPK_CLIENT_H
#define __GPK_CLIENT_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <packagekit-glib/packagekit.h>

G_BEGIN_DECLS

#define GPK_TYPE_CLIENT		(gpk_client_get_type ())
#define GPK_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_CLIENT, GpkClient))
#define GPK_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_CLIENT, GpkClientClass))
#define GPK_IS_CLIENT(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_CLIENT))
#define GPK_IS_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_CLIENT))
#define GPK_CLIENT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_CLIENT, GpkClientClass))
#define GPK_CLIENT_ERROR	(gpk_client_error_quark ())
#define GPK_CLIENT_TYPE_ERROR	(gpk_client_error_get_type ())

/**
 * GpkClientError:
 * @GPK_CLIENT_ERROR_FAILED: the transaction failed for an unknown reason
 *
 * Errors that can be thrown
 */
typedef enum
{
	GPK_CLIENT_ERROR_FAILED,
	GPK_CLIENT_ERROR_INTERNAL_ERROR,
	GPK_CLIENT_ERROR_NO_PACKAGES_FOUND,
	GPK_CLIENT_ERROR_FORBIDDEN,
	GPK_CLIENT_ERROR_CANCELLED,
	GPK_CLIENT_ERROR_LAST
} GpkClientError;

/**
 * GpkClientInteract:
 */
typedef enum
{
	GPK_CLIENT_INTERACT_CONFIRM_SEARCH,
	GPK_CLIENT_INTERACT_CONFIRM_DEPS,
	GPK_CLIENT_INTERACT_CONFIRM_INSTALL,
	GPK_CLIENT_INTERACT_PROGRESS,
	GPK_CLIENT_INTERACT_FINISHED,
	GPK_CLIENT_INTERACT_WARNING,
	GPK_CLIENT_INTERACT_UNKNOWN
} GpkClientInteract;

#define GPK_CLIENT_INTERACT_NEVER			0
#define GPK_CLIENT_INTERACT_ALWAYS			pk_bitfield_from_enums (GPK_CLIENT_INTERACT_WARNING, \
										GPK_CLIENT_INTERACT_CONFIRM_SEARCH, \
										GPK_CLIENT_INTERACT_CONFIRM_DEPS, \
										GPK_CLIENT_INTERACT_CONFIRM_INSTALL, \
										GPK_CLIENT_INTERACT_PROGRESS, \
										GPK_CLIENT_INTERACT_FINISHED, -1)
#define GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS	pk_bitfield_from_enums (GPK_CLIENT_INTERACT_WARNING, \
										GPK_CLIENT_INTERACT_CONFIRM_SEARCH, \
										GPK_CLIENT_INTERACT_CONFIRM_DEPS, \
										GPK_CLIENT_INTERACT_CONFIRM_INSTALL, \
										GPK_CLIENT_INTERACT_PROGRESS, -1)
#define GPK_CLIENT_INTERACT_WARNING			pk_bitfield_from_enums (GPK_CLIENT_INTERACT_WARNING, -1)
#define GPK_CLIENT_INTERACT_WARNING_PROGRESS		pk_bitfield_from_enums (GPK_CLIENT_INTERACT_WARNING, \
										GPK_CLIENT_INTERACT_PROGRESS, -1)

typedef struct _GpkClientPrivate	 GpkClientPrivate;
typedef struct _GpkClient		 GpkClient;
typedef struct _GpkClientClass		 GpkClientClass;

struct _GpkClient
{
	GObject				 parent;
	GpkClientPrivate		*priv;
};

struct _GpkClientClass
{
	GObjectClass	parent_class;
};

GQuark		 gpk_client_error_quark			(void);
GType		 gpk_client_get_type			(void) G_GNUC_CONST;
GpkClient	*gpk_client_new				(void);

gboolean	 gpk_client_install_local_files		(GpkClient	*gclient,
							 gchar		**files_rel,
							 GError		**error);
gboolean	 gpk_client_install_provide_file	(GpkClient	*gclient,
							 const gchar	*full_path,
							 GError		**error);
gboolean	 gpk_client_install_mime_type		(GpkClient	*gclient,
							 const gchar	*mime_type,
							 GError		**error);
gboolean	 gpk_client_install_gstreamer_codecs	(GpkClient	*gclient,
							 gchar		**codec_name_strings,
							 GError		**error);
gboolean	 gpk_client_install_fonts		(GpkClient	*gclient,
							 gchar		**fonts,
							 GError		**error);
gboolean	 gpk_client_install_package_names	(GpkClient	*gclient,
							 gchar		**packages,
							 GError		**error);
gboolean	 gpk_client_install_package_ids		(GpkClient	*gclient,
							 gchar		**package_ids,
							 GError		**error);
gboolean	 gpk_client_install_catalogs		(GpkClient	*gclient,
							 gchar		**filenames,
							 GError		**error);
gboolean	 gpk_client_remove_package_ids		(GpkClient	*gclient,
							 gchar		**package_ids,
							 GError		**error);
gboolean	 gpk_client_update_system		(GpkClient	*gclient,
							 GError		**error);
gboolean	 gpk_client_refresh_cache		(GpkClient	*gclient,
							 GError		**error);
gboolean	 gpk_client_update_packages		(GpkClient	*gclient,
							 gchar		**package_ids,
							 GError		**error);
void		 gpk_client_set_interaction		(GpkClient	*gclient,
							 PkBitfield	 interact);
PkPackageList	*gpk_client_get_updates			(GpkClient	*gclient,
							 GError		**error);
const GPtrArray	*gpk_client_get_distro_upgrades		(GpkClient	*gclient,
							 GError		**error);
gchar		**gpk_client_get_file_list		(GpkClient	*gclient,
							 const gchar	*package_id,
							 GError		**error);
gboolean	 gpk_client_monitor_tid			(GpkClient	*gclient,
							 const gchar	*tid);
gboolean	 gpk_client_set_parent			(GpkClient	*gclient,
							 GtkWindow	*window);
gboolean	 gpk_client_set_parent_xid		(GpkClient	*gclient,
							 guint		 xid);
gboolean	 gpk_client_set_parent_exec		(GpkClient	*gclient,
							 const gchar	*exec);
gboolean	 gpk_client_update_timestamp		(GpkClient	*gclient,
							 guint		 timestamp);

G_END_DECLS

#endif /* __GPK_CLIENT_H */
