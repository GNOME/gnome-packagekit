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
#include "gpk-error.h"
#include "gpk-animated-icon.h"

struct _CcUpdatePanelPrivate {
	GtkBuilder		*builder;
	GSettings		*settings;
	GtkListStore		*list_store;
	PkClient		*client;
	PkBitfield		 roles;
	GtkTreePath		*path_tmp;
	const gchar		*id_tmp;
	PkStatusEnum		 status;
	GtkWidget		*image_animation;
	guint			 status_id;
};

enum {
	GPK_COLUMN_ENABLED,
	GPK_COLUMN_TEXT,
	GPK_COLUMN_ID,
	GPK_COLUMN_ACTIVE,
	GPK_COLUMN_SENSITIVE,
	GPK_COLUMN_LAST
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
	GpkUpdateEnum update;

	update = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));
	if (update == -1)
		return;
	g_settings_set_enum (panel->priv->settings, GPK_SETTINGS_AUTO_UPDATE, update);
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
			  G_CALLBACK (cc_update_panel_update_freq_combo_changed), panel);
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
			  G_CALLBACK (cc_update_panel_upgrade_freq_combo_changed), panel);
}

/**
 * cc_update_panel_auto_update_combo_setup:
 **/
static void
cc_update_panel_auto_update_combo_setup (CcUpdatePanel *panel)
{
	gboolean is_writable;
	GtkWidget *widget;
	GpkUpdateEnum update;

	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "combobox_install"));
	is_writable = g_settings_is_writable (panel->priv->settings, GPK_SETTINGS_AUTO_UPDATE);
	update = g_settings_get_enum (panel->priv->settings, GPK_SETTINGS_AUTO_UPDATE);

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
			  G_CALLBACK (cc_update_panel_update_combo_changed), panel);
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
 * cc_update_panel_find_iter_model_cb:
 **/
static gboolean
cc_update_panel_find_iter_model_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, CcUpdatePanel *panel)
{
	gchar *repo_id_tmp = NULL;
	gtk_tree_model_get (model, iter,
			    GPK_COLUMN_ID, &repo_id_tmp,
			    -1);
	if (strcmp (repo_id_tmp, panel->priv->id_tmp) == 0) {
		panel->priv->path_tmp = gtk_tree_path_copy (path);
		return TRUE;
	}
	return FALSE;
}

/**
 * cc_update_panel_mark_nonactive_cb:
 **/
static gboolean
cc_update_panel_mark_nonactive_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, CcUpdatePanel *panel)
{
	gtk_list_store_set (GTK_LIST_STORE(model), iter,
			    GPK_COLUMN_ACTIVE, FALSE,
			    -1);
	return FALSE;
}

/**
 * cc_update_panel_mark_nonactive:
 **/
static void
cc_update_panel_mark_nonactive (CcUpdatePanel *panel, GtkTreeModel *model)
{
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) cc_update_panel_mark_nonactive_cb, panel);
}

/**
 * cc_update_panel_model_get_iter:
 **/
static gboolean
cc_update_panel_model_get_iter (CcUpdatePanel *panel, GtkTreeModel *model, GtkTreeIter *iter, const gchar *id)
{
	gboolean ret = TRUE;
	panel->priv->id_tmp = id;
	panel->priv->path_tmp = NULL;
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) cc_update_panel_find_iter_model_cb, panel);
	if (panel->priv->path_tmp == NULL) {
		gtk_list_store_append (GTK_LIST_STORE(model), iter);
	} else {
		ret = gtk_tree_model_get_iter (model, iter, panel->priv->path_tmp);
		gtk_tree_path_free (panel->priv->path_tmp);
	}
	return ret;
}

/**
 * cc_update_panel_remove_nonactive_cb:
 **/
static gboolean
cc_update_panel_remove_nonactive_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gboolean *ret)
{
	gboolean active;
	gtk_tree_model_get (model, iter,
			    GPK_COLUMN_ACTIVE, &active,
			    -1);
	if (!active) {
		*ret = TRUE;
		gtk_list_store_remove (GTK_LIST_STORE(model), iter);
		return TRUE;
	}
	return FALSE;
}

/**
 * cc_update_panel_remove_nonactive:
 **/
