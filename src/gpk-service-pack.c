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

#include <glib/gi18n.h>
#include <locale.h>

#include <gtk/gtk.h>
#include <sys/utsname.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-string.h"

#include "gpk-common.h"
#include "gpk-error.h"
#include "gpk-gnome.h"
#include "gpk-enum.h"

typedef enum {
	GPK_ACTION_ENUM_COPY,
	GPK_ACTION_ENUM_PACKAGE,
	GPK_ACTION_ENUM_UPDATES
} GpkActionEnum;

static GtkBuilder *builder = NULL;
static PkClient *client = NULL;
static PkControl *control = NULL;
static GpkActionEnum action = GPK_ACTION_ENUM_UPDATES;
static guint pulse_id = 0;

/**
 * pk_get_node_name:
 **/
static gchar *
pk_get_node_name (void)
{
	gint retval;
	struct utsname buf;

	retval = uname (&buf);
	if (retval != 0)
		return g_strdup ("localhost");
	return g_strdup (buf.nodename);
}

/**
 * gpk_pack_get_default_filename:
 **/
static gchar *
gpk_pack_get_default_filename (const gchar *directory)
{
	GtkEntry *entry;
	gchar *filename = NULL;
	gchar *distro_id;
	gchar *iso_time = NULL;
	gchar *nodename = NULL;
	const gchar *package;

	/* get the distro-id from the daemon */
	g_object_get (control,
		      "distro-id", &distro_id,
		      NULL);

	/* make a sane filename */
	g_strdelimit (distro_id, ";", '-');

	if (action == GPK_ACTION_ENUM_PACKAGE) {
		entry = GTK_ENTRY (gtk_builder_get_object (builder, "entry_package"));
		package = gtk_entry_get_text (entry);
		filename = g_strdup_printf ("%s/%s-%s.servicepack", directory, package, distro_id);
	} else if (action == GPK_ACTION_ENUM_COPY) {
		nodename = pk_get_node_name ();
		filename = g_strdup_printf ("%s/%s.package-array", directory, nodename);
	} else if (action == GPK_ACTION_ENUM_UPDATES) {
		iso_time = pk_iso8601_present ();
		/* don't include the time, just use the date prefix */
		iso_time[10] = '\0';
		filename = g_strdup_printf ("%s/updates-%s-%s.servicepack", directory, iso_time, distro_id);
	}
	g_free (nodename);
	g_free (distro_id);
	g_free (iso_time);
	return filename;
}
/**
 * gpk_pack_button_help_cb:
 **/
static void
gpk_pack_button_help_cb (GtkWidget *widget, gpointer data)
{
	gpk_gnome_help ("service-pack");
}

/**
 * gpk_pack_widgets_activate:
 **/
static void
gpk_pack_widgets_activate (gboolean enable)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
	gtk_widget_set_sensitive (widget, enable && action == GPK_ACTION_ENUM_PACKAGE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "radiobutton_updates"));
	gtk_widget_set_sensitive (widget, enable);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "radiobutton_package"));
	gtk_widget_set_sensitive (widget, enable);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "radiobutton_copy"));
	gtk_widget_set_sensitive (widget, enable);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_directory"));
	gtk_widget_set_sensitive (widget, enable);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_exclude"));
	gtk_widget_set_sensitive (widget, enable && action != GPK_ACTION_ENUM_COPY);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_create"));
	gtk_widget_set_sensitive (widget, enable);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	gtk_widget_set_sensitive (widget, enable);
}

/**
 * gpk_pack_percentage_pulse_cb:
 **/
static gboolean
gpk_pack_percentage_pulse_cb (gpointer data)
{
	GtkProgressBar *progress_bar;
	progress_bar = GTK_PROGRESS_BAR (gtk_builder_get_object (builder, "progressbar_percentage"));
	gtk_progress_bar_pulse (progress_bar);
	return TRUE;
}

/**
 * gpk_pack_set_percentage:
 **/
