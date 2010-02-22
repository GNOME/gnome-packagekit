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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <packagekit-glib2/packagekit.h>
#include <locale.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-console-kit.h"

#include "gpk-enum.h"
#include "gpk-common.h"
#include "gpk-error.h"

#define GNOME_SESSION_MANAGER_NAME		"org.gnome.SessionManager"
#define GNOME_SESSION_MANAGER_PATH		"/org/gnome/SessionManager"
#define GNOME_SESSION_MANAGER_INTERFACE		"org.gnome.SessionManager"

/* if the dialog is going to cover more than this much of the screen, then maximise it at startup */
#define GPK_SMALL_FORM_FACTOR_SCREEN_PERCENT	75 /* % */

/* static, so local to process */
static gboolean small_form_factor_mode = FALSE;

gchar **
pk_package_array_to_strv (GPtrArray *array)
{
	PkPackage *item;
	gchar **results;
	guint i;

	results = g_new0 (gchar *, array->len+1);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		results[i] = g_strdup (pk_package_get_id (item));
	}
	return results;
}

/**
 * pk_strv_to_ptr_array:
 * @array: the gchar** array of strings
 *
 * Form a GPtrArray array of strings.
 * The data in the array is copied.
 *
 * Return value: the string array, or %NULL if invalid
 **/
GPtrArray *
pk_strv_to_ptr_array (gchar **array)
{
	guint i;
	guint length;
	GPtrArray *parray;

	g_return_val_if_fail (array != NULL, NULL);

	parray = g_ptr_array_new ();
	length = g_strv_length (array);
	for (i=0; i<length; i++)
		g_ptr_array_add (parray, g_strdup (array[i]));
	return parray;
}

/**
 * gtk_text_buffer_insert_markup:
 * @buffer: a #GtkTextBuffer
 * @markup: nul-terminated UTF-8 text with pango markup to insert
 **/
