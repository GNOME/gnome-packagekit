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
#include <locale.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <string.h>
#include <dbus/dbus-glib.h>
#include <packagekit-glib2/packagekit.h>
#include <unique/unique.h>

#include <gpk-common.h>
#include <gpk-gnome.h>

#include "egg-debug.h"
#include "gpk-enum.h"

/* TRANSLATORS: check once an hour */
#define PK_FREQ_HOURLY_TEXT		_("Hourly")
/* TRANSLATORS: check once a day */
#define PK_FREQ_DAILY_TEXT		_("Daily")
/* TRANSLATORS: check once a week */
#define PK_FREQ_WEEKLY_TEXT		_("Weekly")
/* TRANSLATORS: never check for updates/upgrades */
#define PK_FREQ_NEVER_TEXT		_("Never")

/* TRANSLATORS: update everything */
#define PK_UPDATE_ALL_TEXT		_("All updates")
/* TRANSLATORS: update just security updates */
#define PK_UPDATE_SECURITY_TEXT		_("Only security updates")
/* TRANSLATORS: don't update anything */
#define PK_UPDATE_NONE_TEXT		_("Nothing")

#define GPK_PREFS_VALUE_NEVER		(0)
#define GPK_PREFS_VALUE_HOURLY		(60*60)
#define GPK_PREFS_VALUE_DAILY		(60*60*24)
#define GPK_PREFS_VALUE_WEEKLY		(60*60*24*7)

static GtkBuilder *builder = NULL;
static GSettings *settings = NULL;

/**
 * gpk_prefs_help_cb:
 **/
static void
gpk_prefs_help_cb (GtkWidget *widget, gpointer data)
{
	gpk_gnome_help ("prefs");
}

/**
 * gpk_prefs_update_freq_combo_changed:
 **/
static void
gpk_prefs_update_freq_combo_changed (GtkWidget *widget, gpointer data)
{
	gchar *value;
	guint freq = 0;

	value = gtk_combo_box_get_active_text (GTK_COMBO_BOX (widget));
	if (strcmp (value, PK_FREQ_HOURLY_TEXT) == 0)
		freq = GPK_PREFS_VALUE_HOURLY;
	else if (strcmp (value, PK_FREQ_DAILY_TEXT) == 0)
		freq = GPK_PREFS_VALUE_DAILY;
	else if (strcmp (value, PK_FREQ_WEEKLY_TEXT) == 0)
		freq = GPK_PREFS_VALUE_WEEKLY;
	else if (strcmp (value, PK_FREQ_NEVER_TEXT) == 0)
		freq = GPK_PREFS_VALUE_NEVER;
	else
		g_assert (FALSE);

	egg_debug ("Changing %s to %i", GPK_SETTINGS_FREQUENCY_GET_UPDATES, freq);
	g_settings_set_int (settings, GPK_SETTINGS_FREQUENCY_GET_UPDATES, freq);
	g_free (value);
}

/**
 * gpk_prefs_upgrade_freq_combo_changed:
 **/
static void
gpk_prefs_upgrade_freq_combo_changed (GtkWidget *widget, gpointer data)
{
	gchar *value;
	guint freq = 0;

	value = gtk_combo_box_get_active_text (GTK_COMBO_BOX (widget));
	if (strcmp (value, PK_FREQ_DAILY_TEXT) == 0)
		freq = GPK_PREFS_VALUE_DAILY;
	else if (strcmp (value, PK_FREQ_WEEKLY_TEXT) == 0)
		freq = GPK_PREFS_VALUE_WEEKLY;
	else if (strcmp (value, PK_FREQ_NEVER_TEXT) == 0)
		freq = GPK_PREFS_VALUE_NEVER;
	else
		g_assert (FALSE);

	egg_debug ("Changing %s to %i", GPK_SETTINGS_FREQUENCY_GET_UPGRADES, freq);
	g_settings_set_int (settings, GPK_SETTINGS_FREQUENCY_GET_UPGRADES, freq);
	g_free (value);
}

/**
 * gpk_prefs_update_combo_changed:
 **/
static void
gpk_prefs_update_combo_changed (GtkWidget *widget, gpointer data)
{
	gchar *value;
	const gchar *action;
	GpkUpdateEnum update = GPK_UPDATE_ENUM_UNKNOWN;

	value = gtk_combo_box_get_active_text (GTK_COMBO_BOX (widget));
	if (value == NULL) {
		egg_warning ("value NULL");
		return;
	}
	if (strcmp (value, PK_UPDATE_ALL_TEXT) == 0) {
		update = GPK_UPDATE_ENUM_ALL;
	} else if (strcmp (value, PK_UPDATE_SECURITY_TEXT) == 0) {
		update = GPK_UPDATE_ENUM_SECURITY;
	} else if (strcmp (value, PK_UPDATE_NONE_TEXT) == 0) {
		update = GPK_UPDATE_ENUM_NONE;
	} else {
		g_assert (FALSE);
	}

	action = gpk_update_enum_to_text (update);
	egg_debug ("Changing %s to %s", GPK_SETTINGS_AUTO_UPDATE, action);
	g_settings_set_string (settings, GPK_SETTINGS_AUTO_UPDATE, action);
	g_free (value);
}

