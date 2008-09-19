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

#ifndef __GPK_ENUM_H
#define __GPK_ENUM_H

#include <glib-object.h>
#include <pk-enum.h>

G_BEGIN_DECLS

/**
 * GpkFreqEnum:
 *
 * The frequency type
 **/
typedef enum {
	GPK_FREQ_ENUM_HOURLY,
	GPK_FREQ_ENUM_DAILY,
	GPK_FREQ_ENUM_WEEKLY,
	GPK_FREQ_ENUM_NEVER,
	GPK_FREQ_ENUM_UNKNOWN
} GpkFreqEnum;

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

void		 gpk_enum_test				(gpointer	 data);
const gchar	*gpk_role_enum_to_localised_past	(PkRoleEnum	 role)
							 G_GNUC_CONST;
const gchar	*gpk_role_enum_to_localised_present	(PkRoleEnum	 role)
							 G_GNUC_CONST;
GpkFreqEnum	 gpk_freq_enum_from_text		(const gchar	*freq);
const gchar	*gpk_freq_enum_to_text			(GpkFreqEnum	 freq);
GpkUpdateEnum	 gpk_update_enum_from_text		(const gchar	*update);
const gchar	*gpk_update_enum_to_text		(GpkUpdateEnum	 update);
const gchar	*gpk_role_enum_to_icon_name		(PkRoleEnum	 role);
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
const gchar	*gpk_restart_enum_to_icon_name		(PkRestartEnum	 restart);
const gchar	*gpk_error_enum_to_localised_text	(PkErrorCodeEnum code)
							 G_GNUC_CONST;
const gchar	*gpk_error_enum_to_localised_message	(PkErrorCodeEnum code);
const gchar	*gpk_restart_enum_to_localised_text	(PkRestartEnum	 restart)
							 G_GNUC_CONST;
const gchar	*gpk_update_state_enum_to_localised_text (PkUpdateStateEnum state)
							 G_GNUC_CONST;
const gchar	*gpk_message_enum_to_icon_name		(PkMessageEnum	 message);
const gchar	*gpk_message_enum_to_localised_text	(PkMessageEnum	 message)
							 G_GNUC_CONST;
const gchar	*gpk_restart_enum_to_localised_text_future(PkRestartEnum	 restart)
							 G_GNUC_CONST;
const gchar	*gpk_group_enum_to_localised_text	(PkGroupEnum	 group)
							 G_GNUC_CONST;
const gchar	*gpk_group_enum_to_icon_name		(PkGroupEnum	 group);
gchar		*gpk_update_enum_to_localised_text	(PkInfoEnum	 info,
							 guint		 number)
							 G_GNUC_CONST;

G_END_DECLS

#endif	/* __GPK_ENUM_H */

