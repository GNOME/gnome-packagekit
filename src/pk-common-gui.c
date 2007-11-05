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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <math.h>
#include <string.h>
#include <gtk/gtk.h>

#include <pk-debug.h>
#include <pk-package-id.h>
#include <pk-enum.h>
#include <pk-common.h>
#include "pk-common-gui.h"

/* icon names */
static PkEnumMatch enum_info_icon_name[] = {
	{PK_INFO_ENUM_UNKNOWN,			"help-browser"},	/* fall though value */
	{PK_INFO_ENUM_INSTALLED,		"package-x-generic"},
	{PK_INFO_ENUM_AVAILABLE,		"network-workgroup"},
	{PK_INFO_ENUM_LOW,			"software-update-available"},
	{PK_INFO_ENUM_NORMAL,			"software-update-available"},
	{PK_INFO_ENUM_IMPORTANT,		"software-update-urgent"},
	{PK_INFO_ENUM_SECURITY,			"software-update-urgent"},
	{PK_INFO_ENUM_DOWNLOADING,		"mail-send-receive"},
	{PK_INFO_ENUM_UPDATING,			"system-software-update"},
	{PK_INFO_ENUM_INSTALLING,		"emblem-system"},
	{PK_INFO_ENUM_REMOVING,			"edit-clear"},
	{PK_INFO_ENUM_OBSOLETING,		"edit-clear"},
	{PK_INFO_ENUM_REMOVING,			"edit-clear"},
	{0, NULL},
};

static PkEnumMatch enum_status_icon_name[] = {
	{PK_STATUS_ENUM_UNKNOWN,		"help-browser"},	/* fall though value */
	{PK_STATUS_ENUM_WAIT,			"media-playback-pause"},
	{PK_STATUS_ENUM_SETUP,			"emblem-system"},
	{PK_STATUS_ENUM_QUERY,			"system-search"},
	{PK_STATUS_ENUM_REFRESH_CACHE,		"view-refresh"},
	{PK_STATUS_ENUM_REMOVE,			"edit-clear"},
	{PK_STATUS_ENUM_DOWNLOAD,		"mail-send-receive"},
	{PK_STATUS_ENUM_INSTALL,		"emblem-system"},
	{PK_STATUS_ENUM_UPDATE,			"system-software-update"},
	{PK_STATUS_ENUM_CLEANUP,		"edit-clear"},
	{PK_STATUS_ENUM_OBSOLETE,		"edit-clear"},
	{0, NULL},
};

static PkEnumMatch enum_role_icon_name[] = {
	{PK_ROLE_ENUM_UNKNOWN,			"help-browser"},	/* fall though value */
	{PK_ROLE_ENUM_CANCEL,			"view-refresh"}, /* TODO: need better icon */
	{PK_ROLE_ENUM_RESOLVE,			"system-search"},
	{PK_ROLE_ENUM_ROLLBACK,			"view-refresh"}, /* TODO: need better icon */
	{PK_ROLE_ENUM_GET_DEPENDS,		"system-search"},
	{PK_ROLE_ENUM_GET_UPDATE_DETAIL,	"system-search"},
	{PK_ROLE_ENUM_GET_DESCRIPTION,		"system-search"},
	{PK_ROLE_ENUM_GET_REQUIRES,		"system-search"},
	{PK_ROLE_ENUM_GET_UPDATES,		"system-search"},
	{PK_ROLE_ENUM_SEARCH_DETAILS,		"system-search"},
	{PK_ROLE_ENUM_SEARCH_FILE,		"system-search"},
	{PK_ROLE_ENUM_SEARCH_GROUP,		"system-search"},
	{PK_ROLE_ENUM_SEARCH_NAME,		"system-search"},
	{PK_ROLE_ENUM_REFRESH_CACHE,		"view-refresh"},
	{PK_ROLE_ENUM_REMOVE_PACKAGE,		"edit-clear"},
	{PK_ROLE_ENUM_INSTALL_PACKAGE,		"emblem-system"}, /* TODO: need better icon */
	{PK_ROLE_ENUM_INSTALL_FILE,		"emblem-system"}, /* TODO: need better icon */
	{PK_ROLE_ENUM_UPDATE_PACKAGE,		"emblem-system"},
	{PK_ROLE_ENUM_UPDATE_SYSTEM,		"system-software-update"},
	{PK_ROLE_ENUM_GET_REPO_LIST,		"emblem-system"},
	{PK_ROLE_ENUM_REPO_ENABLE,		"emblem-system"},
	{PK_ROLE_ENUM_REPO_SET_DATA,		"emblem-system"},
	{0, NULL},
};

