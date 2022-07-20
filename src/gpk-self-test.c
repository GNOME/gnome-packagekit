/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2013 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>
#include <glib-object.h>

#include "gpk-common.h"
#include "gpk-enum.h"
#include "gpk-error.h"
#include "gpk-task.h"

static void
gpk_test_enum_func (void)
{
	guint i;
	const gchar *string;

	/* check we convert all the localized past role enums */
	for (i = 0; i < PK_ROLE_ENUM_LAST; i++) {
		string = gpk_role_enum_to_localised_past (i);
		if (string == NULL) {
			g_warning ("failed to get %i", i);
			break;
		}
	}

	/* check we convert all the role icon name enums */
	for (i = PK_ROLE_ENUM_UNKNOWN+1; i < PK_ROLE_ENUM_LAST; i++) {
		string = gpk_role_enum_to_icon_name (i);
		if (string == NULL || g_strcmp0 (string, "help-browser") == 0) {
			g_warning ("failed to get %s", pk_role_enum_to_string (i));
			break;
		}
	}

	/* check we convert all the info icon names enums */
	for (i = PK_INFO_ENUM_UNKNOWN+1; i < PK_INFO_ENUM_LAST; i++) {
		string = gpk_info_enum_to_icon_name (i);
		if (string == NULL || g_strcmp0 (string, "help-browser") == 0) {
			g_warning ("failed to get %s (got %s instead)", pk_info_enum_to_string (i), string);
			break;
		}
	}

	/* check we convert all the localized status enums */
	for (i = 0; i < PK_STATUS_ENUM_LAST; i++) {
		string = gpk_status_enum_to_localised_text (i);
		if (string == NULL) {
			g_warning ("failed to get %i", i);
			break;
		}
	}

	/* check we convert all the status icon names enums */
	for (i = PK_STATUS_ENUM_UNKNOWN+1; i < PK_STATUS_ENUM_LAST; i++) {
		string = gpk_status_enum_to_icon_name (i);
		if (string == NULL || g_strcmp0 (string, "help-browser") == 0) {
			g_warning ("failed to get %s", pk_status_enum_to_string (i));
			break;
		}
	}

	/* check we convert all the restart icon names enums */
	for (i = PK_RESTART_ENUM_UNKNOWN+1; i < PK_RESTART_ENUM_NONE; i++) {
		string = gpk_restart_enum_to_icon_name (i);
		if (string == NULL) {
			g_warning ("failed to get %s", pk_restart_enum_to_string (i));
			break;
		}
	}

	/* check we convert all the localized error enums */
	for (i = 0; i < PK_ERROR_ENUM_LAST; i++) {
		string = gpk_error_enum_to_localised_text (i);
		if (string == NULL) {
			g_warning ("failed to get %s", pk_error_enum_to_string(i));
			break;
		}
	}

	/* check we convert all the localized error messages */
	for (i = 0; i < PK_ERROR_ENUM_LAST; i++) {
		string = gpk_error_enum_to_localised_message (i);
		if (string == NULL) {
			g_warning ("failed to get %s", pk_error_enum_to_string(i));
			break;
		}
	}

	/* check we convert all the localized restart enums */
	for (i = PK_RESTART_ENUM_UNKNOWN+1; i < PK_RESTART_ENUM_LAST; i++) {
		string = gpk_restart_enum_to_localised_text (i);
		if (string == NULL) {
			g_warning ("failed to get %i", i);
			break;
		}
	}

	/* check we convert all the localized restart future enums */
	for (i = PK_RESTART_ENUM_UNKNOWN+1; i < PK_RESTART_ENUM_LAST; i++) {
		string = gpk_restart_enum_to_localised_text_future (i);
		if (string == NULL) {
			g_warning ("failed to get %i", i);
			break;
		}
	}

	/* check we convert all the localized group enums */
	for (i = 0; i < PK_GROUP_ENUM_LAST; i++) {
		string = gpk_group_enum_to_localised_text (i);
		if (string == NULL) {
			g_warning ("failed to get %i", i);
			break;
		}
	}

	/* check we convert all the group icon name enums */
	for (i = PK_GROUP_ENUM_UNKNOWN+1; i < PK_GROUP_ENUM_LAST; i++) {
		string = gpk_group_enum_to_icon_name (i);
		if (string == NULL || g_strcmp0 (string, "help-browser") == 0) {
			g_warning ("failed to get %s", pk_group_enum_to_string (i));
			break;
		}
	}

}

static void
gpk_test_common_func (void)
{
	gchar *text;

	/* package id pretty valid package id, no summary */
	text = gpk_package_id_format_twoline (NULL, "simon;0.0.1;i386;data", NULL);
	g_assert_cmpstr (text, ==, "simon-0.0.1 (32-bit)");
	g_free (text);

	/* package id pretty valid package id, no summary 2 */
	text = gpk_package_id_format_twoline (NULL, "simon;0.0.1;;data", NULL);
	g_assert_cmpstr (text, ==, "simon-0.0.1");
	g_free (text);

	/* package id pretty valid package id, no summary 3 */
	text = gpk_package_id_format_twoline (NULL, "simon;;;data", NULL);
	g_assert_cmpstr (text, ==, "simon");
	g_free (text);

	/* package id pretty valid package id, no summary 4 */
	text = gpk_package_id_format_twoline (NULL, "simon;0.0.1;;data", "dude");
	g_assert_cmpstr (text, ==, "dude\n<span color=\"gray\">simon-0.0.1</span>");
	g_free (text);
}

int
main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/gnome-packagekit/enum", gpk_test_enum_func);
	g_test_add_func ("/gnome-packagekit/common", gpk_test_common_func);

	return g_test_run ();
}
