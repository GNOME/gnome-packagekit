/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-control.h>
#include <pk-connection.h>

#include <gpk-gnome.h>
#include <gpk-common.h>

#include "gpk-statusbar.h"

static GladeXML *glade_xml = NULL;
static GtkListStore *list_store = NULL;
static PkClient *client = NULL;
static PkRoleEnum roles;
static GpkStatusbar *statusbar;
static GConfClient *gconf_client;
static gboolean show_details;

enum
{
	REPO_COLUMN_ENABLED,
	REPO_COLUMN_TEXT,
	REPO_COLUMN_ID,
	REPO_COLUMN_LAST
};

/**
 * pk_button_help_cb:
 **/
static void
pk_button_help_cb (GtkWidget *widget, gboolean  data)
{
	gpk_gnome_help ("software-sources");
}

/**
 * pk_button_close_cb:
 **/
static void
pk_button_close_cb (GtkWidget	*widget,
		     gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;

	g_main_loop_quit (loop);
	pk_debug ("emitting action-close");
}

static void
pk_misc_installed_toggled (GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	GtkTreeModel *model = (GtkTreeModel *)data;
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	gboolean installed;
	gchar *repo_id;
	gboolean ret;
	GError *error = NULL;

	/* do we have the capability? */
	if (pk_enums_contain (roles, PK_ROLE_ENUM_REPO_ENABLE) == FALSE) {
		pk_debug ("can't change state");
		return;
	}

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    REPO_COLUMN_ENABLED, &installed,
			    REPO_COLUMN_ID, &repo_id, -1);

	/* do something with the value */
	installed ^= 1;

	/* do this to the repo */
	pk_debug ("setting %s to %i", repo_id, installed);
	ret = pk_client_reset (client, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = pk_client_repo_enable (client, repo_id, installed, &error);
	if (!ret) {
		pk_warning ("could not set repo enabled state: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, REPO_COLUMN_ENABLED, installed, -1);

out:
	/* clean up */
	g_free (repo_id);
	gtk_tree_path_free (path);
}

/**
 * pk_repo_detail_cb:
 **/
static void
pk_repo_detail_cb (PkClient *client, const gchar *repo_id,
		   const gchar *description, gboolean enabled, gpointer data)
{
	GtkTreeIter iter;

	pk_debug ("repo = %s:%s:%i", repo_id, description, enabled);

	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (list_store, &iter,
			    REPO_COLUMN_ENABLED, enabled,
			    REPO_COLUMN_TEXT, description,
			    REPO_COLUMN_ID, repo_id,
			    -1);
	/* sort after each entry, which is okay as there shouldn't be many */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(list_store), REPO_COLUMN_TEXT, GTK_SORT_ASCENDING);
}

/**
 * pk_window_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
pk_window_delete_event_cb (GtkWidget *widget,
			   GdkEvent  *event,
			   gpointer   data)
{
	GMainLoop *loop = (GMainLoop *) data;

	g_main_loop_quit (loop);
	return FALSE;
}

/**
 * pk_treeview_add_columns:
 **/
static void
pk_treeview_add_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);

	/* column for installed toggles */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (pk_misc_installed_toggled), model);

	column = gtk_tree_view_column_new_with_attributes (_("Enabled"), renderer,
							   "active", REPO_COLUMN_ENABLED, NULL);
	gtk_tree_view_append_column (treeview, column);


	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Repository"), renderer,
							   "markup", REPO_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, REPO_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * pk_repos_treeview_clicked_cb:
 **/
static void
pk_repos_treeview_clicked_cb (GtkTreeSelection *selection, gpointer data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *repo_id;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    REPO_COLUMN_ID, &repo_id, -1);
		pk_debug ("selected row is: %s", repo_id);
		g_free (repo_id);
	} else {
		pk_debug ("no row selected");
	}
}

/**
 * pk_repo_finished_cb:
 **/
static void
pk_repo_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, gpointer data)
{
	/* nothing? */
}

/**
 * pk_repo_status_changed_cb:
 **/
static void
pk_repo_status_changed_cb (PkClient *client, PkStatusEnum status, gpointer data)
{
	gpk_statusbar_set_status (statusbar, status);
}

/**
 * pk_repo_error_code_cb:
 **/
static void
pk_repo_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, gpointer data)
{
	gpk_error_modal_dialog (gpk_error_enum_to_localised_text (code), details);
}

