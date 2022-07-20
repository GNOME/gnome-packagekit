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

#include "gpk-enum.h"

G_BEGIN_DECLS

#define GPK_SETTINGS_SCHEMA				"org.gnome.packagekit"
#define GPK_SETTINGS_CATEGORY_GROUPS			"category-groups"
#define GPK_SETTINGS_DBUS_DEFAULT_INTERACTION		"dbus-default-interaction"
#define GPK_SETTINGS_DBUS_ENFORCED_INTERACTION		"dbus-enforced-interaction"
#define GPK_SETTINGS_ENABLE_AUTOREMOVE			"enable-autoremove"
#define GPK_SETTINGS_ENABLE_CODEC_HELPER		"enable-codec-helper"
#define GPK_SETTINGS_ENABLE_FONT_HELPER			"enable-font-helper"
#define GPK_SETTINGS_ENABLE_MIME_TYPE_HELPER		"enable-mime-type-helper"
#define GPK_SETTINGS_FILTER_ARCH			"filter-arch"
#define GPK_SETTINGS_FILTER_BASENAME			"filter-basename"
#define GPK_SETTINGS_FILTER_NEWEST			"filter-newest"
#define GPK_SETTINGS_FILTER_SUPPORTED			"filter-supported"
#define GPK_SETTINGS_IGNORED_DBUS_REQUESTS		"ignored-dbus-requests"
#define GPK_SETTINGS_ONLY_NEWEST			"only-newest"
#define GPK_SETTINGS_REPO_SHOW_DETAILS			"repo-show-details"
#define GPK_SETTINGS_SCROLL_ACTIVE			"scroll-active"
#define GPK_SETTINGS_SEARCH_MODE			"search-mode"
#define GPK_SETTINGS_SHOW_ALL_PACKAGES			"show-all-packages"
#define GPK_SETTINGS_SHOW_DEPENDS			"show-depends"

#define GPK_ICON_SOFTWARE_UPDATE		"system-software-update"
#define GPK_ICON_SOFTWARE_SOURCES		"gpk-repo"
#define GPK_ICON_SOFTWARE_INSTALLER		"system-software-install"
#define GPK_ICON_SOFTWARE_LOG			"gpk-log"
#define GPK_ICON_SOFTWARE_UPDATE_PREFS		"gpk-prefs"

/* any status that is slower than this will not be shown in the UI */
#define GPK_UI_STATUS_SHOW_DELAY		750 /* ms */

gchar		*gpk_package_id_format_twoline		(GtkStyleContext *style,
							 const gchar 	*package_id,
							 const gchar	*summary);
gchar		*gpk_package_id_format_oneline		(const gchar 	*package_id,
							 const gchar	*summary);
gboolean	 gpk_check_privileged_user		(const gchar	*application_name,
							 gboolean	 show_ui);
gchar		*gpk_strv_join_locale			(gchar		**array);
gboolean	 gpk_window_set_size_request		(GtkWindow	*window,
							 gint		 width,
							 gint		 height);
gboolean	 gpk_window_set_parent_xid		(GtkWindow	*window,
							 guint32	 xid);
GPtrArray	*pk_strv_to_ptr_array			(gchar		**array)
							 G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

#endif	/* __GPK_COMMON_H */