static PkEnumMatch enum_group_icon_name[] = {
	{PK_GROUP_ENUM_UNKNOWN,			"help-browser"},	/* fall though value */
	{PK_GROUP_ENUM_ACCESSIBILITY,		"preferences-desktop-accessibility"},
	{PK_GROUP_ENUM_ACCESSORIES,		"applications-accessories"},
	{PK_GROUP_ENUM_EDUCATION,		"utilities-system-monitor"},
	{PK_GROUP_ENUM_GAMES,			"applications-games"},
	{PK_GROUP_ENUM_GRAPHICS,		"applications-graphics"},
	{PK_GROUP_ENUM_INTERNET,		"applications-internet"},
	{PK_GROUP_ENUM_OFFICE,			"applications-office"},
	{PK_GROUP_ENUM_OTHER,			"applications-other"},
	{PK_GROUP_ENUM_PROGRAMMING,		"applications-development"},
	{PK_GROUP_ENUM_MULTIMEDIA,		"applications-multimedia"},
	{PK_GROUP_ENUM_SYSTEM,			"applications-system"},
	{PK_GROUP_ENUM_DESKTOP_GNOME,		"pk-desktop-gnome"},
	{PK_GROUP_ENUM_DESKTOP_KDE,		"pk-desktop-kde"},
	{PK_GROUP_ENUM_DESKTOP_XFCE,		"pk-desktop-xfce"},
	{PK_GROUP_ENUM_DESKTOP_OTHER,		"user-desktop"},
	{PK_GROUP_ENUM_PUBLISHING,		"internet-news-reader"},
	{PK_GROUP_ENUM_SERVERS,			"network-server"},
	{PK_GROUP_ENUM_FONTS,			"preferences-desktop-font"},
	{PK_GROUP_ENUM_ADMIN_TOOLS,		"system-lock-screen"},
	{PK_GROUP_ENUM_LEGACY,			"media-floppy"},
	{PK_GROUP_ENUM_LOCALIZATION,		"preferences-desktop-locale"},
	{PK_GROUP_ENUM_VIRTUALIZATION,		"computer"},
	{PK_GROUP_ENUM_SECURITY,		"network-wireless-encrypted"},
	{PK_GROUP_ENUM_POWER_MANAGEMENT,	"battery"},
	{0, NULL},
};

/**
 * pk_execute_url:
 **/
gboolean
pk_execute_url (const gchar *url)
{
	gchar *data;
	gboolean ret;

	g_return_val_if_fail (url != NULL, FALSE);

	data = g_strconcat ("gnome-open ", url, NULL);
	ret = g_spawn_command_line_async (data, NULL);
	if (ret == FALSE) {
		pk_warning ("spawn of '%s' failed", data);
	}
	g_free (data);
	return ret;
}

/**
 * pk_size_to_si_size_text:
 **/
gchar *
pk_size_to_si_size_text (guint64 size)
{
	gdouble frac;

	/* double cast, not sure why, but it works */
	frac = (gdouble) (long int) size;

	/* first chunk */
	if (frac < 1024) {
		return g_strdup_printf ("%li bytes", (long int) size);
	}
	/* next chunk */
	frac /= 1024.0;
	if (frac < 1024) {
		return g_strdup_printf ("%.1lf kB", frac);
	}
	/* next chunk */
	frac /= 1024.0;
	if (frac < 1024) {
		return g_strdup_printf ("%.1lf MB", frac);
	}
	/* next chunk */
	frac /= 1024.0;
	if (frac < 1024) {
		return g_strdup_printf ("%.1lf GB", frac);
	}
	/* no way.... */
	pk_error ("cannot have a file this large!");
	return NULL;
}

