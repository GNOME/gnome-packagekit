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
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-package-id.h>
#include <pk-enum.h>
#include <pk-common.h>
#include "pk-common-gui.h"

/* icon names */
static PkEnumMatch enum_info_icon_name[] = {
	{PK_INFO_ENUM_UNKNOWN,			"help-browser"},	/* fall though value */
	{PK_INFO_ENUM_INSTALLED,		"pk-package-installed"},
	{PK_INFO_ENUM_AVAILABLE,		"pk-package-available"},
	{PK_INFO_ENUM_LOW,			"pk-update-low"},
	{PK_INFO_ENUM_NORMAL,			"pk-update-normal"},
	{PK_INFO_ENUM_IMPORTANT,		"pk-update-high"},
	{PK_INFO_ENUM_SECURITY,			"pk-update-security"},
	{PK_INFO_ENUM_BUGFIX,			"pk-update-bugfix"},
	{PK_INFO_ENUM_ENHANCEMENT,		"pk-update-enhancement"},
	{PK_INFO_ENUM_BLOCKED,			"help-browser"}, /* TODO: need better icon */
	{PK_INFO_ENUM_DOWNLOADING,		"pk-package-download"},
	{PK_INFO_ENUM_UPDATING,			"pk-package-update"},
	{PK_INFO_ENUM_INSTALLING,		"pk-package-add"},
	{PK_INFO_ENUM_REMOVING,			"pk-package-delete"},
	{PK_INFO_ENUM_OBSOLETING,		"pk-package-cleanup"},
	{PK_INFO_ENUM_CLEANUP,			"pk-package-cleanup"},
	{0, NULL}
};

static PkEnumMatch enum_status_icon_name[] = {
	{PK_STATUS_ENUM_UNKNOWN,		"help-browser"},	/* fall though value */
	{PK_STATUS_ENUM_WAIT,			"pk-wait"},
	{PK_STATUS_ENUM_SETUP,			"pk-setup"},
	{PK_STATUS_ENUM_QUERY,			"pk-package-search"},
	{PK_STATUS_ENUM_INFO,			"pk-package-info"},
	{PK_STATUS_ENUM_REFRESH_CACHE,		"pk-refresh-cache"},
	{PK_STATUS_ENUM_REMOVE,			"pk-package-delete"},
	{PK_STATUS_ENUM_DOWNLOAD,		"pk-package-download"},
	{PK_STATUS_ENUM_INSTALL,		"pk-package-add"},
	{PK_STATUS_ENUM_UPDATE,			"pk-package-update"},
	{PK_STATUS_ENUM_CLEANUP,		"pk-package-cleanup"},
	{PK_STATUS_ENUM_OBSOLETE,		"pk-package-cleanup"},
	{PK_STATUS_ENUM_DEP_RESOLVE,		"pk-package-info"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_ROLLBACK,		"pk-package-info"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_COMMIT,			"pk-setup"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_REQUEST,		"pk-package-search"},
	{PK_STATUS_ENUM_FINISHED,		"pk-package-cleanup"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_CANCEL,			"pk-package-cleanup"}, /* TODO: need better icon */
	{0, NULL}
};

