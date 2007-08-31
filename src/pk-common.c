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

#include <pk-debug.h>
#include <pk-task-utils.h>
#include "pk-common.h"

/**
 * pk_task_error_code_to_localised_text:
 **/
const gchar *
pk_task_error_code_to_localised_text (PkTaskErrorCode code)
{
	const gchar *text = NULL;
	switch (code) {
	case PK_TASK_ERROR_CODE_NO_NETWORK:
		text = _("No network connection available");
		break;
	case PK_TASK_ERROR_CODE_NOT_SUPPORTED:
		text = _("Not supported by this backend");
		break;
	case PK_TASK_ERROR_CODE_INTERNAL_ERROR:
		text = _("An internal system error has occurred");
		break;
	case PK_TASK_ERROR_CODE_GPG_FAILURE:
		text = _("A security trust relationship is not present");
		break;
	case PK_TASK_ERROR_CODE_PACKAGE_NOT_INSTALLED:
		text = _("The package is not installed");
		break;
	case PK_TASK_ERROR_CODE_PACKAGE_ALREADY_INSTALLED:
		text = _("The package is already installed");
		break;
	case PK_TASK_ERROR_CODE_PACKAGE_DOWNLOAD_FAILED:
		text = _("The package download failed");
		break;
	case PK_TASK_ERROR_CODE_DEP_RESOLUTION_FAILED:
		text = _("Dependency resolution failed");
		break;
	case PK_TASK_ERROR_CODE_FILTER_INVALID:
		text = _("Search filter was invalid");
		break;
	case PK_TASK_ERROR_CODE_PACKAGE_ID_INVALID:
		text = _("The package ID was not well formed");
		break;
	default:
		text = _("Unknown error");
	}
	return text;
}

/**
 * pk_task_restart_to_localised_text:
 **/
const gchar *
pk_task_restart_to_localised_text (PkTaskRestart restart)
{
	const gchar *text = NULL;
	switch (restart) {
	case PK_TASK_RESTART_SYSTEM:
		text = _("A system restart is required");
		break;
	case PK_TASK_RESTART_SESSION:
		text = _("You will need to log off and log back on");
		break;
	case PK_TASK_RESTART_APPLICATION:
		text = _("You need to restart the application");
		break;
	default:
		pk_error ("restart unrecognised: %i", restart);
	}
	return text;
}

/**
 * pk_task_status_to_localised_text:
 **/
const gchar *
pk_task_status_to_localised_text (PkTaskStatus status)
{
	const gchar *text = NULL;
	switch (status) {
	case PK_TASK_STATUS_SETUP:
		text = _("Setting up");
		break;
	case PK_TASK_STATUS_QUERY:
		text = _("Querying");
		break;
	case PK_TASK_STATUS_REMOVE:
		text = _("Removing");
		break;
	case PK_TASK_STATUS_DOWNLOAD:
		text = _("Downloading");
		break;
	case PK_TASK_STATUS_INSTALL:
		text = _("Installing");
		break;
	case PK_TASK_STATUS_REFRESH_CACHE:
		text = _("Refreshing package cache");
		break;
	case PK_TASK_STATUS_UPDATE:
		text = _("Updating");
		break;
	default:
		pk_error ("status unrecognised: %i", status);
	}
	return text;
}

/**
 * pk_task_status_to_icon_name:
 **/
const gchar *
pk_task_status_to_icon_name (PkTaskStatus status)
{
	const gchar *text = NULL;
	switch (status) {
	case PK_TASK_STATUS_SETUP:
		text = "emblem-system";
		break;
	case PK_TASK_STATUS_QUERY:
		text = "system-search";
		break;
	case PK_TASK_STATUS_REMOVE:
		text = "edit-clear";
		break;
	case PK_TASK_STATUS_DOWNLOAD:
		text = "mail-send-receive";
		break;
	case PK_TASK_STATUS_INSTALL:
		text = "emblem-system";
		break;
	case PK_TASK_STATUS_REFRESH_CACHE:
		text = "view-refresh";
		break;
	case PK_TASK_STATUS_UPDATE:
		text = "system-software-update";
		break;
	default:
		pk_error ("status unrecognised: %i", status);
	}
	return text;
}

