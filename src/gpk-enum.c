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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <pk-enum.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-enum.h"
#include "gpk-common.h"

/* icon names */
static const PkEnumMatch enum_info_icon_name[] = {
	{PK_INFO_ENUM_UNKNOWN,			"help-browser"},	/* fall though value */
	{PK_INFO_ENUM_INSTALLED,		"pk-package-installed"},
	{PK_INFO_ENUM_AVAILABLE,		"pk-package-available"},
	{PK_INFO_ENUM_LOW,			"pk-update-low"},
	{PK_INFO_ENUM_NORMAL,			"pk-update-normal"},
	{PK_INFO_ENUM_IMPORTANT,		"pk-update-high"},
	{PK_INFO_ENUM_SECURITY,			"pk-update-security"},
	{PK_INFO_ENUM_BUGFIX,			"pk-update-bugfix"},
	{PK_INFO_ENUM_ENHANCEMENT,		"pk-update-enhancement"},
	{PK_INFO_ENUM_BLOCKED,			"pk-package-blocked"},
	{PK_INFO_ENUM_DOWNLOADING,		"pk-package-download"},
	{PK_INFO_ENUM_UPDATING,			"pk-package-update"},
	{PK_INFO_ENUM_INSTALLING,		"pk-package-add"},
	{PK_INFO_ENUM_REMOVING,			"pk-package-delete"},
	{PK_INFO_ENUM_OBSOLETING,		"pk-package-cleanup"},
	{PK_INFO_ENUM_CLEANUP,			"pk-package-cleanup"},
	{PK_INFO_ENUM_COLLECTION_INSTALLED,	"pk-collection-installed"},
	{PK_INFO_ENUM_COLLECTION_AVAILABLE,	"pk-collection-available"},
	{0, NULL}
};

static const PkEnumMatch enum_status_icon_name[] = {
	{PK_STATUS_ENUM_UNKNOWN,		"help-browser"},	/* fall though value */
	{PK_STATUS_ENUM_CANCEL,			"pk-package-cleanup"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_CLEANUP,		"pk-package-cleanup"},
	{PK_STATUS_ENUM_COMMIT,			"pk-setup"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_DEP_RESOLVE,		"pk-package-info"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_DOWNLOAD_CHANGELOG,	"pk-refresh-cache"},
	{PK_STATUS_ENUM_DOWNLOAD_FILELIST,	"pk-refresh-cache"},
	{PK_STATUS_ENUM_DOWNLOAD_GROUP,		"pk-refresh-cache"},
	{PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST,	"pk-refresh-cache"},
	{PK_STATUS_ENUM_DOWNLOAD,		"pk-package-download"},
	{PK_STATUS_ENUM_DOWNLOAD_REPOSITORY,	"pk-refresh-cache"},
	{PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO,	"pk-refresh-cache"},
	{PK_STATUS_ENUM_FINISHED,		"pk-package-cleanup"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_GENERATE_PACKAGE_LIST,	"pk-refresh-cache"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_INFO,			"pk-package-info"},
	{PK_STATUS_ENUM_INSTALL,		"pk-package-add"},
	{PK_STATUS_ENUM_LOADING_CACHE,		"pk-refresh-cache"},
	{PK_STATUS_ENUM_OBSOLETE,		"pk-package-cleanup"},
	{PK_STATUS_ENUM_QUERY,			"pk-package-search"},
	{PK_STATUS_ENUM_REFRESH_CACHE,		"pk-refresh-cache"},
	{PK_STATUS_ENUM_REMOVE,			"pk-package-delete"},
	{PK_STATUS_ENUM_REPACKAGING,		"pk-package-cleanup"},
	{PK_STATUS_ENUM_REQUEST,		"pk-package-search"},
	{PK_STATUS_ENUM_ROLLBACK,		"pk-package-info"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_RUNNING,		"pk-setup"},
	{PK_STATUS_ENUM_SCAN_APPLICATIONS,	"pk-package-search"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_SETUP,			"pk-setup"},
	{PK_STATUS_ENUM_SIG_CHECK,		"pk-package-info"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_TEST_COMMIT,		"pk-package-info"}, /* TODO: need better icon */
	{PK_STATUS_ENUM_UPDATE,			"pk-package-update"},
	{PK_STATUS_ENUM_WAIT,			"pk-wait"},
	{0, NULL}
};

