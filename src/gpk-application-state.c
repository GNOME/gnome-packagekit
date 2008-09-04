/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
#include <unistd.h>
#include <sys/types.h>

#include "egg-debug.h"
#include <pk-enum.h>
#include <pk-common.h>

#include "gpk-common.h"
#include "gpk-application-state.h"

/**
 * gpk_application_state_installed:
 **/
gboolean
gpk_application_state_installed (GpkPackageState state)
{
	if (state == GPK_STATE_INSTALLED_TO_BE_REMOVED) {
		return TRUE;
	} else if (state == GPK_STATE_AVAILABLE_TO_BE_INSTALLED) {
		return FALSE;
	} else if (state == GPK_STATE_INSTALLED) {
		return TRUE;
	} else if (state == GPK_STATE_AVAILABLE) {
		return FALSE;
	}
	return FALSE;
}

/**
 * gpk_application_state_in_queue:
 **/
gboolean
gpk_application_state_in_queue (GpkPackageState state)
{
	if (state == GPK_STATE_INSTALLED_TO_BE_REMOVED) {
		return TRUE;
	} else if (state == GPK_STATE_AVAILABLE_TO_BE_INSTALLED) {
		return TRUE;
	} else if (state == GPK_STATE_INSTALLED) {
		return FALSE;
	} else if (state == GPK_STATE_AVAILABLE) {
		return FALSE;
	}
	return FALSE;
}

/**
 * gpk_application_state_invert:
 **/
gboolean
gpk_application_state_invert (GpkPackageState *state)
{
	if (*state == GPK_STATE_INSTALLED_TO_BE_REMOVED) {
		*state = GPK_STATE_INSTALLED;
	} else if (*state == GPK_STATE_AVAILABLE_TO_BE_INSTALLED) {
		*state = GPK_STATE_AVAILABLE;
	} else if (*state == GPK_STATE_INSTALLED) {
		*state = GPK_STATE_INSTALLED_TO_BE_REMOVED;
	} else if (*state == GPK_STATE_AVAILABLE) {
		*state = GPK_STATE_AVAILABLE_TO_BE_INSTALLED;
	}

	return TRUE;
}

/**
 * gpk_application_state_select:
 **/
gboolean
gpk_application_state_select (GpkPackageState *state)
{
	gboolean ret = FALSE;

	if (*state == GPK_STATE_INSTALLED) {
		*state = GPK_STATE_INSTALLED_TO_BE_REMOVED;
		ret = TRUE;
	} else if (*state == GPK_STATE_AVAILABLE) {
		*state = GPK_STATE_AVAILABLE_TO_BE_INSTALLED;
		ret = TRUE;
	}

	return ret;
}

/**
 * gpk_application_state_unselect:
 **/
gboolean
gpk_application_state_unselect (GpkPackageState *state)
{
	gboolean ret = FALSE;

	if (*state == GPK_STATE_INSTALLED_TO_BE_REMOVED) {
		*state = GPK_STATE_INSTALLED;
		ret = TRUE;
	} else if (*state == GPK_STATE_AVAILABLE_TO_BE_INSTALLED) {
		*state = GPK_STATE_AVAILABLE;
		ret = TRUE;
	}

	return ret;
}

/**
 * gpk_application_state_get_icon:
 **/
const gchar *
gpk_application_state_get_icon (GpkPackageState state)
{
	if (state == GPK_STATE_INSTALLED) {
		return gpk_info_enum_to_icon_name (PK_INFO_ENUM_INSTALLED);
	} else if (state == GPK_STATE_INSTALLED_TO_BE_REMOVED) {
		return gpk_info_enum_to_icon_name (PK_INFO_ENUM_REMOVING);
	} else if (state == GPK_STATE_AVAILABLE) {
		return gpk_info_enum_to_icon_name (PK_INFO_ENUM_AVAILABLE);
	} else if (state == GPK_STATE_AVAILABLE_TO_BE_INSTALLED) {
		return gpk_info_enum_to_icon_name (PK_INFO_ENUM_INSTALLING);
	}
	return NULL;
}

/**
 * gpk_application_state_get_checkbox:
 **/
gboolean
gpk_application_state_get_checkbox (GpkPackageState state)
{
	if (state == GPK_STATE_INSTALLED) {
		return TRUE;
	} else if (state == GPK_STATE_INSTALLED_TO_BE_REMOVED) {
		return FALSE;
	} else if (state == GPK_STATE_AVAILABLE) {
		return FALSE;
	} else if (state == GPK_STATE_AVAILABLE_TO_BE_INSTALLED) {
		return TRUE;
	}
	return FALSE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpk_common_test (gpointer data)
{
	EggTest *test = (EggTest *) data;

	if (egg_test_start (test, "GpkCommon") == FALSE) {
		return;
	}

	egg_test_end (test);
}
#endif

