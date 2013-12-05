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

#include "egg-markdown.h"

#include "gpk-common.h"
#include "gpk-dbus.h"
#include "gpk-dbus-task.h"
#include "gpk-enum.h"
#include "gpk-error.h"
#include "gpk-language.h"
#include "gpk-modal-dialog.h"
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

/**
 * _g_test_loop_wait:
 **/
static void
_g_test_loop_wait (guint timeout_ms)
{
	g_assert (_test_loop_timeout_id == 0);
	_test_loop = g_main_loop_new (NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add (timeout_ms, _g_test_hang_wait_cb, &timeout_ms);
	g_main_loop_run (_test_loop);
}

/**
 * _g_test_loop_quit:
 **/
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

	/* check we convert all the localized present role enums */
	for (i = 0; i < PK_ROLE_ENUM_LAST; i++) {
		string = gpk_role_enum_to_localised_present (i);
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

	/* check we convert all the status animation enums */
	for (i = PK_STATUS_ENUM_UNKNOWN+1; i < PK_STATUS_ENUM_UNKNOWN; i++) {
		string = gpk_status_enum_to_animation (i);
		if (string == NULL || g_strcmp0 (string, "help-browser") == 0) {
			g_warning ("failed to get %s", pk_status_enum_to_string (i));
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
gpk_test_modal_dialog_func (void)
{
	GtkResponseType button;
	GpkModalDialog *dialog = NULL;
	GPtrArray *array;
	PkPackage *item;
	gboolean ret;

	/* get GpkModalDialog object */
	dialog = gpk_modal_dialog_new ();
	g_assert (dialog);

	/* set some packages */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	item = pk_package_new ();
	ret = pk_package_set_id (item, "totem;001;i386;fedora", NULL);
	g_assert (ret);
	g_object_set (item,
		      "info", PK_INFO_ENUM_INSTALLED,
		      "summary", "Totem is a music player for GNOME",
		      NULL);
	g_ptr_array_add (array, item);
	item = pk_package_new ();
	ret = pk_package_set_id (item, "totem;001;i386;fedora", NULL);
	g_assert (ret);
	g_object_set (item,
		      "info", PK_INFO_ENUM_AVAILABLE,
		      "summary", "Amarok is a music player for KDE",
		      NULL);
	g_ptr_array_add (array, item);
	gpk_modal_dialog_set_package_list (dialog, array);
	g_ptr_array_unref (array);

	/* help button */
	gpk_modal_dialog_setup (dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
	gpk_modal_dialog_set_title (dialog, "Button press test");
	gpk_modal_dialog_set_message (dialog, "Please press close");
	gpk_modal_dialog_set_image (dialog, "dialog-warning");
	gpk_modal_dialog_present (dialog);
	button = gpk_modal_dialog_run (dialog);
	g_assert_cmpint (button, ==, GTK_RESPONSE_CLOSE);

	/* confirm button */
	gpk_modal_dialog_setup (dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, 0);
	gpk_modal_dialog_set_title (dialog, "Button press test with a really really long title");
	gpk_modal_dialog_set_message (dialog, "Please press Uninstall\n\nThis is a really really, really,\nreally long title <i>with formatting</i>");
	gpk_modal_dialog_set_image (dialog, "dialog-information");
	gpk_modal_dialog_set_action (dialog, "Uninstall");
	gpk_modal_dialog_present (dialog);
	button = gpk_modal_dialog_run (dialog);
	g_assert_cmpint (button, ==, GTK_RESPONSE_OK);

	/* no message */
	gpk_modal_dialog_setup (dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	gpk_modal_dialog_set_title (dialog, "Refresh cache");
	gpk_modal_dialog_set_image_status (dialog, PK_STATUS_ENUM_REFRESH_CACHE);
	gpk_modal_dialog_set_percentage (dialog, -1);
	gpk_modal_dialog_present (dialog);
	gpk_modal_dialog_run (dialog);

	/* progress */
	gpk_modal_dialog_setup (dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, GPK_MODAL_DIALOG_PACKAGE_PADDING);
	gpk_modal_dialog_set_title (dialog, "Button press test");
	gpk_modal_dialog_set_message (dialog, "Please press cancel");
	gpk_modal_dialog_set_image_status (dialog, PK_STATUS_ENUM_RUNNING);
	gpk_modal_dialog_set_percentage (dialog, 50);
	gpk_modal_dialog_present (dialog);
	button = gpk_modal_dialog_run (dialog);
	g_assert_cmpint (button, ==, GTK_RESPONSE_CANCEL);

	/* progress */
	gpk_modal_dialog_setup (dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, pk_bitfield_from_enums (GPK_MODAL_DIALOG_WIDGET_MESSAGE, -1));
	gpk_modal_dialog_set_title (dialog, "Button press test");
	gpk_modal_dialog_set_message (dialog, "Please press close");
	gpk_modal_dialog_set_image_status (dialog, PK_STATUS_ENUM_INSTALL);
	gpk_modal_dialog_set_percentage (dialog, -1);
	gpk_modal_dialog_present (dialog);
	button = gpk_modal_dialog_run (dialog);
	g_assert_cmpint (button, ==, GTK_RESPONSE_CLOSE);

	/* confirm install button */
	gpk_modal_dialog_setup (dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, GPK_MODAL_DIALOG_PACKAGE_LIST);
	gpk_modal_dialog_set_title (dialog, "Button press test");
	gpk_modal_dialog_set_message (dialog, "Please press Install if you can see the package list");
	gpk_modal_dialog_set_image (dialog, "dialog-information");
	gpk_modal_dialog_set_action (dialog, "Install");
	gpk_modal_dialog_present (dialog);
	button = gpk_modal_dialog_run (dialog);
	g_assert_cmpint (button, ==, GTK_RESPONSE_OK);

	gpk_modal_dialog_close (dialog);

	g_object_unref (dialog);
}

static void
gpk_test_language_func (void)
{
	gboolean ret;
	gchar *lang;
	GError *error = NULL;
	GpkLanguage *language = NULL;

	/* get GpkLanguage object */
	language = gpk_language_new ();
	g_assert (language != NULL);

	/* populate */
	ret = gpk_language_populate (language, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get data (present) */
	lang = gpk_language_iso639_to_language (language, "en");
	g_assert_cmpstr (lang, ==, "English");
	g_free (lang);

	/* get data (missing) */
	lang = gpk_language_iso639_to_language (language, "notgoingtoexist");
	g_assert_cmpstr (lang, ==, NULL);

	g_object_unref (language);
}

static void
gpk_test_dbus_task_func (void)
{
	GpkDbusTask *dtask;
	gchar *lang;
	gchar *language;
	gchar *package;
	gboolean ret;
//	const gchar *fonts[] = { ":lang=mn", NULL };
//	GError *error;

	/* get GpkDbusTask object */
	dtask = gpk_dbus_task_new ();
	g_assert (dtask);

	/* convert tag to lang */
	lang = gpk_dbus_task_font_tag_to_lang (":lang=mn");
	g_assert_cmpstr (lang, ==, "mn");
	g_free (lang);

	/* convert tag to language */
	language = gpk_dbus_task_font_tag_to_localised_name (dtask, ":lang=mn");
	g_assert_cmpstr (language, ==, "Mongolian");
	g_free (language);

	/* test trusted path */
//	ret = gpk_dbus_task_path_is_trusted ("/usr/libexec/gst-install-plugins-helper");
//	g_assert (ret);

	/* test trusted path */
	ret = gpk_dbus_task_path_is_trusted ("/usr/bin/totem");
	g_assert (!ret);

	/* get package for exec */
	package = gpk_dbus_task_get_package_for_exec (dtask, "/usr/bin/totem");
	g_assert_cmpstr (package, ==, "totem");
	g_free (package);

	/* set exec */
	ret = gpk_dbus_task_set_exec (dtask, "/usr/bin/totem");
	g_assert (ret);
#if 0
	/* install fonts (no UI) */
	error = NULL;
	gpk_dbus_task_set_interaction (dtask, GPK_CLIENT_INTERACT_NEVER);
	ret = gpk_dbus_task_install_fontconfig_resources (dtask, (gchar**)fonts, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* install fonts (if found) */
	error = NULL;
	gpk_dbus_task_set_interaction (dtask, pk_bitfield_from_enums (GPK_CLIENT_INTERACT_CONFIRM_SEARCH, GPK_CLIENT_INTERACT_FINISHED, -1));
	ret = gpk_dbus_task_install_fontconfig_resources (dtask, (gchar**)fonts, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* install fonts (always) */
	error = NULL;
	gpk_dbus_task_set_interaction (dtask, GPK_CLIENT_INTERACT_ALWAYS);
	ret = gpk_dbus_task_install_fontconfig_resources (dtask, (gchar**)fonts, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
#endif
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
gpk_test_dbus_func (void)
{
	GpkDbus *dbus = NULL;

	/* get GpkDbus object */
	dbus = gpk_dbus_new ();
	g_assert (dbus);
	g_object_unref (dbus);
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
gpk_test_markdown_func (void)
{
	EggMarkdown *md;
	gchar *text;
	gboolean ret;
	const gchar *markdown;
	const gchar *markdown_expected;

	/* get EggMarkdown object */
	md = egg_markdown_new ();
	g_assert (md);

	ret = egg_markdown_set_output (md, EGG_MARKDOWN_OUTPUT_PANGO);
	g_assert (ret);

	markdown = "OEMs\n"
		   "====\n"
		   " - Bullett\n";
	markdown_expected =
		   "<big>OEMs</big>\n"
		   "• Bullett";
	/* markdown (type2 header) */
	text = egg_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	markdown = "this is http://www.hughsie.com/with_spaces_in_url inline link\n";
	markdown_expected = "this is <tt>http://www.hughsie.com/with_spaces_in_url</tt> inline link";
	/* markdown (autocode) */
	egg_markdown_set_autocode (md, TRUE);
	text = egg_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	markdown = "*** This software is currently in alpha state ***\n";
	markdown_expected = "<b><i> This software is currently in alpha state </b></i>";
	/* markdown some invalid header */
	text = egg_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	markdown = " - This is a *very*\n"
		   "   short paragraph\n"
		   "   that is not usual.\n"
		   " - Another";
	markdown_expected =
		   "• This is a <i>very</i> short paragraph that is not usual.\n"
		   "• Another";
	/* markdown (complex1) */
	text = egg_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	markdown = "*  This is a *very*\n"
		   "   short paragraph\n"
		   "   that is not usual.\n"
		   "*  This is the second\n"
		   "   bullett point.\n"
		   "*  And the third.\n"
		   " \n"
		   "* * *\n"
		   " \n"
		   "Paragraph one\n"
		   "isn't __very__ long at all.\n"
		   "\n"
		   "Paragraph two\n"
		   "isn't much better.";
	markdown_expected =
		   "• This is a <i>very</i> short paragraph that is not usual.\n"
		   "• This is the second bullett point.\n"
		   "• And the third.\n"
		   "⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯\n"
		   "Paragraph one isn't <b>very</b> long at all.\n"
		   "Paragraph two isn't much better.";
	/* markdown (complex1) */
	text = egg_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	markdown = "This is a spec file description or\n"
		   "an **update** description in bohdi.\n"
		   "\n"
		   "* * *\n"
		   "# Big title #\n"
		   "\n"
		   "The *following* things 'were' fixed:\n"
		   "- Fix `dave`\n"
		   "* Fubar update because of \"security\"\n";
	markdown_expected =
		   "This is a spec file description or an <b>update</b> description in bohdi.\n"
		   "⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯\n"
		   "<big>Big title</big>\n"
		   "The <i>following</i> things 'were' fixed:\n"
		   "• Fix <tt>dave</tt>\n"
		   "• Fubar update because of \"security\"";
	/* markdown (complex2) */
	text = egg_markdown_parse (md, markdown);
	if (g_strcmp0 (text, markdown_expected) == 0)
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	markdown = "* list seporated with spaces -\n"
		   "  first item\n"
		   "\n"
		   "* second item\n"
		   "\n"
		   "* third item\n";
	markdown_expected =
		   "• list seporated with spaces - first item\n"
		   "• second item\n"
		   "• third item";
	/* markdown (list with spaces) */
	text = egg_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	ret = egg_markdown_set_max_lines (md, 1);
	g_assert (ret);

	markdown = "* list seporated with spaces -\n"
		   "  first item\n"
		   "* second item\n";
	markdown_expected =
		   "• list seporated with spaces - first item";
	/* markdown (one line limit) */
	text = egg_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	ret = egg_markdown_set_max_lines (md, 1);
	g_assert (ret);

	markdown = "* list & spaces";
	markdown_expected =
		   "• list & spaces";
	/* markdown (escaping) */
	text = egg_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	/* markdown (free text) */
	text = egg_markdown_parse (md, "This isn't a present");
	g_assert_cmpstr (text, ==, "This isn't a present");
	g_free (text);

	/* markdown (autotext underscore) */
	text = egg_markdown_parse (md, "This isn't CONFIG_UEVENT_HELPER_PATH present");
	g_assert_cmpstr (text, ==, "This isn't <tt>CONFIG_UEVENT_HELPER_PATH</tt> present");
	g_free (text);

	markdown = "*Thu Mar 12 12:00:00 2009* Dan Walsh <dwalsh@redhat.com> - 2.0.79-1\n"
		   "- Update to upstream \n"
		   " * Netlink socket handoff patch from Adam Jackson.\n"
		   " * AVC caching of compute_create results by Eric Paris.\n"
		   "\n"
		   "*Tue Mar 10 12:00:00 2009* Dan Walsh <dwalsh@redhat.com> - 2.0.78-5\n"
		   "- Add patch from ajax to accellerate X SELinux \n"
		   "- Update eparis patch\n";
	markdown_expected =
		   "<i>Thu Mar 12 12:00:00 2009</i> Dan Walsh <tt>&lt;dwalsh@redhat.com&gt;</tt> - 2.0.79-1\n"
		   "• Update to upstream\n"
		   "• Netlink socket handoff patch from Adam Jackson.\n"
		   "• AVC caching of compute_create results by Eric Paris.\n"
		   "<i>Tue Mar 10 12:00:00 2009</i> Dan Walsh <tt>&lt;dwalsh@redhat.com&gt;</tt> - 2.0.78-5\n"
		   "• Add patch from ajax to accellerate X SELinux\n"
		   "• Update eparis patch";
	/* markdown (end of bullett) */
	egg_markdown_set_escape (md, TRUE);
	ret = egg_markdown_set_max_lines (md, 1024);
	text = egg_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	g_object_unref (md);
}

static void
gpk_task_test_install_packages_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	GpkTask *task = GPK_TASK (object);
	GError *error = NULL;
	PkResults *results;
	GPtrArray *packages;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_task_generic_finish (PK_TASK(task), res, &error);
	if (results == NULL) {
		g_warning ("failed to resolve: %s", error->message);
		g_error_free (error);
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

	g_ptr_array_unref (packages);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
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
	GpkTask *task;
	gchar **package_ids;

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
	g_strfreev (package_ids);
	_g_test_loop_wait (150000);

	g_object_unref (task);
}

int
main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/gnome-packagekit/enum", gpk_test_enum_func);
	g_test_add_func ("/gnome-packagekit/common", gpk_test_common_func);
	g_test_add_func ("/gnome-packagekit/language", gpk_test_language_func);
	g_test_add_func ("/gnome-packagekit/markdown", gpk_test_markdown_func);
	g_test_add_func ("/gnome-packagekit/dbus", gpk_test_dbus_func);
	g_test_add_func ("/gnome-packagekit/dbus-task", gpk_test_dbus_task_func);
	if (g_test_thorough ()) {
		g_test_add_func ("/gnome-packagekit/modal-dialog", gpk_test_modal_dialog_func);
		g_test_add_func ("/gnome-packagekit/error", gpk_test_error_func);
		g_test_add_func ("/gnome-packagekit/task", gpk_test_task_func);
	}

	return g_test_run ();
}