static void
gpk_pack_set_percentage (guint percentage)
{
	GtkProgressBar *progress_bar;

	/* no info */
	if (percentage == 101) {
		/* set pulsing */
		if (pulse_id == 0) {
			pulse_id = g_timeout_add (100, gpk_pack_percentage_pulse_cb, NULL);
			g_source_set_name_by_id (pulse_id, "[GpkServicePack] pulse");
		}
		return;
	}

	/* clear pulse */
	if (pulse_id != 0) {
		g_source_remove (pulse_id);
		pulse_id = 0;
	}
	
	progress_bar = GTK_PROGRESS_BAR (gtk_builder_get_object (builder, "progressbar_percentage"));
	gtk_progress_bar_set_fraction (progress_bar, percentage / 100.0f);
}

/**
 * gpk_pack_resolve_package_id:
 **/
static gchar *
gpk_pack_resolve_package_id (const gchar *package)
{
	GPtrArray *array = NULL;
	gchar *package_id = NULL;
	gchar **packages = NULL;
	GError *error = NULL;
	PkPackage *item;
	PkResults *results;
	PkError *error_code = NULL;

	/* get package array */
	packages = g_strsplit (package, ";", 0);
	results = pk_client_resolve (client, pk_bitfield_value (PK_FILTER_ENUM_NEWEST), packages, NULL, NULL, NULL, &error);
	if (results == NULL) {
		g_warning ("failed to resolve: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to resolve: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* get the deps */
	array = pk_results_get_package_array (results);

	/* no matches */
	if (array->len == 0)
		goto out;

	/* display warning if not exactly one match */
	if (array->len > 1)
		g_warning ("More than one possible package for '%s' found!", package);

	/* convert to a text package id */
	item = g_ptr_array_index (array, 0);
	package_id = g_strdup (pk_package_get_id (item));

out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	g_strfreev (packages);
	return package_id;
}

/**
 * gpk_pack_resolve_package_ids:
 **/
static gchar **
gpk_pack_resolve_package_ids (gchar **package, GError **error)
{
	gchar **package_ids = NULL;
	guint i, length;
	gboolean ret = TRUE;
	GPtrArray *array;
	gchar *package_id;

	length = g_strv_length (package);
	array = g_ptr_array_new_with_free_func (g_free);

	/* for each package, resolve to a package_id */
	for (i=0; i<length; i++) {

		/* nothing */
		if (package[i][0] == '\0')
			continue;

		/* try to resolve */
		package_id = gpk_pack_resolve_package_id (package[i]);
		if (package_id == NULL) {
			/* TRANSLATORS: cannot resolve name to package name */
			g_set_error (error, 1, 0, _("Could not find any packages named '%s'"), package[i]);
			ret = FALSE;
			break;
		}

		/* add to array as a match */
		g_ptr_array_add (array, package_id);
	}

	/* no packages */
	if (array->len == 0) {
		/* TRANSLATORS: cannot find any valid package names */
		g_set_error_literal (error, 1, 0, _("Could not find any valid package names"));
		goto out;
	}

	/* we got package_ids for all of them */
	if (ret)
		package_ids = pk_ptr_array_to_strv (array);

out:
	/* free temp array */
	g_ptr_array_unref (array);
	return package_ids;
}

/**
 * gpk_pack_package_array_to_string:
 **/
static gchar *
gpk_pack_package_array_to_string (GPtrArray *array)
{
	guint i;
	PkPackage *item;
	GString *string;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *summary = NULL;

	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);
		g_string_append_printf (string, "%s\t%s\t%s\n",
					pk_info_enum_to_string (info), package_id, summary);
		g_free (package_id);
		g_free (summary);
	}

	/* remove trailing newline */
	if (string->len != 0)
		g_string_set_size (string, string->len-1);
	return g_string_free (string, FALSE);
}

/**
 * gpk_pack_copy_package_lists:
 **/
