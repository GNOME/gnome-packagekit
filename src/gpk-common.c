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
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <gtk/gtk.h>
#include <dbus/dbus-glib.h>
#include <polkit-gnome/polkit-gnome.h>
#include <packagekit-glib/packagekit.h>
#include <locale.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-console-kit.h"

#include "gpk-enum.h"
#include "gpk-common.h"
#include "gpk-error.h"

/**
 * gpk_window_set_size_request:
 **/
gboolean
gpk_window_set_size_request (GtkWindow *window, guint width, guint height)
{
	GdkScreen *screen;
	guint screen_w;
	guint screen_h;

	/* check for tiny screen, like for instance a OLPC or EEE */
	screen = gdk_screen_get_default ();
	screen_w = gdk_screen_get_width (screen);
	screen_h = gdk_screen_get_height (screen);
	if (screen_w < width || screen_h < height) {
		egg_error ("using small form factor mode as %ix%i and requested %ix%i",
			   screen_w, screen_h, width, height);
		gtk_window_maximize (window);
		return FALSE;
	}

	/* normal size laptop panel */
	egg_debug ("using native mode: %ix%i", width, height);
	gtk_widget_set_size_request (GTK_WIDGET(window), width, height);
	return TRUE;
}

/**
 * gpk_package_id_format_twoline:
 *
 * Return value: "<b>GTK Toolkit</b>\ngtk2-2.12.2 (i386)"
 **/
gchar *
gpk_package_id_format_twoline (const PkPackageId *id, const gchar *summary)
{
	gchar *summary_safe;
	gchar *text;
	GString *string;

	g_return_val_if_fail (id != NULL, NULL);

	/* optional */
	if (egg_strzero (summary)) {
		string = g_string_new (id->name);
	} else {
		string = g_string_new ("");
		summary_safe = g_markup_escape_text (summary, -1);
		g_string_append_printf (string, "<b>%s</b>\n%s", summary_safe, id->name);
		g_free (summary_safe);
	}

	/* some backends don't provide this */
	if (id->version != NULL)
		g_string_append_printf (string, "-%s", id->version);
	if (id->arch != NULL)
		g_string_append_printf (string, " (%s)", id->arch);

	text = g_string_free (string, FALSE);
	return text;
}

/**
 * gpk_package_id_format_oneline:
 *
 * Return value: "<b>GTK Toolkit</b> (gtk2)"
 **/
gchar *
gpk_package_id_format_oneline (const PkPackageId *id, const gchar *summary)
{
	gchar *summary_safe;
	gchar *text;

	if (egg_strzero (summary)) {
		/* just have name */
		text = g_strdup (id->name);
	} else {
		summary_safe = g_markup_escape_text (summary, -1);
		text = g_strdup_printf ("<b>%s</b> (%s)", summary_safe, id->name);
		g_free (summary_safe);
	}
	return text;
}

/**
 * gpk_package_id_name_version:
 **/
gchar *
gpk_package_id_name_version (const PkPackageId *id)
{
	gchar *text;
	GString *string;

	if (id == NULL) {
		return g_strdup("");
	}

	string = g_string_new (id->name);
	if (id->version != NULL)
		g_string_append_printf (string, "-%s", id->version);
	text = g_string_free (string, FALSE);

	return text;
}

/**
 * pk_package_id_get_name:
 **/
gchar *
gpk_package_get_name (const gchar *package_id)
{
	gchar *package = NULL;
	PkPackageId *id;

	/* pk_package_id_new_from_string can't accept NULL */
	if (package_id == NULL)
		return NULL;
	id = pk_package_id_new_from_string (package_id);
	if (id == NULL)
		package = g_strdup (package_id);
	else
		package = g_strdup (id->name);
	pk_package_id_free (id);
	return package;
}

/**
 * gpk_check_privileged_user
 **/