/**
 * pk_task_role_to_localised_text:
 **/
const gchar *
pk_task_role_to_localised_text (PkTaskRole role)
{
	const gchar *text = NULL;
	switch (role) {
	case PK_TASK_ROLE_QUERY:
		text = _("Querying");
		break;
	case PK_TASK_ROLE_PACKAGE_REMOVE:
		text = _("Removing");
		break;
	case PK_TASK_ROLE_PACKAGE_INSTALL:
		text = _("Installing");
		break;
	case PK_TASK_ROLE_REFRESH_CACHE:
		text = _("Refreshing package cache");
		break;
	case PK_TASK_ROLE_PACKAGE_UPDATE:
		text = _("Updating package");
		break;
	case PK_TASK_ROLE_SYSTEM_UPDATE:
		text = _("Updating system");
		break;
	default:
		pk_error ("role unrecognised: %i", role);
	}
	return text;
}

/**
 * pk_task_group_to_localised_text:
 **/
const gchar *
pk_task_group_to_localised_text (PkTaskGroup group)
{
	const gchar *text = NULL;
	switch (group) {
	case PK_TASK_GROUP_ACCESSIBILITY:
		text = _("Accessibility");
		break;
	case PK_TASK_GROUP_ACCESSORIES:
		text = _("Accessories");
		break;
	case PK_TASK_GROUP_EDUCATION:
		text = _("Education");
		break;
	case PK_TASK_GROUP_GAMES:
		text = _("Games");
		break;
	case PK_TASK_GROUP_GRAPHICS:
		text = _("Graphics");
		break;
	case PK_TASK_GROUP_INTERNET:
		text = _("Internet");
		break;
	case PK_TASK_GROUP_OFFICE:
		text = _("Office");
		break;
	case PK_TASK_GROUP_OTHER:
		text = _("Other");
		break;
	case PK_TASK_GROUP_PROGRAMMING:
		text = _("Programming");
		break;
	case PK_TASK_GROUP_MULTIMEDIA:
		text = _("Multimedia");
		break;
	case PK_TASK_GROUP_SYSTEM:
		text = _("System");
		break;
	default:
		pk_error ("group unrecognised: %i", group);
	}
	return text;
}

/**
 * pk_task_group_to_icon_name:
 **/
const gchar *
pk_task_group_to_icon_name (PkTaskGroup group)
{
	const gchar *text = NULL;
	switch (group) {
	case PK_TASK_GROUP_ACCESSIBILITY:
		text = "preferences-desktop-accessibility";
		break;
	case PK_TASK_GROUP_ACCESSORIES:
		text = "applications-accessories";
		break;
	case PK_TASK_GROUP_EDUCATION:
		text = "utilities-system-monitor";
		break;
	case PK_TASK_GROUP_GAMES:
		text = "applications-games";
		break;
	case PK_TASK_GROUP_GRAPHICS:
		text = "applications-graphics";
		break;
	case PK_TASK_GROUP_INTERNET:
		text = "applications-internet";
		break;
	case PK_TASK_GROUP_OFFICE:
		text = "applications-office";
		break;
	case PK_TASK_GROUP_OTHER:
		text = "applications-other";
		break;
	case PK_TASK_GROUP_PROGRAMMING:
		text = "applications-development";
		break;
	case PK_TASK_GROUP_MULTIMEDIA:
		text = "applications-multimedia";
		break;
	case PK_TASK_GROUP_SYSTEM:
		text = "applications-system";
		break;
	default:
		pk_error ("group unrecognised: %i", group);
	}
	return text;
}