void
gtk_text_buffer_insert_markup (GtkTextBuffer *buffer, GtkTextIter *iter, const gchar *markup)
{
	PangoAttrIterator *paiter;
	PangoAttrList *attrlist;
	GtkTextMark *mark;
	GError *error = NULL;
	gchar *text;

	g_return_if_fail (GTK_IS_TEXT_BUFFER (buffer));
	g_return_if_fail (markup != NULL);

	if (*markup == '\000')
		return;

	/* invalid */
	if (!pango_parse_markup (markup, -1, 0, &attrlist, &text, NULL, &error)) {
		g_warning ("Invalid markup string: %s", error->message);
		g_error_free (error);
		return;
	}

	/* trivial, no markup */
	if (attrlist == NULL) {
		gtk_text_buffer_insert (buffer, iter, text, -1);
		g_free (text);
		return;
	}

	/* create mark with right gravity */
	mark = gtk_text_buffer_create_mark (buffer, NULL, iter, FALSE);
	paiter = pango_attr_list_get_iterator (attrlist);

	do {
		PangoAttribute *attr;
		GtkTextTag *tag;
		GtkTextTag *tag_para;
		gint start, end;

		pango_attr_iterator_range (paiter, &start, &end);

		if (end == G_MAXINT)	/* last chunk */
			end = start-1; /* resulting in -1 to be passed to _insert */

		tag = gtk_text_tag_new (NULL);

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_LANGUAGE)))
			g_object_set (tag, "language", pango_language_to_string ( ( (PangoAttrLanguage*)attr)->value), NULL);

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_FAMILY)))
			g_object_set (tag, "family", ( (PangoAttrString*)attr)->value, NULL);

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_STYLE)))
			g_object_set (tag, "style", ( (PangoAttrInt*)attr)->value, NULL);

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_WEIGHT)))
			g_object_set (tag, "weight", ( (PangoAttrInt*)attr)->value, NULL);

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_VARIANT)))
			g_object_set (tag, "variant", ( (PangoAttrInt*)attr)->value, NULL);

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_STRETCH)))
			g_object_set (tag, "stretch", ( (PangoAttrInt*)attr)->value, NULL);

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_SIZE)))
			g_object_set (tag, "size", ( (PangoAttrInt*)attr)->value, NULL);

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_FONT_DESC)))
			g_object_set (tag, "font-desc", ( (PangoAttrFontDesc*)attr)->desc, NULL);

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_FOREGROUND))) {
			GdkColor col = { 0,
					( (PangoAttrColor*)attr)->color.red,
					( (PangoAttrColor*)attr)->color.green,
					( (PangoAttrColor*)attr)->color.blue
					};

			g_object_set (tag, "foreground-gdk", &col, NULL);
		}

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_BACKGROUND))) {
			GdkColor col = { 0,
					( (PangoAttrColor*)attr)->color.red,
					( (PangoAttrColor*)attr)->color.green,
					( (PangoAttrColor*)attr)->color.blue
					};

			g_object_set (tag, "background-gdk", &col, NULL);
		}

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_UNDERLINE)))
			g_object_set (tag, "underline", ( (PangoAttrInt*)attr)->value, NULL);

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_STRIKETHROUGH)))
			g_object_set (tag, "strikethrough", (gboolean) ( ( (PangoAttrInt*)attr)->value != 0), NULL);

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_RISE)))
			g_object_set (tag, "rise", ( (PangoAttrInt*)attr)->value, NULL);

		if ( (attr = pango_attr_iterator_get (paiter, PANGO_ATTR_SCALE)))
			g_object_set (tag, "scale", ( (PangoAttrFloat*)attr)->value, NULL);

		gtk_text_tag_table_add (gtk_text_buffer_get_tag_table (buffer), tag);

		tag_para = gtk_text_tag_table_lookup (gtk_text_buffer_get_tag_table (buffer), "para");
		gtk_text_buffer_insert_with_tags (buffer, iter, text+start, end - start, tag, tag_para, NULL);

		/* mark had right gravity, so it should be
		 *	at the end of the inserted text now */
		gtk_text_buffer_get_iter_at_mark (buffer, iter, mark);
	} while (pango_attr_iterator_next (paiter));

	gtk_text_buffer_delete_mark (buffer, mark);
	pango_attr_iterator_destroy (paiter);
	pango_attr_list_unref (attrlist);
	g_free (text);
}

/**
 * gpk_window_set_size_request:
 **/
gboolean
gpk_window_set_size_request (GtkWindow *window, guint width, guint height)
{
#ifdef PK_BUILD_SMALL_FORM_FACTOR
	GdkScreen *screen;
	guint screen_w;
	guint screen_h;
	guint percent_w;
	guint percent_h;

	/* check for tiny screen, like for instance a OLPC or EEE */
	screen = gdk_screen_get_default ();
	screen_w = gdk_screen_get_width (screen);
	screen_h = gdk_screen_get_height (screen);

	/* find percentage of screen area */
	percent_w = (width * 100) / screen_w;
	percent_h = (height * 100) / screen_h;
	egg_debug ("window coverage x:%i%% y:%i%%", percent_w, percent_h);

	if (percent_w > GPK_SMALL_FORM_FACTOR_SCREEN_PERCENT ||
	    percent_h > GPK_SMALL_FORM_FACTOR_SCREEN_PERCENT) {
		egg_debug ("using small form factor mode as %ix%i and requested %ix%i",
			   screen_w, screen_h, width, height);
		gtk_window_maximize (window);
		small_form_factor_mode = TRUE;
		goto out;
	}
#else
	/* skip invalid values */
	if (width == 0 || height == 0)
		goto out;
#endif
	/* normal size laptop panel */
	egg_debug ("using native mode: %ix%i", width, height);
	gtk_window_set_default_size (window, width, height);
	small_form_factor_mode = FALSE;
out:
	return !small_form_factor_mode;
}

/**
 * gpk_window_set_parent_xid:
 **/
