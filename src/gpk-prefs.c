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
#include <locale.h>

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <dbus/dbus-glib.h>
#include <gconf/gconf-client.h>
#include <packagekit-glib/packagekit.h>

#include <gpk-common.h>
#include <gpk-gnome.h>

#include "egg-debug.h"
#include "egg-unique.h"
#include "gpk-enum.h"

#define PK_FREQ_HOURLY_TEXT		_("Hourly")
#define PK_FREQ_DAILY_TEXT		_("Daily")
#define PK_FREQ_WEEKLY_TEXT		_("Weekly")
#define PK_FREQ_NEVER_TEXT		_("Never")

#define PK_UPDATE_ALL_TEXT		_("All updates")
#define PK_UPDATE_SECURITY_TEXT		_("Only security updates")
#define PK_UPDATE_NONE_TEXT		_("Nothing")

static GladeXML *glade_xml = NULL;

/**
 * pk_button_help_cb:
 **/
static void
pk_button_help_cb (GtkWidget *widget, gpointer data)
{
	gpk_gnome_help ("prefs");
}

/**
 * pk_button_checkbutton_clicked_cb:
 **/
static void
pk_button_checkbutton_clicked_cb (GtkWidget *widget, gpointer data)
{
	gboolean checked;
	GConfClient *client;
	const gchar *gconf_key;

	client = gconf_client_get_default ();
	checked = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

	gconf_key = (const char *) g_object_get_data (G_OBJECT (widget), "gconf_key");
	egg_debug ("Changing %s to %i", gconf_key, checked);
	gconf_client_set_bool (client, gconf_key, checked, NULL);

	g_object_unref (client);
}

/**
 * pk_prefs_update_freq_combo_changed:
 **/
static void
pk_prefs_update_freq_combo_changed (GtkWidget *widget, gpointer data)
{
	gchar *value;
	const gchar *action;
	GpkFreqEnum freq = GPK_FREQ_ENUM_UNKNOWN;
	GConfClient *client;

	client = gconf_client_get_default ();
	value = gtk_combo_box_get_active_text (GTK_COMBO_BOX (widget));
	if (strcmp (value, PK_FREQ_HOURLY_TEXT) == 0) {
		freq = GPK_FREQ_ENUM_HOURLY;
	} else if (strcmp (value, PK_FREQ_DAILY_TEXT) == 0) {
		freq = GPK_FREQ_ENUM_DAILY;
	} else if (strcmp (value, PK_FREQ_WEEKLY_TEXT) == 0) {
		freq = GPK_FREQ_ENUM_WEEKLY;
	} else if (strcmp (value, PK_FREQ_NEVER_TEXT) == 0) {
		freq = GPK_FREQ_ENUM_NEVER;
	} else {
		g_assert (FALSE);
	}

	action = gpk_freq_enum_to_text (freq);
	egg_debug ("Changing %s to %s", GPK_CONF_FREQUENCY_GET_UPDATES, action);
	gconf_client_set_string (client, GPK_CONF_FREQUENCY_GET_UPDATES, action, NULL);
	g_free (value);
	g_object_unref (client);
}

/**
 * pk_prefs_upgrade_freq_combo_changed:
 **/
static void
pk_prefs_upgrade_freq_combo_changed (GtkWidget *widget, gpointer data)
{
	gchar *value;
	const gchar *action;
	GpkFreqEnum freq = GPK_FREQ_ENUM_UNKNOWN;
	GConfClient *client;

	client = gconf_client_get_default ();
	value = gtk_combo_box_get_active_text (GTK_COMBO_BOX (widget));
	if (strcmp (value, PK_FREQ_DAILY_TEXT) == 0) {
		freq = GPK_FREQ_ENUM_DAILY;
	} else if (strcmp (value, PK_FREQ_WEEKLY_TEXT) == 0) {
		freq = GPK_FREQ_ENUM_WEEKLY;
	} else if (strcmp (value, PK_FREQ_NEVER_TEXT) == 0) {
		freq = GPK_FREQ_ENUM_NEVER;
	} else {
		g_assert (FALSE);
	}

	action = gpk_freq_enum_to_text (freq);
	egg_debug ("Changing %s to %s", GPK_CONF_FREQUENCY_GET_UPGRADES, action);
	gconf_client_set_string (client, GPK_CONF_FREQUENCY_GET_UPGRADES, action, NULL);
	g_free (value);
	g_object_unref (client);
}

