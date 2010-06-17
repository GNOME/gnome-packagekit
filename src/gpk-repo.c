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

#include <glib/gi18n.h>
#include <locale.h>

#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-debug.h"

#include "gpk-gnome.h"
#include "gpk-common.h"
#include "gpk-error.h"
#include "gpk-animated-icon.h"
#include "gpk-enum.h"

static GtkBuilder *builder = NULL;
static GtkListStore *list_store = NULL;
static PkClient *client = NULL;
static PkBitfield roles;
static GSettings *settings;
static GtkTreePath *path_global = NULL;
static GtkWidget *image_animation = NULL;
static guint status_id = 0;

enum {
	REPO_COLUMN_ENABLED,
	REPO_COLUMN_TEXT,
	REPO_COLUMN_ID,
	REPO_COLUMN_ACTIVE,
	REPO_COLUMN_SENSITIVE,
	REPO_COLUMN_LAST
};

/**
 * gpk_repo_find_iter_model_cb:
 **/
static gboolean
gpk_repo_find_iter_model_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, const gchar *repo_id)
{
	gchar *repo_id_tmp = NULL;
	gtk_tree_model_get (model, iter, REPO_COLUMN_ID, &repo_id_tmp, -1);
	if (strcmp (repo_id_tmp, repo_id) == 0) {
		path_global = gtk_tree_path_copy (path);
		return TRUE;
	}
	return FALSE;
}

/**
 * gpk_repo_mark_nonactive_cb:
 **/
static gboolean
gpk_repo_mark_nonactive_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	gtk_list_store_set (GTK_LIST_STORE(model), iter, REPO_COLUMN_ACTIVE, FALSE, -1);
	return FALSE;
}

/**
 * gpk_repo_mark_nonactive:
 **/
static void
gpk_repo_mark_nonactive (GtkTreeModel *model)
{
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_repo_mark_nonactive_cb, NULL);
}

/**
 * gpk_repo_model_get_iter:
 **/
static gboolean
gpk_repo_model_get_iter (GtkTreeModel *model, GtkTreeIter *iter, const gchar *id)
{
	gboolean ret = TRUE;
	path_global = NULL;
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_repo_find_iter_model_cb, (gpointer) id);
	if (path_global == NULL) {
		gtk_list_store_append (GTK_LIST_STORE(model), iter);
	} else {
		ret = gtk_tree_model_get_iter (model, iter, path_global);
		gtk_tree_path_free (path_global);
	}
	return ret;
}

/**
 * gpk_repo_remove_nonactive_cb:
 **/
static gboolean
gpk_repo_remove_nonactive_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gboolean *ret)
{
	gboolean active;
	gtk_tree_model_get (model, iter, REPO_COLUMN_ACTIVE, &active, -1);
	if (!active) {
		*ret = TRUE;
		gtk_list_store_remove (GTK_LIST_STORE(model), iter);
		return TRUE;
	}
	return FALSE;
}

/**
 * gpk_repo_remove_nonactive:
 **/
static void
gpk_repo_remove_nonactive (GtkTreeModel *model)
{
	gboolean ret;
	/* do this again and again as removing in gtk_tree_model_foreach causes errors */
	do {
		ret = FALSE;
		gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_repo_remove_nonactive_cb, &ret);
	} while (ret);
}

/**
 * gpk_button_help_cb:
 **/
static void
gpk_button_help_cb (GtkWidget *widget, gboolean  data)
{
	gpk_gnome_help ("software-sources");
}

/**
 * gpk_repo_status_changed_timeout_cb:
 **/
static gboolean
gpk_repo_status_changed_timeout_cb (PkProgress *progress)
{
	const gchar *text;
	GtkWidget *widget;
	PkStatusEnum status;

	/* get the last status */
	g_object_get (progress,
		      "status", &status,
		      NULL);

	/* set the text and show */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "viewport_animation_preview"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_animation"));
	text = gpk_status_enum_to_localised_text (status);
	gtk_label_set_label (GTK_LABEL (widget), text);

	/* set icon */
	gpk_set_animated_icon_from_status (GPK_ANIMATED_ICON (image_animation), status, GTK_ICON_SIZE_LARGE_TOOLBAR);

	/* never repeat */
	status_id = 0;
	return FALSE;
}

