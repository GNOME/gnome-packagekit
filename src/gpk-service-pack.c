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
#include <locale.h>

#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <sys/utsname.h>
#include <dbus/dbus-glib.h>
#include <gconf/gconf-client.h>
#include <packagekit-glib/packagekit.h>
#include <unique/unique.h>

#include "egg-debug.h"
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

	distro_id = pk_get_distro_id ();
	if (action == GPK_ACTION_ENUM_PACKAGE) {
		entry = GTK_ENTRY (gtk_builder_get_object (builder, "entry_package"));
		package = gtk_entry_get_text (entry);
		filename = g_strdup_printf ("%s/%s-%s.servicepack", directory, package, distro_id);
	} else if (action == GPK_ACTION_ENUM_COPY) {
		nodename = pk_get_node_name ();
		filename = g_strdup_printf ("%s/%s.package-list", directory, nodename);
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
 * gpk_pack_package_cb:
 **/
static void
gpk_pack_package_cb (PkServicePack *pack, const PkPackageObj *obj, gpointer data)
{
	GtkProgressBar *progress_bar;
	gchar *text;

	progress_bar = GTK_PROGRESS_BAR (gtk_builder_get_object (builder, "progressbar_percentage"));
	/* TRANSLATORS: This is the package name that is being downloaded */
	text = g_strdup_printf ("%s: %s-%s.%s", _("Downloading"), obj->id->name, obj->id->version, obj->id->arch);
	gtk_progress_bar_set_text (progress_bar, text);
	g_free (text);
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
		if (pulse_id == 0)
			pulse_id = g_timeout_add (100, gpk_pack_percentage_pulse_cb, NULL);
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
 * gpk_pack_percentage_cb:
 **/
static void
gpk_pack_percentage_cb (PkServicePack *pack, guint percentage, gpointer data)
{
	gpk_pack_set_percentage (percentage);
}

/**
 * gpk_pack_progress_changed_cb:
 **/
static void
gpk_pack_progress_changed_cb (PkClient *_client, guint percentage, guint subpercentage,
			      guint elapsed, guint remaining, gpointer data)
{
	gpk_pack_set_percentage (percentage);
}

/**
 * gpk_pack_resolve_package_id:
 **/
static gchar *
gpk_pack_resolve_package_id (const gchar *package)
{
	PkPackageList *list = NULL;
	gchar *package_id = NULL;
	gchar **packages = NULL;
	GError *error = NULL;
	const PkPackageObj *obj;
	gboolean ret = FALSE;
	guint len;

	/* reset client */
	ret = pk_client_reset (client, &error);
	if (!ret) {
		egg_warning ("could not reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get package list */
	packages = g_strsplit (package, ";", 0);
	ret = pk_client_resolve (client, pk_bitfield_value (PK_FILTER_ENUM_NEWEST), packages, &error);
	if (!ret) {
		egg_warning ("failed to resolve: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get the deps */
	list = pk_client_get_package_list (client);
	len = pk_package_list_get_size (list);

	/* no matches */
	if (len == 0)
		goto out;

	/* display warning if not exactly one match */
	if (len > 1)
		egg_warning ("More than one possible package for '%s' found!", package);

	/* convert to a text package id */
	obj = pk_package_list_get_obj (list, 0);
	package_id = pk_package_id_to_string (obj->id);

out:
	if (list != NULL)
		g_object_unref (list);
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
	array = g_ptr_array_new ();

	/* for each package, resolve to a package_id */
	for (i=0; i<length; i++) {

		/* nothing */
		if (package[i][0] == '\0')
			continue;

		/* try to resolve */
		package_id = gpk_pack_resolve_package_id (package[i]);
		if (package_id == NULL) {
			/* TRANSLATORS: cannot resolve name to package name */
			*error = g_error_new (1, 0, _("Could not find any packages named '%s'"), package[i]);
			ret = FALSE;
			break;
		}

		/* add to array as a match */
		g_ptr_array_add (array, package_id);
	}

	/* no packages */
	if (array->len == 0) {
		/* TRANSLATORS: cannot find any valid package names */
		*error = g_error_new (1, 0, _("Could not find any valid package names"));
		goto out;
	}

	/* we got package_ids for all of them */
	if (ret)
		package_ids = pk_ptr_array_to_strv (array);

out:
	/* free temp array */
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);
	return package_ids;
}

/**
 * gpk_pack_copy_package_lists:
 **/
static gboolean
gpk_pack_copy_package_lists (const gchar *filename, GError **error)
{
	gboolean ret = FALSE;
	PkPackageList *list = NULL;
	GError *error_local = NULL;

	/* reset client */
	ret = pk_client_reset (client, &error_local);
	if (!ret) {
		/* TRANSLATORS: internal error */
		*error = g_error_new (1, 0, _("Could not reset client: %s"), error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get package list */
	ret = pk_client_get_packages (client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), &error_local);
	if (!ret) {
		/* TRANSLATORS: cannot get package list */
		*error = g_error_new (1, 0, _("Could not get list of installed packages: %s"), error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the deps */
	list = pk_client_get_package_list (client);

	/* write new file : FIXME: return a GError */
	ret = pk_obj_list_to_file (PK_OBJ_LIST(list), filename);
	if (!ret) {
		/* TRANSLATORS: we could not write to the destination directory for some reason */
		*error = g_error_new (1, 0, _("Could not write package list"));
		goto out;
	}
out:
	if (list != NULL)
		g_object_unref (list);
	return ret;
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
	PkServicePack *pack;
	PkPackageList *list = NULL;
	GError *error = NULL;
	gboolean ret;
	gboolean use_default = FALSE;
	GtkProgressBar *progress_bar;

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_directory"));
	directory = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(widget));

	/* use a default filename */
	filename = gpk_pack_get_default_filename (directory);

	/* start the action */
	gpk_pack_widgets_activate (FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "frame_progress"));
	gtk_widget_show (widget);

	/* copy the system package list */
	if (action == GPK_ACTION_ENUM_COPY) {
		ret = gpk_pack_copy_package_lists (filename, &error);
		if (!ret) {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
			/* TRANSLATORS: Could not create package list */
			gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), _("Cannot copy system package list"), error->message);
			g_error_free (error);
		}
		goto out;
	}

	/* get the exclude list, and fall back to the system copy */
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
			/* TRANSLATORS: Could not create package list */
			gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), _("No package name selected"), NULL);
			goto out;
		}
		/* split the package list with common delimiters */
		packages = g_strsplit_set (package, ";, ", 0);
		package_ids = gpk_pack_resolve_package_ids (packages, &error);
		if (package_ids == NULL) {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
			/* TRANSLATORS: Could not create package list */
			gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), error->message, NULL);
			g_error_free (error);
			goto out;
		}
	}

	/* if we're using the default list, and it doesn't exist, refresh and create it */
	if (use_default && !g_file_test (exclude, G_FILE_TEST_EXISTS)) {
		/* reset client */
		ret = pk_client_reset (client, &error);
		if (!ret) {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
			/* TRANSLATORS: we could not reset internal state */
			gpk_error_dialog_modal (GTK_WINDOW (widget), _("Refresh error"), _("Could not reset client"), error->message);
			g_error_free (error);
			goto out;
		}

		/* tell the user what we are doing */
		progress_bar = GTK_PROGRESS_BAR (gtk_builder_get_object (builder, "progressbar_percentage"));
		/* TRANSLATORS: progressbar text */
		gtk_progress_bar_set_text (progress_bar, _("Refreshing system package list"));

		/* refresh package list */
		ret = pk_client_refresh_cache (client, TRUE, &error);
		if (!ret) {
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
			/* TRANSLATORS: we could not reset internal state */
			gpk_error_dialog_modal (GTK_WINDOW (widget), _("Refresh error"), _("Could not refresh package list"), error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* add the exclude list */
	list = pk_package_list_new ();
	ret = pk_obj_list_from_file (PK_OBJ_LIST(list), exclude);
	if (!ret) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
		/* TRANSLATORS: we could not read the file list for the destination computer */
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), _("Cannot read destination package list"), NULL);
		goto out;
	}

	/* create pack and set initial values */
	pack = pk_service_pack_new ();
	g_signal_connect (pack, "package", G_CALLBACK (gpk_pack_package_cb), pack);
	g_signal_connect (pack, "percentage", G_CALLBACK (gpk_pack_percentage_cb), pack);
	pk_service_pack_set_filename (pack, filename);
	pk_service_pack_set_temp_directory (pack, NULL);
	pk_service_pack_set_exclude_list (pack, list);

	if (action == GPK_ACTION_ENUM_UPDATES)
		ret = pk_service_pack_create_for_updates (pack, &error);
	else if (action == GPK_ACTION_ENUM_PACKAGE)
		ret = pk_service_pack_create_for_package_ids (pack, package_ids, &error);
	if (!ret) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));
		/* TRANSLATORS: we could not create the pack file, generic error */
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), _("Cannot create service pack"), error->message);
		g_error_free (error);
	}
	g_object_unref (pack);