/**
 * pk_package_id_pretty:
 **/
gchar *
pk_package_id_pretty (const gchar *package_id, const gchar *summary)
{
	PkPackageId *ident;
	gchar *text;
	GString *string;

	if (package_id == NULL) {
		return g_strdup (_("unknown"));
	}

	/* split by delimeter */
	ident = pk_package_id_new_from_string (package_id);

	string = g_string_new (ident->name);
	if (ident->version != NULL) {
		g_string_append_printf (string, "-%s", ident->version);
	}
	if (ident->arch != NULL) {
		g_string_append_printf (string, " (%s)", ident->arch);
	}
	g_string_prepend (string, "<b>");
	g_string_append (string, "</b>");

	/* ITS4: ignore, we generated this */
	if (pk_strzero (summary) == FALSE) {
		g_string_append_printf (string, "\n%s", summary);
	}
	text = g_string_free (string, FALSE);

	pk_package_id_free (ident);
	return text;
}

/**
 * pk_package_id_name_version:
 **/
gchar *
pk_package_id_name_version (const gchar *package_id)
{
	PkPackageId *ident;
	gchar *text;
	GString *string;

	if (package_id == NULL) {
		return g_strdup (_("unknown"));
	}

	/* split by delimeter */
	ident = pk_package_id_new_from_string (package_id);
	string = g_string_new (ident->name);
	if (ident->version != NULL) {
		g_string_append_printf (string, "-%s", ident->version);
	}
	text = g_string_free (string, FALSE);

	pk_package_id_free (ident);
	return text;
}

/**
 * pk_package_id_get_name:
 **/
gchar *
pk_package_get_name (const gchar *package_id)
{
	gchar *package = NULL;
	PkPackageId *ident;

	/* not set! */
	if (pk_strzero (package_id) == TRUE) {
		pk_warning ("package_id blank, returning 'unknown'");
		return g_strdup ("unknown");
	}

	ident = pk_package_id_new_from_string (package_id);
	if (ident == NULL) {
		package = g_strdup (package_id);
	} else {
		package = g_strdup (ident->name);
	}
	pk_package_id_free (ident);
	return package;
}

/**
 * pk_error_modal_dialog_cb:
 **/
static void
pk_error_modal_dialog_cb (GtkWidget *dialog, gint arg1, GMainLoop *loop)
{
	g_main_loop_quit (loop);
}

/**
 * pk_error_modal_dialog:
 *
 * Shows a modal error, and blocks until the user clicks close
 **/
gboolean
pk_error_modal_dialog (const gchar *title, const gchar *message)
{
	GtkWidget *dialog;
	GMainLoop *loop;

	loop = g_main_loop_new (NULL, FALSE);
	dialog = gtk_message_dialog_new_with_markup (NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
					 	     GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
						     "<span size='larger'><b>%s</b></span>", title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), message);
	g_signal_connect (dialog, "response", G_CALLBACK (pk_error_modal_dialog_cb), loop);
	gtk_window_present (GTK_WINDOW (dialog));
	g_main_loop_run (loop);
	return TRUE;
}

/**
 * pk_error_enum_to_localised_text:
 **/