static PkEnumMatch enum_role_icon_name[] = {
	{PK_ROLE_ENUM_UNKNOWN,			"help-browser"},	/* fall though value */
	{PK_ROLE_ENUM_CANCEL,			"process-stop.svg"},
	{PK_ROLE_ENUM_RESOLVE,			"pk-package-search"},
	{PK_ROLE_ENUM_ROLLBACK,			"pk-rollback"},
	{PK_ROLE_ENUM_GET_DEPENDS,		"pk-package-info"},
	{PK_ROLE_ENUM_GET_UPDATE_DETAIL,	"pk-package-info"},
	{PK_ROLE_ENUM_GET_DESCRIPTION,		"pk-package-info"},
	{PK_ROLE_ENUM_GET_REQUIRES,		"pk-package-info"},
	{PK_ROLE_ENUM_GET_UPDATES,		"pk-package-info"},
	{PK_ROLE_ENUM_SEARCH_DETAILS,		"pk-package-search"},
	{PK_ROLE_ENUM_SEARCH_FILE,		"pk-package-search"},
	{PK_ROLE_ENUM_SEARCH_GROUP,		"pk-package-search"},
	{PK_ROLE_ENUM_SEARCH_NAME,		"pk-package-search"},
	{PK_ROLE_ENUM_REFRESH_CACHE,		"pk-refresh-cache"},
	{PK_ROLE_ENUM_REMOVE_PACKAGE,		"pk-package-delete"},
	{PK_ROLE_ENUM_INSTALL_PACKAGE,		"pk-package-add"},
	{PK_ROLE_ENUM_INSTALL_FILE,		"pk-package-add"},
	{PK_ROLE_ENUM_UPDATE_PACKAGES,		"pk-package-update"},
	{PK_ROLE_ENUM_SERVICE_PACK,		"pk-package-update"},
	{PK_ROLE_ENUM_UPDATE_SYSTEM,		"system-software-update"},
	{PK_ROLE_ENUM_GET_REPO_LIST,		"emblem-system"},
	{PK_ROLE_ENUM_REPO_ENABLE,		"emblem-system"},
	{PK_ROLE_ENUM_REPO_SET_DATA,		"emblem-system"},
	{0, NULL}
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
	{PK_GROUP_ENUM_COMMUNICATION,		"folder-remote"},
	{PK_GROUP_ENUM_NETWORK,			"network-wired"},
	{PK_GROUP_ENUM_MAPS,			"applications-multimedia"},
	{PK_GROUP_ENUM_REPOS,			"system-file-manager"},
	{0, NULL}
};

static PkEnumMatch enum_restart_icon_name[] = {
	{PK_RESTART_ENUM_UNKNOWN,		"help-browser"},	/* fall though value */
	{PK_RESTART_ENUM_NONE,			"dialog-information"},
	{PK_RESTART_ENUM_SYSTEM,		"dialog-error"},
	{PK_RESTART_ENUM_SESSION,		"dialog-warning"},
	{PK_RESTART_ENUM_APPLICATION,		"dialog-warning"},
	{0, NULL}
};

static PkEnumMatch enum_message_icon_name[] = {
	{PK_MESSAGE_ENUM_UNKNOWN,		"help-browser"},	/* fall though value */
	{PK_MESSAGE_ENUM_NOTICE,		"dialog-information"},
	{PK_MESSAGE_ENUM_WARNING,		"dialog-warning"},
	{PK_MESSAGE_ENUM_DAEMON,		"dialog-error"},
	{0, NULL}
};

/**
 * pk_restart_system:
 **/