static gboolean
gpk_pack_copy_package_lists (const gchar *filename, GError **error)
{
	gboolean ret = FALSE;
	GPtrArray *array = NULL;
	GError *error_local = NULL;
	PkResults *results;
	gchar *data = NULL;
	PkError *error_code = NULL;

	/* get package array */
	results = pk_client_get_packages (client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), NULL, NULL, NULL, &error_local);
	if (results == NULL) {
		/* TRANSLATORS: cannot get package array */
		g_set_error (error, 1, 0, _("Could not get array of installed packages: %s"), error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get packages: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* get the deps */
	array = pk_results_get_package_array (results);

	/* convert to a file */
	data = gpk_pack_package_array_to_string (array);
	ret = g_file_set_contents (filename, data, -1, &error_local);
	if (!ret) {
		g_set_error (error, 1, 0, _("Could not save to file: %s"), error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	g_free (data);
	return ret;
}

/**
 * gpk_pack_reset_ui:
 **/
static void
gpk_pack_reset_ui (void)
{
	GtkWidget *widget;

	/* stop the action */
	gpk_pack_widgets_activate (TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "frame_progress"));
	gtk_widget_hide (widget);
	gpk_pack_set_percentage (100);

	/* blank */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar_percentage"));
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (widget), "");
}

/**
 * gpk_pack_ready_cb:
 **/
static void
gpk_pack_ready_cb (GObject *object, GAsyncResult *res, gpointer userdata)
{
	GtkWidget *widget;
	PkServicePack *pack = PK_SERVICE_PACK (object);
	GError *error = NULL;
	gboolean ret;

	/* get the results */
	ret = pk_service_pack_generic_finish (pack, res, &error);
	if (!ret) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
		/* TRANSLATORS: we could not create the pack file, generic error */
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), _("Cannot create service pack"), error->message);
		g_error_free (error);
	}

	/* reset regardless of success/failure */
	gpk_pack_reset_ui ();
}

/**
 * gpk_pack_progress_cb:
 **/
static void
gpk_pack_progress_cb (PkProgress *progress, PkProgressType type, gpointer userdata)
{
	PkStatusEnum status;
	gint percentage;
	GtkProgressBar *progress_bar;
	gchar *text;
	gchar **split;
	gchar *package_id;

	g_object_get (progress,
		      "status", &status,
		      "percentage", &percentage,
		      "package-id", &package_id,
		      NULL);

	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_debug ("now %s", pk_status_enum_to_string (status));
	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		gpk_pack_set_percentage (percentage);
	} else if (type == PK_PROGRESS_TYPE_PACKAGE_ID) {
		progress_bar = GTK_PROGRESS_BAR (gtk_builder_get_object (builder, "progressbar_percentage"));
		split = pk_package_id_split (package_id);
		/* TRANSLATORS: This is the package name that is being downloaded */
		text = g_strdup_printf ("%s: %s-%s.%s",
					_("Downloading"),
					split[PK_PACKAGE_ID_NAME],
					split[PK_PACKAGE_ID_VERSION],
					split[PK_PACKAGE_ID_ARCH]);
		gtk_progress_bar_set_text (progress_bar, text);
		g_free (text);
		g_strfreev (split);
	}
}

/**
 * gpk_pack_get_excludes_for_filename:
 **/
static gchar **
gpk_pack_get_excludes_for_filename (const gchar *filename, GError **error)
{
	gboolean ret;
	gchar *data = NULL;
	gchar **split = NULL;
	gchar **lines = NULL;
	gchar **exclude_ids = NULL;
	GPtrArray *array = NULL;
	guint i;

	/* get contents */
	ret = g_file_get_contents (filename, &data, NULL, error);
	if (!ret)
		goto out;

	/* split into lines */
	array = g_ptr_array_new_with_free_func (g_free);
	lines = g_strsplit (data, "\n", -1);
	for (i=0; lines[i] != NULL; i++) {
		/* split into sections */
		split = g_strsplit (lines[i], "\t", -1);
		if (g_strv_length (split) == 3)
			g_ptr_array_add (array, g_strdup (split[1]));
		g_strfreev (split);
	}

	/* convert to a string array */
	exclude_ids = pk_ptr_array_to_strv (array);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	g_free (data);
	g_strfreev (lines);
	return exclude_ids;
}

