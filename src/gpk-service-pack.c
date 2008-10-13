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
#include <locale.h>

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <sys/utsname.h>
#include <dbus/dbus-glib.h>
#include <gconf/gconf-client.h>

#include <pk-common.h>
#include <pk-client.h>
#include <pk-control.h>
#include <pk-service-pack.h>

#include "egg-debug.h"
#include "egg-unique.h"
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

static GladeXML *glade_xml = NULL;
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
	GtkWidget *widget;
	gchar *filename = NULL;
	gchar *distro_id;
	gchar *iso_time = NULL;
	gchar *nodename = NULL;
	const gchar *package;

	distro_id = pk_get_distro_id ();
	if (action == GPK_ACTION_ENUM_PACKAGE) {
		widget = glade_xml_get_widget (glade_xml, "entry_package");
		package = gtk_entry_get_text (GTK_ENTRY(widget));
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
	widget = glade_xml_get_widget (glade_xml, "entry_package");
	gtk_widget_set_sensitive (widget, enable && action == GPK_ACTION_ENUM_PACKAGE);
	widget = glade_xml_get_widget (glade_xml, "radiobutton_updates");
	gtk_widget_set_sensitive (widget, enable);
	widget = glade_xml_get_widget (glade_xml, "radiobutton_package");
	gtk_widget_set_sensitive (widget, enable);
	widget = glade_xml_get_widget (glade_xml, "radiobutton_copy");
	gtk_widget_set_sensitive (widget, enable);
	widget = glade_xml_get_widget (glade_xml, "filechooserbutton_directory");
	gtk_widget_set_sensitive (widget, enable);
	widget = glade_xml_get_widget (glade_xml, "filechooserbutton_exclude");
	gtk_widget_set_sensitive (widget, enable && action != GPK_ACTION_ENUM_COPY);
	widget = glade_xml_get_widget (glade_xml, "button_create");
	gtk_widget_set_sensitive (widget, enable);
	widget = glade_xml_get_widget (glade_xml, "button_close");
	gtk_widget_set_sensitive (widget, enable);
}

/**
 * gpk_pack_package_cb:
 **/
static void
gpk_pack_package_cb (PkServicePack *pack, const PkPackageObj *obj, gpointer data)
{
	GtkWidget *widget;
	gchar *text;
	widget = glade_xml_get_widget (glade_xml, "progressbar_percentage");
	text = g_strdup_printf ("%s-%s.%s", obj->id->name, obj->id->version, obj->id->arch);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR(widget), text);
	g_free (text);
}

/**
 * gpk_pack_percentage_pulse_cb:
 **/
static gboolean
gpk_pack_percentage_pulse_cb (gpointer data)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (glade_xml, "progressbar_percentage");
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR(widget));
	return TRUE;
}

/**
 * gpk_pack_set_percentage:
 **/
static void
gpk_pack_set_percentage (guint percentage)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (glade_xml, "progressbar_percentage");

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
	
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(widget), percentage / 100.0f);
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
gpk_pack_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
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
	GtkWidget *widget;
	PkPackageList *list = NULL;
	gchar *package_id = NULL;
	gchar **packages;
	gchar *text;
	PkClient *client;
	GError *error = NULL;
	const PkPackageObj *obj;
	gboolean ret = FALSE;
	guint len;

	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, TRUE, NULL);
	g_signal_connect (client, "progress-changed", G_CALLBACK (gpk_pack_progress_changed_cb), NULL);

	/* resolve */
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

	/* display errors if not exactly one match */
	if (len == 0) {
		widget = glade_xml_get_widget (glade_xml, "window_pack");
		text = g_strdup_printf (_("No package '%s' found!"), package);
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), text, NULL);
		g_free (text);
		goto out;
	} else if (len > 1) {
		widget = glade_xml_get_widget (glade_xml, "window_pack");
		text = g_strdup_printf (_("More than one possible package '%s' found!"), package);
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), text, NULL);
		g_free (text);
		goto out;
	}

	/* convert to a text package id */
	obj = pk_package_list_get_obj (list, 0);
	package_id = pk_package_id_to_string (obj->id);

out:
	if (list != NULL)
		g_object_unref (list);
	g_object_unref (client);
	g_strfreev (packages);
	return package_id;
}

/**
 * gpk_pack_resolve_package_ids:
 **/
static gchar **
gpk_pack_resolve_package_ids (gchar **package)
{
	gchar **package_ids;
	guint i, length;
	gboolean ret = TRUE;

	length = g_strv_length (package);
	package_ids = g_strdupv (package);

	/* for each package, resolve to a package_id */
	for (i=0; i<length; i++) {
		g_free (package_ids[i]);
		package_ids[i] = gpk_pack_resolve_package_id (package[i]);
		if (package_ids[i] == NULL) {
			egg_warning ("failed to resolve %s", package[i]);
			ret = FALSE;
			break;
		}
	}

	/* we failed at least one resolve */
	if (!ret) {
		g_strfreev (package_ids);
		package_ids = NULL;
	}
	return package_ids;
}

