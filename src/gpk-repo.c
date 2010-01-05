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

#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <dbus/dbus-glib.h>
#include <gconf/gconf-client.h>
#include <packagekit-glib/packagekit.h>
#include <unique/unique.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-gnome.h"
#include "gpk-common.h"
#include "gpk-error.h"
#include "gpk-animated-icon.h"
#include "gpk-enum.h"

static GtkBuilder *builder = NULL;
static GtkListStore *list_store = NULL;
static PkClient *client_query = NULL;
#if PK_CHECK_VERSION(0,5,1)
static PkClientPool *pool = NULL;
#else
static GPtrArray *client_array = NULL;
#endif
static PkBitfield roles;
static GConfClient *gconf_client;
static gboolean show_details;
static GtkTreePath *path_global = NULL;
static GtkWidget *image_animation = NULL;
#if !PK_CHECK_VERSION(0,5,1)
static PkStatusEnum status_last = PK_STATUS_ENUM_UNKNOWN;
#endif
static guint status_id = 0;

enum {
	REPO_COLUMN_ENABLED,
	REPO_COLUMN_TEXT,
	REPO_COLUMN_ID,
	REPO_COLUMN_ACTIVE,
	REPO_COLUMN_SENSITIVE,
	REPO_COLUMN_LAST
};

#if !PK_CHECK_VERSION(0,5,1)
static PkClient *gpk_repo_create_client (void);
#endif

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

static void
gpk_misc_enabled_toggled (GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	GtkTreeModel *model = (GtkTreeModel *)data;
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	gboolean enabled;
	gchar *repo_id = NULL;
	gboolean ret;
	GError *error = NULL;
	PkClient *client = NULL;

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

	/* do this to the repo */
	egg_debug ("setting %s to %i", repo_id, enabled);
#if PK_CHECK_VERSION(0,5,1)
	client = pk_client_pool_create (pool);
#else
	client = gpk_repo_create_client ();
#endif
	ret = pk_client_repo_enable (client, repo_id, enabled, &error);
	if (!ret) {
		egg_warning ("could not set repo enabled state: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    REPO_COLUMN_SENSITIVE, FALSE,
			    -1);

out:
	/* clean up */
	if (client != NULL)
		g_object_unref (client);
	g_free (repo_id);
	gtk_tree_path_free (path);
}

/**
 * gpk_repo_detail_cb:
 **/
static void
gpk_repo_detail_cb (PkClient *client, const gchar *repo_id,
		    const gchar *description, gboolean enabled, gpointer data)
{
	GtkTreeIter iter;
	GtkTreeView *treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_repo"));
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);

	egg_debug ("repo = %s:%s:%i", repo_id, description, enabled);

	gpk_repo_model_get_iter (model, &iter, repo_id);
	gtk_list_store_set (list_store, &iter,
			    REPO_COLUMN_ENABLED, enabled,
			    REPO_COLUMN_TEXT, description,
			    REPO_COLUMN_ID, repo_id,
			    REPO_COLUMN_ACTIVE, TRUE,
			    REPO_COLUMN_SENSITIVE, TRUE,
			    -1);

	/* sort after each entry, which is okay as there shouldn't be many */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(list_store), REPO_COLUMN_TEXT, GTK_SORT_ASCENDING);
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
 * gpk_repo_finished_cb:
 **/
static void
gpk_repo_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, gpointer data)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;

	/* remove the items that are not used */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_repo"));
	model = gtk_tree_view_get_model (treeview);
	gpk_repo_remove_nonactive (model);
}

/**
 * gpk_repo_status_changed_timeout_cb:
 **/
static gboolean
gpk_repo_status_changed_timeout_cb (PkClient *client)
{
	const gchar *text;
	GtkWidget *widget;
	PkStatusEnum status;

#if PK_CHECK_VERSION(0,5,1)
	/* get the last status */
	g_object_get (client,
		      "status", &status,
		      NULL);
#else
	status = status_last;
#endif

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
 * gpk_repo_status_changed_cb:
 **/
static void
gpk_repo_status_changed_cb (PkClient *client, PkStatusEnum status, gpointer data)
{
	GtkWidget *widget;

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
	status_id = g_timeout_add (GPK_UI_STATUS_SHOW_DELAY, (GSourceFunc) gpk_repo_status_changed_timeout_cb, client);
out:
#if !PK_CHECK_VERSION(0,5,1)
	/* save for the callback */
	status_last = status;
#endif
	return;
}

/**
 * gpk_repo_error_code_cb:
 **/
static void
gpk_repo_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, gpointer data)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_repo"));
	/* TRANSLATORS: for one reason or another, we could not enable or disable a software source */
	gpk_error_dialog_modal (window, _("Failed to change status"),
				gpk_error_enum_to_localised_text (code), details);
}

/**
 * gpk_repo_repo_list_refresh:
 **/
static void
gpk_repo_repo_list_refresh (void)
{
	gboolean ret;
	GError *error = NULL;
	PkBitfield filters;
	GtkTreeView *treeview;
	GtkTreeModel *model;

	/* mark the items as not used */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_repo"));
	model = gtk_tree_view_get_model (treeview);
	gpk_repo_mark_nonactive (model);

	egg_debug ("refreshing list");
	ret = pk_client_reset (client_query, &error);
	if (!ret) {
		egg_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return;
	}
	if (!show_details)
		filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT);
	else
		filters = pk_bitfield_value (PK_FILTER_ENUM_NONE);
	ret = pk_client_get_repo_list (client_query, filters, &error);
	if (!ret) {
		egg_warning ("failed to get repo list: %s", error->message);
		g_error_free (error);
	}
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
 * gpk_repo_checkbutton_details:
 **/