static void
cc_update_panel_remove_nonactive (GtkTreeModel *model)
{
	gboolean ret;
	/* do this again and again as removing in gtk_tree_model_foreach causes errors */
	do {
		ret = FALSE;
		gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) cc_update_panel_remove_nonactive_cb, &ret);
	} while (ret);
}

/**
 * cc_update_panel_status_changed_timeout_cb:
 **/
static gboolean
cc_update_panel_status_changed_timeout_cb (CcUpdatePanel *panel)
{
	const gchar *text;
	GtkWidget *widget;

	/* set the text and show */
	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "viewport_animation_preview"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "label_animation"));
	text = gpk_status_enum_to_localised_text (panel->priv->status);
	gtk_label_set_label (GTK_LABEL (widget), text);

	/* set icon */
	gpk_set_animated_icon_from_status (GPK_ANIMATED_ICON (panel->priv->image_animation), panel->priv->status, GTK_ICON_SIZE_LARGE_TOOLBAR);

	/* never repeat */
	panel->priv->status_id = 0;
	return FALSE;
}

/**
 * cc_update_panel_progress_cb:
 **/
static void
cc_update_panel_progress_cb (PkProgress *progress, PkProgressType type, CcUpdatePanel *panel)
{
	GtkWidget *widget;

	if (type != PK_PROGRESS_TYPE_STATUS)
		return;

	/* get value */
	g_object_get (progress,
		      "status", &panel->priv->status,
		      NULL);
	egg_debug ("now %s", pk_status_enum_to_text (panel->priv->status));

	if (panel->priv->status == PK_STATUS_ENUM_FINISHED) {
		/* we've not yet shown, so don't bother */
		if (panel->priv->status_id > 0) {
			g_source_remove (panel->priv->status_id);
			panel->priv->status_id = 0;
		}
		widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "viewport_animation_preview"));
		gtk_widget_hide (widget);
		gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (panel->priv->image_animation), FALSE);
		goto out;
	}

	/* already pending show */
	if (panel->priv->status_id > 0)
		goto out;

	/* only show after some time in the transaction */
	panel->priv->status_id = g_timeout_add (GPK_UI_STATUS_SHOW_DELAY, (GSourceFunc) cc_update_panel_status_changed_timeout_cb, panel);
#if GLIB_CHECK_VERSION(2,25,8)
	g_source_set_name_by_id (panel->priv->status_id, "[GpkRepo] status");
#endif
out:
	return;
}

/**
 * cc_update_panel_process_messages_cb:
 **/
static void
cc_update_panel_process_messages_cb (PkMessage *item, CcUpdatePanel *panel)
{
	GtkWindow *window;
	PkMessageEnum type;
	gchar *details;
	const gchar *title;

	/* get data */
	g_object_get (item,
		      "type", &type,
		      "details", &details,
		      NULL);

	/* show a modal window */
	window = GTK_WINDOW (gtk_builder_get_object (panel->priv->builder, "dialog_prefs"));
	title = gpk_message_enum_to_localised_text (type);
	gpk_error_dialog_modal (window, title, details, NULL);

	g_free (details);
}

/**
 * cc_update_panel_repo_enable_cb
 **/
static void
cc_update_panel_repo_enable_cb (GObject *object, GAsyncResult *res, CcUpdatePanel *panel)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkError *error_code = NULL;
	GtkWindow *window;
	GPtrArray *array;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to get set repo: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to set repo: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		window = GTK_WINDOW (gtk_builder_get_object (panel->priv->builder, "dialog_prefs"));
		/* TRANSLATORS: for one reason or another, we could not enable or disable a software source */
		gpk_error_dialog_modal (window, _("Failed to change status"),
					gpk_error_enum_to_localised_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* process messages */
	array = pk_results_get_message_array (results);
	g_ptr_array_foreach (array, (GFunc) cc_update_panel_process_messages_cb, panel);
	g_ptr_array_unref (array);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

static void
gpk_misc_enabled_toggled (GtkCellRendererToggle *cell, gchar *path_str, CcUpdatePanel *panel)
{
	GtkTreeModel *model;
	GtkTreeView *treeview;
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	gboolean enabled;
	gchar *repo_id = NULL;

	/* do we have the capability? */
	if (pk_bitfield_contain (panel->priv->roles, PK_ROLE_ENUM_REPO_ENABLE) == FALSE) {
		egg_debug ("can't change state");
		goto out;
	}

	/* get toggled iter */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (panel->priv->builder, "treeview_repo"));
	model = gtk_tree_view_get_model (treeview);
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    GPK_COLUMN_ENABLED, &enabled,
			    GPK_COLUMN_ID, &repo_id, -1);

	/* do something with the value */
	enabled ^= 1;

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    GPK_COLUMN_SENSITIVE, FALSE,
			    -1);

	/* set the repo */
	egg_debug ("setting %s to %i", repo_id, enabled);
	pk_client_repo_enable_async (panel->priv->client, repo_id, enabled, NULL,
				     (PkProgressCallback) cc_update_panel_progress_cb, panel,
				     (GAsyncReadyCallback) cc_update_panel_repo_enable_cb, panel);