gboolean
gpk_check_privileged_user (const gchar *application_name, gboolean show_ui)
{
	EggConsoleKit *ck = NULL;
	guint uid;
	gboolean ret = FALSE;
	gchar *message;
	gchar *title;
	GtkResponseType result;
	GtkWidget *dialog;

	uid = getuid ();
	if (uid == 0) {
		if (!show_ui)
			goto out;
		if (application_name == NULL)
			/* TRANSLATORS: these tools cannot run as root (unknown name) */
			title = g_strdup (_("This application is running as a privileged user"));
		else
			/* TRANSLATORS: cannot run as root user, and we display the applicaiton name */
			title = g_strdup_printf (_("%s is running as a privileged user"), application_name);
		message = g_strjoin ("\n",
				     /* TRANSLATORS: tell the user off */
				     _("Package management applications are security sensitive."),
				     /* TRANSLATORS: and explain why */
				     _("Running graphical applications as a privileged user should be avoided for security reasons."), NULL);

		/* give the user a choice */
		dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
						 GTK_MESSAGE_WARNING, GTK_BUTTONS_CANCEL, "%s", title);
		/* TRANSLATORS: button: allow the user to run this, even when insecure */
		gtk_dialog_add_button (GTK_DIALOG(dialog), _("Continue Anyway"), GTK_RESPONSE_OK);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog), "%s", message);
		gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_INSTALLER);
		result = gtk_dialog_run (GTK_DIALOG(dialog));
		gtk_widget_destroy (dialog);

		g_free (title);
		g_free (message);

		/* user did not agree to run insecure */
		if (result != GTK_RESPONSE_OK) {
			egg_warning ("uid=%i so closing", uid);
			goto out;
		}
	}

	/* talk to ConsoleKit */
	ck = egg_console_kit_new ();

	/* we are not local */
	ret = egg_console_kit_is_local (ck);
	if (!ret) {
		if (!show_ui)
			goto out;
		if (application_name == NULL)
			/* TRANSLATORS: the user is not sitting in front of the keyboard */
			title = g_strdup (_("This application is running when the session is not local"));
		else
			/* TRANSLATORS: same, but we know the application name */
			title = g_strdup_printf (_("%s is running when the session is not local"), application_name);
		message = g_strjoin ("\n",
				     /* TRANSLATORS: tell the user off */
				     _("These applications should be run only when on local console."),
				     /* TRANSLATORS: explain what to do */
				     _("This normally indicates a bug with ConsoleKit or with the way your session has started."), NULL);
		gpk_error_dialog (title, message, "");
		g_free (title);
		g_free (message);
		egg_warning ("not LOCAL so closing");
		goto out;
	}

	/* we are not active */
	ret = egg_console_kit_is_active (ck);
	if (!ret) {
		if (!show_ui)
			goto out;
		if (application_name == NULL)
			/* TRANSLATORS: the user is not active, i.e. is idle */
			title = g_strdup (_("This application is running when the session is not active"));
		else
			/* TRANSLATORS: same, but we know the application name */
			title = g_strdup_printf (_("%s is running when the session is not active"), application_name);
		message = g_strjoin ("\n",
				     /* TRANSLATORS: tell the user off */
				     _("These applications should be run only when on active console."),
				     /* TRANSLATORS: explain what to do */
				     _("This normally indicates a bug with your remote desktop implementation."), NULL);
		gpk_error_dialog (title, message, "");
		g_free (title);
		g_free (message);
		egg_warning ("not ACTIVE so closing");
		goto out;
	}
out:
	if (ck != NULL)
		g_object_unref (ck);
	return ret;
}

/**
 * gpk_desktop_check_icon_valid:
 *
 * Check icon actually exists and is valid in this theme
 **/
gboolean
gpk_desktop_check_icon_valid (const gchar *icon)
{
	GtkIconInfo *icon_info;
	GtkIconTheme *icon_theme = NULL;
	gboolean ret = TRUE;

	/* trivial case */
	if (egg_strzero (icon))
		return FALSE;

	/* no unref required */
	icon_theme = gtk_icon_theme_get_default ();

	/* default to 32x32 */
	icon_info = gtk_icon_theme_lookup_icon (icon_theme, icon, 32, GTK_ICON_LOOKUP_USE_BUILTIN);
	if (icon_info == NULL) {
		egg_debug ("ignoring broken icon %s", icon);
		ret = FALSE;
	} else {
		/* we only used this to see if it was valid */
		gtk_icon_info_free (icon_info);
	}
	return ret;
}