/**
 * gpk_pack_button_create_cb:
 **/
static void
gpk_pack_button_create_cb (GtkWidget *widget2, gpointer data)
{
	GtkWidget *widget;
	const gchar *package = NULL;
	gchar *directory;
	gchar *filename;
	gchar *exclude = NULL;
	gchar **packages = NULL;
	gchar **package_ids = NULL;
	gchar **exclude_ids = NULL;
	PkServicePack *pack;
	GError *error = NULL;
	gboolean ret;
	gboolean use_default = FALSE;
	GtkProgressBar *progress_bar;
	PkError *error_code = NULL;

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_directory"));
	directory = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(widget));

	/* use a default filename */
	filename = gpk_pack_get_default_filename (directory);

	/* start the action */
	gpk_pack_widgets_activate (FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "frame_progress"));
	gtk_widget_show (widget);

	/* copy the system package array */
	if (action == GPK_ACTION_ENUM_COPY) {
		ret = gpk_pack_copy_package_lists (filename, &error);
		if (!ret) {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
			/* TRANSLATORS: Could not create package array */
			gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), _("Cannot copy system package array"), error->message);
			g_error_free (error);
		}
		gpk_pack_reset_ui ();
		goto out;
	}

	/* get the exclude array, and fall back to the system copy */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_exclude"));
	exclude = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(widget));
	if (exclude == NULL) {
		exclude = g_strdup (PK_SYSTEM_PACKAGE_LIST_FILENAME);
		use_default = TRUE;
	}

	/* get the package to download */
	if (action == GPK_ACTION_ENUM_PACKAGE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
		package = gtk_entry_get_text (GTK_ENTRY(widget));
		if (egg_strzero (package)) {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
			/* TRANSLATORS: Could not create package array */
			gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), _("No package name selected"), NULL);
			goto out;
		}
		/* split the package array with common delimiters */
		packages = g_strsplit_set (package, ";, ", 0);
		package_ids = gpk_pack_resolve_package_ids (packages, &error);
		if (package_ids == NULL) {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
			/* TRANSLATORS: Could not create package array */
			gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), error->message, NULL);
			g_error_free (error);
			goto out;
		}
	}

	/* if we're using the default array, and it doesn't exist, refresh and create it */
	if (use_default && !g_file_test (exclude, G_FILE_TEST_EXISTS)) {
		PkResults *results;

		/* tell the user what we are doing */
		progress_bar = GTK_PROGRESS_BAR (gtk_builder_get_object (builder, "progressbar_percentage"));
		/* TRANSLATORS: progress bar text */
		gtk_progress_bar_set_text (progress_bar, _("Refreshing system package array"));

		/* refresh package array */
		results = pk_client_refresh_cache (client, TRUE, NULL, NULL, NULL, &error);
		if (results == NULL) {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
			/* TRANSLATORS: we could not reset internal state */
			gpk_error_dialog_modal (GTK_WINDOW (widget), _("Refresh error"), _("Could not refresh package array"), error->message);
			g_error_free (error);
			goto out;
		}

		/* check error code */
		error_code = pk_results_get_error_code (results);
		if (error_code != NULL) {
			g_warning ("failed to refresh cache: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
			goto out;
		}

		g_object_unref (results);
	}

	/* add the exclude array */
	exclude_ids = gpk_pack_get_excludes_for_filename (exclude, &error);
	if (exclude_ids == NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
		/* TRANSLATORS: we could not read the file array for the destination computer */
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), _("Cannot read destination package array"), error->message);
		g_error_free (error);
		goto out;
	}

	/* create pack and set initial values */
	pack = pk_service_pack_new ();
	pk_service_pack_set_temp_directory (pack, NULL);

	if (action == GPK_ACTION_ENUM_UPDATES) {
		pk_service_pack_create_for_updates_async (pack, filename, exclude_ids, NULL,
							  (PkProgressCallback) gpk_pack_progress_cb, NULL,
							  (GAsyncReadyCallback) gpk_pack_ready_cb, NULL);
	} else if (action == GPK_ACTION_ENUM_PACKAGE) {
		pk_service_pack_create_for_package_ids_async (pack, filename, package_ids, exclude_ids, NULL,
							      (PkProgressCallback) gpk_pack_progress_cb, NULL,
							      (GAsyncReadyCallback) gpk_pack_ready_cb, NULL);
	}
	g_object_unref (pack);

