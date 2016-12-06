/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010-2011 Richard Hughes <richard@hughsie.com>
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
#include <packagekit-glib2/packagekit.h>

#include "gpk-common.h"
#include "gpk-debug.h"
#include "gpk-enum.h"
#include "gpk-error.h"

typedef struct {
	const gchar		*id_tmp;
	GCancellable		*cancellable;
	GSettings		*settings_gpk;
	GtkApplication		*application;
	GtkBuilder		*builder;
	GtkListStore		*list_store;
	GtkTreePath		*path_tmp;
	guint			 status_id;
	PkBitfield		 roles;
	PkClient		*client;
	PkStatusEnum		 status;
} GpkPrefsPrivate;

enum {
	GPK_COLUMN_ENABLED,
	GPK_COLUMN_TEXT,
	GPK_COLUMN_ID,
	GPK_COLUMN_ACTIVE,
	GPK_COLUMN_SENSITIVE,
	GPK_COLUMN_LAST
};

static gboolean
gpk_prefs_find_iter_model_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, GpkPrefsPrivate *priv)
{
	gchar *repo_id_tmp = NULL;
	gtk_tree_model_get (model, iter,
			    GPK_COLUMN_ID, &repo_id_tmp,
			    -1);
	if (strcmp (repo_id_tmp, priv->id_tmp) == 0) {
		priv->path_tmp = gtk_tree_path_copy (path);
		return TRUE;
	}
	return FALSE;
}

static gboolean
gpk_prefs_mark_nonactive_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, GpkPrefsPrivate *priv)
{
	gtk_list_store_set (GTK_LIST_STORE(model), iter,
			    GPK_COLUMN_ACTIVE, FALSE,
			    -1);
	return FALSE;
}

static void
gpk_prefs_mark_nonactive (GpkPrefsPrivate *priv, GtkTreeModel *model)
{
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_prefs_mark_nonactive_cb, priv);
}

static gboolean
gpk_prefs_model_get_iter (GpkPrefsPrivate *priv, GtkTreeModel *model, GtkTreeIter *iter, const gchar *id)
{
	gboolean ret = TRUE;
	priv->id_tmp = id;
	priv->path_tmp = NULL;
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_prefs_find_iter_model_cb, priv);
	if (priv->path_tmp == NULL) {
		gtk_list_store_append (GTK_LIST_STORE(model), iter);
	} else {
		ret = gtk_tree_model_get_iter (model, iter, priv->path_tmp);
		gtk_tree_path_free (priv->path_tmp);
	}
	return ret;
}

static gboolean
gpk_prefs_remove_nonactive_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gboolean *ret)
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

static void
gpk_prefs_remove_nonactive (GtkTreeModel *model)
{
	gboolean ret;
	/* do this again and again as removing in gtk_tree_model_foreach causes errors */
	do {
		ret = FALSE;
		gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_prefs_remove_nonactive_cb, &ret);
	} while (ret);
}

static gboolean
gpk_prefs_status_changed_timeout_cb (GpkPrefsPrivate *priv)
{
	GtkWidget *widget;

	/* set the text and show */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_repo"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_status"));
	gtk_widget_show (widget);

	/* never repeat */
	priv->status_id = 0;
	return FALSE;
}

static void
gpk_prefs_progress_cb (PkProgress *progress, PkProgressType type, GpkPrefsPrivate *priv)
{
	GtkWidget *widget;

	if (type != PK_PROGRESS_TYPE_STATUS)
		return;

	/* get value */
	g_object_get (progress,
		      "status", &priv->status,
		      NULL);
	g_debug ("now %s", pk_status_enum_to_string (priv->status));

	if (priv->status == PK_STATUS_ENUM_FINISHED) {
		/* we've not yet shown, so don't bother */
		if (priv->status_id > 0) {
			g_source_remove (priv->status_id);
			priv->status_id = 0;
		}
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "box_status"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_repo"));
		gtk_widget_show (widget);
		return;
	}

	/* already pending show */
	if (priv->status_id > 0)
		return;

	/* only show after some time in the transaction */
	priv->status_id = g_timeout_add (GPK_UI_STATUS_SHOW_DELAY, (GSourceFunc) gpk_prefs_status_changed_timeout_cb, priv);
	g_source_set_name_by_id (priv->status_id, "[GpkRepo] status");
}