/**
 * gpk_prefs_set_combo_model_simple_text:
 **/
static void
gpk_prefs_update_freq_combo_simple_text (GtkWidget *combo_box)
{
	GtkCellRenderer *cell;
	GtkListStore *store;

	store = gtk_list_store_new (1, G_TYPE_STRING);
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo_box), GTK_TREE_MODEL (store));
	g_object_unref (store);

	cell = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), cell, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), cell,
					"text", 0,
					NULL);
}

/**
 * gpk_prefs_update_freq_combo_setup:
 **/
static void
gpk_prefs_update_freq_combo_setup (void)
{
	guint value;
	gboolean is_writable;
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "combobox_check"));
	is_writable = g_settings_is_writable (settings, GPK_SETTINGS_FREQUENCY_GET_UPDATES);
	value = g_settings_get_int (settings, GPK_SETTINGS_FREQUENCY_GET_UPDATES);
	egg_debug ("value from settings %i", value);

	/* do we have permission to write? */
	gtk_widget_set_sensitive (widget, is_writable);

	/* set a simple text model */
	gpk_prefs_update_freq_combo_simple_text (widget);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_HOURLY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_DAILY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_WEEKLY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_NEVER_TEXT);

	/* select the correct entry */
	if (value == GPK_PREFS_VALUE_HOURLY)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
	else if (value == GPK_PREFS_VALUE_DAILY)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 1);
	else if (value == GPK_PREFS_VALUE_WEEKLY)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 2);
	else if (value == GPK_PREFS_VALUE_NEVER)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 3);

	/* only do this after else we redraw the window */
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (gpk_prefs_update_freq_combo_changed), NULL);
}

/**
 * gpk_prefs_upgrade_freq_combo_setup:
 **/
static void
gpk_prefs_upgrade_freq_combo_setup (void)
{
	guint value;
	gboolean is_writable;
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "combobox_upgrade"));
	is_writable = g_settings_is_writable (settings, GPK_SETTINGS_FREQUENCY_GET_UPGRADES);
	value = g_settings_get_int (settings, GPK_SETTINGS_FREQUENCY_GET_UPGRADES);
	egg_debug ("value from settings %i", value);

	/* do we have permission to write? */
	gtk_widget_set_sensitive (widget, is_writable);

	/* set a simple text model */
	gpk_prefs_update_freq_combo_simple_text (widget);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_DAILY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_WEEKLY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_NEVER_TEXT);

	/* select the correct entry */
	if (value == GPK_PREFS_VALUE_DAILY)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
	else if (value == GPK_PREFS_VALUE_WEEKLY)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 1);
	else if (value == GPK_PREFS_VALUE_NEVER)
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 2);

	/* only do this after else we redraw the window */
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (gpk_prefs_upgrade_freq_combo_changed), NULL);
}

/**
 * gpk_prefs_auto_update_combo_setup:
 **/
static void
gpk_prefs_auto_update_combo_setup (void)
{
	gchar *value;
	gboolean is_writable;
	GtkWidget *widget;
	GpkUpdateEnum update;

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "combobox_install"));
	is_writable = g_settings_is_writable (settings, GPK_SETTINGS_AUTO_UPDATE);
	value = g_settings_get_string (settings, GPK_SETTINGS_AUTO_UPDATE);
	if (value == NULL) {
		egg_warning ("invalid schema, please re-install");
		return;
	}
	egg_debug ("value from settings %s", value);
	update = gpk_update_enum_from_text (value);
	g_free (value);

	/* do we have permission to write? */
	gtk_widget_set_sensitive (widget, is_writable);

	/* set a simple text model */
	gpk_prefs_update_freq_combo_simple_text (widget);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_UPDATE_ALL_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_UPDATE_SECURITY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_UPDATE_NONE_TEXT);
	/* we can do this as it's the same order */
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), update);

	/* only do this after else we redraw the window */
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (gpk_prefs_update_combo_changed), NULL);
}

/**
 * gpk_prefs_message_received_cb
 **/
static void
gpk_prefs_message_received_cb (UniqueApp *app, UniqueCommand command, UniqueMessageData *message_data, guint time_ms, gpointer data)
{
	GtkWindow *window;
	if (command == UNIQUE_ACTIVATE) {
		window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_prefs"));
		gtk_window_present (window);
	}
}

/**
 * gpk_prefs_notify_network_state_cb:
 **/