/**
 * gpk_repo_progress_cb:
 **/
static void
gpk_repo_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
{
	PkStatusEnum status;
	GtkWidget *widget;

	if (type != PK_PROGRESS_TYPE_STATUS)
		return;

	/* get value */
	g_object_get (progress,
		      "status", &status,
		      NULL);
	egg_debug ("now %s", pk_status_enum_to_text (status));

	if (status == PK_STATUS_ENUM_FINISHED) {
		/* we've not yet shown, so don't bother */
		if (status_id > 0) {
			g_source_remove (status_id);
			status_id = 0;
		}
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "viewport_animation_preview"));
		gtk_widget_hide (widget);
		gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (image_animation), FALSE);
		goto out;
	}

	/* already pending show */
	if (status_id > 0)
		goto out;

	/* only show after some time in the transaction */
	status_id = g_timeout_add (GPK_UI_STATUS_SHOW_DELAY, (GSourceFunc) gpk_repo_status_changed_timeout_cb, progress);
#if GLIB_CHECK_VERSION(2,25,8)
	g_source_set_name_by_id (status_id, "[GpkRepo] status");
#endif
out:
	return;
}

/**
 * gpk_repo_process_messages_cb:
 **/
static void
gpk_repo_process_messages_cb (PkMessage *item, gpointer user_data)
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
	window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_repo"));
	title = gpk_message_enum_to_localised_text (type);
	gpk_error_dialog_modal (window, title, details, NULL);

	g_free (details);
}

/**
 * gpk_repo_repo_enable_cb
 **/
static void
gpk_repo_repo_enable_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
//	PkClient *client = PK_CLIENT (object);
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
		window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_repo"));
		/* TRANSLATORS: for one reason or another, we could not enable or disable a software source */
		gpk_error_dialog_modal (window, _("Failed to change status"),
					gpk_error_enum_to_localised_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* process messages */
	array = pk_results_get_message_array (results);
	g_ptr_array_foreach (array, (GFunc) gpk_repo_process_messages_cb, NULL);
	g_ptr_array_unref (array);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

static void
gpk_misc_enabled_toggled (GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	GtkTreeModel *model = (GtkTreeModel *)data;
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	gboolean enabled;
	gchar *repo_id = NULL;

	/* do we have the capability? */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REPO_ENABLE) == FALSE) {
		egg_debug ("can't change state");
		goto out;
	}

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    REPO_COLUMN_ENABLED, &enabled,
			    REPO_COLUMN_ID, &repo_id, -1);

	/* do something with the value */
	enabled ^= 1;

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    REPO_COLUMN_SENSITIVE, FALSE,
			    -1);

	/* set the repo */
	egg_debug ("setting %s to %i", repo_id, enabled);
	pk_client_repo_enable_async (client, repo_id, enabled, NULL,
				     gpk_repo_progress_cb, NULL,
				     gpk_repo_repo_enable_cb, NULL);

out:
	/* clean up */
	g_free (repo_id);
	gtk_tree_path_free (path);
}

/**
 * gpk_treeview_add_columns:
 **/
static void
gpk_treeview_add_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);

	/* column for enabled toggles */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (gpk_misc_enabled_toggled), model);

	/* TRANSLATORS: column if the source is enabled */
	column = gtk_tree_view_column_new_with_attributes (_("Enabled"), renderer,
							   "active", REPO_COLUMN_ENABLED,
							   "sensitive", REPO_COLUMN_SENSITIVE,
							   NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	/* TRANSLATORS: column for the source description */
	column = gtk_tree_view_column_new_with_attributes (_("Software Source"), renderer,
							   "markup", REPO_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, REPO_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * gpk_repos_treeview_clicked_cb:
 **/
static void
gpk_repos_treeview_clicked_cb (GtkTreeSelection *selection, gpointer data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *repo_id;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter, REPO_COLUMN_ID, &repo_id, -1);
		egg_debug ("selected row is: %s", repo_id);
		g_free (repo_id);
	} else {
		egg_debug ("no row selected");
	}
}