/**
 * gpk_desktop_get_file_weight:
 **/
gint
gpk_desktop_get_file_weight (const gchar *filename)
{
	GKeyFile *file;
	gboolean ret;
	gchar *value;
	gint weight = 0;
	const gchar *locale;

	locale = setlocale (LC_ALL, NULL);
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, filename, G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
	if (!ret) {
		egg_debug ("failed to open %s", filename);
		weight = G_MININT;
		goto out;
	}

	/* application */
	value = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_TYPE, NULL);
	if (egg_strequal (value, G_KEY_FILE_DESKTOP_TYPE_APPLICATION))
		weight += 10;
	g_free (value);

	/* icon */
	value = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_ICON, NULL);
	if (value != NULL && gpk_desktop_check_icon_valid (value))
		weight += 50;
	g_free (value);

	/* hidden */
	value = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL);
	if (value != NULL)
		weight -= 100;
	g_free (value);

	value = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY, NULL);
	if (egg_strequal (value, "true"))
		weight -= 100;
	g_free (value);

	/* hidden */
	value = g_key_file_get_locale_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NAME, locale, NULL);
	if (value != NULL)
		weight += 30;
	g_free (value);

out:
	g_key_file_free (file);
	return weight;
}

/**
 * gpk_desktop_guess_best_file:
 **/
gchar *
gpk_desktop_guess_best_file (PkDesktop *desktop, const gchar *package)
{
	GPtrArray *array;
	const gchar *filename;
	gchar *best_file = NULL;
	guint i;
	guint max = G_MININT;
	guint max_index = 0;
	gint weight;

	array = pk_desktop_get_files_for_package (desktop, package, NULL);
	if (array == NULL)
		goto out;
	if (array->len == 0)
		goto out;

	/* go through each option, and weight each one */
	for (i=0; i<array->len; i++) {
		filename = g_ptr_array_index (array, i);
		weight = gpk_desktop_get_file_weight (filename);
		egg_debug ("file %s has weight %i", filename, weight);
		if (weight > max) {
			max = weight;
			max_index = i;
		}
	}

	/* we've got a best */
	best_file = g_strdup (g_ptr_array_index (array, max_index));
	egg_debug ("using %s", best_file);
out:
	if (array != NULL) {
		g_ptr_array_foreach (array, (GFunc) g_free, NULL);
		g_ptr_array_free (array, TRUE);
	}
	return best_file;
}

/**
 * gpk_desktop_guess_icon_name:
 **/
gchar *
gpk_desktop_guess_icon_name (PkDesktop *desktop, const gchar *package)
{
	GKeyFile *file;
	gchar *filename;
	gchar *data = NULL;
	gboolean ret;

	filename = gpk_desktop_guess_best_file (desktop, package);
	if (filename == NULL)
		goto out;

	/* get data from file */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, filename, G_KEY_FILE_NONE, NULL);
	if (!ret) {
		egg_warning ("failed to open %s", filename);
		goto out;
	}
	data = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_ICON, NULL);
	g_key_file_free (file);

	/* one final check */
	if (data != NULL && !gpk_desktop_check_icon_valid (data)) {
		g_free (data);
		data = NULL;
	}
out:
	g_free (filename);
	return data;
}

/**
 * gpk_desktop_guess_localised_name:
 **/
gchar *
gpk_desktop_guess_localised_name (PkDesktop *desktop, const gchar *package)
{
	GKeyFile *file;
	gchar *filename;
	gchar *data = NULL;
	gboolean ret;

	filename = gpk_desktop_guess_best_file (desktop, package);
	if (filename == NULL)
		goto out;

	/* get data from file */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, filename, G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
	if (!ret) {
		egg_warning ("failed to open %s", filename);
		goto out;
	}
	data = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NAME, NULL);
	g_key_file_free (file);
out:
	g_free (filename);
	return data;
}

/**
 * gpk_set_animated_icon_from_status:
 **/
