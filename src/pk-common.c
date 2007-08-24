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