const gchar *
pk_error_enum_to_localised_text (PkErrorCodeEnum code)
{
	const gchar *text = NULL;
	switch (code) {
	case PK_ERROR_ENUM_NO_NETWORK:
		text = _("No network connection available");
		break;
	case PK_ERROR_ENUM_NO_CACHE:
		text = _("No package cache is available.");
		break;
	case PK_ERROR_ENUM_OOM:
		text = _("Out of memory");
		break;
	case PK_ERROR_ENUM_CREATE_THREAD_FAILED:
		text = _("Failed to create a thread");
		break;
	case PK_ERROR_ENUM_NOT_SUPPORTED:
		text = _("Not supported by this backend");
		break;
	case PK_ERROR_ENUM_INTERNAL_ERROR:
		text = _("An internal system error has occurred");
		break;
	case PK_ERROR_ENUM_GPG_FAILURE:
		text = _("A security trust relationship is not present");
		break;
	case PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED:
		text = _("The package is not installed");
		break;
	case PK_ERROR_ENUM_PACKAGE_NOT_FOUND:
		text = _("The package was not found");
		break;
	case PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED:
		text = _("The package is already installed");
		break;
	case PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED:
		text = _("The package download failed");
		break;
	case PK_ERROR_ENUM_GROUP_NOT_FOUND:
		text = _("The group was not found");
		break;
	case PK_ERROR_ENUM_DEP_RESOLUTION_FAILED:
		text = _("Dependency resolution failed");
		break;
	case PK_ERROR_ENUM_FILTER_INVALID:
		text = _("Search filter was invalid");
		break;
	case PK_ERROR_ENUM_PACKAGE_ID_INVALID:
		text = _("The package ID was not well formed");
		break;
	case PK_ERROR_ENUM_TRANSACTION_ERROR:
		text = _("Transaction error");
		break;
	case PK_ERROR_ENUM_REPO_NOT_FOUND:
		text = _("Repository name was not found");
		break;
	case PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE:
		text = _("Could not remove a protected system package");
		break;
	case PK_ERROR_ENUM_PROCESS_QUIT:
		text = _("The transaction was cancelled");
		break;
	case PK_ERROR_ENUM_PROCESS_KILL:
		text = _("The transaction was forcibly cancelled");
		break;
	case PK_ERROR_ENUM_FAILED_INITIALIZATION:
		text = _("Initialization of the package manager failed");
		break;
	case PK_ERROR_ENUM_FAILED_FINALISE:
		text = _("Unloading of the package manager failed");
		break;
	case PK_ERROR_ENUM_FAILED_CONFIG_PARSING:
		text = _("Reading the config file failed");
		break;
	default:
		text = _("Unknown error");
	}
	return text;
}

/**
 * pk_restart_enum_to_localised_text:
 **/
const gchar *
pk_restart_enum_to_localised_text (PkRestartEnum restart)
{
	const gchar *text = NULL;
	switch (restart) {
	case PK_RESTART_ENUM_SYSTEM:
		text = _("A system restart is required");
		break;
	case PK_RESTART_ENUM_SESSION:
		text = _("You will need to log off and log back on");
		break;
	case PK_RESTART_ENUM_APPLICATION:
		text = _("You need to restart the application");
		break;
	default:
		pk_error ("restart unrecognised: %i", restart);
	}
	return text;
}

/**
 * pk_status_enum_to_localised_text:
 **/
const gchar *
pk_status_enum_to_localised_text (PkStatusEnum status)
{
	const gchar *text = NULL;
	switch (status) {
	case PK_STATUS_ENUM_UNKNOWN:
		text = _("Unknown");
		break;
	case PK_STATUS_ENUM_SETUP:
		text = _("Setting up");
		break;
	case PK_STATUS_ENUM_WAIT:
		text = _("Waiting");
		break;
	case PK_STATUS_ENUM_QUERY:
		text = _("Querying");
		break;
	case PK_STATUS_ENUM_REMOVE:
		text = _("Removing");
		break;
	case PK_STATUS_ENUM_DOWNLOAD:
		text = _("Downloading");
		break;
	case PK_STATUS_ENUM_INSTALL:
		text = _("Installing");
		break;
	case PK_STATUS_ENUM_REFRESH_CACHE:
		text = _("Refreshing software list");
		break;
	case PK_STATUS_ENUM_UPDATE:
		text = _("Updating");
		break;
	case PK_STATUS_ENUM_CLEANUP:
		text = _("Cleaned up");
		break;
	case PK_STATUS_ENUM_OBSOLETE:
		text = _("Obsoleting");
		break;
	default:
		pk_error ("status unrecognised: %s", pk_status_enum_to_text (status));
	}
	return text;
}