/**
 * gpk_pack_copy_package_lists:
 **/
static gboolean
gpk_pack_copy_package_lists (const gchar *filename, GError **error)
{
	gboolean ret = FALSE;
	PkPackageList *system = NULL;
	PkPackageList *installed = NULL;
	guint i;
	guint length;
	const PkPackageObj *obj;

	/* no feedback */
	gpk_pack_set_percentage (101);

	if (!g_file_test (PK_SYSTEM_PACKAGE_LIST_FILENAME, G_FILE_TEST_EXISTS)) {
		*error = g_error_new (0, 0, _("The file does not exists"));
		goto out;
	}

	/* open the list */
	system = pk_package_list_new ();
	ret = pk_package_list_add_file (system, PK_SYSTEM_PACKAGE_LIST_FILENAME);
	if (!ret) {
		*error = g_error_new (0, 0, _("Could not read package list"));
		goto out;
	}

	/* get all the installed entries */
	installed = pk_package_list_new ();
	length = pk_package_list_get_size (system);
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (system, i);
		if (obj->info == PK_INFO_ENUM_INSTALLED)
			pk_package_list_add_obj (installed, obj);
		/* don't hang the GUI */
		while (gtk_events_pending ())
			gtk_main_iteration ();
	}

	/* write new file */
	ret = pk_package_list_to_file (installed, filename);
	if (!ret) {
		*error = g_error_new (0, 0, _("Could not write package list"));
		goto out;
	}
out:
	if (system != NULL)
		g_object_unref (system);
	if (installed != NULL)
		g_object_unref (installed);
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

	widget = glade_xml_get_widget (glade_xml, "filechooserbutton_directory");
	directory = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(widget));

	/* use a default filename */
	filename = gpk_pack_get_default_filename (directory);

	/* start the action */
	gpk_pack_widgets_activate (FALSE);
	widget = glade_xml_get_widget (glade_xml, "frame_progress");
	gtk_widget_show (widget);

	/* copy the system package list */
	if (action == GPK_ACTION_ENUM_COPY) {
		ret = gpk_pack_copy_package_lists (filename, &error);
		if (!ret) {
			widget = glade_xml_get_widget (glade_xml, "window_pack");
			gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), _("Cannot copy system package list"), error->message);
			g_error_free (error);
		}
		goto out;
	}

	/* get the exclude list, and fall back to the system copy */
	widget = glade_xml_get_widget (glade_xml, "filechooserbutton_exclude");
	exclude = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(widget));
	if (exclude == NULL)
		exclude = g_strdup (PK_SYSTEM_PACKAGE_LIST_FILENAME);

	/* get the package to download */
	if (action == GPK_ACTION_ENUM_PACKAGE) {
		widget = glade_xml_get_widget (glade_xml, "entry_package");
		package = gtk_entry_get_text (GTK_ENTRY(widget));
		if (egg_strzero (package)) {
			widget = glade_xml_get_widget (glade_xml, "window_pack");
			gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), _("No package name selected"), NULL);
			goto out;
		}
		packages = g_strsplit (package, ",", 0);
		package_ids = gpk_pack_resolve_package_ids (packages);
		if (package_ids == NULL)
			goto out;
	}

	/* add the exclude list */
	list = pk_package_list_new ();
	ret = pk_package_list_add_file (list, exclude);
	if (!ret) {
		widget = glade_xml_get_widget (glade_xml, "window_pack");
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
		widget = glade_xml_get_widget (glade_xml, "window_pack");
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Create error"), _("Cannot create service pack"), error->message);
		g_error_free (error);
	}
	g_object_unref (pack);

out:
	/* stop the action */
	gpk_pack_widgets_activate (TRUE);
	widget = glade_xml_get_widget (glade_xml, "frame_progress");
	gtk_widget_hide (widget);
	gpk_pack_set_percentage (100);

	/* blank */
	widget = glade_xml_get_widget (glade_xml, "progressbar_percentage");
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR(widget), "");

	if (list != NULL)
		g_object_unref (list);
	g_strfreev (packages);
	g_strfreev (package_ids);
	g_free (directory);
	g_free (exclude);
}

/**
 * gpk_pack_activated_cb
 **/
static void
gpk_pack_activated_cb (EggUnique *egg_unique, gpointer data)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (glade_xml, "window_prefs");
	gtk_window_present (GTK_WINDOW (widget));
}

/**
 * gpk_pack_radio_updates_cb:
 **/
