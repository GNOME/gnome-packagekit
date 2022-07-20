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
#include <gdk/gdkx.h>
#include <packagekit-glib2/packagekit.h>
#include <locale.h>

#include "gpk-enum.h"
#include "gpk-common.h"
#include "gpk-error.h"

#define GNOME_SESSION_MANAGER_NAME		"org.gnome.SessionManager"
#define GNOME_SESSION_MANAGER_PATH		"/org/gnome/SessionManager"
#define GNOME_SESSION_MANAGER_INTERFACE		"org.gnome.SessionManager"

/* if the dialog is going to cover more than this much of the screen, then maximize it at startup */
#define GPK_SMALL_FORM_FACTOR_SCREEN_PERCENT	75 /* % */

/* static, so local to process */
static gboolean small_form_factor_mode = FALSE;

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
	for (i = 0; i < length; i++)
		g_ptr_array_add (parray, g_strdup (array[i]));
	return parray;
}

gboolean
gpk_window_set_size_request (GtkWindow *window, gint width, gint height)
{
#ifdef PK_BUILD_SMALL_FORM_FACTOR
	GdkDisplay *display;
	GdkMonitor *monitor;
	GdkRectangle workarea;
	guint win_w;
	guint win_h;
	gdouble percent_scr;

	/* check for tiny screen, like for instance a OLPC or EEE */
	display = gdk_display_get_default ();
	monitor = gdk_display_get_primary_monitor (display);
	if (monitor == NULL) {
		g_debug ("no primary monitor was found, unable to determine small screen support");
		goto out_normal_size;
	}

	/* find percentage of screen area */
	gdk_monitor_get_workarea (monitor, &workarea);
	win_w = (width > 0)? width : workarea.width;
	win_h = (height > 0)? height : workarea.height;
	percent_scr = (100.0 / (workarea.width * workarea.height)) * (win_w * win_h);

	g_debug ("window coverage %.2f%%", percent_scr);
	if (percent_scr > GPK_SMALL_FORM_FACTOR_SCREEN_PERCENT) {
		g_debug ("using small form factor mode as %ix%i and requested %ix%i",
			   workarea.width, workarea.height, width, height);
		gtk_window_maximize (window);
		small_form_factor_mode = TRUE;
		goto out;
	}
#else
	/* skip invalid values */
	if (width == 0 || height == 0)
		goto out;
#endif
out_normal_size:
	/* normal size laptop panel */
	g_debug ("using native mode: %ix%i", width, height);
	gtk_window_set_default_size (window, width, height);
	small_form_factor_mode = FALSE;
out:
	return !small_form_factor_mode;
}

gboolean
gpk_window_set_parent_xid (GtkWindow *window, guint32 xid)
{
	GdkDisplay *display;
	GdkWindow *parent_window;
	GdkWindow *our_window;

	g_return_val_if_fail (xid != 0, FALSE);

	display = gdk_display_get_default ();
	parent_window = gdk_x11_window_foreign_new_for_display (display, xid);
	our_window = gtk_widget_get_window (GTK_WIDGET (window));

	/* set this above our parent */
	gtk_window_set_modal (window, TRUE);
	gdk_window_set_transient_for (our_window, parent_window);
	return TRUE;
}

static const gchar *
gpk_get_pretty_arch (const gchar *arch)
{
	const gchar *id = NULL;

	if (arch[0] == '\0')
		goto out;

	/* 32 bit */
	if (g_str_has_prefix (arch, "i")) {
		/* TRANSLATORS: a 32 bit package */
		id = _("32-bit");
		goto out;
	}

	/* 64 bit */
	if (g_str_has_suffix (arch, "64")) {
		/* TRANSLATORS: a 64 bit package */
		id = _("64-bit");
		goto out;
	}
out:
	return id;
}