/**
 * pk_info_enum_to_localised_text:
 **/
const gchar *
pk_info_enum_to_localised_text (PkInfoEnum info)
{
	const gchar *text = NULL;
	switch (info) {
	case PK_INFO_ENUM_LOW:
		text = _("Low priority update");
		break;
	case PK_INFO_ENUM_NORMAL:
		text = _("Normal update");
		break;
	case PK_INFO_ENUM_IMPORTANT:
		text = _("Important update");
		break;
	case PK_INFO_ENUM_SECURITY:
		text = _("Security update");
		break;
	case PK_INFO_ENUM_DOWNLOADING:
		text = _("Downloading");
		break;
	case PK_INFO_ENUM_UPDATING:
		text = _("Updating");
		break;
	case PK_INFO_ENUM_INSTALLING:
		text = _("Installing");
		break;
	case PK_INFO_ENUM_REMOVING:
		text = _("Removing");
		break;
	case PK_INFO_ENUM_INSTALLED:
		text = _("Installed");
		break;
	case PK_INFO_ENUM_AVAILABLE:
		text = _("Available");
		break;
	case PK_INFO_ENUM_CLEANUP:
		text = _("Cleaned up");
		break;
	case PK_INFO_ENUM_OBSOLETING:
		text = _("Obsoleting");
		break;
	default:
		pk_error ("info unrecognised: %s", pk_info_enum_to_text (info));
	}
	return text;
}

/**
 * pk_role_enum_to_localised_present:
 **/
const gchar *
pk_role_enum_to_localised_present (PkRoleEnum role)
{
	const gchar *text = NULL;
	switch (role) {
	case PK_ROLE_ENUM_UNKNOWN:
		text = _("Unknown");
		break;
	case PK_ROLE_ENUM_GET_DEPENDS:
		text = _("Getting dependencies");
		break;
	case PK_ROLE_ENUM_GET_UPDATE_DETAIL:
		text = _("Getting update detail");
		break;
	case PK_ROLE_ENUM_GET_DESCRIPTION:
		text = _("Getting description");
		break;
	case PK_ROLE_ENUM_GET_REQUIRES:
		text = _("Getting requires");
		break;
	case PK_ROLE_ENUM_GET_UPDATES:
		text = _("Getting updates");
		break;
	case PK_ROLE_ENUM_SEARCH_DETAILS:
		text = _("Searching details");
		break;
	case PK_ROLE_ENUM_SEARCH_FILE:
		text = _("Searching for file");
		break;
	case PK_ROLE_ENUM_SEARCH_GROUP:
		text = _("Searching groups");
		break;
	case PK_ROLE_ENUM_SEARCH_NAME:
		text = _("Searching for package name");
		break;
	case PK_ROLE_ENUM_REMOVE_PACKAGE:
		text = _("Removing");
		break;
	case PK_ROLE_ENUM_INSTALL_PACKAGE:
		text = _("Installing");
		break;
	case PK_ROLE_ENUM_REFRESH_CACHE:
		text = _("Refreshing package cache");
		break;
	case PK_ROLE_ENUM_UPDATE_PACKAGE:
		text = _("Updating package");
		break;
	case PK_ROLE_ENUM_UPDATE_SYSTEM:
		text = _("Updating system");
		break;
	case PK_ROLE_ENUM_CANCEL:
		text = _("Canceling");
		break;
	case PK_ROLE_ENUM_ROLLBACK:
		text = _("Rolling back");
		break;
	case PK_ROLE_ENUM_GET_REPO_LIST:
		text = _("Getting list of repositories");
		break;
	case PK_ROLE_ENUM_REPO_ENABLE:
		text = _("Enabling repository");
		break;
	case PK_ROLE_ENUM_REPO_SET_DATA:
		text = _("Setting repository data");
		break;
	default:
		pk_warning ("role unrecognised: %s", pk_role_enum_to_text (role));
	}
	return text;
}