gboolean
gpk_set_animated_icon_from_status (GpkAnimatedIcon *icon, PkStatusEnum status, GtkIconSize size)
{
	const gchar *name = NULL;
	guint delay = 0;

	/* see if there is an animation */
	name = gpk_status_enum_to_animation (status);

	/* get the timing */
	if (g_str_has_prefix (name, "pk-action-"))
		delay = 150;
	else if (g_str_has_prefix (name, "process-working"))
		delay = 50;

	/* animate or set static */
	if (delay != 0) {
		gpk_animated_icon_set_frame_delay (icon, delay);
		gpk_animated_icon_set_filename_tile (icon, size, name);
		gpk_animated_icon_enable_animation (icon, TRUE);
	} else {
		gpk_animated_icon_enable_animation (icon, FALSE);
		gtk_image_set_from_icon_name (GTK_IMAGE (icon), name, size);
	}

	/* stop spinning */
	if (status == PK_STATUS_ENUM_FINISHED)
		gpk_animated_icon_enable_animation (icon, FALSE);
	return TRUE;
}

/**
 * gpk_time_to_localised_string:
 * @time_secs: The time value to convert in seconds
 *
 * Returns a localised timestring
 *
 * Return value: The time string, e.g. "2 hours 3 minutes"
 **/
gchar *
gpk_time_to_localised_string (guint time_secs)
{
	gchar* timestring = NULL;
	guint hours;
	guint minutes;
	guint seconds;

	/* is valid? */
	if (time_secs == 0) {
		/* TRANSLATORS: The actions has just literally happened */
		timestring = g_strdup_printf (_("Now"));
		return timestring;
	}

	/* make local copy */
	seconds = time_secs;

	/* less than a minute */
	if (seconds < 60) {
		/* TRANSLATORS: time */
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
		/* TRANSLATORS: time */
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

/**
 * gpk_strv_join_locale:
 *
 * Return value: "dave" or "dave and john" or "dave, john and alice",
 * or %NULL for no match
 **/
gchar *
gpk_strv_join_locale (gchar **array)
{
	guint length;

	/* trivial case */
	length = g_strv_length (array);
	if (length == 0)
		return g_strdup ("none");

	/* try and get a print format */
	if (length == 1)
		return g_strdup (array[0]);
	else if (length == 2)
		return g_strdup_printf (_("%s and %s"),
					array[0], array[1]);
	else if (length == 3)
		return g_strdup_printf (_("%s, %s and %s"),
					array[0], array[1], array[2]);
	else if (length == 4)
		return g_strdup_printf (_("%s, %s, %s and %s"),
					array[0], array[1],
					array[2], array[3]);
	else if (length == 5)
		return g_strdup_printf (_("%s, %s, %s, %s and %s"),
					array[0], array[1], array[2],
					array[3], array[4]);
	return NULL;
}

/**
 * gpk_package_entry_completion_model_new:
 *
 * Creates a tree model containing completions from the system package list
 **/
static GtkTreeModel *
gpk_package_entry_completion_model_new (void)
{
	PkPackageList *list;
	guint i;
	guint length;
	gboolean ret;
	const PkPackageObj *obj;
	GHashTable *hash;
	gpointer data;
	GtkListStore *store;
	GtkTreeIter iter;

	store = gtk_list_store_new (1, G_TYPE_STRING);
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	list = pk_package_list_new ();
	ret = pk_obj_list_from_file (PK_OBJ_LIST(list), PK_SYSTEM_PACKAGE_LIST_FILENAME);
	if (!ret) {
		egg_warning ("no package list, try refreshing");
		return NULL;
	}

	length = pk_package_list_get_size (list);
	egg_debug ("loading %i autocomplete items", length);
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		if (obj == NULL || obj->id == NULL || obj->id->name == NULL) {
			egg_warning ("obj invalid!");
			break;
		}
		data = g_hash_table_lookup (hash, (gpointer) obj->id->name);
		if (data == NULL) {
			/* append just the name */
			g_hash_table_insert (hash, g_strdup (obj->id->name), GINT_TO_POINTER (1));
			gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter, 0, obj->id->name, -1);
		}
	}
	g_hash_table_unref (hash);
	g_object_unref (list);

	return GTK_TREE_MODEL (store);
}