out:
	if (error_code != NULL)
		g_object_unref (error_code);
	g_strfreev (packages);
	g_strfreev (package_ids);
	g_strfreev (exclude_ids);
	g_free (directory);
	g_free (exclude);
}

/**
 * gpk_pack_radio_updates_cb:
 **/
static void
gpk_pack_radio_updates_cb (GtkWidget *widget2, gpointer data)
{
	GtkWidget *widget;
	g_debug ("got updates");
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
	gtk_widget_set_sensitive (widget, FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_exclude"));
	gtk_widget_set_sensitive (widget, TRUE);
	action = GPK_ACTION_ENUM_UPDATES;
}

/**
 * gpk_pack_radio_package_cb:
 **/
static void
gpk_pack_radio_package_cb (GtkWidget *widget2, gpointer data)
{
	GtkWidget *widget;
	g_debug ("got package");
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_exclude"));
	gtk_widget_set_sensitive (widget, TRUE);
	action = GPK_ACTION_ENUM_PACKAGE;
}

/**
 * gpk_pack_radio_copy_cb:
 **/
static void
gpk_pack_radio_copy_cb (GtkWidget *widget2, gpointer data)
{
	GtkWidget *widget;
	g_debug ("got copy");
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
	gtk_widget_set_sensitive (widget, FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_exclude"));
	gtk_widget_set_sensitive (widget, FALSE);
	action = GPK_ACTION_ENUM_COPY;
}

/**
 * gpk_pack_button_close_cb:
 **/
static void
gpk_pack_button_close_cb (GtkWidget *widget, GtkApplication *application)
{
	g_application_release (G_APPLICATION (application));
}

/**
 * gpk_pack_startup_cb:
 **/
static void
gpk_pack_startup_cb (GtkApplication *application, gpointer user_data)
{
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkFileFilter *filter;
	GtkEntryCompletion *completion;
	gboolean ret;
	guint retval;
	GSettings *settings = NULL;
	GError *error = NULL;

	/* use a client to download packages */
	client = pk_client_new ();
	g_object_set (client,
		      "background", FALSE,
		      NULL);

	/* get the properties of the daemon */
	control = pk_control_new ();
	ret = pk_control_get_properties (control, NULL, &error);
	if (!ret) {
		g_error ("Failed to contact PackageKit: %s", error->message);
		g_error_free (error);
	}

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-service-pack.ui", &error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
	gtk_application_add_window (application, GTK_WINDOW (main_window));

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SERVICE_PACK);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_exclude"));
	filter = gtk_file_filter_new ();
	/* TRANSLATORS: file search type, lists of packages */
	gtk_file_filter_set_name (filter, _("Package array files"));
	gtk_file_filter_add_pattern (filter, "*.package-array");
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER(widget), filter);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_directory"));
	filter = gtk_file_filter_new ();
	/* TRANSLATORS: file search type, service pack destination file type */
	gtk_file_filter_set_name (filter, _("Service pack files"));
	gtk_file_filter_add_pattern (filter, "*.servicepack");
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER(widget), filter);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "radiobutton_updates"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_radio_updates_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "radiobutton_package"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_radio_package_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "radiobutton_copy"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_radio_copy_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_button_close_cb), application);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_create"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_button_create_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_help"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_button_help_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "frame_progress"));
	gtk_widget_hide (widget);

	/* autocompletion can be turned off as it's slow */
	settings = g_settings_new (GPK_SETTINGS_SCHEMA);
	ret = g_settings_get_boolean (settings, GPK_SETTINGS_AUTOCOMPLETE);
	if (ret) {
		/* create the completion object */
		completion = gpk_package_entry_completion_new ();
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
		gtk_entry_set_completion (GTK_ENTRY (widget), completion);
		g_object_unref (completion);
	}

	gtk_widget_show (main_window);