/**
 * pk_role_enum_to_localised_past:
 **/
const gchar *
pk_role_enum_to_localised_past (PkRoleEnum role)
{
	const gchar *text = NULL;
	switch (role) {
	case PK_ROLE_ENUM_UNKNOWN:
		text = _("Unknown");
		break;
	case PK_ROLE_ENUM_GET_DEPENDS:
		text = _("Got dependencies");
		break;
	case PK_ROLE_ENUM_GET_UPDATE_DETAIL:
		text = _("Got update detail");
		break;
	case PK_ROLE_ENUM_GET_DESCRIPTION:
		text = _("Got description");
		break;
	case PK_ROLE_ENUM_GET_REQUIRES:
		text = _("Got requires");
		break;
	case PK_ROLE_ENUM_GET_UPDATES:
		text = _("Got updates");
		break;
	case PK_ROLE_ENUM_SEARCH_DETAILS:
		text = _("Got details");
		break;
	case PK_ROLE_ENUM_SEARCH_FILE:
		text = _("Searched for file");
		break;
	case PK_ROLE_ENUM_SEARCH_GROUP:
		text = _("Searched groups");
		break;
	case PK_ROLE_ENUM_SEARCH_NAME:
		text = _("Searched for package name");
		break;
	case PK_ROLE_ENUM_REMOVE_PACKAGE:
		text = _("Removed package");
		break;
	case PK_ROLE_ENUM_INSTALL_PACKAGE:
	case PK_ROLE_ENUM_INSTALL_FILE:
		text = _("Installed package");
		break;
	case PK_ROLE_ENUM_REFRESH_CACHE:
		text = _("Refreshed package cache");
		break;
	case PK_ROLE_ENUM_UPDATE_PACKAGE:
		text = _("Updated package");
		break;
	case PK_ROLE_ENUM_UPDATE_SYSTEM:
		text = _("Updated system");
		break;
	case PK_ROLE_ENUM_CANCEL:
		text = _("Canceled");
		break;
	case PK_ROLE_ENUM_ROLLBACK:
		text = _("Rolled back");
		break;
	case PK_ROLE_ENUM_GET_REPO_LIST:
		text = _("Got list of repositories");
		break;
	case PK_ROLE_ENUM_REPO_ENABLE:
		text = _("Enabled repository");
		break;
	case PK_ROLE_ENUM_REPO_SET_DATA:
		text = _("Set repository data");
		break;
	default:
		pk_warning ("role unrecognised: %s", pk_role_enum_to_text (role));
	}
	return text;
}

/**
 * pk_group_enum_to_localised_text:
 **/