out:
	/* stop the action */
	gpk_pack_widgets_activate (TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "frame_progress"));
	gtk_widget_hide (widget);
	gpk_pack_set_percentage (100);

	/* blank */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar_percentage"));
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (widget), "");

	if (list != NULL)
		g_object_unref (list);
	g_strfreev (packages);
	g_strfreev (package_ids);
	g_free (directory);
	g_free (exclude);
}

/**
 * gpk_pack_message_received_cb
 **/
static void
gpk_pack_message_received_cb (UniqueApp *app, UniqueCommand command, UniqueMessageData *message_data, guint time_ms, gpointer data)
{
	GtkWindow *window;
	if (command == UNIQUE_ACTIVATE) {
		window = GTK_WINDOW (gtk_builder_get_object (builder, "window_prefs"));
		gtk_window_present (window);
	}
}

/**
 * gpk_pack_radio_updates_cb:
 **/
static void
gpk_pack_radio_updates_cb (GtkWidget *widget2, gpointer data)
{
	GtkWidget *widget;
	egg_debug ("got updates");
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
	egg_debug ("got package");
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
	egg_debug ("got copy");
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
	gtk_widget_set_sensitive (widget, FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_exclude"));
	gtk_widget_set_sensitive (widget, FALSE);
	action = GPK_ACTION_ENUM_COPY;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean verbose = FALSE;
	GOptionContext *context;
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkFileFilter *filter;
	GtkEntryCompletion *completion;
	PkBitfield roles;
	PkControl *control = NULL;
	UniqueApp *unique_app;
	gboolean ret;
	GConfClient *gconf_client = NULL;
	gchar *option = NULL;
	gchar *package = NULL;
	gchar *with_list = NULL;
	gchar *output = NULL;
	guint retval;
	GError *error = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "option", 'o', 0, G_OPTION_ARG_STRING, &option,
		  /* TRANSLATORS: the constants should not be translated */
		  _("Set the option, allowable values are 'list', 'updates' and 'package'"), NULL },
		{ "package", 'p', 0, G_OPTION_ARG_STRING, &package,
		  /* TRANSLATORS: this refers to the GtkTextEntry in gpk-service-pack */
		  _("Add the package name to the text entry box"), NULL },
		{ "with-list", 'p', 0, G_OPTION_ARG_STRING, &with_list,
		  /* TRANSLATORS: this is the destination computer package list */
		  _("Set the remote package list filename"), NULL },
		{ "output", 'p', 0, G_OPTION_ARG_STRING, &output,
		  /* TRANSLATORS: this is the file output directory */
		  _("Set the default output directory"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (NULL);
	/* TRANSLATORS: program description, an application to create service packs */
	g_option_context_set_summary(context, _("Service Pack Creator"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);
	gtk_init (&argc, &argv);

	/* are we already activated? */
	unique_app = unique_app_new ("org.freedesktop.PackageKit.ServicePack", NULL);
	if (unique_app_is_running (unique_app)) {
		egg_debug ("You have another instance running. This program will now close");
		unique_app_send_message (unique_app, UNIQUE_ACTIVATE, NULL);
		goto out_unique;
	}
	g_signal_connect (unique_app, "message-received",
			  G_CALLBACK (gpk_pack_message_received_cb), NULL);

	/* get actions */
	control = pk_control_new ();
	roles = pk_control_get_actions (control, NULL);

	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, TRUE, NULL);
	g_signal_connect (client, "progress-changed", G_CALLBACK (gpk_pack_progress_changed_cb), NULL);

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-service-pack.ui", &error);
	if (error != NULL) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out_build;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_pack"));

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SERVICE_PACK);

	/* Get the main window quit */
	g_signal_connect_swapped (main_window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_exclude"));
	filter = gtk_file_filter_new ();
	/* TRANSLATORS: file search type, lists of packages */
	gtk_file_filter_set_name (filter, _("Package list files"));
	gtk_file_filter_add_pattern (filter, "*.package-list");
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
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_create"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_button_create_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_help"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_button_help_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "frame_progress"));
	gtk_widget_hide (widget);

	/* autocompletion can be turned off as it's slow */
	gconf_client = gconf_client_get_default ();
	ret = gconf_client_get_bool (gconf_client, GPK_CONF_AUTOCOMPLETE, NULL);
	if (ret) {
		/* create the completion object */
		completion = gpk_package_entry_completion_new ();
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
		gtk_entry_set_completion (GTK_ENTRY (widget), completion);
		g_object_unref (completion);
	}

	/* if command line arguments are set, then setup UI */
	if (option != NULL) {
		if (g_strcmp0 (option, "list") == 0) {
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
	if (with_list != NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_exclude"));
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(widget), with_list);
	}
	if (output != NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "filechooserbutton_directory"));
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(widget), output);
	}

	gtk_widget_show (main_window);

	/* wait */
	gtk_main ();

out_build:
	g_object_unref (builder);
out_unique:
	g_object_unref (unique_app);
	if (gconf_client != NULL)
		g_object_unref (gconf_client);
	if (control != NULL)
		g_object_unref (control);
	if (client != NULL)
		g_object_unref (client);
	g_free (option);
	g_free (package);
	g_free (with_list);
	g_free (output);

	return 0;
}