gboolean
gpk_window_set_parent_xid (GtkWindow *window, guint32 xid)
{
	GdkDisplay *display;
	GdkWindow *parent_window;
	GdkWindow *our_window;

	g_return_val_if_fail (xid != 0, FALSE);

	display = gdk_display_get_default ();
	parent_window = gdk_window_foreign_new_for_display (display, xid);
	our_window = gtk_widget_get_window (GTK_WIDGET (window));

	/* set this above our parent */
	gtk_window_set_modal (window, TRUE);
	gdk_window_set_transient_for (our_window, parent_window);
	return TRUE;
}


/**
 * gpk_package_id_format_twoline:
 *
 * Return value: "<b>GTK Toolkit</b>\ngtk2-2.12.2 (i386)"
 **/
gchar *
gpk_package_id_format_twoline (const gchar *package_id, const gchar *summary)
{
	gchar *summary_safe;
	gchar *text = NULL;
	GString *string;
	gchar **split = NULL;

	g_return_val_if_fail (package_id != NULL, NULL);

	/* optional */
	split = pk_package_id_split (package_id);
	if (split == NULL) {
		egg_warning ("could not parse %s", package_id);
		goto out;
	}
	if (summary == NULL || summary[PK_PACKAGE_ID_NAME] == '\0') {
		string = g_string_new (split[PK_PACKAGE_ID_NAME]);
	} else {
		string = g_string_new ("");
		summary_safe = g_markup_escape_text (summary, -1);
		g_string_append_printf (string, "<b>%s</b>\n%s", summary_safe, split[PK_PACKAGE_ID_NAME]);
		g_free (summary_safe);
	}

	/* some backends don't provide this */
	if (split[PK_PACKAGE_ID_VERSION][0] != '\0')
		g_string_append_printf (string, "-%s", split[PK_PACKAGE_ID_VERSION]);
	if (split[PK_PACKAGE_ID_ARCH][0] != '\0')
		g_string_append_printf (string, " (%s)", split[PK_PACKAGE_ID_ARCH]);

	text = g_string_free (string, FALSE);
out:
	g_strfreev (split);
	return text;
}

/**
 * gpk_package_id_format_oneline:
 *
 * Return value: "<b>GTK Toolkit</b> (gtk2)"
 **/
gchar *
gpk_package_id_format_oneline (const gchar *package_id, const gchar *summary)
{
	gchar *summary_safe;
	gchar *text;
	gchar **split;

	g_return_val_if_fail (package_id != NULL, NULL);

	split = pk_package_id_split (package_id);
	if (summary == NULL || summary[0] == '\0') {
		/* just have name */
		text = g_strdup (split[PK_PACKAGE_ID_NAME]);
	} else {
		summary_safe = g_markup_escape_text (summary, -1);
		text = g_strdup_printf ("<b>%s</b> (%s)", summary_safe, split[PK_PACKAGE_ID_NAME]);
		g_free (summary_safe);
	}
	g_strfreev (split);
	return text;
}

/**
 * gpk_check_privileged_user
 **/
gboolean
gpk_check_privileged_user (const gchar *application_name, gboolean show_ui)
{
	guint uid;
	gboolean ret = TRUE;
	gchar *message = NULL;
	gchar *title = NULL;
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
		gtk_dialog_add_button (GTK_DIALOG(dialog), _("Continue _Anyway"), GTK_RESPONSE_OK);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog), "%s", message);
		gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_INSTALLER);
		result = gtk_dialog_run (GTK_DIALOG(dialog));
		gtk_widget_destroy (dialog);

		/* user did not agree to run insecure */
		if (result != GTK_RESPONSE_OK) {
			ret = FALSE;
			egg_warning ("uid=%i so closing", uid);
			goto out;
		}
	}