/**
 * gpk_repo_get_repo_list_cb
 **/
static void
gpk_repo_get_repo_list_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
//	PkClient *client = PK_CLIENT (object);
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
		window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_repo"));
		/* TRANSLATORS: for one reason or another, we could not get the list of sources */
		gpk_error_dialog_modal (window, _("Failed to get the list of sources"),
					gpk_error_enum_to_localised_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* add repos */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_repo"));
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
		gpk_repo_model_get_iter (model, &iter, repo_id);
		gtk_list_store_set (list_store, &iter,
				    REPO_COLUMN_ENABLED, enabled,
				    REPO_COLUMN_TEXT, description,
				    REPO_COLUMN_ID, repo_id,
				    REPO_COLUMN_ACTIVE, TRUE,
				    REPO_COLUMN_SENSITIVE, TRUE,
				    -1);

		g_free (repo_id);
		g_free (description);
	}

	/* remove the items that are not now present */
	gpk_repo_remove_nonactive (model);

	/* sort */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(list_store), REPO_COLUMN_TEXT, GTK_SORT_ASCENDING);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_repo_repo_list_refresh:
 **/
static void
gpk_repo_repo_list_refresh (void)
{
	PkBitfield filters;
	GtkWidget *widget;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	gboolean show_details;

	/* mark the items as not used */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_repo"));
	model = gtk_tree_view_get_model (treeview);
	gpk_repo_mark_nonactive (model);

	egg_debug ("refreshing list");
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "checkbutton_detail"));
	show_details = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	if (!show_details)
		filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT);
	else
		filters = pk_bitfield_value (PK_FILTER_ENUM_NONE);
	pk_client_get_repo_list_async (client, filters, NULL,
				       gpk_repo_progress_cb, NULL,
				       gpk_repo_get_repo_list_cb, NULL);
}

/**
 * gpk_repo_repo_list_changed_cb:
 **/
static void
gpk_repo_repo_list_changed_cb (PkControl *control, gpointer data)
{
	gpk_repo_repo_list_refresh ();
}

/**
 * gpk_repo_checkbutton_detail_cb:
 **/
static void
gpk_repo_checkbutton_detail_cb (GtkWidget *widget, gpointer data)
{
	gpk_repo_repo_list_refresh ();
}

/**
 * gpk_repo_application_prepare_action_cb:
 **/
static void
gpk_repo_application_prepare_action_cb (GApplication *application, GVariant *arguments,
					GVariant *platform_data, gpointer user_data)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_repo"));
	gtk_window_present (window);
}


/**
 * gpk_repo_get_properties_cb:
 **/
static void
gpk_repo_get_properties_cb (GObject *object, GAsyncResult *res, GMainLoop *loop)
{
	GtkWidget *widget;
	GError *error = NULL;
	PkControl *control = PK_CONTROL(object);
	gboolean ret;
//	PkBitfield roles;

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
		      NULL);

	/* setup GUI */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_REPO_LIST)) {
		/* get the update list */
		gpk_repo_repo_list_refresh ();
	} else {
		GtkTreeIter iter;
		GtkTreeView *treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_repo"));
		GtkTreeModel *model = gtk_tree_view_get_model (treeview);

		gtk_list_store_append (GTK_LIST_STORE(model), &iter);
		gtk_list_store_set (list_store, &iter,
				    REPO_COLUMN_ENABLED, FALSE,
				    REPO_COLUMN_TEXT, _("Getting software source list not supported by backend"),
				    REPO_COLUMN_ACTIVE, FALSE,
				    REPO_COLUMN_SENSITIVE, FALSE,
				    -1);

		widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_repo"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "checkbutton_detail"));
		gtk_widget_set_sensitive (widget, FALSE);
	}
