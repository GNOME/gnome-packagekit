/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-debug.h"

#include "cc-update-panel.h"

#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-enum.h"

struct _CcUpdatePanelPrivate {
	GtkBuilder		*builder;
	GSettings		*settings;
};

G_DEFINE_DYNAMIC_TYPE (CcUpdatePanel, cc_update_panel, CC_TYPE_PANEL)

static void cc_update_panel_finalize (GObject *object);

#define CC_UPDATE_PREFS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_UPDATE_PANEL, CcUpdatePanelPrivate))


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

/**
 * cc_update_panel_help_cb:
 **/
static void
cc_update_panel_help_cb (GtkWidget *widget, CcUpdatePanel *panel)
{
	gpk_gnome_help ("prefs");
}

/**
 * cc_update_panel_update_freq_combo_changed:
 **/
static void
cc_update_panel_update_freq_combo_changed (GtkWidget *widget, CcUpdatePanel *panel)
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
	g_settings_set_int (panel->priv->settings, GPK_SETTINGS_FREQUENCY_GET_UPDATES, freq);
	g_free (value);
}

/**
 * cc_update_panel_upgrade_freq_combo_changed:
 **/
static void
cc_update_panel_upgrade_freq_combo_changed (GtkWidget *widget, CcUpdatePanel *panel)
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
	g_settings_set_int (panel->priv->settings, GPK_SETTINGS_FREQUENCY_GET_UPGRADES, freq);
	g_free (value);
}

/**
 * cc_update_panel_update_combo_changed:
 **/
static void
cc_update_panel_update_combo_changed (GtkWidget *widget, CcUpdatePanel *panel)
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
	g_settings_set_string (panel->priv->settings, GPK_SETTINGS_AUTO_UPDATE, action);
	g_free (value);
}

/**
 * cc_update_panel_set_combo_model_simple_text:
 **/
static void
cc_update_panel_update_freq_combo_simple_text (GtkWidget *combo_box)
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
 * cc_update_panel_update_freq_combo_setup:
 **/
static void
cc_update_panel_update_freq_combo_setup (CcUpdatePanel *panel)
{
	guint value;
	gboolean is_writable;
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "combobox_check"));
	is_writable = g_settings_is_writable (panel->priv->settings, GPK_SETTINGS_FREQUENCY_GET_UPDATES);
	value = g_settings_get_int (panel->priv->settings, GPK_SETTINGS_FREQUENCY_GET_UPDATES);
	egg_debug ("value from settings %i", value);

	/* do we have permission to write? */
	gtk_widget_set_sensitive (widget, is_writable);

	/* set a simple text model */
	cc_update_panel_update_freq_combo_simple_text (widget);
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
			  G_CALLBACK (cc_update_panel_update_freq_combo_changed), NULL);
}

/**
 * cc_update_panel_upgrade_freq_combo_setup:
 **/
static void
cc_update_panel_upgrade_freq_combo_setup (CcUpdatePanel *panel)
{
	guint value;
	gboolean is_writable;
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "combobox_upgrade"));
	is_writable = g_settings_is_writable (panel->priv->settings, GPK_SETTINGS_FREQUENCY_GET_UPGRADES);
	value = g_settings_get_int (panel->priv->settings, GPK_SETTINGS_FREQUENCY_GET_UPGRADES);
	egg_debug ("value from settings %i", value);

	/* do we have permission to write? */
	gtk_widget_set_sensitive (widget, is_writable);

	/* set a simple text model */
	cc_update_panel_update_freq_combo_simple_text (widget);
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
			  G_CALLBACK (cc_update_panel_upgrade_freq_combo_changed), NULL);
}

/**
 * cc_update_panel_auto_update_combo_setup:
 **/
static void
cc_update_panel_auto_update_combo_setup (CcUpdatePanel *panel)
{
	gchar *value;
	gboolean is_writable;
	GtkWidget *widget;
	GpkUpdateEnum update;

	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "combobox_install"));
	is_writable = g_settings_is_writable (panel->priv->settings, GPK_SETTINGS_AUTO_UPDATE);
	value = g_settings_get_string (panel->priv->settings, GPK_SETTINGS_AUTO_UPDATE);
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
	cc_update_panel_update_freq_combo_simple_text (widget);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_UPDATE_ALL_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_UPDATE_SECURITY_TEXT);
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), PK_UPDATE_NONE_TEXT);
	/* we can do this as it's the same order */
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), update);

	/* only do this after else we redraw the window */
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (cc_update_panel_update_combo_changed), NULL);
}