static const PkEnumMatch enum_status_animation[] = {
	{PK_STATUS_ENUM_UNKNOWN,		"help-browser"},
	{PK_STATUS_ENUM_CANCEL,			"pk-package-cleanup"},
	{PK_STATUS_ENUM_CLEANUP,		"pk-package-cleanup"},
	{PK_STATUS_ENUM_COMMIT,			"pk-setup"},
	{PK_STATUS_ENUM_DEP_RESOLVE,		"pk-action-testing"},
	{PK_STATUS_ENUM_DOWNLOAD_CHANGELOG,	"pk-action-refresh-cache"},
	{PK_STATUS_ENUM_DOWNLOAD_FILELIST,	"pk-action-refresh-cache"},
	{PK_STATUS_ENUM_DOWNLOAD_GROUP,		"pk-action-refresh-cache"},
	{PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST,	"pk-action-refresh-cache"},
	{PK_STATUS_ENUM_DOWNLOAD,		"pk-action-download"},
	{PK_STATUS_ENUM_DOWNLOAD_REPOSITORY,	"pk-action-refresh-cache"},
	{PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO,	"pk-action-refresh-cache"},
	{PK_STATUS_ENUM_FINISHED,		"pk-package-cleanup"},
	{PK_STATUS_ENUM_GENERATE_PACKAGE_LIST,	"pk-action-searching"},
	{PK_STATUS_ENUM_INFO,			"process-working"},
	{PK_STATUS_ENUM_INSTALL,		"pk-action-installing"},
	{PK_STATUS_ENUM_LOADING_CACHE,		"pk-action-refresh-cache"},
	{PK_STATUS_ENUM_OBSOLETE,		"pk-package-cleanup"},
	{PK_STATUS_ENUM_QUERY,			"pk-action-searching"},
	{PK_STATUS_ENUM_REFRESH_CACHE,		"pk-action-refresh-cache"},
	{PK_STATUS_ENUM_REMOVE,			"pk-package-delete"},
	{PK_STATUS_ENUM_REPACKAGING,		"pk-package-info"},
	{PK_STATUS_ENUM_REQUEST,		"process-working"},
	{PK_STATUS_ENUM_ROLLBACK,		"pk-package-info"},
	{PK_STATUS_ENUM_RUNNING,		"pk-setup"},
	{PK_STATUS_ENUM_SCAN_APPLICATIONS,	"pk-action-searching"},
	{PK_STATUS_ENUM_SETUP,			"pk-package-info"},
	{PK_STATUS_ENUM_SIG_CHECK,		"pk-package-info"},
	{PK_STATUS_ENUM_TEST_COMMIT,		"pk-action-testing"},
	{PK_STATUS_ENUM_UPDATE,			"pk-action-installing"},
	{PK_STATUS_ENUM_WAIT,			"pk-action-waiting"},
	{0, NULL}
};

static const PkEnumMatch enum_role_icon_name[] = {
	{PK_ROLE_ENUM_UNKNOWN,			"help-browser"},	/* fall though value */
	{PK_ROLE_ENUM_ACCEPT_EULA,		"pk-package-info"},
	{PK_ROLE_ENUM_CANCEL,			"process-stop"},
	{PK_ROLE_ENUM_DOWNLOAD_PACKAGES,	"pk-package-download"},
	{PK_ROLE_ENUM_GET_DEPENDS,		"pk-package-info"},
	{PK_ROLE_ENUM_GET_DETAILS,		"pk-package-info"},
	{PK_ROLE_ENUM_GET_DISTRO_UPGRADES,	"pk-package-info"},
	{PK_ROLE_ENUM_GET_FILES,		"pk-package-search"},
	{PK_ROLE_ENUM_GET_PACKAGES,		"pk-package-search"},
	{PK_ROLE_ENUM_GET_REPO_LIST,		"pk-package-sources"},
	{PK_ROLE_ENUM_GET_REQUIRES,		"pk-package-info"},
	{PK_ROLE_ENUM_GET_UPDATE_DETAIL,	"pk-package-info"},
	{PK_ROLE_ENUM_GET_UPDATES,		"pk-package-info"},
	{PK_ROLE_ENUM_INSTALL_FILES,		"pk-package-add"},
	{PK_ROLE_ENUM_INSTALL_PACKAGES,		"pk-package-add"},
	{PK_ROLE_ENUM_INSTALL_SIGNATURE,	"emblem-system"},
	{PK_ROLE_ENUM_REFRESH_CACHE,		"pk-refresh-cache"},
	{PK_ROLE_ENUM_REMOVE_PACKAGES,		"pk-package-delete"},
	{PK_ROLE_ENUM_REPO_ENABLE,		"pk-package-sources"},
	{PK_ROLE_ENUM_REPO_SET_DATA,		"pk-package-sources"},
	{PK_ROLE_ENUM_RESOLVE,			"pk-package-search"},
	{PK_ROLE_ENUM_ROLLBACK,			"pk-rollback"},
	{PK_ROLE_ENUM_SEARCH_DETAILS,		"pk-package-search"},
	{PK_ROLE_ENUM_SEARCH_FILE,		"pk-package-search"},
	{PK_ROLE_ENUM_SEARCH_GROUP,		"pk-package-search"},
	{PK_ROLE_ENUM_SEARCH_NAME,		"pk-package-search"},
	{PK_ROLE_ENUM_SERVICE_PACK,		"pk-package-update"},
	{PK_ROLE_ENUM_UPDATE_PACKAGES,		"pk-package-update"},
	{PK_ROLE_ENUM_UPDATE_SYSTEM,		"system-software-update"},
	{PK_ROLE_ENUM_WHAT_PROVIDES,		"pk-package-search"},
	{0, NULL}
};