static void
gpk_prefs_repo_enable_cb (GObject *object, GAsyncResult *res, GpkPrefsPrivate *priv)
{
	g_autoptr(GError) error = NULL;
	GtkWindow *window;
	PkClient *client = PK_CLIENT (object);
	g_autoptr(PkError) error_code = NULL;
	PkResults *results = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get set repo: %s", error->message);
		return;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to set repo: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_prefs"));
		/* TRANSLATORS: for one reason or another, we could not enable or disable a package source */
		gpk_error_dialog_modal (window, _("Failed to change status"),
					gpk_error_enum_to_localised_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		return;
	}
}

static void
gpk_misc_enabled_toggled (GtkCellRendererToggle *cell, gchar *path_str, GpkPrefsPrivate *priv)
{
	gboolean enabled;
	g_autofree gchar *repo_id = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	GtkTreeView *treeview;

	/* do we have the capability? */
	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_REPO_ENABLE) == FALSE) {
		g_debug ("can't change state");
		return;
	}

	/* get toggled iter */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_repo"));
	model = gtk_tree_view_get_model (treeview);
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    GPK_COLUMN_ENABLED, &enabled,
			    GPK_COLUMN_ID, &repo_id, -1);
	gtk_tree_path_free (path);

	/* do something with the value */
	enabled ^= 1;

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    GPK_COLUMN_SENSITIVE, FALSE,
			    -1);

	/* set the repo */
	g_debug ("setting %s to %i", repo_id, enabled);
	pk_client_repo_enable_async (priv->client, repo_id, enabled,
				     priv->cancellable,
				     (PkProgressCallback) gpk_prefs_progress_cb, priv,
				     (GAsyncReadyCallback) gpk_prefs_repo_enable_cb, priv);

}

static void
gpk_treeview_add_columns (GpkPrefsPrivate *priv, GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* column for enabled toggles */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (gpk_misc_enabled_toggled), priv);

	/* TRANSLATORS: column if the source is enabled */
	column = gtk_tree_view_column_new_with_attributes (_("Enabled"), renderer,
							   "active", GPK_COLUMN_ENABLED,
							   "sensitive", GPK_COLUMN_SENSITIVE,
							   NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	/* TRANSLATORS: column for the source description */
	column = gtk_tree_view_column_new_with_attributes (_("Package Source"), renderer,
							   "markup", GPK_COLUMN_TEXT,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
}

static void
gpk_repos_treeview_clicked_cb (GtkTreeSelection *selection, GpkPrefsPrivate *priv)
{
	gchar *repo_id;
	GtkTreeIter iter;
	GtkTreeModel *model;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter, GPK_COLUMN_ID, &repo_id, -1);
		g_debug ("selected row is: %s", repo_id);
		g_free (repo_id);
	} else {
		g_debug ("no row selected");
	}
}