out:
	/* clean up */
	g_free (repo_id);
	gtk_tree_path_free (path);
}

/**
 * gpk_treeview_add_columns:
 **/
static void
gpk_treeview_add_columns (CcUpdatePanel *panel, GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* column for enabled toggles */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (gpk_misc_enabled_toggled), panel);

	/* TRANSLATORS: column if the source is enabled */
	column = gtk_tree_view_column_new_with_attributes (_("Enabled"), renderer,
							   "active", GPK_COLUMN_ENABLED,
							   "sensitive", GPK_COLUMN_SENSITIVE,
							   NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	/* TRANSLATORS: column for the source description */
	column = gtk_tree_view_column_new_with_attributes (_("Software Source"), renderer,
							   "markup", GPK_COLUMN_TEXT,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * gpk_repos_treeview_clicked_cb:
 **/
static void
gpk_repos_treeview_clicked_cb (GtkTreeSelection *selection, CcUpdatePanel *panel)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *repo_id;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter, GPK_COLUMN_ID, &repo_id, -1);
		egg_debug ("selected row is: %s", repo_id);
		g_free (repo_id);
	} else {
		egg_debug ("no row selected");
	}
}

/**
 * cc_update_panel_get_repo_list_cb
 **/
static void
cc_update_panel_get_repo_list_cb (GObject *object, GAsyncResult *res, CcUpdatePanel *panel)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkError *error_code = NULL;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkWindow *window;
	GPtrArray *array = NULL;
	guint i;
	PkRepoDetail *item;
	GtkTreeIter iter;
	gchar *repo_id;
	gchar *description;
	gboolean enabled;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to get repo list: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to get repo list: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		window = GTK_WINDOW (gtk_builder_get_object (panel->priv->builder, "dialog_prefs"));
		/* TRANSLATORS: for one reason or another, we could not get the list of sources */
		gpk_error_dialog_modal (window, _("Failed to get the list of sources"),
					gpk_error_enum_to_localised_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* add repos */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (panel->priv->builder, "treeview_repo"));
	model = gtk_tree_view_get_model (treeview);
	array = pk_results_get_repo_detail_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "repo-id", &repo_id,
			      "description", &description,
			      "enabled", &enabled,
			      NULL);
		egg_debug ("repo = %s:%s:%i", repo_id, description, enabled);
		cc_update_panel_model_get_iter (panel, model, &iter, repo_id);
		gtk_list_store_set (panel->priv->list_store, &iter,
				    GPK_COLUMN_ENABLED, enabled,
				    GPK_COLUMN_TEXT, description,
				    GPK_COLUMN_ID, repo_id,
				    GPK_COLUMN_ACTIVE, TRUE,
				    GPK_COLUMN_SENSITIVE, TRUE,
				    -1);

		g_free (repo_id);
		g_free (description);
	}

	/* remove the items that are not now present */
	cc_update_panel_remove_nonactive (model);

	/* sort */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(panel->priv->list_store), GPK_COLUMN_TEXT, GTK_SORT_ASCENDING);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * cc_update_panel_repo_list_refresh:
 **/