/**
 * pk_prefs_update_combo_changed:
 **/
static void
pk_prefs_update_combo_changed (GtkWidget *widget, gpointer data)
{
	gchar *value;
	const gchar *action;
	GpkUpdateEnum update = GPK_UPDATE_ENUM_UNKNOWN;
	GConfClient *client;
	GtkWidget *notify_widget;

	client = gconf_client_get_default ();
	value = gtk_combo_box_get_active_text (GTK_COMBO_BOX (widget));
	if (value == NULL) {
		egg_warning ("value NULL");
		return;
	}
	notify_widget = glade_xml_get_widget (glade_xml, "checkbutton_notify_updates");
	if (strcmp (value, PK_UPDATE_ALL_TEXT) == 0) {
		update = GPK_UPDATE_ENUM_ALL;
		gtk_widget_set_sensitive (notify_widget, FALSE);
	} else if (strcmp (value, PK_UPDATE_SECURITY_TEXT) == 0) {
		update = GPK_UPDATE_ENUM_SECURITY;
		gtk_widget_set_sensitive (notify_widget, TRUE);
	} else if (strcmp (value, PK_UPDATE_NONE_TEXT) == 0) {
		update = GPK_UPDATE_ENUM_NONE;
		gtk_widget_set_sensitive (notify_widget, TRUE);
	} else {
		g_assert (FALSE);
	}

	action = gpk_update_enum_to_text (update);
	egg_debug ("Changing %s to %s", GPK_CONF_AUTO_UPDATE, action);
	gconf_client_set_string (client, GPK_CONF_AUTO_UPDATE, action, NULL);
	g_free (value);
	g_object_unref (client);
}

/**
 * pk_prefs_update_freq_combo_setup:
 **/
static void
pk_prefs_update_freq_combo_setup (void)
{
	gchar *value;
	gboolean is_writable;
	GtkWidget *widget;
	GpkFreqEnum freq;
	GConfClient *client;

	client = gconf_client_get_default ();
	widget = glade_xml_get_widget (glade_xml, "combobox_check");
	is_writable = gconf_client_key_is_writable (client, GPK_CONF_FREQUENCY_GET_UPDATES, NULL);
	value = gconf_client_get_string (client, GPK_CONF_FREQUENCY_GET_UPDATES, NULL);
	if (value == NULL) {
		egg_warning ("invalid schema, please re-install");
		return;
	}
	egg_debug ("value from gconf %s", value);
	freq = gpk_freq_enum_from_text (value);
	g_free (value);
	g_object_unref (client);

	/* do we have permission to write? */
	gtk_widget_set_sensitive (widget, is_writable);

	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_HOURLY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_DAILY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_WEEKLY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_NEVER_TEXT);
	/* we can do this as it's the same order */
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), freq);

	/* only do this after else we redraw the window */
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (pk_prefs_update_freq_combo_changed), NULL);
}

/**
 * pk_prefs_upgrade_freq_combo_setup:
 **/
static void
pk_prefs_upgrade_freq_combo_setup (void)
{
	gchar *value;
	gboolean is_writable;
	GtkWidget *widget;
	GpkFreqEnum freq;
	GConfClient *client;

	client = gconf_client_get_default ();
	widget = glade_xml_get_widget (glade_xml, "combobox_upgrade");
	is_writable = gconf_client_key_is_writable (client, GPK_CONF_FREQUENCY_GET_UPGRADES, NULL);
	value = gconf_client_get_string (client, GPK_CONF_FREQUENCY_GET_UPGRADES, NULL);
	if (value == NULL) {
		egg_warning ("invalid schema, please re-install");
		return;
	}
	egg_debug ("value from gconf %s", value);
	freq = gpk_freq_enum_from_text (value);
	g_free (value);
	g_object_unref (client);

	/* do we have permission to write? */
	gtk_widget_set_sensitive (widget, is_writable);

	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_DAILY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_WEEKLY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_FREQ_NEVER_TEXT);
	/* don't do daily */
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), freq - 1);

	/* only do this after else we redraw the window */
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (pk_prefs_upgrade_freq_combo_changed), NULL);
}

