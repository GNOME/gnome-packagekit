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

#ifndef __GPK_COMMON_H
#define __GPK_COMMON_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <packagekit-glib/packagekit.h>

#include "gpk-animated-icon.h"

G_BEGIN_DECLS

#define GPK_CONF_DIR				"/apps/gnome-packagekit"
#define GPK_CONF_NOTIFY_COMPLETED		"/apps/gnome-packagekit/notify_complete"
#define GPK_CONF_NOTIFY_AVAILABLE		"/apps/gnome-packagekit/notify_available"
#define GPK_CONF_NOTIFY_DISTRO_UPGRADES		"/apps/gnome-packagekit/notify_distro_upgrades"
#define GPK_CONF_NOTIFY_CRITICAL		"/apps/gnome-packagekit/notify_critical"
#define GPK_CONF_NOTIFY_ERROR			"/apps/gnome-packagekit/notify_errors"
#define GPK_CONF_NOTIFY_MESSAGE			"/apps/gnome-packagekit/notify_message"
#define GPK_CONF_NOTIFY_UPDATE_STARTED		"/apps/gnome-packagekit/notify_update_started"
#define GPK_CONF_NOTIFY_UPDATE_NOT_BATTERY	"/apps/gnome-packagekit/notify_update_not_battery"
#define GPK_CONF_NOTIFY_UPDATE_FAILED		"/apps/gnome-packagekit/notify_update_failed"
#define GPK_CONF_NOTIFY_UPDATE_COMPLETE		"/apps/gnome-packagekit/notify_update_complete"
#define GPK_CONF_NOTIFY_UPDATE_COMPLETE_RESTART	"/apps/gnome-packagekit/notify_update_complete_restart"
#define GPK_CONF_CONNECTION_USE_MOBILE		"/apps/gnome-packagekit/connection_use_mobile"
#define GPK_CONF_CONNECTION_USE_WIFI		"/apps/gnome-packagekit/connection_use_wifi"
#define GPK_CONF_AUTOCOMPLETE			"/apps/gnome-packagekit/autocomplete"
#define GPK_CONF_SESSION_STARTUP_TIMEOUT	"/apps/gnome-packagekit/session_startup_timeout"
#define GPK_CONF_FORCE_GET_UPDATES_LOGIN	"/apps/gnome-packagekit/force_get_updates_login"
#define GPK_CONF_FREQUENCY_GET_UPDATES		"/apps/gnome-packagekit/frequency_get_updates"
#define GPK_CONF_FREQUENCY_GET_UPGRADES		"/apps/gnome-packagekit/frequency_get_upgrades"
#define GPK_CONF_FREQUENCY_REFRESH_CACHE	"/apps/gnome-packagekit/frequency_refresh_cache"
#define GPK_CONF_AUTO_UPDATE			"/apps/gnome-packagekit/auto_update"
#define GPK_CONF_UPDATE_BATTERY			"/apps/gnome-packagekit/update_battery"
#define GPK_CONF_SHOW_DEPENDS			"/apps/gnome-packagekit/show_depends"
#define GPK_CONF_SHOW_COPY_CONFIRM		"/apps/gnome-packagekit/show_copy_confirm"
#define GPK_CONF_BANNED_FIRMWARE		"/apps/gnome-packagekit/banned_firmware"
#define GPK_CONF_DBUS_DEFAULT_INTERACTION	"/apps/gnome-packagekit/dbus_default_interaction"
#define GPK_CONF_DBUS_ENFORCED_INTERACTION	"/apps/gnome-packagekit/dbus_enforced_interaction"
#define GPK_CONF_REPO_SHOW_DETAILS		"/apps/gnome-packagekit/repo/show_details"
#define GPK_CONF_APPLICATION_FILTER_BASENAME	"/apps/gnome-packagekit/application/filter_basename"
#define GPK_CONF_APPLICATION_FILTER_NEWEST	"/apps/gnome-packagekit/application/filter_newest"
#define GPK_CONF_APPLICATION_CATEGORY_GROUPS	"/apps/gnome-packagekit/application/category_groups"
#define GPK_CONF_APPLICATION_SEARCH_MODE	"/apps/gnome-packagekit/application/search_mode"
#define GPK_CONF_UPDATE_VIEWER_PRECACHE_DETAILS	"/apps/gnome-packagekit/update-viewer/precache_details"
#define GPK_CONF_UPDATE_VIEWER_MOBILE_BBAND	"/apps/gnome-packagekit/update-viewer/notify_mobile_connection"
#define GPK_CONF_IGNORED_MESSAGES		"/apps/gnome-packagekit/ignored_messages"

#define GPK_CONF_ENABLE_FONT_HELPER		"/apps/gnome-packagekit/enable_font_helper"
#define GPK_CONF_ENABLE_CODEC_HELPER		"/apps/gnome-packagekit/enable_codec_helper"
#define GPK_CONF_ENABLE_MIME_TYPE_HELPER	"/apps/gnome-packagekit/enable_mime_type_helper"
#define GPK_CONF_ENABLE_CHECK_FIRMWARE		"/apps/gnome-packagekit/enable_check_firmware"
#define GPK_CONF_ENABLE_CHECK_HARDWARE		"/apps/gnome-packagekit/enable_check_hardware"

#define GPK_BUGZILLA_URL			"https://bugs.freedesktop.org/"

#define GPK_ICON_SOFTWARE_UPDATE		"system-software-update"
#define GPK_ICON_SOFTWARE_SOURCES		"gpk-repo"
#define GPK_ICON_SOFTWARE_INSTALLER		"system-software-install"
#define GPK_ICON_SOFTWARE_LOG			"gpk-log"
#define GPK_ICON_SOFTWARE_UPDATE_PREFS		"gpk-prefs"
#define GPK_ICON_SOFTWARE_UPDATE_AVAILABLE	"software-update-available"
#define GPK_ICON_SERVICE_PACK			"gpk-service-pack"

void		 gpk_common_test			(gpointer	 data);
void		 gtk_text_buffer_insert_markup		(GtkTextBuffer	*buffer,
							 GtkTextIter	*iter,
							 const gchar	*markup);
gchar		*gpk_package_get_name			(const gchar	*package_id);
gchar		*gpk_package_id_format_twoline		(const PkPackageId *id,
							 const gchar	*summary);
gchar		*gpk_package_id_format_oneline		(const PkPackageId *id,
							 const gchar	*summary);
gchar		*gpk_package_id_name_version		(const PkPackageId *id);

gchar		*gpk_time_to_localised_string		(guint		 time_secs);
gchar		*gpk_time_to_imprecise_string		(guint		 time_secs);
gboolean	 gpk_check_privileged_user		(const gchar	*application_name,
							 gboolean	 show_ui);
gboolean	 gpk_set_animated_icon_from_status	(GpkAnimatedIcon *icon,
							 PkStatusEnum	 status,
							 GtkIconSize	 size);
gchar		*gpk_strv_join_locale			(gchar		**array);
GtkEntryCompletion *gpk_package_entry_completion_new	(void);
gboolean	 gpk_window_set_size_request		(GtkWindow	*window,
							 guint		 width,
							 guint		 height);
gboolean	 gpk_session_logout			(void);

G_END_DECLS

#endif	/* __GPK_COMMON_H */