out:
	g_free (title);
	g_free (message);
	return ret;
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
	} else {
		gpk_animated_icon_set_icon_name (icon, size, name);
	}

	/* stop spinning */
	if (status == PK_STATUS_ENUM_FINISHED)
		gpk_animated_icon_enable_animation (icon, FALSE);
	return TRUE;
}

/**
 * gpk_time_to_imprecise_string:
 * @time_secs: The time value to convert in seconds
 *
 * Returns a localised timestring
 *
 * Return value: The time string, e.g. "2 hours"
 **/
gchar *
gpk_time_to_imprecise_string (guint time_secs)
{
	gchar* timestring = NULL;
	guint hours;
	guint minutes;
	guint seconds;

	/* is valid? */
	if (time_secs == 0) {
		/* TRANSLATORS: The actions has just literally happened */
		timestring = g_strdup_printf (_("Now"));
		goto out;
	}

	/* make local copy */
	seconds = time_secs;

	/* less than a minute */
	if (seconds < 60) {
		/* TRANSLATORS: time */
		timestring = g_strdup_printf (ngettext ("%i second", "%i seconds", seconds), seconds);
		goto out;
	}

	/* Add 0.5 to do rounding */
	minutes = (guint) ((time_secs / 60.0 ) + 0.5);

	/* less than an hour */
	if (minutes < 60) {
		/* TRANSLATORS: time */
		timestring = g_strdup_printf (ngettext ("%i minute", "%i minutes", minutes), minutes);
		goto out;
	}

	hours = minutes / 60;
	/* TRANSLATORS: time */
	timestring = g_strdup_printf (ngettext ("%i hour", "%i hours", hours), hours);
out:
	return timestring;
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
		goto out;
	}

	/* make local copy */
	seconds = time_secs;

	/* less than a minute */
	if (seconds < 60) {
		/* TRANSLATORS: time */
		timestring = g_strdup_printf (ngettext ("%i second",
							"%i seconds",
							seconds), seconds);
		goto out;
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
		goto out;
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
out:
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
 * gpk_package_entry_completion_get_names_from_file:
 *
 * Creates a tree model containing completions from the system package list
 **/
static GPtrArray *
gpk_package_entry_completion_get_names_from_file (const gchar *filename)
{
	GPtrArray *array = NULL;
	gboolean ret;
	GError *error = NULL;
	gchar *data = NULL;
	gchar **lines = NULL;
	guint i;
	gchar **split;
	PkPackage *item;

	/* get data */
	ret = g_file_get_contents (filename, &data, NULL, &error);
	if (!ret) {
		egg_warning ("failed to open package list: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* create array of PkPackage's */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* split */
	lines = g_strsplit (data, "\n", -1);
	for (i=0; lines[i] != NULL; i++) {
		split = g_strsplit (lines[i], "\t", 3);
		if (g_strv_length (split) != 3)
			continue;
		item = pk_package_new ();
		g_object_set (item,
			      "info", pk_info_enum_from_text (split[0]),
			      "package-id", split[1],
			      "summary", split[2],
			      NULL);
		g_ptr_array_add (array, item);
		g_strfreev (split);
	}
out:
	g_free (data);
	g_strfreev (lines);
	return array;
}

/**
 * gpk_package_entry_completion_model_new:
 *
 * Creates a tree model containing completions from the system package list
 **/
static GtkTreeModel *
gpk_package_entry_completion_model_new (void)
{
	GPtrArray *list;
	guint i;
	PkPackage *item;
	GHashTable *hash;
	gpointer data;
	GtkListStore *store;
	GtkTreeIter iter;
	gchar **split;

	store = gtk_list_store_new (1, G_TYPE_STRING);
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	list = gpk_package_entry_completion_get_names_from_file (PK_SYSTEM_PACKAGE_LIST_FILENAME);
	if (list == NULL) {
		egg_warning ("no package list, try refreshing");
		return NULL;
	}

	egg_debug ("loading %i autocomplete items", list->len);
	for (i=0; i<list->len; i++) {
		item = g_ptr_array_index (list, i);
		if (item == NULL || pk_package_get_id (item) == NULL) {
			egg_warning ("item invalid!");
			break;
		}

		split = pk_package_id_split (pk_package_get_id (item));
		data = g_hash_table_lookup (hash, (gpointer) split[PK_PACKAGE_ID_NAME]);
		if (data == NULL) {
			/* append just the name */
			g_hash_table_insert (hash, g_strdup (split[PK_PACKAGE_ID_NAME]), GINT_TO_POINTER (1));
			gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter, 0, split[PK_PACKAGE_ID_NAME], -1);
		}
		g_strfreev (split);
	}
	g_hash_table_unref (hash);
	g_ptr_array_unref (list);

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

/**
 * gpk_ignore_session_error:
 *
 * Returns true if the error is a remote exception where we cancelled
 **/
gboolean
gpk_ignore_session_error (GError *error)
{
	gboolean ret = FALSE;
	const gchar *name;

	if (error == NULL)
		goto out;
	if (error->domain != DBUS_GERROR)
		goto out;
	if (error->code != DBUS_GERROR_REMOTE_EXCEPTION)
		goto out;

	/* use one of our local codes */
	name = dbus_g_error_get_name (error);
	if (name == NULL)
		goto out;

	if (g_str_has_prefix (name, "org.freedesktop.PackageKit.Modify.Cancelled")) {
		ret = TRUE;
		goto out;
	}

	/* DBus-glib hates us */
	if (g_str_has_prefix (name, "Org.freedesktop.PackageKit.Modify.Cancelled")) {
		ret = TRUE;
		goto out;
	}
out:
	return ret;
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
	if (text != NULL && strcmp (text, "1 second") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 1m");
	text = gpk_time_to_localised_string (1*60);
	if (text != NULL && strcmp (text, "1 minute") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 1h");
	text = gpk_time_to_localised_string (1*60*60);
	if (text != NULL && strcmp (text, "1 hour") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 30s");
	text = gpk_time_to_localised_string (30);
	if (text != NULL && strcmp (text, "30 seconds") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 30m");
	text = gpk_time_to_localised_string (30*60);
	if (text != NULL && strcmp (text, "30 minutes") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 30m1s");
	text = gpk_time_to_localised_string (30*60+1);
	if (text != NULL && strcmp (text, "30 minutes 1 second") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "time 30m10s");
	text = gpk_time_to_localised_string (30*60+10);
	if (text != NULL && strcmp (text, "30 minutes 10 seconds") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "imprecise time 1s");
	text = gpk_time_to_imprecise_string (1);
	if (text != NULL && strcmp (text, "1 second") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "imprecise time 30m");
	text = gpk_time_to_imprecise_string (30*60);
	if (text != NULL && strcmp (text, "30 minutes") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "imprecise time 30m10s");
	text = gpk_time_to_imprecise_string (30*60+10);
	if (text != NULL && strcmp (text, "30 minutes") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************
	 ****************     package name text        **************
	 ************************************************************/
	egg_test_title (test, "package id pretty valid package id, no summary");
	text = gpk_package_id_format_twoline ("simon;0.0.1;i386;data", NULL);
	if (text != NULL && strcmp (text, "simon-0.0.1 (i386)") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "package id pretty valid package id, no summary 2");
	text = gpk_package_id_format_twoline ("simon;0.0.1;;data", NULL);
	if (text != NULL && strcmp (text, "simon-0.0.1") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "package id pretty valid package id, no summary 3");
	text = gpk_package_id_format_twoline ("simon;;;data", NULL);
	if (text != NULL && strcmp (text, "simon") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "package id pretty valid package id, no summary 4");
	text = gpk_package_id_format_twoline ("simon;0.0.1;;data", "dude");
	if (text != NULL && strcmp (text, "<b>dude</b>\nsimon-0.0.1") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed, got %s", text);
	g_free (text);

	egg_test_end (test);
}
#endif