static const PkEnumMatch enum_group_icon_name[] = {
	{PK_GROUP_ENUM_UNKNOWN,			"help-browser"},	/* fall though value */
	{PK_GROUP_ENUM_ACCESSIBILITY,		"preferences-desktop-accessibility"},
	{PK_GROUP_ENUM_ACCESSORIES,		"applications-accessories"},
	{PK_GROUP_ENUM_ADMIN_TOOLS,		"system-lock-screen"},
	{PK_GROUP_ENUM_COLLECTIONS,		"pk-collection-installed"},
	{PK_GROUP_ENUM_COMMUNICATION,		"folder-remote"},
	{PK_GROUP_ENUM_DESKTOP_GNOME,		"pk-desktop-gnome"},
	{PK_GROUP_ENUM_DESKTOP_KDE,		"pk-desktop-kde"},
	{PK_GROUP_ENUM_DESKTOP_OTHER,		"user-desktop"},
	{PK_GROUP_ENUM_DESKTOP_XFCE,		"pk-desktop-xfce"},
	{PK_GROUP_ENUM_DOCUMENTATION,		"x-office-address-book"},
	{PK_GROUP_ENUM_EDUCATION,		"utilities-system-monitor"},
	{PK_GROUP_ENUM_ELECTRONICS,		"video-display"},
	{PK_GROUP_ENUM_FONTS,			"preferences-desktop-font"},
	{PK_GROUP_ENUM_GAMES,			"applications-games"},
	{PK_GROUP_ENUM_GRAPHICS,		"applications-graphics"},
	{PK_GROUP_ENUM_INTERNET,		"applications-internet"},
	{PK_GROUP_ENUM_LEGACY,			"media-floppy"},
	{PK_GROUP_ENUM_LOCALIZATION,		"preferences-desktop-locale"},
	{PK_GROUP_ENUM_MAPS,			"applications-multimedia"},
	{PK_GROUP_ENUM_MULTIMEDIA,		"applications-multimedia"},
	{PK_GROUP_ENUM_NETWORK,			"network-wired"},
	{PK_GROUP_ENUM_OFFICE,			"applications-office"},
	{PK_GROUP_ENUM_OTHER,			"applications-other"},
	{PK_GROUP_ENUM_POWER_MANAGEMENT,	"battery"},
	{PK_GROUP_ENUM_PROGRAMMING,		"applications-development"},
	{PK_GROUP_ENUM_PUBLISHING,		"accessories-dictionary"},
	{PK_GROUP_ENUM_REPOS,			"system-file-manager"},
	{PK_GROUP_ENUM_SCIENCE,			"application-certificate"},
	{PK_GROUP_ENUM_SECURITY,		"network-wireless-encrypted"},
	{PK_GROUP_ENUM_SERVERS,			"network-server"},
	{PK_GROUP_ENUM_SYSTEM,			"applications-system"},
	{PK_GROUP_ENUM_VIRTUALIZATION,		"computer"},
	{0, NULL}
};

static const PkEnumMatch enum_restart_icon_name[] = {
	{PK_RESTART_ENUM_UNKNOWN,		"help-browser"},	/* fall though value */
	{PK_RESTART_ENUM_NONE,			"dialog-information"},
	{PK_RESTART_ENUM_SYSTEM,		"dialog-error"},
	{PK_RESTART_ENUM_SESSION,		"dialog-warning"},
	{PK_RESTART_ENUM_APPLICATION,		"dialog-warning"},
	{0, NULL}
};

static const PkEnumMatch enum_message_icon_name[] = {
	{PK_MESSAGE_ENUM_UNKNOWN,		"help-browser"},	/* fall though value */
	{PK_MESSAGE_ENUM_BROKEN_MIRROR,		"dialog-error"},
	{PK_MESSAGE_ENUM_CONNECTION_REFUSED,	"dialog-error"},
	{PK_MESSAGE_ENUM_PARAMETER_INVALID,	"dialog-error"},
	{PK_MESSAGE_ENUM_PRIORITY_INVALID,	"dialog-error"},
	{PK_MESSAGE_ENUM_BACKEND_ERROR,		"dialog-error"},
	{PK_MESSAGE_ENUM_DAEMON_ERROR,		"dialog-error"},
	{PK_MESSAGE_ENUM_CACHE_BEING_REBUILT,	"dialog-information"},
	{PK_MESSAGE_ENUM_UNTRUSTED_PACKAGE,	"dialog-warning"},
	{PK_MESSAGE_ENUM_NEWER_PACKAGE_EXISTS,	"dialog-information"},
	{PK_MESSAGE_ENUM_COULD_NOT_FIND_PACKAGE,"dialog-error"},
	{PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED,	"dialog-information"},
	{PK_MESSAGE_ENUM_PACKAGE_ALREADY_INSTALLED,	"dialog-information"},
	{0, NULL}
};

static const PkEnumMatch enum_freq[] = {
	{GPK_FREQ_ENUM_UNKNOWN,			"unknown"},	/* fall though value */
	{GPK_FREQ_ENUM_HOURLY,			"hourly"},
	{GPK_FREQ_ENUM_DAILY,			"daily"},
	{GPK_FREQ_ENUM_WEEKLY,			"weekly"},
	{GPK_FREQ_ENUM_NEVER,			"never"},
	{0, NULL}
};