static void
gpk_repo_checkbutton_details (GtkWidget *widget, gpointer data)
{
	show_details = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	egg_debug ("Changing %s to %i", GPK_CONF_REPO_SHOW_DETAILS, show_details);
	gconf_client_set_bool (gconf_client, GPK_CONF_REPO_SHOW_DETAILS, show_details, NULL);
	gpk_repo_repo_list_refresh ();
}

/**
 * gpk_repo_message_received_cb
 **/
static void
gpk_repo_message_received_cb (UniqueApp *app, UniqueCommand command, UniqueMessageData *message_data, guint time_ms, gpointer data)
{
	GtkWindow *window;
	if (command == UNIQUE_ACTIVATE) {
		window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_repo"));
		gtk_window_present (window);
	}
}

#if !PK_CHECK_VERSION(0,5,1)
/**
 * gpk_repo_destroy_cb:
 **/
static void
gpk_repo_destroy_cb (PkClient *client, gpointer data)
{
	gboolean ret;
	egg_debug ("client destroyed");
	ret = g_ptr_array_remove (client_array, client);
	if (!ret)
		egg_warning ("failed to remove %p", client);
	/* TODO: disconnect signals? */
	g_object_unref (client);
}

/**
 * gpk_repo_create_client
 **/
static PkClient *
gpk_repo_create_client (void)
{
	PkClient *client;
	client = pk_client_new ();
	g_signal_connect (client, "repo-detail",
			  G_CALLBACK (gpk_repo_detail_cb), NULL);
	g_signal_connect (client, "status-changed",
			  G_CALLBACK (gpk_repo_status_changed_cb), NULL);
	g_signal_connect (client, "error-code",
			  G_CALLBACK (gpk_repo_error_code_cb), NULL);
	g_signal_connect (client, "destroy",
			  G_CALLBACK (gpk_repo_destroy_cb), NULL);
	g_ptr_array_add (client_array, client);
	egg_debug ("added %p", client);
	return g_object_ref (client);
}
#endif

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
	GtkTreeSelection *selection;
	PkControl *control;
	UniqueApp *unique_app;
	GError *error = NULL;
	guint retval;
	guint xid = 0;
	gboolean ret;
	GtkBox *box;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
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

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("Software Source Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);
	gtk_init (&argc, &argv);

	/* TRANSLATORS: title to pass to to the user if there are not enough privs */
	ret = gpk_check_privileged_user (_("Software source viewer"), TRUE);
	if (!ret)
		return 1;

        /* add application specific icons to search path */
        gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
                                           GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* are we already activated? */
	unique_app = unique_app_new ("org.freedesktop.PackageKit.Repo", NULL);
	if (unique_app_is_running (unique_app)) {
		egg_debug ("You have another instance running. This program will now close");
		unique_app_send_message (unique_app, UNIQUE_ACTIVATE, NULL);
		goto unique_out;
	}
	g_signal_connect (unique_app, "message-received",
			  G_CALLBACK (gpk_repo_message_received_cb), NULL);

	gconf_client = gconf_client_get_default ();

	client_query = pk_client_new ();
	g_signal_connect (client_query, "repo-detail",
			  G_CALLBACK (gpk_repo_detail_cb), NULL);
	g_signal_connect (client_query, "status-changed",
			  G_CALLBACK (gpk_repo_status_changed_cb), NULL);
	g_signal_connect (client_query, "finished",
			  G_CALLBACK (gpk_repo_finished_cb), NULL);
	g_signal_connect (client_query, "error-code",
			  G_CALLBACK (gpk_repo_error_code_cb), NULL);

#if PK_CHECK_VERSION(0,5,1)
	pool = pk_client_pool_new ();
	pk_client_pool_connect (pool, "repo-detail",
				G_CALLBACK (gpk_repo_detail_cb), NULL);
	pk_client_pool_connect (pool, "status-changed",
				G_CALLBACK (gpk_repo_status_changed_cb), NULL);
	pk_client_pool_connect (pool, "error-code",
				G_CALLBACK (gpk_repo_error_code_cb), NULL);
#else
	client_array = g_ptr_array_new ();
#endif

	control = pk_control_new ();
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (gpk_repo_repo_list_changed_cb), NULL);
	roles = pk_control_get_actions (control, NULL);

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-repo.ui", &error);
	if (error != NULL) {
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
	g_signal_connect_swapped (main_window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_button_help_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "checkbutton_detail"));
	show_details = gconf_client_get_bool (gconf_client, GPK_CONF_REPO_SHOW_DETAILS, NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), show_details);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_repo_checkbutton_details), NULL);

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

	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_REPO_LIST)) {
		/* get the update list */
		gpk_repo_repo_list_refresh ();
	} else {
		gpk_repo_detail_cb (client_query, "default",
				   _("Getting software source list not supported by backend"), FALSE, NULL);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_repo"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "checkbutton_detail"));
		gtk_widget_set_sensitive (widget, FALSE);
	}

	/* wait */
	gtk_main ();

	g_object_unref (list_store);
out_build:
	g_object_unref (builder);
	g_object_unref (gconf_client);
	g_object_unref (client_query);
	g_object_unref (control);
#if PK_CHECK_VERSION(0,5,1)
	g_object_unref (pool);
#else
	g_ptr_array_foreach (client_array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (client_array, TRUE);
#endif
unique_out:
	g_object_unref (unique_app);

	return 0;
}