static void
gpk_prefs_notify_network_state_cb (PkControl *control, GParamSpec *pspec, gpointer data)
{
	GtkWidget *widget;
	PkNetworkEnum state;

	/* only show label on mobile broadband */
	g_object_get (control,
		      "network-state", &state,
		      NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_mobile_broadband"));
	if (state == PK_NETWORK_ENUM_MOBILE)
		gtk_widget_show (widget);
	else
		gtk_widget_hide (widget);
}

/**
 * gpk_prefs_get_properties_cb:
 **/
static void
gpk_prefs_get_properties_cb (GObject *object, GAsyncResult *res, GMainLoop *loop)
{
	GtkWidget *widget;
	GError *error = NULL;
	PkControl *control = PK_CONTROL(object);
	gboolean ret;
	PkBitfield roles;
	PkNetworkEnum state;

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	if (!ret) {
		/* TRANSLATORS: backend is broken, and won't tell us what it supports */
		g_print ("%s: %s\n", _("Exiting as backend details could not be retrieved"), error->message);
		g_error_free (error);
		g_main_loop_quit (loop);
		goto out;
	}

	/* get values */
	g_object_get (control,
		      "roles", &roles,
		      "network-state", &state,
		      NULL);

	/* only show label on mobile broadband */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_mobile_broadband"));
	gtk_widget_set_visible (widget, (state == PK_NETWORK_ENUM_MOBILE));

	/* hide if not supported */
	if (!pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_upgrade"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "combobox_upgrade"));
		gtk_widget_hide (widget);
	}
out:
	return;
}

/**
 * gpk_prefs_close_cb:
 **/
static void
gpk_prefs_close_cb (GtkWidget *widget, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	g_main_loop_quit (loop);
}

/**
 * gpk_prefs_delete_event_cb:
 **/
static gboolean
gpk_prefs_delete_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	gpk_prefs_close_cb (widget, data);
	return FALSE;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean program_version = FALSE;
	GOptionContext *context;
	GtkWidget *main_window;
	GtkWidget *widget;
	PkControl *control;
	UniqueApp *unique_app;
	guint retval;
	guint xid = 0;
	GError *error = NULL;
	GMainLoop *loop;

	const GOptionEntry options[] = {
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
		  _("Show the program version and exit"), NULL },
		{ "parent-window", 'p', 0, G_OPTION_ARG_INT, &xid,
		  /* TRANSLATORS: we can make this modal (stay on top of) another window */
		  _("Set the parent window to make this modal"), NULL },
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
	gtk_init (&argc, &argv);

	context = g_option_context_new (NULL);
	/* TRANSLATORS: program name, an application to set per-user policy for updates */
	g_option_context_set_summary(context, _("Software Update Preferences"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, egg_debug_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	/* are we already activated? */
	unique_app = unique_app_new ("org.freedesktop.PackageKit.Prefs", NULL);
	if (unique_app_is_running (unique_app)) {
		egg_debug ("You have another instance running. This program will now close");
		unique_app_send_message (unique_app, UNIQUE_ACTIVATE, NULL);
		goto unique_out;
	}
	g_signal_connect (unique_app, "message-received",
			  G_CALLBACK (gpk_prefs_message_received_cb), NULL);

	/* load settings */
	settings = g_settings_new (GPK_SETTINGS_SCHEMA);

	/* get actions */
	loop = g_main_loop_new (NULL, FALSE);
	control = pk_control_new ();
	g_signal_connect (control, "notify::network-state",
			  G_CALLBACK (gpk_prefs_notify_network_state_cb), NULL);

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-prefs.ui", &error);
	if (retval == 0) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out_build;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_prefs"));

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SOFTWARE_UPDATE_PREFS);
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (gpk_prefs_delete_event_cb), loop);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "checkbutton_mobile_broadband"));
	g_settings_bind (settings,
			 GPK_SETTINGS_CONNECTION_USE_MOBILE,
			 widget, "active",
			 G_SETTINGS_BIND_DEFAULT);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_prefs_close_cb), loop);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_prefs_help_cb), NULL);

	/* update the combo boxes */
	gpk_prefs_update_freq_combo_setup ();
	gpk_prefs_upgrade_freq_combo_setup ();
	gpk_prefs_auto_update_combo_setup ();

	gtk_widget_show (main_window);

	/* set the parent window if it is specified */
	if (xid != 0) {
		egg_debug ("Setting xid %i", xid);
		gpk_window_set_parent_xid (GTK_WINDOW (main_window), xid);
	}

	/* get some data */
	pk_control_get_properties_async (control, NULL, (GAsyncReadyCallback) gpk_prefs_get_properties_cb, loop);

	/* wait */
	g_main_loop_run (loop);

out_build:
	g_main_loop_unref (loop);
	g_object_unref (control);
	g_object_unref (builder);
	g_object_unref (settings);
unique_out:
	g_object_unref (unique_app);

	return 0;
}
