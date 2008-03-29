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

#ifndef __PK_COMMON_GUI_H
#define __PK_COMMON_GUI_H

#include <glib-object.h>
#include <pk-enum.h>

G_BEGIN_DECLS

#define PK_PROGRESS_BAR_PULSE_DELAY	50
#define PK_PROGRESS_BAR_PULSE_STEP	0.05

#define PK_CONF_NOTIFY_COMPLETED	"/apps/gnome-packagekit/notify_complete"
#define PK_CONF_NOTIFY_AVAILABLE	"/apps/gnome-packagekit/notify_available"
#define PK_CONF_NOTIFY_CRITICAL		"/apps/gnome-packagekit/notify_critical"
#define PK_CONF_NOTIFY_ERROR		"/apps/gnome-packagekit/notify_errors"
#define PK_CONF_NOTIFY_MESSAGE		"/apps/gnome-packagekit/notify_message"
#define PK_CONF_NOTIFY_STARTED		"/apps/gnome-packagekit/notify_started"
#define PK_CONF_NOTIFY_BATTERY_UPDATE	"/apps/gnome-packagekit/notify_battery_update"
#define PK_CONF_NOTIFY_RESTART		"/apps/gnome-packagekit/notify_restart"
#define PK_CONF_AUTOCOMPLETE		"/apps/gnome-packagekit/autocomplete"
#define PK_CONF_SESSION_STARTUP_TIMEOUT	"/apps/gnome-packagekit/session_startup_timeout"
#define PK_CONF_FREQUENCY_GET_UPDATES	"/apps/gnome-packagekit/frequency_get_updates"
#define PK_CONF_FREQUENCY_REFRESH_CACHE	"/apps/gnome-packagekit/frequency_refresh_cache"
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

gchar		*pk_package_get_name			(const gchar	*package_id);
gchar		*pk_package_id_pretty			(const gchar	*package_id,
							 const gchar	*summary);
gchar		*pk_package_id_pretty_oneline		(const gchar	*package_id,
							 const gchar	*summary);
gchar		*pk_package_id_name_version		(const gchar	*package_id);
gboolean	 pk_icon_valid				(const gchar	*icon);
gboolean	 pk_error_modal_dialog			(const gchar	*title,
							 const gchar	*message);
gboolean	 pk_execute_url				(const gchar	*url);
gboolean	 pk_restart_system			(void);
const gchar	*pk_role_enum_to_localised_past		(PkRoleEnum	 role);
const gchar	*pk_role_enum_to_localised_present	(PkRoleEnum	 role);
const gchar	*pk_role_enum_to_icon_name		(PkRoleEnum	 role);
const gchar	*pk_info_enum_to_localised_text		(PkInfoEnum	 info);
const gchar	*pk_info_enum_to_icon_name		(PkInfoEnum	 info);
const gchar	*pk_status_enum_to_localised_text	(PkStatusEnum	 status);
const gchar	*pk_status_enum_to_icon_name		(PkStatusEnum	 status);
const gchar	*pk_restart_enum_to_icon_name		(PkRestartEnum	 restart);
const gchar	*pk_error_enum_to_localised_text	(PkErrorCodeEnum code);
const gchar	*pk_error_enum_to_localised_message	(PkErrorCodeEnum code);
const gchar	*pk_restart_enum_to_localised_text	(PkRestartEnum	 restart);
const gchar	*pk_message_enum_to_icon_name		(PkMessageEnum	 message);
const gchar	*pk_message_enum_to_localised_text	(PkMessageEnum	 message);
const gchar	*pk_restart_enum_to_localised_text_future(PkRestartEnum	 restart);
const gchar	*pk_group_enum_to_localised_text	(PkGroupEnum	 group);
const gchar	*pk_group_enum_to_icon_name		(PkGroupEnum	 group);
gchar		*pk_size_to_si_size_text		(guint64	 size);
gchar		*pk_update_enum_to_localised_text	(PkInfoEnum	 info,
							 guint		 number);
gchar		*pk_time_to_localised_string		(guint		 time_secs);

G_END_DECLS

#endif	/* __PK_COMMON_GUI_H */