static const PkEnumMatch enum_update[] = {
	{GPK_UPDATE_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{GPK_UPDATE_ENUM_ALL,			"all"},
	{GPK_UPDATE_ENUM_SECURITY,		"security"},
	{GPK_UPDATE_ENUM_NONE,			"none"},
	{0, NULL}
};

/**
 * gpk_freq_enum_from_text:
 * @freq: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 **/
GpkFreqEnum
gpk_freq_enum_from_text (const gchar *freq)
{
	return pk_enum_find_value (enum_freq, freq);
}

/**
 * gpk_freq_enum_to_text:
 * @freq: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
gpk_freq_enum_to_text (GpkFreqEnum freq)
{
	return pk_enum_find_string (enum_freq, freq);
}

/**
 * gpk_update_enum_from_text:
 * @update: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 **/
GpkUpdateEnum
gpk_update_enum_from_text (const gchar *update)
{
	return pk_enum_find_value (enum_update, update);
}

/**
 * gpk_update_enum_to_text:
 * @update: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
gpk_update_enum_to_text (GpkUpdateEnum update)
{
	return pk_enum_find_string (enum_update, update);
}

/**
 * gpk_error_enum_to_localised_text:
 **/
const gchar *
gpk_error_enum_to_localised_text (PkErrorCodeEnum code)
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
	case PK_ERROR_ENUM_GROUP_LIST_INVALID:
		text = _("The group list was invalid");
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
		text = _("The task was canceled");
		break;
	case PK_ERROR_ENUM_PROCESS_KILL:
		text = _("The task was forcibly canceled");
		break;
	case PK_ERROR_ENUM_FAILED_CONFIG_PARSING:
		text = _("Reading the config file failed");
		break;
	case PK_ERROR_ENUM_CANNOT_CANCEL:
		text = _("The task cannot be canceled");
		break;
	case PK_ERROR_ENUM_CANNOT_INSTALL_SOURCE_PACKAGE:
		text = _("Source packages cannot be installed");
		break;
	case PK_ERROR_ENUM_NO_LICENSE_AGREEMENT:
		text = _("The license agreement failed");
		break;
	case PK_ERROR_ENUM_FILE_CONFLICTS:
		text = _("Local file conflict between packages");
		break;
	case PK_ERROR_ENUM_PACKAGE_CONFLICTS:
		text = _("Packages are not compatible");
		break;
	case PK_ERROR_ENUM_REPO_NOT_AVAILABLE:
		text = _("Problem connecting to a software source");
		break;
	case PK_ERROR_ENUM_FAILED_INITIALIZATION:
		text = _("Failed to initialize");
		break;
	case PK_ERROR_ENUM_FAILED_FINALISE:
		text = _("Failed to finalise");
		break;
	case PK_ERROR_ENUM_CANNOT_GET_LOCK:
		text = _("Cannot get lock");
		break;
	case PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE:
		text = _("No packages to update");
		break;
	case PK_ERROR_ENUM_CANNOT_WRITE_REPO_CONFIG:
		text = _("Cannot write repository configuration");
		break;
	case PK_ERROR_ENUM_LOCAL_INSTALL_FAILED:
		text = _("Local install failed");
		break;
	case PK_ERROR_ENUM_BAD_GPG_SIGNATURE:
		text = _("Bad GPG signature");
		break;
	case PK_ERROR_ENUM_MISSING_GPG_SIGNATURE:
		text = _("Missing GPG signature");
		break;
	case PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR:
		text = _("Repository configuration invalid");
		break;
	case PK_ERROR_ENUM_INVALID_PACKAGE_FILE:
		text = _("Invalid package file");
		break;
	case PK_ERROR_ENUM_PACKAGE_INSTALL_BLOCKED:
		text = _("Package install blocked");
		break;
	case PK_ERROR_ENUM_PACKAGE_CORRUPT:
		text = _("Package is corrupt");
		break;
	case PK_ERROR_ENUM_ALL_PACKAGES_ALREADY_INSTALLED:
		text = _("All packages are already installed");
		break;
	case PK_ERROR_ENUM_FILE_NOT_FOUND:
		text = _("The specified file could not be found");
		break;
	case PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY:
		text = _("No more mirrors are available");
		break;
	default:
		egg_warning ("Unknown error");
	}
	return text;
}

/**
 * gpk_error_enum_to_localised_message:
 **/
