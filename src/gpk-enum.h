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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __GPK_ENUM_H
#define __GPK_ENUM_H

#include <glib-object.h>
#include <packagekit-glib2/packagekit.h>

G_BEGIN_DECLS

/**
 * GpkUpdateEnum:
 *
 * The update type
 **/
typedef enum {
	GPK_UPDATE_ENUM_ALL,
	GPK_UPDATE_ENUM_SECURITY,
	GPK_UPDATE_ENUM_NONE,
	GPK_UPDATE_ENUM_UNKNOWN
} GpkUpdateEnum;

typedef enum {
	GPK_INFO_ENUM_DOWNLOADING	= PK_INFO_ENUM_DOWNLOADING,
	GPK_INFO_ENUM_UPDATING		= PK_INFO_ENUM_UPDATING,
	GPK_INFO_ENUM_INSTALLING	= PK_INFO_ENUM_INSTALLING,
	GPK_INFO_ENUM_REMOVING		= PK_INFO_ENUM_REMOVING,
	GPK_INFO_ENUM_CLEANUP		= PK_INFO_ENUM_CLEANUP,
	GPK_INFO_ENUM_OBSOLETING	= PK_INFO_ENUM_OBSOLETING,
	GPK_INFO_ENUM_DOWNLOADED	= PK_INFO_ENUM_UNKNOWN + PK_INFO_ENUM_DOWNLOADING,
	GPK_INFO_ENUM_UPDATED		= PK_INFO_ENUM_UNKNOWN + PK_INFO_ENUM_UPDATING,
	GPK_INFO_ENUM_INSTALLED		= PK_INFO_ENUM_UNKNOWN + PK_INFO_ENUM_INSTALLING,
	GPK_INFO_ENUM_REMOVED		= PK_INFO_ENUM_UNKNOWN + PK_INFO_ENUM_REMOVING,
	GPK_INFO_ENUM_CLEANEDUP		= PK_INFO_ENUM_UNKNOWN + PK_INFO_ENUM_CLEANUP,
	GPK_INFO_ENUM_OBSOLETED		= PK_INFO_ENUM_UNKNOWN + PK_INFO_ENUM_OBSOLETING,
	GPK_INFO_ENUM_UNKNOWN
} GpkInfoStatusEnum;

/* constants defined in previous versions */
#if (!PK_CHECK_VERSION(0,7,2))
#define PK_ROLE_ENUM_REPAIR_SYSTEM			(PK_ROLE_ENUM_UPGRADE_SYSTEM+1)
#endif

#if (!PK_CHECK_VERSION(0,8,1))
#define PK_ERROR_ENUM_UNFINISHED_TRANSACTION		(PK_ERROR_ENUM_CANCELLED_PRIORITY+1)
#define PK_ERROR_ENUM_LOCK_REQUIRED			(PK_ERROR_ENUM_UNFINISHED_TRANSACTION+1)
#endif

#if (!PK_CHECK_VERSION(0,9,1))
#define PK_ROLE_ENUM_DEPENDS_ON				(PK_ROLE_ENUM_GET_DEPENDS)
#define PK_ROLE_ENUM_REQUIRED_BY			(PK_ROLE_ENUM_GET_REQUIRES)
#define PK_ROLE_ENUM_GET_DETAILS_LOCAL			(PK_ROLE_ENUM_REPAIR_SYSTEM+1)
#define PK_ROLE_ENUM_GET_FILES_LOCAL			(PK_ROLE_ENUM_GET_DETAILS_LOCAL+1)
#define PK_ROLE_ENUM_REPO_REMOVE			(PK_ROLE_ENUM_GET_FILES_LOCAL+1)
#endif

const gchar	*gpk_role_enum_to_localised_past	(PkRoleEnum	 role)
							 G_GNUC_CONST;
const gchar	*gpk_role_enum_to_localised_present	(PkRoleEnum	 role)
							 G_GNUC_CONST;
const gchar	*gpk_role_enum_to_icon_name		(PkRoleEnum	 role);
const gchar	*gpk_media_type_enum_to_localised_text	(PkMediaTypeEnum type)
							 G_GNUC_CONST;
const gchar	*gpk_info_enum_to_localised_text	(PkInfoEnum	 info)
							 G_GNUC_CONST;
const gchar	*gpk_info_enum_to_localised_past	(PkInfoEnum	 info)
							 G_GNUC_CONST;
const gchar	*gpk_info_enum_to_localised_present	(PkInfoEnum	 info)
							 G_GNUC_CONST;
const gchar	*gpk_info_enum_to_icon_name		(PkInfoEnum	 info);
const gchar	*gpk_status_enum_to_localised_text	(PkStatusEnum	 status)
							 G_GNUC_CONST;
const gchar	*gpk_status_enum_to_icon_name		(PkStatusEnum	 status);
const gchar	*gpk_status_enum_to_animation		(PkStatusEnum	 status);
const gchar	*gpk_restart_enum_to_icon_name		(PkRestartEnum	 restart);
const gchar	*gpk_restart_enum_to_dialog_icon_name	(PkRestartEnum	 restart);
const gchar	*gpk_error_enum_to_localised_text	(PkErrorEnum code)
							 G_GNUC_CONST;
const gchar	*gpk_error_enum_to_localised_message	(PkErrorEnum code);
const gchar	*gpk_restart_enum_to_localised_text	(PkRestartEnum	 restart)
							 G_GNUC_CONST;
const gchar	*gpk_update_state_enum_to_localised_text (PkUpdateStateEnum state)
							 G_GNUC_CONST;
const gchar	*gpk_restart_enum_to_localised_text_future(PkRestartEnum	 restart)
							 G_GNUC_CONST;
const gchar	*gpk_group_enum_to_localised_text	(PkGroupEnum	 group)
							 G_GNUC_CONST;
const gchar	*gpk_group_enum_to_icon_name		(PkGroupEnum	 group);
gchar		*gpk_update_enum_to_localised_text	(PkInfoEnum	 info,
							 guint		 number)
							 G_GNUC_CONST;
const gchar	*gpk_info_status_enum_to_string		(GpkInfoStatusEnum info);
const gchar	*gpk_info_status_enum_to_icon_name	(GpkInfoStatusEnum info);

G_END_DECLS

#endif	/* __GPK_ENUM_H */

