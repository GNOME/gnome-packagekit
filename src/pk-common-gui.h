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

#ifndef __PK_COMMON_H
#define __PK_COMMON_H

#include <glib-object.h>
#include <pk-enum.h>

G_BEGIN_DECLS

#define PK_CONF_NOTIFY_COMPLETED	"/apps/gnome-packagekit/notify_complete"
#define PK_CONF_NOTIFY_AVAILABLE	"/apps/gnome-packagekit/notify_available"
#define PK_CONF_FIND_AS_TYPE		"/apps/gnome-packagekit/find_as_you_type"
#define PK_CONF_UPDATE_TIMEOUT		"/apps/gnome-packagekit/update_timeout"
#define PK_CONF_UPDATE_CHECK		"/apps/gnome-packagekit/update_check"
#define PK_CONF_AUTO_UPDATE		"/apps/gnome-packagekit/auto_update"
#define PK_CONF_UPDATE_BATTERY		"/apps/gnome-packagekit/update_battery"

#define GS_DBUS_SERVICE			"org.gnome.ScreenSaver"
#define GS_DBUS_PATH			"/org/gnome/ScreenSaver"
#define GS_DBUS_INTERFACE		"org.gnome.ScreenSaver"

#define GPM_DBUS_SERVICE		"org.freedesktop.PowerManagement"
#define GPM_DBUS_PATH			"/org/freedesktop/PowerManagement"
#define GPM_DBUS_PATH_INHIBIT		"/org/freedesktop/PowerManagement/Inhibit"
#define GPM_DBUS_INTERFACE		"org.freedesktop.PowerManagement"
#define GPM_DBUS_INTERFACE_INHIBIT	"org.freedesktop.PowerManagement.Inhibit"

gchar *		 pk_package_get_name			(const gchar	*package_id);
gchar *		 pk_package_id_pretty			(const gchar	*package_id,
							 const gchar	*summary);
gboolean	 pk_error_modal_dialog			(const gchar	*title,
							 const gchar	*message);
const gchar	*pk_role_enum_to_localised_past		(PkRoleEnum	 role);
const gchar	*pk_role_enum_to_localised_present	(PkRoleEnum	 role);
const gchar	*pk_role_enum_to_icon_name		(PkRoleEnum	 role);
const gchar	*pk_info_enum_to_localised_text		(PkInfoEnum	 info);
const gchar	*pk_info_enum_to_icon_name		(PkInfoEnum	 info);
const gchar	*pk_status_enum_to_localised_text	(PkStatusEnum	 status);
const gchar	*pk_status_enum_to_icon_name		(PkStatusEnum	 status);
const gchar	*pk_error_enum_to_localised_text	(PkErrorCodeEnum code);
const gchar	*pk_restart_enum_to_localised_text	(PkRestartEnum	 restart);
const gchar	*pk_group_enum_to_localised_text	(PkGroupEnum	 group);
const gchar	*pk_group_enum_to_icon_name		(PkGroupEnum	 group);
gchar		*pk_size_to_si_size_text		(guint64	 size);

G_END_DECLS

#endif	/* __PK_COMMON_H */