/**
 * pk_prefs_auto_update_combo_setup:
 **/
static void
pk_prefs_auto_update_combo_setup (void)
{
	gchar *value;
	gboolean is_writable;
	GtkWidget *widget;
	GpkUpdateEnum update;
	GConfClient *client;

	client = gconf_client_get_default ();
	widget = glade_xml_get_widget (glade_xml, "combobox_install");
	is_writable = gconf_client_key_is_writable (client, GPK_CONF_AUTO_UPDATE, NULL);
	value = gconf_client_get_string (client, GPK_CONF_AUTO_UPDATE, NULL);
	if (value == NULL) {
		egg_warning ("invalid schema, please re-install");
		return;
	}
	egg_debug ("value from gconf %s", value);
	update = gpk_update_enum_from_text (value);
	g_free (value);
	g_object_unref (client);

	/* do we have permission to write? */
	gtk_widget_set_sensitive (widget, is_writable);

	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_UPDATE_ALL_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_UPDATE_SECURITY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_UPDATE_NONE_TEXT);
	/* we can do this as it's the same order */
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), update);

	/* only do this after else we redraw the window */
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (pk_prefs_update_combo_changed), NULL);
}

/**
 * pk_prefs_notify_checkbutton_setup:
 **/
static void
pk_prefs_notify_checkbutton_setup (GtkWidget *widget, const gchar *gconf_key)
{
	GConfClient *client;
	gboolean value;

	client = gconf_client_get_default ();
	value = gconf_client_get_bool (client, gconf_key, NULL);
	egg_debug ("value from gconf %i for %s", value, gconf_key);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), value);

	g_object_set_data (G_OBJECT (widget), "gconf_key", (gpointer) gconf_key);
	g_signal_connect (widget, "clicked", G_CALLBACK (pk_button_checkbutton_clicked_cb), NULL);
	g_object_unref (client);
}

/**
 * gpk_prefs_activated_cb
 **/
static void
gpk_prefs_activated_cb (EggUnique *egg_unique, gpointer data)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (glade_xml, "window_prefs");
	gtk_window_present (GTK_WINDOW (widget));
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	GOptionContext *context;
	GtkWidget *main_window;
	GtkWidget *widget;
	PkBitfield roles;
	PkControl *control;
	EggUnique *egg_unique;
	gboolean ret;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
		  _("Show the program version and exit"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (NULL);
	g_option_context_set_summary(context, _("Software Update Preferences"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	egg_debug_init (verbose);
	gtk_init (&argc, &argv);

	/* are we already activated? */
	egg_unique = egg_unique_new ();
	ret = egg_unique_assign (egg_unique, "org.freedesktop.PackageKit.Prefs");
	if (!ret) {
		goto unique_out;
	}
	g_signal_connect (egg_unique, "activated",
			  G_CALLBACK (gpk_prefs_activated_cb), NULL);

	/* get actions */
	control = pk_control_new ();
	roles = pk_control_get_actions (control, NULL);
	g_object_unref (control);

	glade_xml = glade_xml_new (GPK_DATA "/gpk-prefs.glade", NULL, NULL);
	main_window = glade_xml_get_widget (glade_xml, "window_prefs");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SOFTWARE_UPDATE_PREFS);

	/* Get the main window quit */
	g_signal_connect_swapped (main_window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	widget = glade_xml_get_widget (glade_xml, "checkbutton_notify_updates");
	pk_prefs_notify_checkbutton_setup (widget, GPK_CONF_NOTIFY_AVAILABLE);

	widget = glade_xml_get_widget (glade_xml, "checkbutton_notify_completed");
	pk_prefs_notify_checkbutton_setup (widget, GPK_CONF_NOTIFY_COMPLETED);

	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_help_cb), NULL);

	/* update the combo boxes */
	pk_prefs_update_freq_combo_setup ();
	pk_prefs_upgrade_freq_combo_setup ();
	pk_prefs_auto_update_combo_setup ();

	/* hide if not supported */
	if (!pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		widget = glade_xml_get_widget (glade_xml, "label_upgrade");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "combobox_upgrade");
		gtk_widget_hide (widget);
	}

	gtk_widget_show (main_window);

	/* wait */
	gtk_main ();

	g_object_unref (glade_xml);
unique_out:
	g_object_unref (egg_unique);

	return 0;
}