out:
	if (settings != NULL)
		g_object_unref (settings);
}

/**
 * gpm_pack_commandline_cb:
 **/
static int
gpm_pack_commandline_cb (GApplication *application,
			 GApplicationCommandLine *cmdline,
			 gpointer user_data)
{
	gboolean ret;
	gchar **argv;
	gchar *option = NULL;
	gchar *package = NULL;
	gchar *with_array = NULL;
	gchar *output = NULL;
	gint argc;
	GOptionContext *context;
	GtkWidget *widget;
	GtkWindow *window;

	const GOptionEntry options[] = {
		{ "option", 'o', 0, G_OPTION_ARG_STRING, &option,
		  /* TRANSLATORS: the constants should not be translated */
		  _("Set the option, allowable values are 'array', 'updates' and 'package'"), NULL },
		{ "package", 'p', 0, G_OPTION_ARG_STRING, &package,
		  /* TRANSLATORS: this refers to the GtkTextEntry in gpk-service-pack */
		  _("Add the package name to the text entry box"), NULL },
		{ "with-array", 'p', 0, G_OPTION_ARG_STRING, &with_array,
		  /* TRANSLATORS: this is the destination computer package array */
		  _("Set the remote package array file name"), NULL },
		{ "output", 'p', 0, G_OPTION_ARG_STRING, &output,
		  /* TRANSLATORS: this is the file output directory */
		  _("Set the default output directory"), NULL },
		{ NULL}
	};

	/* get arguments */
	argv = g_application_command_line_get_arguments (cmdline, &argc);

	context = g_option_context_new (NULL);
	/* TRANSLATORS: the program name */
	g_option_context_set_summary (context, _("Service Pack Creator"));
	g_option_context_add_main_entries (context, options, NULL);
//	g_option_context_add_group (context, gpk_debug_get_option_group ());
	ret = g_option_context_parse (context, &argc, &argv, NULL);
	if (!ret)
		goto out;

	/* if command line arguments are set, then setup UI */
	if (option != NULL) {
		if (g_strcmp0 (option, "array") == 0) {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "radiobutton_copy"));
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
		} else if (g_strcmp0 (option, "updates") == 0) {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "radiobutton_updates"));
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
		} else if (g_strcmp0 (option, "package") == 0) {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "radiobutton_package"));
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
		}
	}
	if (package != NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
		gtk_entry_set_text (GTK_ENTRY(widget), package);
	}
	if (with_array != NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_exclude"));
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(widget), with_array);
	}
	if (output != NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_directory"));
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(widget), output);
	}

	/* make sure the window is raised */
	window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_pack"));
	gtk_window_present (window);
out:
	g_free (option);
	g_free (package);
	g_free (with_array);
	g_free (output);
	g_strfreev (argv);
	g_option_context_free (context);
	return ret;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GtkApplication *application;
	gint status = 0;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	g_type_init ();

	gtk_init (&argc, &argv);

	/* are we already activated? */
	application = gtk_application_new ("org.freedesktop.PackageKit.ServicePack",
					   G_APPLICATION_HANDLES_COMMAND_LINE);
	g_signal_connect (application, "startup",
			  G_CALLBACK (gpk_pack_startup_cb), NULL);
	g_signal_connect (application, "command-line",
			  G_CALLBACK (gpm_pack_commandline_cb), NULL);

	/* run */
	status = g_application_run (G_APPLICATION (application), argc, argv);

	g_object_unref (application);
	if (builder != NULL)
		g_object_unref (builder);
	if (control != NULL)
		g_object_unref (control);
	if (client != NULL)
		g_object_unref (client);

	return status;
}
