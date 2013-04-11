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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>
#include <locale.h>
#include <string.h>

#include "egg-string.h"

#include "gpk-desktop.h"

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
		g_debug ("ignoring broken icon %s", icon);
		ret = FALSE;
	} else {
		/* we only used this to see if it was valid */
		g_object_unref (icon_info);
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

	/* autostart files usually are not hat we are looking for */
	value = g_strstr_len (filename, -1, "autostart");
	if (value != NULL)
		weight -= 100;

	locale = setlocale (LC_ALL, NULL);
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, filename, G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
	if (!ret) {
		g_debug ("failed to open %s", filename);
		weight = G_MININT;
		goto out;
	}

	/* application */
	value = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_TYPE, NULL);
	if (g_strcmp0 (value, G_KEY_FILE_DESKTOP_TYPE_APPLICATION) == 0)
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
	if (g_strcmp0 (value, "true") == 0)
		weight -= 100;
	g_free (value);

	/* has locale */
	value = g_key_file_get_locale_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NAME, locale, NULL);
	if (value != NULL)
		weight += 30;
	g_free (value);

	/* has autostart phase */
	value = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, "X-GNOME-Autostart-Phase", NULL);
	if (value != NULL)
		weight -= 30;
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
	gint max = G_MININT;
	gint weight;
	guint max_index = 0;

	array = pk_desktop_get_files_for_package (desktop, package, NULL);
	if (array == NULL)
		goto out;
	if (array->len == 0)
		goto out;

	/* go through each option, and weight each one */
	for (i=0; i<array->len; i++) {
		filename = g_ptr_array_index (array, i);
		weight = gpk_desktop_get_file_weight (filename);
		g_debug ("file %s has weight %i", filename, weight);
		if (weight > max) {
			max = weight;
			max_index = i;
		}
	}

	/* nothing was processed */
	if (max == G_MININT)
		goto out;

	/* we've got a best */
	best_file = g_strdup (g_ptr_array_index (array, max_index));
	g_debug ("using %s", best_file);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
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
		g_warning ("failed to open %s", filename);
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
		g_warning ("failed to open %s", filename);
		goto out;
	}
	data = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NAME, NULL);
	g_key_file_free (file);
out:
	g_free (filename);
	return data;
}
