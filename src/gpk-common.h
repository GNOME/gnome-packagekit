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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __GPK_COMMON_H
#define __GPK_COMMON_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

#include "gpk-animated-icon.h"
#include "gpk-enum.h"

G_BEGIN_DECLS

#define GPK_SETTINGS_SCHEMA				"org.gnome.packagekit"
#define GPK_SETTINGS_AUTOCOMPLETE			"autocomplete"
#define GPK_SETTINGS_AUTO_UPDATE			"auto-update"
#define GPK_SETTINGS_BANNED_FIRMWARE			"banned-firmware"
#define GPK_SETTINGS_CATEGORY_GROUPS			"category-groups"
#define GPK_SETTINGS_CONNECTION_USE_MOBILE		"connection-use-mobile"
#define GPK_SETTINGS_CONNECTION_USE_WIFI		"connection-use-wifi"
#define GPK_SETTINGS_DBUS_DEFAULT_INTERACTION		"dbus-default-interaction"
#define GPK_SETTINGS_DBUS_ENFORCED_INTERACTION		"dbus-enforced-interaction"
#define GPK_SETTINGS_ENABLE_AUTOREMOVE			"enable-autoremove"
#define GPK_SETTINGS_ENABLE_CHECK_FIRMWARE		"enable-check-firmware"
#define GPK_SETTINGS_ENABLE_CHECK_HARDWARE		"enable-check-hardware"
#define GPK_SETTINGS_ENABLE_CODEC_HELPER		"enable-codec-helper"
#define GPK_SETTINGS_ENABLE_FONT_HELPER			"enable-font-helper"
#define GPK_SETTINGS_ENABLE_MIME_TYPE_HELPER		"enable-mime-type-helper"
#define GPK_SETTINGS_FILTER_ARCH			"filter-arch"
#define GPK_SETTINGS_FILTER_BASENAME			"filter-basename"
#define GPK_SETTINGS_FILTER_NEWEST			"filter-newest"
#define GPK_SETTINGS_FORCE_GET_UPDATES_LOGIN		"force-get-updates-login"
#define GPK_SETTINGS_FREQUENCY_GET_UPDATES		"frequency-get-updates"
#define GPK_SETTINGS_FREQUENCY_GET_UPGRADES		"frequency-get-upgrades"
#define GPK_SETTINGS_FREQUENCY_REFRESH_CACHE		"frequency-refresh-cache"
#define GPK_SETTINGS_IGNORED_DBUS_REQUESTS		"ignored-dbus-requests"
#define GPK_SETTINGS_IGNORED_DEVICES			"ignored-devices"
#define GPK_SETTINGS_IGNORED_MESSAGES			"ignored-messages"
#define GPK_SETTINGS_INSTALL_ROOT			"install-root"
#define GPK_SETTINGS_MEDIA_REPO_FILENAMES		"media-repo-filenames"
#define GPK_SETTINGS_NOTIFY_AVAILABLE			"notify-available"
#define GPK_SETTINGS_NOTIFY_COMPLETED			"notify-complete"
#define GPK_SETTINGS_NOTIFY_CRITICAL			"notify-critical"
#define GPK_SETTINGS_NOTIFY_DISTRO_UPGRADES		"notify-distro-upgrades"
#define GPK_SETTINGS_NOTIFY_ERROR			"notify-errors"
#define GPK_SETTINGS_NOTIFY_MESSAGE			"notify-message"
#define GPK_SETTINGS_NOTIFY_MOBILE_CONNECTION		"notify-mobile-connection"
#define GPK_SETTINGS_NOTIFY_UPDATE_COMPLETE		"notify-update-complete"
#define GPK_SETTINGS_NOTIFY_UPDATE_COMPLETE_RESTART	"notify-update-complete-restart"
#define GPK_SETTINGS_NOTIFY_UPDATE_FAILED		"notify-update-failed"
#define GPK_SETTINGS_NOTIFY_UPDATE_NOT_BATTERY		"notify-update-not-battery"
#define GPK_SETTINGS_NOTIFY_UPDATE_STARTED		"notify-update-started"
#define GPK_SETTINGS_ONLY_NEWEST			"only-newest"
#define GPK_SETTINGS_REPO_SHOW_DETAILS			"repo-show-details"
#define GPK_SETTINGS_SCROLL_ACTIVE			"scroll-active"
#define GPK_SETTINGS_SEARCH_MODE			"search-mode"
#define GPK_SETTINGS_SESSION_STARTUP_TIMEOUT		"session-startup-timeout"
#define GPK_SETTINGS_SHOW_ALL_PACKAGES			"show-all-packages"
#define GPK_SETTINGS_SHOW_COPY_CONFIRM			"show-copy-confirm"
#define GPK_SETTINGS_SHOW_DEPENDS			"show-depends"
#define GPK_SETTINGS_UPDATE_BATTERY			"update-battery"

#define GPK_BUGZILLA_URL			"https://bugs.freedesktop.org/"

#define GPK_ICON_SOFTWARE_UPDATE		"system-software-update"
#define GPK_ICON_SOFTWARE_SOURCES		"gpk-repo"
#define GPK_ICON_SOFTWARE_INSTALLER		"system-software-install"
#define GPK_ICON_SOFTWARE_LOG			"gpk-log"
#define GPK_ICON_SOFTWARE_UPDATE_PREFS		"gpk-prefs"
#define GPK_ICON_SERVICE_PACK			"gpk-service-pack"

/* any status that is slower than this will not be shown in the UI */
#define GPK_UI_STATUS_SHOW_DELAY		250 /* ms */

/* libnotify dummy code */
#ifndef HAVE_NOTIFY
#define	notify_init(f1)						/* nothing */
#define	notify_is_initted(f1)					FALSE
#define	notify_notification_close(f1,f2)			TRUE
#define	notify_notification_show(f1,f2)				TRUE
#define	notify_notification_set_timeout(f1,f2)			/* nothing */
#define	notify_notification_set_urgency(f1,f2)			/* nothing */
#define	notify_notification_add_action(f1,f2,f3,f4,f5,f6)	/* nothing */
#define NotifyNotification					GtkWidget
#define	NotifyUrgency						guint
#define	notify_notification_new(f1,f2,f3,f4)			gtk_fixed_new()
#define	notify_notification_new_with_status_icon(f1,f2,f3,f4)	gtk_fixed_new()
#define NOTIFY_URGENCY_LOW					0
#define NOTIFY_URGENCY_NORMAL					1
#define NOTIFY_URGENCY_CRITICAL					2
#define NOTIFY_EXPIRES_NEVER					0
#endif

void		 gpk_common_test			(gpointer	 data);
void		 gtk_text_buffer_insert_markup		(GtkTextBuffer	*buffer,
							 GtkTextIter	*iter,
							 const gchar	*markup);
gchar		*gpk_package_id_format_twoline		(const gchar 	*package_id,
							 const gchar	*summary);
gchar		*gpk_package_id_format_oneline		(const gchar 	*package_id,
							 const gchar	*summary);
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
gboolean	 gpk_ignore_session_error		(GError		*error);
gboolean	 gpk_window_set_parent_xid		(GtkWindow	*window,
							 guint32	 xid);
GPtrArray	*pk_strv_to_ptr_array			(gchar		**array)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		**pk_package_array_to_strv		(GPtrArray	*array);

G_END_DECLS

#endif	/* __GPK_COMMON_H */