/**
 * pk_repo_repo_list_refresh:
 **/
static void
pk_repo_repo_list_refresh (void)
{
	gboolean ret;
	GError *error = NULL;
	PkFilterEnum filter;

	pk_debug ("refreshing list");
	gtk_list_store_clear (list_store);
	ret = pk_client_reset (client, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return;
	}

	if (!show_details) {
		filter = PK_FILTER_ENUM_NOT_DEVELOPMENT;
	} else {
		filter = PK_FILTER_ENUM_NONE;
	}
	ret = pk_client_get_repo_list (client, filter, &error);
	if (!ret) {
		pk_warning ("failed to get repo list: %s", error->message);
		g_error_free (error);
	}
}

/**
 * pk_repo_repo_list_changed_cb:
 **/
static void
pk_repo_repo_list_changed_cb (PkControl *control, gpointer data)
{
	pk_repo_repo_list_refresh ();
}

/**
 * pk_repo_checkbutton_details:
 **/
static void
pk_repo_checkbutton_details (GtkWidget *widget, gpointer data)
{
	show_details = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	pk_debug ("Changing %s to %i", GPK_CONF_REPO_SHOW_DETAILS, show_details);
	gconf_client_set_bool (gconf_client, GPK_CONF_REPO_SHOW_DETAILS, show_details, NULL);
	pk_repo_repo_list_refresh ();
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	gboolean verbose = FALSE;
	GOptionContext *context;
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkTreeSelection *selection;
	PkControl *control;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  N_("Show extra debugging information"), NULL },
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
	g_option_context_set_summary (context, _("Software Update Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	pk_debug_init (verbose);
	gtk_init (&argc, &argv);

	loop = g_main_loop_new (NULL, FALSE);

	gconf_client = gconf_client_get_default ();

	client = pk_client_new ();
	g_signal_connect (client, "repo-detail",
			  G_CALLBACK (pk_repo_detail_cb), NULL);
	g_signal_connect (client, "status-changed",
			  G_CALLBACK (pk_repo_status_changed_cb), NULL);
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_repo_finished_cb), NULL);
	g_signal_connect (client, "error-code",
			  G_CALLBACK (pk_repo_error_code_cb), NULL);

	control = pk_control_new ();
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (pk_repo_repo_list_changed_cb), NULL);
	roles = pk_control_get_actions (control);

	glade_xml = glade_xml_new (PK_DATA "/gpk-repo.glade", NULL, NULL);
	main_window = glade_xml_get_widget (glade_xml, "window_repo");
	gtk_window_set_icon_name (GTK_WINDOW (main_window), "system-installer");

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (pk_window_delete_event_cb), loop);

	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_close_cb), loop);
	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_help_cb), NULL);

	widget = glade_xml_get_widget (glade_xml, "checkbutton_detail");
	show_details = gconf_client_get_bool (gconf_client, GPK_CONF_REPO_SHOW_DETAILS, NULL);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), show_details);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_repo_checkbutton_details), NULL);

	gtk_widget_set_size_request (main_window, 500, 300);

	/* create list stores */
	list_store = gtk_list_store_new (REPO_COLUMN_LAST, G_TYPE_BOOLEAN,
					 G_TYPE_STRING, G_TYPE_STRING);

	/* create repo tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_repo");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (pk_repos_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	pk_treeview_add_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* use the in-statusbar for progress */
	statusbar = gpk_statusbar_new ();
	widget = glade_xml_get_widget (glade_xml, "statusbar_status");
	gpk_statusbar_set_widget (statusbar, widget);

	if (pk_enums_contain (roles, PK_ROLE_ENUM_GET_REPO_LIST)) {
		/* get the update list */
		pk_repo_repo_list_refresh ();
	} else {
		pk_repo_detail_cb (client, "default",
				   _("Getting repository list not supported by backend"), FALSE, NULL);
		widget = glade_xml_get_widget (glade_xml, "treeview_repo");
		gtk_widget_set_sensitive (widget, FALSE);
		widget = glade_xml_get_widget (glade_xml, "checkbutton_detail");
		gtk_widget_set_sensitive (widget, FALSE);
	}

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	g_object_unref (glade_xml);
	g_object_unref (list_store);
	g_object_unref (gconf_client);
	g_object_unref (client);
	g_object_unref (control);
	g_object_unref (statusbar);

	return 0;
}