static void
gpk_prefs_get_repo_list_cb (GObject *object, GAsyncResult *res, GpkPrefsPrivate *priv)
{
	gboolean enabled;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeView *treeview;
	GtkWindow *window;
	guint i;
	PkClient *client = PK_CLIENT (object);
	g_autoptr(PkError) error_code = NULL;
	PkRepoDetail *item;
	PkResults *results = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get repo list: %s", error->message);
		return;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get repo list: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_prefs"));
		/* TRANSLATORS: for one reason or another, we could not get the list of sources */
		gpk_error_dialog_modal (window, _("Failed to get the list of sources"),
					gpk_error_enum_to_localised_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		return;
	}

	/* add repos */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_repo"));
	model = gtk_tree_view_get_model (treeview);
	array = pk_results_get_repo_detail_array (results);
	for (i = 0; i < array->len; i++) {
		g_autofree gchar *description = NULL;
		g_autofree gchar *repo_id = NULL;
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "repo-id", &repo_id,
			      "description", &description,
			      "enabled", &enabled,
			      NULL);
		g_debug ("repo = %s:%s:%i", repo_id, description, enabled);
		gpk_prefs_model_get_iter (priv, model, &iter, repo_id);
		gtk_list_store_set (priv->list_store, &iter,
				    GPK_COLUMN_ENABLED, enabled,
				    GPK_COLUMN_TEXT, description,
				    GPK_COLUMN_ID, repo_id,
				    GPK_COLUMN_ACTIVE, TRUE,
				    GPK_COLUMN_SENSITIVE, TRUE,
				    -1);
	}

	/* remove the items that are not now present */
	gpk_prefs_remove_nonactive (model);

	/* sort */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(priv->list_store), GPK_COLUMN_TEXT, GTK_SORT_ASCENDING);
}

static void
gpk_prefs_repo_list_refresh (GpkPrefsPrivate *priv)
{
	gboolean show_details;
	GtkTreeModel *model;
	GtkTreeView *treeview;
	GtkWidget *widget;
	PkBitfield filters;

	/* mark the items as not used */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_repo"));
	model = gtk_tree_view_get_model (treeview);
	gpk_prefs_mark_nonactive (priv, model);

	g_debug ("refreshing list");
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_detail"));
	show_details = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	if (!show_details)
		filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT);
	else
		filters = pk_bitfield_value (PK_FILTER_ENUM_NONE);
	pk_client_get_repo_list_async (priv->client, filters,
				       priv->cancellable,
				       (PkProgressCallback) gpk_prefs_progress_cb, priv,
				       (GAsyncReadyCallback) gpk_prefs_get_repo_list_cb, priv);
}

static void
gpk_prefs_repo_list_changed_cb (PkControl *control, GpkPrefsPrivate *priv)
{
	gpk_prefs_repo_list_refresh (priv);
}

static void
gpk_prefs_checkbutton_detail_cb (GtkWidget *widget, GpkPrefsPrivate *priv)
{
	gpk_prefs_repo_list_refresh (priv);
}

static void
gpk_prefs_get_properties_cb (GObject *object, GAsyncResult *res, GpkPrefsPrivate *priv)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	GtkWidget *widget;
	PkControl *control = PK_CONTROL(object);

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	if (!ret) {
		/* TRANSLATORS: backend is broken, and won't tell us what it supports */
		g_print ("%s: %s\n", _("Exiting as backend details could not be retrieved"), error->message);
		return;
	}

	/* get values */
	g_object_get (control,
		      "roles", &priv->roles,
		      NULL);

	/* setup sources GUI elements */
	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_GET_REPO_LIST)) {
		gpk_prefs_repo_list_refresh (priv);
	} else {
		GtkTreeIter iter;
		GtkTreeView *treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_repo"));
		GtkTreeModel *model = gtk_tree_view_get_model (treeview);

		gtk_list_store_append (GTK_LIST_STORE(model), &iter);
		gtk_list_store_set (priv->list_store, &iter,
				    GPK_COLUMN_ENABLED, FALSE,
				    GPK_COLUMN_TEXT, _("Getting package source list not supported by backend"),
				    GPK_COLUMN_ACTIVE, FALSE,
				    GPK_COLUMN_SENSITIVE, FALSE,
				    -1);

		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_repo"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_detail"));
		gtk_widget_set_sensitive (widget, FALSE);
	}
}