const gchar *
gpk_error_enum_to_localised_message (PkErrorCodeEnum code)
{
	const gchar *text = NULL;
	switch (code) {
	case PK_ERROR_ENUM_NO_NETWORK:
		text = _("There is no network connection available.\n"
			 "Please check your connection settings and try again");
		break;
	case PK_ERROR_ENUM_NO_CACHE:
		text = _("The package list needs to be rebuilt.\n"
			 "This should have been done by the backend automatically.");
		break;
	case PK_ERROR_ENUM_OOM:
		text = _("The service that is responsible for handling user requests is out of memory.\n"
			 "Please restart your computer.");
		break;
	case PK_ERROR_ENUM_CREATE_THREAD_FAILED:
		text = _("A thread could not be created to service the user request.");
		break;
	case PK_ERROR_ENUM_NOT_SUPPORTED:
		text = _("The action is not supported by this backend.\n"
			 "Please report a bug at " GPK_BUGZILLA_URL " as this should not have happened.");
		break;
	case PK_ERROR_ENUM_INTERNAL_ERROR:
		text = _("A problem that we were not expecting has occurred.\n"
			 "Please report this bug at " GPK_BUGZILLA_URL " with the error description.");
		break;
	case PK_ERROR_ENUM_GPG_FAILURE:
		text = _("A security trust relationship could not be made with software source.\n"
			 "Please check your security settings.");
		break;
	case PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED:
		text = _("The package that is trying to be removed or updated is not already installed.");
		break;
	case PK_ERROR_ENUM_PACKAGE_NOT_FOUND:
		text = _("The package that is being modified was not found on your system or in any software source.");
		break;
	case PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED:
		text = _("The package that is trying to be installed is already installed.");
		break;
	case PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED:
		text = _("The package download failed.\n"
			 "Please check your network connectivity.");
		break;
	case PK_ERROR_ENUM_GROUP_NOT_FOUND:
		text = _("The group type was not found.\n"
			 "Please check your group list and try again.");
		break;
	case PK_ERROR_ENUM_GROUP_LIST_INVALID:
		text = _("The group list could not be loaded.\n"
			 "Refreshing your cache may help, although this is normally a software "
			 "source error.");
		break;
	case PK_ERROR_ENUM_DEP_RESOLUTION_FAILED:
		text = _("A package could not be found that allows the task to complete.\n"
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
		text = _("An unspecified task error has occurred.\n"
			 "More information is available in the detailed report.");
		break;
	case PK_ERROR_ENUM_REPO_NOT_FOUND:
		text = _("The remote software source name was not found.\n"
			 "You may need to enable an item in Software Sources");
		break;
	case PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE:
		text = _("Removing a protected system package is not allowed.");
		break;
	case PK_ERROR_ENUM_TRANSACTION_CANCELLED:
		text = _("The task was canceled successfully and no packages were changed.");
		break;
	case PK_ERROR_ENUM_PROCESS_KILL:
		text = _("The task was canceled successfully and no packages were changed.\n"
			 "The backend did not exit cleanly.");
		break;
	case PK_ERROR_ENUM_FAILED_CONFIG_PARSING:
		text = _("The native package configuration file could not be opened.\n"
			 "Please make sure configuration is valid.");
		break;
	case PK_ERROR_ENUM_CANNOT_CANCEL:
		text = _("The task is not safe to be canceled at this time.");
		break;
	case PK_ERROR_ENUM_CANNOT_INSTALL_SOURCE_PACKAGE:
		text = _("Source packages are not normally installed this way.\n"
			 "Check the extension of the file you are trying to install.");
		break;
	case PK_ERROR_ENUM_NO_LICENSE_AGREEMENT:
		text = _("The license agreement was not agreed to.\n"
			 "To use this software you have to accept the license.");
		break;
	case PK_ERROR_ENUM_FILE_CONFLICTS:
		text = _("Two packages provide the same file.\n"
			 "This is usually due to mixing packages from different software sources.");
		break;
	case PK_ERROR_ENUM_PACKAGE_CONFLICTS:
		text = _("Multiple packages exist that are not compatible with each other.\n"
			 "This is usually due to mixing packages from different software sources.");
		break;
	case PK_ERROR_ENUM_REPO_NOT_AVAILABLE:
		text = _("There was a (possibly temporary) problem connecting to a software source\n"
			 "Please check the detailed error for further details.");
		break;
	case PK_ERROR_ENUM_FAILED_INITIALIZATION:
		text = _("Failed to initialize packaging backend.\n"
			 "This may occur if other packaging tools are being used simultaneously.");
		break;
	case PK_ERROR_ENUM_FAILED_FINALISE:
		text = _("Failed to close down the backend instance.\n"
			 "This error can normally be ignored.");
		break;
	case PK_ERROR_ENUM_CANNOT_GET_LOCK:
		text = _("Cannot get the exclusive lock on the packaging backend.\n"
			 "Please close any other legacy packaging tools that may be open.");
		break;
	case PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE:
		text = _("None of the selected packages could be updated.");
		break;
	case PK_ERROR_ENUM_CANNOT_WRITE_REPO_CONFIG:
		text = _("The repository configuration could not be modified.");
		break;
	case PK_ERROR_ENUM_LOCAL_INSTALL_FAILED:
		text = _("Installing the local file failed.\n"
			 "More information is available in the detailed report.");
		break;
	case PK_ERROR_ENUM_BAD_GPG_SIGNATURE:
		text = _("The package signature could not be verified.");
		break;
	case PK_ERROR_ENUM_MISSING_GPG_SIGNATURE:
		text = _("The package signature was missing and this package is untrusted.\n"
			 "This package was not signed with a GPG key when created.");
		break;
	case PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR:
		text = _("Repository configuration was invalid and could not be read.");
		break;
	case PK_ERROR_ENUM_INVALID_PACKAGE_FILE:
		text = _("The package you are attempting to install is not valid.\n"
			 "The package file could be corrupt, or not a proper package.");
		break;
	case PK_ERROR_ENUM_PACKAGE_INSTALL_BLOCKED:
		text = _("Installation of this package prevented by your packaging system's configuration.");
		break;
	case PK_ERROR_ENUM_PACKAGE_CORRUPT:
		text = _("The package that was downloaded is corrupt and needs to be downloaded again.");
		break;
	case PK_ERROR_ENUM_ALL_PACKAGES_ALREADY_INSTALLED:
		text = _("All of the packages selected for install are already installed on the system.");
		break;
	case PK_ERROR_ENUM_FILE_NOT_FOUND:
		text = _("The specified file could not be found on the system.\n"
			 "Check the file still exists and has not been deleted.");
		break;
	case PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY:
		text = _("Required data could not be found on any of the configured software sources.\n"
			 "There were no more download mirrors that could be tried.");
		break;
	default:
		egg_warning ("Unknown error, please report a bug at " GPK_BUGZILLA_URL ".\n"
			    "More information is available in the detailed report.");
	}
	return text;
}

/**
 * gpk_restart_enum_to_localised_text_future:
 **/
const gchar *
gpk_restart_enum_to_localised_text_future (PkRestartEnum restart)
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
		egg_warning ("restart unrecognised: %i", restart);
	}
	return text;
}

/**
 * gpk_restart_enum_to_localised_text:
 **/
const gchar *
gpk_restart_enum_to_localised_text (PkRestartEnum restart)
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
		egg_warning ("restart unrecognised: %i", restart);
	}
	return text;
}

/**
 * gpk_update_state_enum_to_localised_text:
 **/
const gchar *
gpk_update_state_enum_to_localised_text (PkUpdateStateEnum state)
{
	const gchar *text = NULL;
	switch (state) {
	case PK_UPDATE_STATE_ENUM_STABLE:
		text = _("Stable");
		break;
	case PK_UPDATE_STATE_ENUM_UNSTABLE:
		text = _("Unstable");
		break;
	case PK_UPDATE_STATE_ENUM_TESTING:
		text = _("Testing");
		break;
	default:
		egg_warning ("state unrecognised: %i", state);
	}
	return text;
}

/**
 * gpk_message_enum_to_localised_text:
 **/
