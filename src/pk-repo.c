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

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-connection.h>
#include <pk-enum-list.h>
#include "pk-common.h"

static GladeXML *glade_xml = NULL;
static GtkListStore *list_store = NULL;
static PkClient *client = NULL;
static gchar *repo = NULL;

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
pk_button_help_cb (GtkWidget *widget,
		   gboolean  data)
{
	pk_debug ("emitting action-help");
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

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, REPO_COLUMN_ENABLED, &installed, -1);

	/* do something with the value */
	installed ^= 1;

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, REPO_COLUMN_ENABLED, installed, -1);

	/* clean up */
	gtk_tree_path_free (path);
}

/**
 * pk_repo_detail_cb:
 **/
static void
pk_repo_detail_cb (PkClient *client, const gchar *repo_id, const gchar *details, gboolean enabled, gpointer data)
{
	GtkTreeIter iter;

	pk_debug ("repo = %s:%s:%i", repo_id, details, enabled);

	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (list_store, &iter,
			    REPO_COLUMN_ENABLED, enabled,
			    REPO_COLUMN_TEXT, details,
			    REPO_COLUMN_ID, repo_id,
			    -1);
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
		g_free (repo);
		gtk_tree_model_get (model, &iter,
				    REPO_COLUMN_ID, &repo_id, -1);

		/* make back into repo ID */
		repo = g_strdup (repo_id);
		g_free (repo_id);
		g_print ("selected row is: %s\n", repo);
	} else {
		g_print ("no row selected.\n");
	}
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, gpointer data)
{
	pk_debug ("connected=%i", connected);
}

/**
 * pk_repo_finished_cb:
 **/
static void
pk_repo_finished_cb (PkClient *client, PkStatusEnum status, guint runtime, gpointer data)
{
	/* nothing? */
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	GOptionContext *context;
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkTreeSelection *selection;
	PkConnection *pconnection;
	PkEnumList *role_list;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  "Show extra debugging information", NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
		  "Show the program version and exit", NULL },
		{ NULL}
	};

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

	if (program_version == TRUE) {
		g_print (VERSION "\n");
		return 0;
	}

	pk_debug_init (verbose);
	gtk_init (&argc, &argv);

	loop = g_main_loop_new (NULL, FALSE);

	client = pk_client_new ();
	g_signal_connect (client, "repo",
			  G_CALLBACK (pk_repo_detail_cb), NULL);
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_repo_finished_cb), NULL);

	/* get actions */
	role_list = pk_client_get_actions (client);

	pconnection = pk_connection_new ();
	g_signal_connect (pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), NULL);

	glade_xml = glade_xml_new (PK_DATA "/pk-repo.glade", NULL, NULL);
	main_window = glade_xml_get_widget (glade_xml, "window_repo");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
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

	/* get the update list */
//	pk_client_get_repo_list (client);

	pk_repo_detail_cb (client, "development", "Fedora - Development", TRUE, NULL);
	pk_repo_detail_cb (client, "development-debuginfo", "Fedora - Development - Debug", TRUE, NULL);
	pk_repo_detail_cb (client, "development-source", "Fedora - Development - Source", FALSE, NULL);
	pk_repo_detail_cb (client, "livna-development", "Livna for Fedora Core 8 - i386 - Development Tree", TRUE, NULL);
	pk_repo_detail_cb (client, "livna-development-debuginfo", "Livna for Fedora Core 8 - i386 - Development Tree - Debug", TRUE, NULL);
	pk_repo_detail_cb (client, "livna-development-source", "Livna for Fedora Core 8 - i386 - Development Tree - Source", FALSE, NULL);

	gtk_widget_show (main_window);

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	g_object_unref (list_store);
	g_object_unref (client);
	g_object_unref (pconnection);
	g_object_unref (role_list);
	g_free (repo);

	return 0;
}