out:
	return;
}

/**
 * gpk_repo_close_cb:
 **/
static void
gpk_repo_close_cb (GtkWidget *widget, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	egg_debug ("emitting action-close");
	g_main_loop_quit (loop);
}

/**
 * gpk_repo_delete_event_cb:
 **/
static gboolean
gpk_repo_delete_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	gpk_repo_close_cb (widget, data);
	return FALSE;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GOptionContext *context;
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkTreeSelection *selection;
	PkControl *control;
	GApplication *application;
	GError *error = NULL;
	guint retval;
	guint xid = 0;
	gboolean ret;
	GtkBox *box;
	GMainLoop *loop;

	const GOptionEntry options[] = {
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
	g_type_init ();
	gtk_init (&argc, &argv);

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("Software Source Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, egg_debug_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	/* TRANSLATORS: title to pass to to the user if there are not enough privs */
	ret = gpk_check_privileged_user (_("Software source viewer"), TRUE);
	if (!ret)
		return 1;

        /* add application specific icons to search path */
        gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
                                           GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* are we already activated? */
	application = g_application_new ("org.freedesktop.PackageKit.Repo", argc, argv);
	g_signal_connect (application, "prepare-activation",
			  G_CALLBACK (gpk_repo_application_prepare_action_cb), NULL);

	settings = g_settings_new (GPK_SETTINGS_SCHEMA);

	loop = g_main_loop_new (NULL, FALSE);

	client = pk_client_new ();
	g_object_set (client,
		      "background", FALSE,
		      NULL);

	control = pk_control_new ();
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (gpk_repo_repo_list_changed_cb), NULL);

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-repo.ui", &error);
	if (retval == 0) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out_build;
	}

	/* add animated widget */
	image_animation = gpk_animated_icon_new ();
	box = GTK_BOX (gtk_builder_get_object (builder, "hbox_animation"));
	gtk_box_pack_start (box, image_animation, FALSE, FALSE, 0);
	gtk_box_reorder_child (box, image_animation, 0);
	gtk_widget_show (image_animation);

	main_window = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_repo"));
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SOFTWARE_SOURCES);
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (gpk_repo_delete_event_cb), loop);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_repo_close_cb), loop);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_button_help_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "checkbutton_detail"));
	g_settings_bind (settings,
			 GPK_SETTINGS_REPO_SHOW_DETAILS,
			 widget, "active",
			 G_SETTINGS_BIND_DEFAULT);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_repo_checkbutton_detail_cb), NULL);

	/* set a size, if the screen allows */
	gpk_window_set_size_request (GTK_WINDOW (main_window), 500, 300);

	/* create list stores */
	list_store = gtk_list_store_new (REPO_COLUMN_LAST, G_TYPE_BOOLEAN,
					 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);

	/* create repo tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_repo"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_repos_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	gpk_treeview_add_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* show window */
	gtk_widget_show (main_window);

	/* set the parent window if it is specified */
	if (xid != 0) {
		egg_debug ("Setting xid %i", xid);
		gpk_window_set_parent_xid (GTK_WINDOW (main_window), xid);
	}

	/* focus back to the close button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	gtk_widget_grab_focus (widget);

	/* get properties */
	pk_control_get_properties_async (control, NULL, (GAsyncReadyCallback) gpk_repo_get_properties_cb, loop);

	/* wait */
	g_main_loop_run (loop);

	g_object_unref (list_store);
out_build:
	g_object_unref (builder);
	g_object_unref (settings);
	g_object_unref (control);
	g_object_unref (client);
	g_main_loop_unref (loop);
	g_object_unref (application);

	return 0;
}