const gchar *
pk_group_enum_to_localised_text (PkGroupEnum group)
{
	const gchar *text = NULL;
	switch (group) {
	case PK_GROUP_ENUM_ACCESSIBILITY:
		text = _("Accessibility");
		break;
	case PK_GROUP_ENUM_ACCESSORIES:
		text = _("Accessories");
		break;
	case PK_GROUP_ENUM_EDUCATION:
		text = _("Education");
		break;
	case PK_GROUP_ENUM_GAMES:
		text = _("Games");
		break;
	case PK_GROUP_ENUM_GRAPHICS:
		text = _("Graphics");
		break;
	case PK_GROUP_ENUM_INTERNET:
		text = _("Internet");
		break;
	case PK_GROUP_ENUM_OFFICE:
		text = _("Office");
		break;
	case PK_GROUP_ENUM_OTHER:
		text = _("Other");
		break;
	case PK_GROUP_ENUM_PROGRAMMING:
		text = _("Programming");
		break;
	case PK_GROUP_ENUM_MULTIMEDIA:
		text = _("Multimedia");
		break;
	case PK_GROUP_ENUM_SYSTEM:
		text = _("System");
		break;
	case PK_GROUP_ENUM_DESKTOP_GNOME:
		text = _("GNOME desktop");
		break;
	case PK_GROUP_ENUM_DESKTOP_KDE:
		text = _("KDE desktop");
		break;
	case PK_GROUP_ENUM_DESKTOP_XFCE:
		text = _("XFCE desktop");
		break;
	case PK_GROUP_ENUM_DESKTOP_OTHER:
		text = _("Other desktops");
		break;
	case PK_GROUP_ENUM_PUBLISHING:
		text = _("Publishing");
		break;
	case PK_GROUP_ENUM_SERVERS:
		text = _("Servers");
		break;
	case PK_GROUP_ENUM_FONTS:
		text = _("Fonts");
		break;
	case PK_GROUP_ENUM_ADMIN_TOOLS:
		text = _("Admin tools");
		break;
	case PK_GROUP_ENUM_LEGACY:
		text = _("Legacy");
		break;
	case PK_GROUP_ENUM_LOCALIZATION:
		text = _("Localization");
		break;
	case PK_GROUP_ENUM_VIRTUALIZATION:
		text = _("Virtualization");
		break;
	case PK_GROUP_ENUM_SECURITY:
		text = _("Security");
		break;
	case PK_GROUP_ENUM_POWER_MANAGEMENT:
		text = _("Power management");
		break;
	case PK_GROUP_ENUM_UNKNOWN:
		text = _("Unknown");
		break;
	default:
		pk_error ("group unrecognised: %i", group);
	}
	return text;
}

/**
 * pk_info_enum_to_icon_name:
 **/
const gchar *
pk_info_enum_to_icon_name (PkInfoEnum info)
{
	return pk_enum_find_string (enum_info_icon_name, info);
}

/**
 * pk_status_enum_to_icon_name:
 **/
const gchar *
pk_status_enum_to_icon_name (PkStatusEnum status)
{
	return pk_enum_find_string (enum_status_icon_name, status);
}

/**
 * pk_role_enum_to_icon_name:
 **/
const gchar *
pk_role_enum_to_icon_name (PkRoleEnum role)
{
	return pk_enum_find_string (enum_role_icon_name, role);
}

/**
 * pk_group_enum_to_icon_name:
 **/
const gchar *
pk_group_enum_to_icon_name (PkGroupEnum group)
{
	return pk_enum_find_string (enum_group_icon_name, group);
}

/**
 * pk_time_to_localised_string:
 * @time_secs: The time value to convert in seconds
 * @cookie: The cookie we are looking for
 *
 * Returns a localised timestring
 *
 * Return value: The time string, e.g. "2 hours 3 minutes"
 **/