const gchar *
gpk_message_enum_to_localised_text (PkMessageEnum message)
{
	const gchar *text = NULL;
	switch (message) {
	case PK_MESSAGE_ENUM_BROKEN_MIRROR:
		text = _("A mirror is possibly broken");
		break;
	case PK_MESSAGE_ENUM_CONNECTION_REFUSED:
		text = _("The connection was refused");
		break;
	case PK_MESSAGE_ENUM_PARAMETER_INVALID:
		text = _("The parameter was invalid");
		break;
	case PK_MESSAGE_ENUM_PRIORITY_INVALID:
		text = _("The priority was invalid");
		break;
	case PK_MESSAGE_ENUM_BACKEND_ERROR:
		text = _("Generic backend error");
		break;
	case PK_MESSAGE_ENUM_DAEMON_ERROR:
		text = _("Generic daemon error");
		break;
	case PK_MESSAGE_ENUM_CACHE_BEING_REBUILT:
		text = _("The package list cache is being rebuilt");
		break;
	case PK_MESSAGE_ENUM_UNTRUSTED_PACKAGE:
		text = _("An untrusted package was installed");
		break;
	case PK_MESSAGE_ENUM_NEWER_PACKAGE_EXISTS:
		text = _("A newer package exists");
		break;
	case PK_MESSAGE_ENUM_COULD_NOT_FIND_PACKAGE:
		text = _("Could not find package");
		break;
	case PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED:
		text = _("Configuration files were changed");
		break;
	case PK_MESSAGE_ENUM_PACKAGE_ALREADY_INSTALLED:
		text = _("Package is already installed");
		break;

	default:
		egg_warning ("message unrecognised: %i", message);
	}
	return text;
}

/**
 * gpk_status_enum_to_localised_text:
 **/
const gchar *
gpk_status_enum_to_localised_text (PkStatusEnum status)
{
	const gchar *text = NULL;
	switch (status) {
	case PK_STATUS_ENUM_UNKNOWN:
		text = _("Unknown state");
		break;
	case PK_STATUS_ENUM_SETUP:
		text = _("Waiting for service to start");
		break;
	case PK_STATUS_ENUM_WAIT:
		text = _("Waiting for other tasks");
		break;
	case PK_STATUS_ENUM_RUNNING:
		text = _("Running task");
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
	case PK_STATUS_ENUM_SIG_CHECK:
		text = _("Checking signatures");
		break;
	case PK_STATUS_ENUM_ROLLBACK:
		text = _("Rolling back");
		break;
	case PK_STATUS_ENUM_TEST_COMMIT:
		text = _("Testing changes");
		break;
	case PK_STATUS_ENUM_COMMIT:
		text = _("Committing changes");
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
	case PK_STATUS_ENUM_DOWNLOAD_REPOSITORY:
		text = _("Downloading repository information");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST:
		text = _("Downloading list of packages");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_FILELIST:
		text = _("Downloading file lists");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_CHANGELOG:
		text = _("Downloading lists of changes");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_GROUP:
		text = _("Downloading groups");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO:
		text = _("Downloading update information");
		break;
	case PK_STATUS_ENUM_REPACKAGING:
		text = _("Repackaging files");
		break;
	case PK_STATUS_ENUM_LOADING_CACHE:
		text = _("Loading cache");
		break;
	case PK_STATUS_ENUM_SCAN_APPLICATIONS:
		text = _("Scanning installed applications");
		break;
	case PK_STATUS_ENUM_GENERATE_PACKAGE_LIST:
		text = _("Generating package lists");
		break;
	default:
		egg_warning ("status unrecognised: %s", pk_status_enum_to_text (status));
	}
	return text;
}

/**
 * gpk_update_enum_to_localised_text:
 **/
gchar *
gpk_update_enum_to_localised_text (PkInfoEnum info, guint number)
{
	gchar *text = NULL;
	switch (info) {
	case PK_INFO_ENUM_LOW:
		text = g_strdup_printf (ngettext ("%i trivial update", "%i trivial updates", number), number);
		break;
	case PK_INFO_ENUM_NORMAL:
		text = g_strdup_printf (ngettext ("%i update", "%i updates", number), number);
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
	case PK_INFO_ENUM_BLOCKED:
		text = g_strdup_printf (ngettext ("%i blocked update", "%i blocked updates", number), number);
		break;
	default:
		egg_warning ("update info unrecognised: %s", pk_info_enum_to_text (info));
	}
	return text;
}

/**
 * gpk_info_enum_to_localised_text:
 **/
const gchar *
gpk_info_enum_to_localised_text (PkInfoEnum info)
{
	const gchar *text = NULL;
	switch (info) {
	case PK_INFO_ENUM_LOW:
		text = _("Trivial update");
		break;
	case PK_INFO_ENUM_NORMAL:
		text = _("Update");
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
	case PK_INFO_ENUM_INSTALLED:
	case PK_INFO_ENUM_COLLECTION_INSTALLED:
		text = _("Installed");
		break;
	case PK_INFO_ENUM_AVAILABLE:
	case PK_INFO_ENUM_COLLECTION_AVAILABLE:
		text = _("Available");
		break;
	default:
		egg_warning ("info unrecognised: %s", pk_info_enum_to_text (info));
	}
	return text;
}

/**
 * gpk_info_enum_to_localised_present:
 **/
const gchar *
gpk_info_enum_to_localised_present (PkInfoEnum info)
{
	const gchar *text = NULL;
	switch (info) {
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
	case PK_INFO_ENUM_CLEANUP:
		text = _("Cleaning up");
		break;
	case PK_INFO_ENUM_OBSOLETING:
		text = _("Obsoleting");
		break;
	default:
		egg_warning ("info unrecognised: %s", pk_info_enum_to_text (info));
	}
	return text;
}