gboolean
pk_restart_system (void)
{
	DBusGProxy *proxy;
	DBusGConnection	*connection;
	GError *error = NULL;
	gboolean ret;

	/* check dbus connections, exit if not valid */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (error != NULL) {
		pk_warning ("cannot acccess the session bus: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (connection, GPM_DBUS_SERVICE, GPM_DBUS_PATH, GPM_DBUS_INTERFACE);
	if (proxy == NULL) {
		pk_warning ("Cannot connect to gnome-power-manager");
		return FALSE;
	}

	/* do the method */
	ret = dbus_g_proxy_call (proxy, "Reboot", &error, G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		pk_warning ("cannot reboot: %s", error->message);
		g_error_free (error);
	}
	g_object_unref (G_OBJECT (proxy));
	return ret;
}

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
	pk_warning ("cannot have a file this large!");
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

	if (pk_strzero (package_id) == TRUE) {
		return g_strdup (_("Package identifier not valid"));
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
 * pk_package_id_pretty_oneline:
 **/
gchar *
pk_package_id_pretty_oneline (const gchar *package_id, const gchar *summary)
{
	PkPackageId *ident;
	gchar *text;

	if (pk_strzero (package_id) == TRUE) {
		return g_strdup (_("Package identifier not valid"));
	}

	/* split by delimeter */
	ident = pk_package_id_new_from_string (package_id);
	if (pk_strzero (summary) == TRUE) {
		/* just have name */
		text = g_strdup (ident->name);
	} else {
		text = g_strdup_printf ("<b>%s</b> (%s)", summary, ident->name);
	}

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

	if (pk_strzero (package_id) == TRUE) {
		return g_strdup (_("Package identifier not valid"));
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
		return g_strdup ("Package identifier not valid");
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
 * pk_icon_valid:
 *
 * Check icon actually exists and is valid in this theme
 **/
gboolean
pk_icon_valid (const gchar *icon)
{
	GtkIconInfo *icon_info;
	GtkIconTheme *icon_theme;
	gboolean ret = TRUE;

	/* trivial case */
	if (pk_strzero (icon) == TRUE) {
		return FALSE;
	}

	/* no unref */
	icon_theme = gtk_icon_theme_get_default ();

	/* default to 32x32 */
	icon_info = gtk_icon_theme_lookup_icon (icon_theme, icon, 32, GTK_ICON_LOOKUP_USE_BUILTIN);
	if (icon_info == NULL) {
		pk_debug ("ignoring broken icon %s", icon);
		ret = FALSE;
	} else {
		/* we only used this to see if it was valid */
		gtk_icon_info_free (icon_info);
	}
	return ret;
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
	dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE, title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
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
		text = _("The package identifier was not well formed");
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
	case PK_ERROR_ENUM_TRANSACTION_CANCELLED:
		text = _("The transaction was canceled");
		break;
	case PK_ERROR_ENUM_PROCESS_KILL:
		text = _("The transaction was forcibly canceled");
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
	case PK_ERROR_ENUM_CANNOT_CANCEL:
		text = _("The transaction cannot be cancelled");
		break;
	default:
		text = _("Unknown error");
	}
	return text;
}

/**
 * pk_error_enum_to_localised_message:
 **/
const gchar *
pk_error_enum_to_localised_message (PkErrorCodeEnum code)
{
	const gchar *text = NULL;
	switch (code) {
	case PK_ERROR_ENUM_NO_NETWORK:
		text = _("There is no network connection available.\n"
			 "Please check your connection settings and try again");
		break;
	case PK_ERROR_ENUM_NO_CACHE:
		text = _("The package list needs to be rebuilt\nThis should have been done by the backend automatically.");
		break;
	case PK_ERROR_ENUM_OOM:
		text = _("The service that is responsible for handling user requests is out of memory.\nPlease restart your computer.");
		break;
	case PK_ERROR_ENUM_CREATE_THREAD_FAILED:
		text = _("A thread could not be created to service the user request.");
		break;
	case PK_ERROR_ENUM_NOT_SUPPORTED:
		text = _("The action is not supported by this backend\n"
			 "Please report a bug as this shouldn't have happened.");
		break;
	case PK_ERROR_ENUM_INTERNAL_ERROR:
		text = _("A problem that we were not expecting has occurred.\n"
			 "Please report this bug with the error description.");
		break;
	case PK_ERROR_ENUM_GPG_FAILURE:
		text = _("A security trust relationship could not be made with the remote software source\n"
			 "Please check your security settings.");
		break;
	case PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED:
		text = _("The package that is trying to be removed or updated is not already installed, and hence nothing can be done.");
		break;
	case PK_ERROR_ENUM_PACKAGE_NOT_FOUND:
		text = _("The package that is being modified was not found on your system or in any remote software source.");
		break;
	case PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED:
		text = _("The package that is trying to be installed is already installed.");
		break;
	case PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED:
		text = _("The package download failed\nPlease check your network connectivity.");
		break;
	case PK_ERROR_ENUM_GROUP_NOT_FOUND:
		text = _("The group type was not found\nPlease check your group list and try again.");
		break;
	case PK_ERROR_ENUM_DEP_RESOLUTION_FAILED:
		text = _("A package could not be found on your system or in a remote software source that allows the transaction to complete\n"
			 "More information is available in the detailed report.");
		break;
	case PK_ERROR_ENUM_FILTER_INVALID:
		text = _("The search filter was not correctly formed.");
		break;
	case PK_ERROR_ENUM_PACKAGE_ID_INVALID:
		text = _("The package identifier was not well formed when sent to the server.\n"
			 "This normally indicates an internal error and should be reported.");
		break;
	case PK_ERROR_ENUM_TRANSACTION_ERROR:
		text = _("An unspecified transaction error has occurred.\n"
			 "More information is available in the detailed report.");
		break;
	case PK_ERROR_ENUM_REPO_NOT_FOUND:
		text = _("The remote software source name was not found\n"
			 "You may need to disable an item in Software Sources");
		break;
	case PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE:
		text = _("Removing a protected system package was denied, and thus the whole transaction failed.");
		break;
	case PK_ERROR_ENUM_TRANSACTION_CANCELLED:
		text = _("The transaction was canceled successfully and no packages were changed.");
		break;
	case PK_ERROR_ENUM_PROCESS_KILL:
		text = _("The transaction was canceled successfully and no packages were changed.\n"
			 "The backend did not exit cleanly.");
		break;
	case PK_ERROR_ENUM_FAILED_INITIALIZATION:
		text = _("The native package backend could not be initialised.\n"
			 "Please make sure no other tools are accessing package information.");
		break;
	case PK_ERROR_ENUM_FAILED_FINALISE:
		text = _("The native package backend could not be closed.\n"
			 "Please make sure no other tools are accessing package information.");
		break;
	case PK_ERROR_ENUM_FAILED_CONFIG_PARSING:
		text = _("The native package configuration file could not be opened.\n"
			 "Please make sure configuration is valid.");
		break;
	case PK_ERROR_ENUM_CANNOT_CANCEL:
		text = _("The transaction is not safe to be cancelled at this time.");
		break;
	default:
		text = _("Unknown error, please report a bug.\n"
			 "More information is available in the detailed report.");
	}
	return text;
}

/**
 * pk_restart_enum_to_localised_text_future:
 **/
const gchar *
pk_restart_enum_to_localised_text_future (PkRestartEnum restart)
{
	const gchar *text = NULL;
	switch (restart) {
	case PK_RESTART_ENUM_NONE:
		text = _("No restart is necessary for this update");
		break;
	case PK_RESTART_ENUM_APPLICATION:
		text = _("An application restart is required after this update");
		break;
	case PK_RESTART_ENUM_SESSION:
		text = _("You will be required to log off and back on after this update");
		break;
	case PK_RESTART_ENUM_SYSTEM:
		text = _("A system restart is required after this update");
		break;
	default:
		text = _("A restart of unknown type is required after this update");
		pk_warning ("restart unrecognised: %i", restart);
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
	case PK_RESTART_ENUM_NONE:
		text = _("No restart is required");
		break;
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
		text = _("A restart of unknown type is required after this update");
		pk_warning ("restart unrecognised: %i", restart);
	}
	return text;
}

/**
 * pk_message_enum_to_localised_text:
 **/
const gchar *
pk_message_enum_to_localised_text (PkMessageEnum message)
{
	const gchar *text = NULL;
	switch (message) {
	case PK_MESSAGE_ENUM_NOTICE:
		text = _("PackageKit transaction notice");
		break;
	case PK_MESSAGE_ENUM_WARNING:
		text = _("PackageKit transaction warning");
		break;
	case PK_MESSAGE_ENUM_DAEMON:
		text = _("PackageKit daemon");
		break;
	default:
		text = _("PackageKit message of unknown type");
		pk_warning ("message unrecognised: %i", message);
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
		text = _("Unknown state");
		break;
	case PK_STATUS_ENUM_SETUP:
		text = _("Setting up transaction");
		break;
	case PK_STATUS_ENUM_WAIT:
		text = _("Waiting for transaction lock");
		break;
	case PK_STATUS_ENUM_QUERY:
		text = _("Querying");
		break;
	case PK_STATUS_ENUM_INFO:
		text = _("Getting information");
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
		text = _("Cleaning up");
		break;
	case PK_STATUS_ENUM_OBSOLETE:
		text = _("Obsoleting");
		break;
	case PK_STATUS_ENUM_DEP_RESOLVE:
		text = _("Resolving dependencies");
		break;
	case PK_STATUS_ENUM_ROLLBACK:
		text = _("Rolling back");
		break;
	case PK_STATUS_ENUM_COMMIT:
		text = _("Committing transaction");
		break;
	case PK_STATUS_ENUM_REQUEST:
		text = _("Requesting data");
		break;
	case PK_STATUS_ENUM_FINISHED:
		text = _("Finished");
		break;
	case PK_STATUS_ENUM_CANCEL:
		text = _("Cancelling");
		break;
	default:
		text = _("Status unknown");
		pk_warning ("status unrecognised: %s", pk_status_enum_to_text (status));
	}
	return text;
}

/**
 * pk_update_enum_to_localised_text:
 **/
gchar *
pk_update_enum_to_localised_text (PkInfoEnum info, guint number)
{
	gchar *text = NULL;
	switch (info) {
	case PK_INFO_ENUM_LOW:
		text = g_strdup_printf (ngettext ("%i trivial update", "%i trivial updates", number), number);
		break;
	case PK_INFO_ENUM_NORMAL:
		text = g_strdup_printf (ngettext ("%i normal update", "%i normal updates", number), number);
		break;
	case PK_INFO_ENUM_IMPORTANT:
		text = g_strdup_printf (ngettext ("%i important update", "%i important updates", number), number);
		break;
	case PK_INFO_ENUM_SECURITY:
		text = g_strdup_printf (ngettext ("%i security update", "%i security updates", number), number);
		break;
	case PK_INFO_ENUM_BUGFIX:
		text = g_strdup_printf (ngettext ("%i bug fix update", "%i bug fix updates", number), number);
		break;
	case PK_INFO_ENUM_ENHANCEMENT:
		text = g_strdup_printf (ngettext ("%i enhancement update", "%i enhancement updates", number), number);
		break;
	default:
		text = g_strdup_printf (ngettext ("%i unknown update", "%i unknown updates", number), number);
		pk_warning ("update info unrecognised: %s", pk_info_enum_to_text (info));
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
		text = _("Trivial update");
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
	case PK_INFO_ENUM_BUGFIX:
		text = _("Bug fix update");
		break;
	case PK_INFO_ENUM_ENHANCEMENT:
		text = _("Enhancement update");
		break;
	case PK_INFO_ENUM_BLOCKED:
		text = _("Blocked update");
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
		text = _("Info unknown");
		pk_warning ("info unrecognised: %s", pk_info_enum_to_text (info));
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
		text = _("Unknown role type");
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
	case PK_ROLE_ENUM_UPDATE_PACKAGES:
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
		text = _("Role unknown");
		pk_warning ("role unrecognised: %s", pk_role_enum_to_text (role));
	}
	return text;
}

/**
 * pk_role_enum_to_localised_past:
 *
 * These are past tense versions of the action
 **/
const gchar *
pk_role_enum_to_localised_past (PkRoleEnum role)
{
	const gchar *text = NULL;
	switch (role) {
	case PK_ROLE_ENUM_UNKNOWN:
		text = _("Unknown role type");
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
		text = _("Installed package");
		break;
	case PK_ROLE_ENUM_INSTALL_FILE:
		text = _("Installed local file");
		break;
	case PK_ROLE_ENUM_SERVICE_PACK:
		text = _("Updating from service pack");
		break;
	case PK_ROLE_ENUM_REFRESH_CACHE:
		text = _("Refreshed package cache");
		break;
	case PK_ROLE_ENUM_UPDATE_PACKAGES:
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
		text = _("Role unknown");
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
	case PK_GROUP_ENUM_COMMUNICATION:
		text = _("Communication");
		break;
	case PK_GROUP_ENUM_NETWORK:
		text = _("Network");
		break;
	case PK_GROUP_ENUM_MAPS:
		text = _("Maps");
		break;
	case PK_GROUP_ENUM_REPOS:
		text = _("Software sources");
		break;
	case PK_GROUP_ENUM_UNKNOWN:
		text = _("Unknown group");
		break;
	default:
		text = _("Invalid group");
		pk_warning ("group unrecognised: %i", group);
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
 * pk_restart_enum_to_icon_name:
 **/
const gchar *
pk_restart_enum_to_icon_name (PkRestartEnum restart)
{
	return pk_enum_find_string (enum_restart_icon_name, restart);
}

/**
 * pk_message_enum_to_icon_name:
 **/
const gchar *
pk_message_enum_to_icon_name (PkMessageEnum message)
{
	return pk_enum_find_string (enum_message_icon_name, message);
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
		timestring = g_strdup_printf (_("Now"));
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
	if (text != NULL && strcmp (text, _("Now")) == 0) {
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
	if (text != NULL && strcmp (text, _("Package identifier not valid")) == 0) {
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
	if (text != NULL && strcmp (text, _("Package identifier not valid")) == 0) {
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