gchar *
pk_time_to_localised_string (guint time_secs)
{
	gchar* timestring = NULL;
	guint hours;
	guint minutes;
	guint seconds;

	/* is valid? */
	if (time_secs == 0) {
		timestring = g_strdup_printf (_("Zero time"));
		return timestring;
	}

	/* make local copy */
	seconds = time_secs;

	/* less than a minute */
	if (seconds < 60) {
		timestring = g_strdup_printf (ngettext ("%i second",
							"%i seconds",
							seconds), seconds);
		return timestring;
	}

	/* Add 0.5 to do rounding */
	minutes = (guint) ((time_secs / 60.0 ) + 0.5);
	seconds = seconds % 60;

	/* less than an hour */
	if (minutes < 60) {
		if (seconds == 0) {
			timestring = g_strdup_printf (ngettext ("%i minute",
								"%i minutes",
								minutes), minutes);
		} else {
			/* TRANSLATOR: "%i %s %i %s" are "%i minutes %i seconds"
			 * Swap order with "%2$s %2$i %1$s %1$i if needed */
			timestring = g_strdup_printf (_("%i %s %i %s"),
					minutes, ngettext ("minute", "minutes", minutes),
					seconds, ngettext ("second", "seconds", seconds));
		}
		return timestring;
	}

	/* more than an hour */
	hours = minutes / 60;
	minutes = minutes % 60;
	if (minutes == 0) {
		timestring = g_strdup_printf (ngettext (
				"%i hour",
				"%i hours",
				hours), hours);
	} else {
		/* TRANSLATOR: "%i %s %i %s" are "%i hours %i minutes"
		 * Swap order with "%2$s %2$i %1$s %1$i if needed */
		timestring = g_strdup_printf (_("%i %s %i %s"),
				hours, ngettext ("hour", "hours", hours),
				minutes, ngettext ("minute", "minutes", minutes));
	}
	return timestring;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_common_gui (LibSelfTest *test)
{
	gchar *text;

	if (libst_start (test, "PkCommonGui", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************
	 ****************        time text             **************
	 ************************************************************/
	libst_title (test, "time zero");
	text = pk_time_to_localised_string (0);
	if (text != NULL && strcmp (text, _("Zero time")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "time 1s");
	text = pk_time_to_localised_string (1);
	if (text != NULL && strcmp (text, _("1 second")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "time 1m");
	text = pk_time_to_localised_string (1*60);
	if (text != NULL && strcmp (text, _("1 minute")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "time 1h");
	text = pk_time_to_localised_string (1*60*60);
	if (text != NULL && strcmp (text, _("1 hour")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "time 30s");
	text = pk_time_to_localised_string (30);
	if (text != NULL && strcmp (text, _("30 seconds")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "time 30m");
	text = pk_time_to_localised_string (30*60);
	if (text != NULL && strcmp (text, _("30 minutes")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "time 30m1s");
	text = pk_time_to_localised_string (30*60+1);
	if (text != NULL && strcmp (text, _("30 minutes 1 second")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "time 30m10s");
	text = pk_time_to_localised_string (30*60+10);
	if (text != NULL && strcmp (text, _("30 minutes 10 seconds")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************
	 ****************        size text             **************
	 ************************************************************/
	libst_title (test, "size zero");
	text = pk_size_to_si_size_text (0);
	if (text != NULL && strcmp (text, _("0 bytes")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "size 512 bytes");
	text = pk_size_to_si_size_text (512);
	if (text != NULL && strcmp (text, _("512 bytes")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "size 256.2 MB");
	text = pk_size_to_si_size_text (256*1025*1024);
	if (text != NULL && strcmp (text, _("256.2 MB")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************
	 ****************     package name text        **************
	 ************************************************************/
	libst_title (test, "get name null");
	text = pk_package_get_name (NULL);
	if (text != NULL && strcmp (text, _("unknown")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "get name not id");
	text = pk_package_get_name ("ania");
	if (text != NULL && strcmp (text, "ania") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "get name just id");
	text = pk_package_get_name ("simon;1.0.0;i386;moo");
	if (text != NULL && strcmp (text, "simon") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************
	 ****************     package name text        **************
	 ************************************************************/
	libst_title (test, "package id pretty null");
	text = pk_package_id_pretty (NULL, NULL);
	if (text != NULL && strcmp (text, _("unknown")) == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "package id pretty valid package id, no summary");
	text = pk_package_id_pretty ("simon;0.0.1;i386;data", NULL);
	if (text != NULL && strcmp (text, "<b>simon-0.0.1 (i386)</b>") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "package id pretty valid package id, no summary 2");
	text = pk_package_id_pretty ("simon;0.0.1;;data", NULL);
	if (text != NULL && strcmp (text, "<b>simon-0.0.1</b>") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "package id pretty valid package id, no summary 3");
	text = pk_package_id_pretty ("simon;;;data", NULL);
	if (text != NULL && strcmp (text, "<b>simon</b>") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "package id pretty valid package id, no summary 4");
	text = pk_package_id_pretty ("simon;0.0.1;;data", "dude");
	if (text != NULL && strcmp (text, "<b>simon-0.0.1</b>\ndude") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed, got %s", text);
	}
	g_free (text);

	libst_end (test);
}
#endif