/**
 * gpk_info_enum_to_localised_past:
 **/
const gchar *
gpk_info_enum_to_localised_past (PkInfoEnum info)
{
	const gchar *text = NULL;
	switch (info) {
	case PK_INFO_ENUM_DOWNLOADING:
		text = _("Downloaded");
		break;
	case PK_INFO_ENUM_UPDATING:
		text = _("Updated");
		break;
	case PK_INFO_ENUM_INSTALLING:
		text = _("Installed");
		break;
	case PK_INFO_ENUM_REMOVING:
		text = _("Removed");
		break;
	case PK_INFO_ENUM_CLEANUP:
		text = _("Cleaned up");
		break;
	case PK_INFO_ENUM_OBSOLETING:
		text = _("Obsoleted");
		break;
	default:
		egg_warning ("info unrecognised: %s", pk_info_enum_to_text (info));
	}
	return text;
}

/**
 * gpk_role_enum_to_localised_present:
 **/
const gchar *
gpk_role_enum_to_localised_present (PkRoleEnum role)
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
	case PK_ROLE_ENUM_GET_DETAILS:
		text = _("Getting details");
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
	case PK_ROLE_ENUM_REMOVE_PACKAGES:
		text = _("Removing");
		break;
	case PK_ROLE_ENUM_INSTALL_PACKAGES:
		text = _("Installing");
		break;
	case PK_ROLE_ENUM_INSTALL_FILES:
		text = _("Installing file");
		break;
	case PK_ROLE_ENUM_REFRESH_CACHE:
		text = _("Refreshing package cache");
		break;
	case PK_ROLE_ENUM_UPDATE_PACKAGES:
		text = _("Updating packages");
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
	case PK_ROLE_ENUM_RESOLVE:
		text = _("Resolving");
		break;
	case PK_ROLE_ENUM_GET_FILES:
		text = _("Getting file list");
		break;
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		text = _("Getting what provides");
		break;
	case PK_ROLE_ENUM_SERVICE_PACK:
		text = _("Service pack");
		break;
	case PK_ROLE_ENUM_INSTALL_SIGNATURE:
		text = _("Installing signature");
		break;
	case PK_ROLE_ENUM_GET_PACKAGES:
		text = _("Getting package lists");
		break;
	case PK_ROLE_ENUM_ACCEPT_EULA:
		text = _("Accepting EULA");
		break;
	case PK_ROLE_ENUM_DOWNLOAD_PACKAGES:
		text = _("Downloading packages");
		break;
	case PK_ROLE_ENUM_GET_DISTRO_UPGRADES:
		text = _("Getting distribution upgrade information");
		break;
	default:
		egg_warning ("role unrecognised: %s", pk_role_enum_to_text (role));
	}
	return text;
}

/**
 * gpk_role_enum_to_localised_past:
 *
 * These are past tense versions of the action
 **/
const gchar *
gpk_role_enum_to_localised_past (PkRoleEnum role)
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
	case PK_ROLE_ENUM_GET_DETAILS:
		text = _("Got details");
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
	case PK_ROLE_ENUM_REMOVE_PACKAGES:
		text = _("Removed package");
		break;
	case PK_ROLE_ENUM_INSTALL_PACKAGES:
		text = _("Installed package");
		break;
	case PK_ROLE_ENUM_INSTALL_FILES:
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
	case PK_ROLE_ENUM_RESOLVE:
		text = _("Resolved");
		break;
	case PK_ROLE_ENUM_GET_FILES:
		text = _("Got file list");
		break;
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		text = _("Got what provides");
		break;
	case PK_ROLE_ENUM_INSTALL_SIGNATURE:
		text = _("Installed signature");
		break;
	case PK_ROLE_ENUM_GET_PACKAGES:
		text = _("Got package lists");
		break;
	case PK_ROLE_ENUM_ACCEPT_EULA:
		text = _("Accepted EULA");
		break;
	case PK_ROLE_ENUM_DOWNLOAD_PACKAGES:
		text = _("Downloaded packages");
		break;
	case PK_ROLE_ENUM_GET_DISTRO_UPGRADES:
		text = _("Got distribution upgrades");
		break;
	default:
		egg_warning ("role unrecognised: %s", pk_role_enum_to_text (role));
	}
	return text;
}

/**
 * gpk_group_enum_to_localised_text:
 **/
const gchar *
gpk_group_enum_to_localised_text (PkGroupEnum group)
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
	case PK_GROUP_ENUM_SCIENCE:
		text = _("Science");
		break;
	case PK_GROUP_ENUM_DOCUMENTATION:
		text = _("Documentation");
		break;
	case PK_GROUP_ENUM_ELECTRONICS:
		text = _("Electronics");
		break;
	case PK_GROUP_ENUM_COLLECTIONS:
		text = _("Package collections");
		break;
	case PK_GROUP_ENUM_UNKNOWN:
		text = _("Unknown group");
		break;
	default:
		egg_warning ("group unrecognised: %i", group);
	}
	return text;
}

/**
 * gpk_info_enum_to_icon_name:
 **/
const gchar *
gpk_info_enum_to_icon_name (PkInfoEnum info)
{
	return pk_enum_find_string (enum_info_icon_name, info);
}

/**
 * gpk_status_enum_to_icon_name:
 **/
const gchar *
gpk_status_enum_to_icon_name (PkStatusEnum status)
{
	return pk_enum_find_string (enum_status_icon_name, status);
}

/**
 * gpk_status_enum_to_animation:
 **/