static void
gpk_pack_startup_cb (GtkApplication *application, GpkPrefsPrivate *priv)
{
	g_autoptr(GError) error = NULL;
	GtkTreeSelection *selection;
	GtkWidget *main_window;
	GtkWidget *widget;
	guint retval;
	g_autoptr(PkControl) control = NULL;

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   PKGDATADIR G_DIR_SEPARATOR_S "icons");

	/* get actions */
	control = pk_control_new ();
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (gpk_prefs_repo_list_changed_cb), priv);

	/* get UI */
	retval = gtk_builder_add_from_resource (priv->builder,
						"/org/gnome/packagekit/gpk-prefs.ui",
						&error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		return;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "checkbutton_detail"));
	g_settings_bind (priv->settings_gpk,
			 GPK_SETTINGS_REPO_SHOW_DETAILS,
			 widget, "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_prefs_checkbutton_detail_cb), priv);

	/* create repo tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_repo"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (priv->list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_repos_treeview_clicked_cb), priv);

	/* add columns to the tree view */
	gpk_treeview_add_columns (priv, GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "dialog_prefs"));
	gtk_application_add_window (application, GTK_WINDOW (main_window));

	gtk_widget_show (main_window);

	/* get some data */
	pk_control_get_properties_async (control, NULL, (GAsyncReadyCallback) gpk_prefs_get_properties_cb, priv);
}


static int
gpm_prefs_commandline_cb (GApplication *application,
			  GApplicationCommandLine *cmdline,
			  GpkPrefsPrivate *priv)
{
	g_auto(GStrv) argv = NULL;
	gint argc;
	g_autoptr(GOptionContext) context = NULL;
	GtkWindow *window;
	guint xid = 0;

	const GOptionEntry options[] = {
		{ "parent-window", 'p', 0, G_OPTION_ARG_INT, &xid,
		  /* TRANSLATORS: we can make this modal (stay on top of) another window */
		  _("Set the parent window to make this modal"), NULL },
		{ NULL}
	};

	/* get arguments */
	argv = g_application_command_line_get_arguments (cmdline, &argc);

	context = g_option_context_new (NULL);
	/* TRANSLATORS: program name, an application to add and remove software repositories */
	g_option_context_set_summary(context, _("Package Sources"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gpk_debug_get_option_group ());
	if (!g_option_context_parse (context, &argc, &argv, NULL))
		return FALSE;

	/* make sure the window is raised */
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "dialog_prefs"));
	gtk_window_present (window);

	/* set the parent window if it is specified */
	if (xid != 0) {
		g_debug ("Setting xid %u", xid);
		gpk_window_set_parent_xid (window, xid);
	}
	return TRUE;
}

int
main (int argc, char *argv[])
{
	gint status = 0;
	GpkPrefsPrivate *priv = NULL;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	priv = g_new0 (GpkPrefsPrivate, 1);
	priv->cancellable = g_cancellable_new ();
	priv->builder = gtk_builder_new ();
	priv->settings_gpk = g_settings_new (GPK_SETTINGS_SCHEMA);
	priv->list_store = gtk_list_store_new (GPK_COLUMN_LAST, G_TYPE_BOOLEAN,
					       G_TYPE_STRING, G_TYPE_STRING,
					       G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
	priv->client = pk_client_new ();
	g_object_set (priv->client,
		      "background", FALSE,
		      NULL);

	/* are we already activated? */
	priv->application = gtk_application_new ("org.freedesktop.PackageKit.Prefs",
						 G_APPLICATION_HANDLES_COMMAND_LINE);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (gpk_pack_startup_cb), priv);
	g_signal_connect (priv->application, "command-line",
			  G_CALLBACK (gpm_prefs_commandline_cb), priv);

	/* run */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	if (priv != NULL) {
		g_cancellable_cancel (priv->cancellable);
		g_object_unref (priv->cancellable);
		g_object_unref (priv->builder);
		g_object_unref (priv->settings_gpk);
		g_object_unref (priv->list_store);
		g_object_unref (priv->client);
		g_free (priv);
	}
	return status;
}