gchar *
gpk_package_id_format_twoline (GtkStyleContext *style,
			       const gchar *package_id,
			       const gchar *summary)
{
	g_autofree gchar *summary_safe = NULL;
	GString *string;
	g_auto(GStrv) split = NULL;
	g_autofree gchar *color = NULL;
	const gchar *arch;
	GdkRGBA inactive;

	g_return_val_if_fail (package_id != NULL, NULL);

	/* get style color */
	if (style != NULL) {
		gtk_style_context_get_color (style,
					     GTK_STATE_FLAG_INSENSITIVE,
					     &inactive);
		color = g_strdup_printf ("#%02x%02x%02x",
					 (guint) (inactive.red * 255.0f),
					 (guint) (inactive.green * 255.0f),
					 (guint) (inactive.blue * 255.0f));
	} else {
		color = g_strdup ("gray");
	}

	/* optional */
	split = pk_package_id_split (package_id);
	if (split == NULL) {
		g_warning ("could not parse %s", package_id);
		return NULL;
	}

	/* no summary */
	if (summary == NULL || summary[0] == '\0') {
		string = g_string_new (split[PK_PACKAGE_ID_NAME]);
		if (split[PK_PACKAGE_ID_VERSION][0] != '\0')
			g_string_append_printf (string, "-%s", split[PK_PACKAGE_ID_VERSION]);
		arch = gpk_get_pretty_arch (split[PK_PACKAGE_ID_ARCH]);
		if (arch != NULL)
			g_string_append_printf (string, " (%s)", arch);
		return g_string_free (string, FALSE);
	}

	/* name and summary */
	string = g_string_new ("");
	summary_safe = g_markup_escape_text (summary, -1);
	g_string_append_printf (string, "%s\n", summary_safe);
	g_string_append_printf (string, "<span color=\"%s\">", color);
	g_string_append (string, split[PK_PACKAGE_ID_NAME]);
	if (split[PK_PACKAGE_ID_VERSION][0] != '\0')
		g_string_append_printf (string, "-%s", split[PK_PACKAGE_ID_VERSION]);
	arch = gpk_get_pretty_arch (split[PK_PACKAGE_ID_ARCH]);
	if (arch != NULL)
		g_string_append_printf (string, " (%s)", arch);
	g_string_append (string, "</span>");
	return g_string_free (string, FALSE);
}

gchar *
gpk_package_id_format_oneline (const gchar *package_id, const gchar *summary)
{
	g_autofree gchar *summary_safe = NULL;
	g_auto(GStrv) split = NULL;

	g_return_val_if_fail (package_id != NULL, NULL);

	split = pk_package_id_split (package_id);
	if (summary == NULL || summary[0] == '\0') {
		/* just have name */
		return g_strdup (split[PK_PACKAGE_ID_NAME]);
	}
	summary_safe = g_markup_escape_text (summary, -1);
	return g_strdup_printf ("<b>%s</b> (%s)", summary_safe, split[PK_PACKAGE_ID_NAME]);
}

gboolean
gpk_check_privileged_user (const gchar *application_name, gboolean show_ui)
{
	guint uid;
	g_autofree gchar *message = NULL;
	g_autofree gchar *title = NULL;
	GtkResponseType result;
	GtkWidget *dialog;

	uid = getuid ();
	if (uid == 0) {
		if (!show_ui)
			return TRUE;
		if (application_name == NULL)
			/* TRANSLATORS: these tools cannot run as root (unknown name) */
			title = g_strdup (_("This application is running as a privileged user"));
		else
			/* TRANSLATORS: cannot run as root user, and we display the application name */
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
			g_warning ("uid=%u so closing", uid);
			return FALSE;
		}
	}
	return TRUE;
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
                /* Translators: a list of two things */
		return g_strdup_printf (_("%s and %s"),
					array[0], array[1]);
	else if (length == 3)
                /* Translators: a list of three things */
		return g_strdup_printf (_("%s, %s and %s"),
					array[0], array[1], array[2]);
	else if (length == 4)
                /* Translators: a list of four things */
		return g_strdup_printf (_("%s, %s, %s and %s"),
					array[0], array[1],
					array[2], array[3]);
	else if (length == 5)
                /* Translators: a list of five things */
		return g_strdup_printf (_("%s, %s, %s, %s and %s"),
					array[0], array[1], array[2],
					array[3], array[4]);
	return NULL;
}