const gchar *
gpk_status_enum_to_animation (PkStatusEnum status)
{
	return pk_enum_find_string (enum_status_animation, status);
}

/**
 * gpk_role_enum_to_icon_name:
 **/
const gchar *
gpk_role_enum_to_icon_name (PkRoleEnum role)
{
	return pk_enum_find_string (enum_role_icon_name, role);
}

/**
 * gpk_group_enum_to_icon_name:
 **/
const gchar *
gpk_group_enum_to_icon_name (PkGroupEnum group)
{
	return pk_enum_find_string (enum_group_icon_name, group);
}

/**
 * gpk_restart_enum_to_icon_name:
 **/
const gchar *
gpk_restart_enum_to_icon_name (PkRestartEnum restart)
{
	return pk_enum_find_string (enum_restart_icon_name, restart);
}

/**
 * gpk_message_enum_to_icon_name:
 **/
const gchar *
gpk_message_enum_to_icon_name (PkMessageEnum message)
{
	return pk_enum_find_string (enum_message_icon_name, message);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpk_enum_test (gpointer data)
{
	guint i;
	const gchar *string;
	EggTest *test = (EggTest *) data;

	if (!egg_test_start (test, "GpkEnum"))
		return;

	/************************************************************
	 ****************     localised enums          **************
	 ************************************************************/
	egg_test_title (test, "check we convert all the localised past role enums");
	for (i=0; i<PK_ROLE_ENUM_UNKNOWN; i++) {
		string = gpk_role_enum_to_localised_past (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the localised present role enums");
	for (i=0; i<PK_ROLE_ENUM_UNKNOWN; i++) {
		string = gpk_role_enum_to_localised_present (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the role icon name enums");
	for (i=0; i<PK_ROLE_ENUM_UNKNOWN; i++) {
		string = gpk_role_enum_to_icon_name (i);
		if (string == NULL || egg_strequal (string, "help-browser")) {
			egg_test_failed (test, "failed to get %s", pk_role_enum_to_text (i));
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the status animation enums");
	for (i=0; i<PK_ROLE_ENUM_UNKNOWN; i++) {
		string = gpk_status_enum_to_animation (i);
		if (string == NULL || egg_strequal (string, "help-browser")) {
			egg_test_failed (test, "failed to get %s", pk_role_enum_to_text (i));
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the info icon names enums");
	for (i=0; i<PK_INFO_ENUM_UNKNOWN; i++) {
		string = gpk_info_enum_to_icon_name (i);
		if (string == NULL || egg_strequal (string, "help-browser")) {
			egg_test_failed (test, "failed to get %s", pk_info_enum_to_text (i));
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the localised status enums");
	for (i=0; i<PK_STATUS_ENUM_UNKNOWN; i++) {
		string = gpk_status_enum_to_localised_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the status icon names enums");
	for (i=0; i<PK_STATUS_ENUM_UNKNOWN; i++) {
		string = gpk_status_enum_to_icon_name (i);
		if (string == NULL || egg_strequal (string, "help-browser")) {
			egg_test_failed (test, "failed to get %s", pk_status_enum_to_text (i));
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the restart icon names enums");
	for (i=0; i<PK_RESTART_ENUM_UNKNOWN; i++) {
		string = gpk_restart_enum_to_icon_name (i);
		if (string == NULL || egg_strequal (string, "help-browser")) {
			egg_test_failed (test, "failed to get %s", pk_restart_enum_to_text (i));
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the localised error enums");
	for (i=0; i<PK_ERROR_ENUM_UNKNOWN; i++) {
		string = gpk_error_enum_to_localised_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %s", pk_error_enum_to_text(i));
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the localised error messages");
	for (i=0; i<PK_ERROR_ENUM_UNKNOWN; i++) {
		string = gpk_error_enum_to_localised_message (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %s", pk_error_enum_to_text(i));
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the localised restart enums");
	for (i=0; i<PK_RESTART_ENUM_UNKNOWN; i++) {
		string = gpk_restart_enum_to_localised_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the message icon name enums");
	for (i=0; i<PK_MESSAGE_ENUM_UNKNOWN; i++) {
		string = gpk_message_enum_to_icon_name (i);
		if (string == NULL || egg_strequal (string, "help-browser")) {
			egg_test_failed (test, "failed to get %s", pk_message_enum_to_text (i));
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the localised message enums");
	for (i=0; i<PK_MESSAGE_ENUM_UNKNOWN; i++) {
		string = gpk_message_enum_to_localised_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the localised restart future enums");
	for (i=0; i<PK_RESTART_ENUM_UNKNOWN; i++) {
		string = gpk_restart_enum_to_localised_text_future (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the localised group enums");
	for (i=0; i<PK_GROUP_ENUM_UNKNOWN; i++) {
		string = gpk_group_enum_to_localised_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the group icon name enums");
	for (i=0; i<PK_GROUP_ENUM_UNKNOWN; i++) {
		string = gpk_group_enum_to_icon_name (i);
		if (string == NULL || egg_strequal (string, "help-browser")) {
			egg_test_failed (test, "failed to get %s", pk_group_enum_to_text (i));
			break;
		}
	}
	egg_test_success (test, NULL);


	/************************************************************/
	egg_test_title (test, "check we convert all the freq bitfield");
	for (i=0; i<=GPK_FREQ_ENUM_UNKNOWN; i++) {
		string = gpk_freq_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the update bitfield");
	for (i=0; i<=GPK_UPDATE_ENUM_UNKNOWN; i++) {
		string = gpk_update_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	egg_test_end (test);
}
#endif