static void
gpk_pack_radio_updates_cb (GtkWidget *widget2, gpointer data)
{
	GtkWidget *widget;
	egg_debug ("got updates");
	widget = glade_xml_get_widget (glade_xml, "entry_package");
	gtk_widget_set_sensitive (widget, FALSE);
	widget = glade_xml_get_widget (glade_xml, "filechooserbutton_exclude");
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
	widget = glade_xml_get_widget (glade_xml, "entry_package");
	gtk_widget_set_sensitive (widget, TRUE);
	widget = glade_xml_get_widget (glade_xml, "filechooserbutton_exclude");
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
	widget = glade_xml_get_widget (glade_xml, "entry_package");
	gtk_widget_set_sensitive (widget, FALSE);
	widget = glade_xml_get_widget (glade_xml, "filechooserbutton_exclude");
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
	PkControl *control;
	EggUnique *egg_unique;
	gboolean ret;
	GConfClient *client;
	gchar *option = NULL;
	gchar *package = NULL;
	gchar *with_list = NULL;
	gchar *output = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "option", 'o', 0, G_OPTION_ARG_STRING, &option,
		  _("Preselect the option, allowable values are list, updates and package"), NULL },
		{ "package", 'p', 0, G_OPTION_ARG_STRING, &package,
		  _("Add the package name to the text entry box"), NULL },
		{ "with-list", 'p', 0, G_OPTION_ARG_STRING, &with_list,
		  _("Set the remote package list filename"), NULL },
		{ "output", 'p', 0, G_OPTION_ARG_STRING, &output,
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
	g_option_context_set_summary(context, _("Software Update Preferences"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);
	gtk_init (&argc, &argv);

	/* are we already activated? */
	egg_unique = egg_unique_new ();
	ret = egg_unique_assign (egg_unique, "org.freedesktop.PackageKit.ServicePack");
	if (!ret)
		goto unique_out;
	g_signal_connect (egg_unique, "activated",
			  G_CALLBACK (gpk_pack_activated_cb), NULL);

	/* get actions */
	control = pk_control_new ();
	roles = pk_control_get_actions (control, NULL);
	g_object_unref (control);

	glade_xml = glade_xml_new (GPK_DATA "/gpk-service-pack.glade", NULL, NULL);
	main_window = glade_xml_get_widget (glade_xml, "window_pack");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SOFTWARE_SOURCES);

	/* Get the main window quit */
	g_signal_connect_swapped (main_window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	widget = glade_xml_get_widget (glade_xml, "filechooserbutton_exclude");
	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("Package list files"));
	gtk_file_filter_add_pattern (filter, "*.package-list");
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER(widget), filter);

	widget = glade_xml_get_widget (glade_xml, "filechooserbutton_directory");
	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("Service pack files"));
	gtk_file_filter_add_pattern (filter, "*.servicepack");
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER(widget), filter);

	widget = glade_xml_get_widget (glade_xml, "radiobutton_updates");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_radio_updates_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "radiobutton_package");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_radio_package_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "radiobutton_copy");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_radio_copy_cb), NULL);

	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_create");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_button_create_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_pack_button_help_cb), NULL);

	widget = glade_xml_get_widget (glade_xml, "frame_progress");
	gtk_widget_hide (widget);

	/* autocompletion can be turned off as it's slow */
	client = gconf_client_get_default ();
	ret = gconf_client_get_bool (client, GPK_CONF_AUTOCOMPLETE, NULL);
	if (ret) {
		/* create the completion object */
		completion = gpk_package_entry_completion_new ();
		widget = glade_xml_get_widget (glade_xml, "entry_package");
		gtk_entry_set_completion (GTK_ENTRY (widget), completion);
		g_object_unref (completion);
	}
	g_object_unref (client);

	/* if command line arguments are set, then setup UI */
	if (option != NULL) {
		if (egg_strequal (option, "list")) {
			widget = glade_xml_get_widget (glade_xml, "radiobutton_copy");
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
		} else if (egg_strequal (option, "updates")) {
			widget = glade_xml_get_widget (glade_xml, "radiobutton_updates");
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
		} else if (egg_strequal (option, "package")) {
			widget = glade_xml_get_widget (glade_xml, "radiobutton_package");
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
		}
	}
	if (package != NULL) {
		widget = glade_xml_get_widget (glade_xml, "entry_package");
		gtk_entry_set_text (GTK_ENTRY(widget), package);
	}
	if (with_list != NULL) {
		widget = glade_xml_get_widget (glade_xml, "filechooserbutton_exclude");
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(widget), with_list);
	}
	if (output != NULL) {
		widget = glade_xml_get_widget (glade_xml, "filechooserbutton_directory");
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(widget), output);
	}

	gtk_widget_show (main_window);

	/* wait */
	gtk_main ();

	g_object_unref (glade_xml);
unique_out:
	g_object_unref (egg_unique);
	g_free (option);
	g_free (package);
	g_free (with_list);
	g_free (output);

	return 0;
}