/**
 * gpk_package_entry_completion_new:
 *
 * Creates a %GtkEntryCompletion containing completions from the system package list
 **/
GtkEntryCompletion *
gpk_package_entry_completion_new (void)
{
	GtkEntryCompletion *completion;
	GtkTreeModel *model;

	/* create a tree model and use it as the completion model */
	completion = gtk_entry_completion_new ();
	model = gpk_package_entry_completion_model_new ();
	if (model == NULL)
		return completion;

	/* set the model for our completion model */
	gtk_entry_completion_set_model (completion, model);
	g_object_unref (model);

	/* use model column 0 as the text column */
	gtk_entry_completion_set_text_column (completion, 0);
	gtk_entry_completion_set_inline_completion (completion, TRUE);

	return completion;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpk_common_test (gpointer data)
{
	gchar *text;
	PkPackageId *id;
	EggTest *test = (EggTest *) data;

	if (!egg_test_start (test, "GpkCommon"))
		return;

	/************************************************************
	 ****************        time text             **************
	 ************************************************************/
	egg_test_title (test, "time zero");
	text = gpk_time_to_localised_string (0);
	if (text != NULL && strcmp (text, "Now") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 1s");
	text = gpk_time_to_localised_string (1);
	if (text != NULL && strcmp (text, "1 second") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 1m");
	text = gpk_time_to_localised_string (1*60);
	if (text != NULL && strcmp (text, "1 minute") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 1h");
	text = gpk_time_to_localised_string (1*60*60);
	if (text != NULL && strcmp (text, "1 hour") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 30s");
	text = gpk_time_to_localised_string (30);
	if (text != NULL && strcmp (text, "30 seconds") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 30m");
	text = gpk_time_to_localised_string (30*60);
	if (text != NULL && strcmp (text, "30 minutes") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 30m1s");
	text = gpk_time_to_localised_string (30*60+1);
	if (text != NULL && strcmp (text, "30 minutes 1 second") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 30m10s");
	text = gpk_time_to_localised_string (30*60+10);
	if (text != NULL && strcmp (text, "30 minutes 10 seconds") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************
	 ****************     package name text        **************
	 ************************************************************/
	egg_test_title (test, "get name null");
	text = gpk_package_get_name (NULL);
	if (text == NULL) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}

	/************************************************************/
	egg_test_title (test, "get name not id");
	text = gpk_package_get_name ("ania");
	if (text != NULL && strcmp (text, "ania") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	egg_test_title (test, "get name just id");
	text = gpk_package_get_name ("simon;1.0.0;i386;moo");
	if (text != NULL && strcmp (text, "simon") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************
	 ****************     package name text        **************
	 ************************************************************/
	egg_test_title (test, "package id pretty valid package id, no summary");
	id = pk_package_id_new_from_string ("simon;0.0.1;i386;data");
	text = gpk_package_id_format_twoline (id, NULL);
	pk_package_id_free (id);
	if (text != NULL && strcmp (text, "simon-0.0.1 (i386)") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	egg_test_title (test, "package id pretty valid package id, no summary 2");
	id = pk_package_id_new_from_string ("simon;0.0.1;;data");
	text = gpk_package_id_format_twoline (id, NULL);
	pk_package_id_free (id);
	if (text != NULL && strcmp (text, "simon-0.0.1") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	egg_test_title (test, "package id pretty valid package id, no summary 3");
	id = pk_package_id_new_from_string ("simon;;;data");
	text = gpk_package_id_format_twoline (id, NULL);
	pk_package_id_free (id);
	if (text != NULL && strcmp (text, "simon") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	/************************************************************/
	egg_test_title (test, "package id pretty valid package id, no summary 4");
	id = pk_package_id_new_from_string ("simon;0.0.1;;data");
	text = gpk_package_id_format_twoline (id, "dude");
	pk_package_id_free (id);
	if (text != NULL && strcmp (text, "<b>dude</b>\nsimon-0.0.1") == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed, got %s", text);
	}
	g_free (text);

	egg_test_success (test, NULL);

	egg_test_end (test);
}
#endif