static void
cc_update_panel_repo_list_refresh (CcUpdatePanel *panel)
{
	PkBitfield filters;
	GtkWidget *widget;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	gboolean show_details;

	/* mark the items as not used */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (panel->priv->builder, "treeview_repo"));
	model = gtk_tree_view_get_model (treeview);
	cc_update_panel_mark_nonactive (panel, model);

	egg_debug ("refreshing list");
	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "checkbutton_detail"));
	show_details = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	if (!show_details)
		filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT);
	else
		filters = pk_bitfield_value (PK_FILTER_ENUM_NONE);
	pk_client_get_repo_list_async (panel->priv->client, filters, NULL,
				       (PkProgressCallback) cc_update_panel_progress_cb, panel,
				       (GAsyncReadyCallback) cc_update_panel_get_repo_list_cb, panel);
}

/**
 * cc_update_panel_repo_list_changed_cb:
 **/
static void
cc_update_panel_repo_list_changed_cb (PkControl *control, CcUpdatePanel *panel)
{
	cc_update_panel_repo_list_refresh (panel);
}

/**
 * cc_update_panel_checkbutton_detail_cb:
 **/
static void
cc_update_panel_checkbutton_detail_cb (GtkWidget *widget, CcUpdatePanel *panel)
{
	cc_update_panel_repo_list_refresh (panel);
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
		      "roles", &panel->priv->roles,
		      "network-state", &state,
		      NULL);

	/* only show label on mobile broadband */
	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "hbox_mobile_broadband"));
	gtk_widget_set_visible (widget, (state == PK_NETWORK_ENUM_MOBILE));

	/* hide if not supported */
	if (!pk_bitfield_contain (panel->priv->roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "label_upgrade"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "combobox_upgrade"));
		gtk_widget_hide (widget);
	}

	/* setup sources GUI elements */
	if (pk_bitfield_contain (panel->priv->roles, PK_ROLE_ENUM_GET_REPO_LIST)) {
		cc_update_panel_repo_list_refresh (panel);
	} else {
		GtkTreeIter iter;
		GtkTreeView *treeview = GTK_TREE_VIEW (gtk_builder_get_object (panel->priv->builder, "treeview_repo"));
		GtkTreeModel *model = gtk_tree_view_get_model (treeview);

		gtk_list_store_append (GTK_LIST_STORE(model), &iter);
		gtk_list_store_set (panel->priv->list_store, &iter,
				    GPK_COLUMN_ENABLED, FALSE,
				    GPK_COLUMN_TEXT, _("Getting software source list not supported by backend"),
				    GPK_COLUMN_ACTIVE, FALSE,
				    GPK_COLUMN_SENSITIVE, FALSE,
				    -1);

		widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "treeview_repo"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "checkbutton_detail"));
		gtk_widget_set_sensitive (widget, FALSE);
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
	g_object_unref (panel->priv->list_store);
	g_object_unref (panel->priv->client);
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
	GtkTreeSelection *selection;
	GtkBox *box;

	panel->priv = CC_UPDATE_PREFS_GET_PRIVATE (panel);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
                                           GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* load settings */
	panel->priv->settings = g_settings_new (GPK_SETTINGS_SCHEMA);

	/* get actions */
	control = pk_control_new ();
	g_signal_connect (control, "notify::network-state",
			  G_CALLBACK (cc_update_panel_notify_network_state_cb), panel);
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (cc_update_panel_repo_list_changed_cb), panel);

	panel->priv->client = pk_client_new ();
	g_object_set (panel->priv->client,
		      "background", FALSE,
		      NULL);

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

	/* add animated widget */
	panel->priv->image_animation = gpk_animated_icon_new ();
	box = GTK_BOX (gtk_builder_get_object (panel->priv->builder, "hbox_animation"));
	gtk_box_pack_start (box, panel->priv->image_animation, FALSE, FALSE, 0);
	gtk_box_reorder_child (box, panel->priv->image_animation, 0);
	gtk_widget_show (panel->priv->image_animation);

	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "checkbutton_detail"));
	g_settings_bind (panel->priv->settings,
			 GPK_SETTINGS_REPO_SHOW_DETAILS,
			 widget, "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (cc_update_panel_checkbutton_detail_cb), panel);

	/* create list stores */
	panel->priv->list_store = gtk_list_store_new (GPK_COLUMN_LAST, G_TYPE_BOOLEAN,
						      G_TYPE_STRING, G_TYPE_STRING,
						      G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);

	/* create repo tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (panel->priv->builder, "treeview_repo"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (panel->priv->list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_repos_treeview_clicked_cb), panel);

	/* add columns to the tree view */
	gpk_treeview_add_columns (panel, GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

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