/**
 * cc_update_panel_notify_network_state_cb:
 **/
static void
cc_update_panel_notify_network_state_cb (PkControl *control, GParamSpec *pspec, CcUpdatePanel *panel)
{
	GtkWidget *widget;
	PkNetworkEnum state;

	/* only show label on mobile broadband */
	g_object_get (control,
		      "network-state", &state,
		      NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "hbox_mobile_broadband"));
	if (state == PK_NETWORK_ENUM_MOBILE)
		gtk_widget_show (widget);
	else
		gtk_widget_hide (widget);
}

/**
 * cc_update_panel_get_properties_cb:
 **/
static void
cc_update_panel_get_properties_cb (GObject *object, GAsyncResult *res, CcUpdatePanel *panel)
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
		goto out;
	}

	/* get values */
	g_object_get (control,
		      "roles", &roles,
		      "network-state", &state,
		      NULL);

	/* only show label on mobile broadband */
	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "hbox_mobile_broadband"));
	gtk_widget_set_visible (widget, (state == PK_NETWORK_ENUM_MOBILE));

	/* hide if not supported */
	if (!pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "label_upgrade"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "combobox_upgrade"));
		gtk_widget_hide (widget);
	}
out:
	return;
}

static void
cc_update_panel_class_init (CcUpdatePanelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	g_type_class_add_private (klass, sizeof (CcUpdatePanelPrivate));
	object_class->finalize = cc_update_panel_finalize;
}

static void
cc_update_panel_class_finalize (CcUpdatePanelClass *klass)
{
}

static void
cc_update_panel_finalize (GObject *object)
{
	CcUpdatePanel *panel = CC_UPDATE_PANEL (object);
	g_object_unref (panel->priv->builder);
	g_object_unref (panel->priv->settings);
	G_OBJECT_CLASS (cc_update_panel_parent_class)->finalize (object);
}

static void
cc_update_panel_init (CcUpdatePanel *panel)
{
	GtkWidget *main_window;
	GtkWidget *widget;
	PkControl *control;
	guint retval;
	GError *error = NULL;

	panel->priv = CC_UPDATE_PREFS_GET_PRIVATE (panel);

	/* load settings */
	panel->priv->settings = g_settings_new (GPK_SETTINGS_SCHEMA);

	/* get actions */
	control = pk_control_new ();
	g_signal_connect (control, "notify::network-state",
			  G_CALLBACK (cc_update_panel_notify_network_state_cb), panel);

	/* get UI */
	panel->priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (panel->priv->builder, GPK_DATA "/gpk-prefs.ui", &error);
	if (retval == 0) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "checkbutton_mobile_broadband"));
	g_settings_bind (panel->priv->settings,
			 GPK_SETTINGS_CONNECTION_USE_MOBILE,
			 widget, "active",
			 G_SETTINGS_BIND_DEFAULT);

	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (cc_update_panel_help_cb), panel);

	/* update the combo boxes */
	cc_update_panel_update_freq_combo_setup (panel);
	cc_update_panel_upgrade_freq_combo_setup (panel);
	cc_update_panel_auto_update_combo_setup (panel);

	/* get some data */
	pk_control_get_properties_async (control, NULL, (GAsyncReadyCallback) cc_update_panel_get_properties_cb, panel);
out:
	g_object_unref (control);
	main_window = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "dialog_prefs"));
	widget = gtk_dialog_get_content_area (GTK_DIALOG (main_window));
	gtk_widget_unparent (widget);

	gtk_container_add (GTK_CONTAINER (panel), widget);
}

void
cc_update_panel_register (GIOModule *module)
{
	cc_update_panel_register_type (G_TYPE_MODULE (module));
	g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
					CC_TYPE_UPDATE_PANEL,
					"update", 0);
}

/* GIO extension stuff */
void
g_io_module_load (GIOModule *module)
{
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	/* register the panel */
	cc_update_panel_register (module);
}

void
g_io_module_unload (GIOModule *module)
{
}
