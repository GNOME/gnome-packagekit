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


/** ver:1.0 ***********************************************************/
static GMainLoop *_test_loop = NULL;
static guint _test_loop_timeout_id = 0;

static gboolean
_g_test_hang_wait_cb (gpointer user_data)
{
	g_main_loop_quit (_test_loop);
	_test_loop_timeout_id = 0;
	return FALSE;
}

static void
_g_test_loop_wait (guint timeout_ms)
{
	g_assert (_test_loop_timeout_id == 0);
	_test_loop = g_main_loop_new (NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add (timeout_ms, _g_test_hang_wait_cb, &timeout_ms);
	g_main_loop_run (_test_loop);
}

static void
_g_test_loop_quit (void)
{
	if (_test_loop_timeout_id > 0) {
		g_source_remove (_test_loop_timeout_id);
		_test_loop_timeout_id = 0;
	}
	if (_test_loop != NULL) {
		g_main_loop_quit (_test_loop);
		g_main_loop_unref (_test_loop);
		_test_loop = NULL;
	}
}

/**********************************************************************/

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
			g_warning ("failed to get %s", pk_info_enum_to_string (i));
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
gpk_test_error_func (void)
{
	gboolean ret;

	/* do dialog */
	ret = gpk_error_dialog ("No space is left on the disk",
				"There is insufficient space on the device.\n"
				"Free some space on the system disk to perform this operation.",
				"[Errno 28] No space left on device");
	g_assert (ret);
}

static void
gpk_test_common_func (void)
{
	gchar *text;

	/* time zero */
	text = gpk_time_to_localised_string (0);
	g_assert_cmpstr (text, ==, "Now");
	g_free (text);

	/* time 1s */
	text = gpk_time_to_localised_string (1);
	g_assert_cmpstr (text, ==, "1 second");
	g_free (text);

	/* time 1m */
	text = gpk_time_to_localised_string (1*60);
	g_assert_cmpstr (text, ==, "1 minute");
	g_free (text);

	/* time 1h */
	text = gpk_time_to_localised_string (1*60*60);
	g_assert_cmpstr (text, ==, "1 hour");
	g_free (text);

	/* time 30s */
	text = gpk_time_to_localised_string (30);
	g_assert_cmpstr (text, ==, "30 seconds");
	g_free (text);

	/* time 30m */
	text = gpk_time_to_localised_string (30*60);
	g_assert_cmpstr (text, ==, "30 minutes");
	g_free (text);

	/* time 30m1s */
	text = gpk_time_to_localised_string (30*60+1);
	g_assert_cmpstr (text, ==, "30 minutes 1 second");
	g_free (text);

	/* time 30m10s */
	text = gpk_time_to_localised_string (30*60+10);
	g_assert_cmpstr (text, ==, "30 minutes 10 seconds");
	g_free (text);

	/* imprecise time 1s */
	text = gpk_time_to_imprecise_string (1);
	g_assert_cmpstr (text, ==, "1 second");
	g_free (text);

	/* imprecise time 30m */
	text = gpk_time_to_imprecise_string (30*60);
	g_assert_cmpstr (text, ==, "30 minutes");
	g_free (text);

	/* imprecise time 30m10s */
	text = gpk_time_to_imprecise_string (30*60+10);
	g_assert_cmpstr (text, ==, "30 minutes");
	g_free (text);

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

static void
gpk_task_test_install_packages_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	GpkTask *task = GPK_TASK (object);
	g_autoptr(GError) error = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) packages = NULL;
	g_autoptr(PkError) error_code = NULL;

	/* get the results */
	results = pk_task_generic_finish (PK_TASK(task), res, &error);
	if (results == NULL) {
		g_warning ("failed to resolve: %s", error->message);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL)
		g_warning ("failed to resolve success: %s", pk_error_get_details (error_code));

	packages = pk_results_get_package_array (results);
	if (packages == NULL)
		g_warning ("no packages!");

	if (packages->len != 4)
		g_warning ("invalid number of packages: %i", packages->len);
out:
	_g_test_loop_quit ();
}

static void
gpk_task_test_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
{
	PkStatusEnum status;
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
			      "status", &status,
			      NULL);
		g_debug ("now %s", pk_status_enum_to_string (status));
	}
}

static void
gpk_test_task_func (void)
{
	g_autoptr(GpkTask) task = NULL;
	g_auto(GStrv) package_ids = NULL;

	/* get task */
	task = gpk_task_new ();
	g_assert (task);

	/* For testing, you will need to manually do:
	pkcon repo-set-data dummy use-gpg 1
	pkcon repo-set-data dummy use-eula 1
	pkcon repo-set-data dummy use-media 1
	*/

	/* install package */
	package_ids = pk_package_ids_from_id ("vips-doc;7.12.4-2.fc8;noarch;linva");
	pk_task_install_packages_async (PK_TASK(task), package_ids, NULL,
				        (PkProgressCallback) gpk_task_test_progress_cb, NULL,
				        (GAsyncReadyCallback) gpk_task_test_install_packages_cb, NULL);
	_g_test_loop_wait (150000);
}

int
main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/gnome-packagekit/enum", gpk_test_enum_func);
	g_test_add_func ("/gnome-packagekit/common", gpk_test_common_func);
	g_test_add_func ("/gnome-packagekit/markdown", gpk_test_markdown_func);
	if (g_test_thorough ()) {
		g_test_add_func ("/gnome-packagekit/error", gpk_test_error_func);
		g_test_add_func ("/gnome-packagekit/task", gpk_test_task_func);
	}

	return g_test_run ();
}
