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
#include <pk-common.h>
#include <pk-task-list.h>
#include <pk-connection.h>
#include <pk-package-id.h>
#include <pk-enum-list.h>
#include "pk-common-gui.h"
#include "pk-statusbar.h"

static GladeXML *glade_xml = NULL;
static GtkListStore *list_store = NULL;
static PkClient *client = NULL;
static PkTaskList *tlist = NULL;
static gchar *package = NULL;
static PkStatusbar *statusbar = NULL;
static gchar *update_url = NULL;

enum
{
	PACKAGES_COLUMN_ICON,
	PACKAGES_COLUMN_TEXT,
	PACKAGES_COLUMN_ID,
	PACKAGES_COLUMN_LAST
};

/**
 * pk_button_help_cb:
 **/
static void
pk_button_help_cb (GtkWidget *widget, gboolean data)
{
	pk_debug ("emitting action-help");
}

/**
 * pk_button_url_cb:
 **/
static void
pk_button_url_cb (GtkWidget *widget, gboolean data)
{
	pk_execute_url (update_url);
}

/**
 * pk_button_update_cb:
 **/
static void
pk_button_update_cb (GtkWidget *widget, gboolean data)
{
	gboolean ret;
	pk_client_reset (client);
	ret = pk_client_update_package (client, package);
	if (ret == TRUE) {
		/* make the refresh button non-clickable until we have completed */
		widget = glade_xml_get_widget (glade_xml, "button_apply");
		gtk_widget_set_sensitive (widget, FALSE);

		widget = glade_xml_get_widget (glade_xml, "button_refresh");
		gtk_widget_set_sensitive (widget, FALSE);
	}
}

/**
 * pk_updates_apply_cb:
 **/
static void
pk_updates_apply_cb (GtkWidget *widget, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	pk_debug ("Doing the system update");

	pk_client_reset (client);
	pk_client_update_system (client);
	g_main_loop_quit (loop);
}

/**
 * pk_updates_refresh_cb:
 **/
static void
pk_updates_refresh_cb (GtkWidget *widget, gboolean data)
{
	gboolean ret;

	/* clear existing list */
	gtk_list_store_clear (list_store);

	/* make the refresh button non-clickable */
	gtk_widget_set_sensitive (widget, FALSE);

	/* make the apply button non-clickable until we get completion */
	widget = glade_xml_get_widget (glade_xml, "button_apply");
	gtk_widget_set_sensitive (widget, FALSE);

	/* we can't click this if we havn't finished */
	pk_client_reset (client);
	ret = pk_client_refresh_cache (client, TRUE);
	if (ret == FALSE) {
		g_object_unref (client);
		pk_warning ("failed to refresh cache");
	}
}

/**
 * pk_button_close_cb:
 **/
static void
pk_button_close_cb (GtkWidget	*widget,
		     gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;

	/* we might have a transaction running */
	pk_client_cancel (client);

	g_main_loop_quit (loop);
	pk_debug ("emitting action-close");
}

/**
 * pk_updates_package_cb:
 **/
static void
pk_updates_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id,
		       const gchar *summary, gpointer data)
{
	GtkTreeIter iter;
	gchar *text;
	PkRoleEnum role;
	const gchar *icon_name;

	pk_client_get_role (client, &role, NULL);

	if (role != PK_ROLE_ENUM_GET_UPDATES) {
		pk_debug ("not in get_updates");
		return;
	}

	pk_debug ("package = %s:%s:%s", pk_info_enum_to_text (info), package_id, summary);

	text = pk_package_id_pretty (package_id, summary);
	icon_name = pk_info_enum_to_icon_name (info);
	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (list_store, &iter,
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_ID, package_id,
			    PACKAGES_COLUMN_ICON, icon_name,
			    -1);
	g_free (text);
}

/**
 * pk_updates_set_text_buffer:
 **/
static void
pk_updates_set_text_buffer (GtkWidget *widget, const gchar *text)
{
	GtkTextBuffer *buffer;
	buffer = gtk_text_buffer_new (NULL);
	/* ITS4: ignore, not used for allocation */
	if (pk_strzero (text) == FALSE) {
		gtk_text_buffer_set_text (buffer, text, -1);
	} else {
		/* no information */
		gtk_text_buffer_set_text (buffer, "", -1);
	}
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);
}

/**
 * pk_updates_update_detail_cb:
 **/
static void
pk_updates_update_detail_cb (PkClient *client, const gchar *package_id,
			     const gchar *updates, const gchar *obsoletes,
			     const gchar *url, PkRestartEnum restart,
			     const gchar *update_text, gpointer data)
{
	GtkWidget *widget;
	PkPackageId *ident;
	const gchar *text;
	gchar *package_pretty = NULL;
	gchar *updates_pretty = NULL;
	gchar *obsoletes_pretty = NULL;

	/* set restart */
	widget = glade_xml_get_widget (glade_xml, "label_restart");
	if (restart == PK_RESTART_ENUM_SESSION ||
	    restart == PK_RESTART_ENUM_SYSTEM) {
		gtk_widget_show (widget);
		text = pk_restart_enum_to_localised_text_future (restart);
		gtk_label_set_label (GTK_LABEL (widget), text);
		widget = glade_xml_get_widget (glade_xml, "label_restart_left");
		gtk_widget_show (widget);
	} else {
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "label_restart_left");
		gtk_widget_hide (widget);
	}

	/* set updates */
	widget = glade_xml_get_widget (glade_xml, "label_version");
	package_pretty = pk_package_id_name_version (package_id);
	gtk_label_set_label (GTK_LABEL (widget), package_pretty);

	/* set updates */
	widget = glade_xml_get_widget (glade_xml, "label_updates");
	if (pk_strzero (updates) == TRUE) {
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "label_updates_left");
		gtk_widget_hide (widget);
	} else {
		gtk_widget_show (widget);
		updates_pretty = pk_package_id_name_version (updates);
		gtk_label_set_label (GTK_LABEL (widget), updates_pretty);
	}

	/* set updates */
	widget = glade_xml_get_widget (glade_xml, "label_obsoletes");
	if (pk_strzero (obsoletes) == TRUE) {
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "label_obsoletes_left");
		gtk_widget_hide (widget);
	} else {
		gtk_widget_show (widget);
		obsoletes_pretty = pk_package_id_name_version (obsoletes);
		gtk_label_set_label (GTK_LABEL (widget), obsoletes_pretty);
	}

	/* set url */
	widget = glade_xml_get_widget (glade_xml, "button_url");
	if (pk_strzero (url) == FALSE) {
		gtk_widget_show (widget);
		g_free (update_url);
		update_url = g_strdup (url);
	} else {
		gtk_widget_hide (widget);
	}

	/* set repo */
	widget = glade_xml_get_widget (glade_xml, "label_repo");
	ident = pk_package_id_new_from_string (package_id);
	if (pk_strzero (ident->data) == TRUE) {
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "label_repo_left");
		gtk_widget_hide (widget);
	} else {
		gtk_widget_show (widget);
		gtk_label_set_label (GTK_LABEL (widget), ident->data);
	}
	pk_package_id_free (ident);

	/* set text buffer */
	widget = glade_xml_get_widget (glade_xml, "scrolledwindow_details");
	if (pk_strzero (update_text) == TRUE) {
		gtk_widget_hide (widget);
	} else {
		gtk_widget_show (widget);
		widget = glade_xml_get_widget (glade_xml, "textview_details");
		pk_updates_set_text_buffer (widget, update_text);
	}

	/* show the notebook */
	widget = glade_xml_get_widget (glade_xml, "frame_details");
	gtk_widget_show (widget);

	g_free (package_pretty);
	g_free (updates_pretty);
	g_free (obsoletes_pretty);
}

/**
 * pk_updates_transaction_status_changed_cb:
 **/
static void
pk_updates_transaction_status_changed_cb (PkClient *client, PkStatusEnum status, gpointer data)
{
	pk_statusbar_set_status (statusbar, status);
}

/**
 * pk_window_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
pk_window_delete_event_cb (GtkWidget	*widget,
			    GdkEvent	*event,
			    gpointer    data)
{
	GMainLoop *loop = (GMainLoop *) data;

	/* we might have a transaction running */
	pk_client_cancel (client);

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

	/* image */
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_DIALOG, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Severity"), renderer,
							   "icon-name", PACKAGES_COLUMN_ICON, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Software"), renderer,
							   "markup", PACKAGES_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * pk_packages_treeview_clicked_cb:
 **/
static void
pk_packages_treeview_clicked_cb (GtkTreeSelection *selection, gpointer data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *package_id;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (package);
		gtk_tree_model_get (model, &iter,
				    PACKAGES_COLUMN_ID, &package_id, -1);

		/* make back into package ID */
		package = g_strdup (package_id);
		g_free (package_id);
		g_print ("selected row is: %s\n", package);
		/* get the decription */
		pk_client_reset (client);
		pk_client_get_update_detail (client, package);
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
 * pk_updates_set_aux_status:
 **/
static void
pk_updates_set_aux_status (PkClient *client, const gchar *message)
{
	GtkTreeIter iter;

	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (list_store, &iter, 
                            PACKAGES_COLUMN_TEXT, message, 
                            PACKAGES_COLUMN_ICON, "dialog-information",
                            -1);
}

/**
 * pk_updates_finished_cb:
 **/
static void
pk_updates_finished_cb (PkClient *client, PkStatusEnum status, guint runtime, gpointer data)
{
	GtkWidget *widget;
	PkRoleEnum role;
	guint length;

	pk_client_get_role (client, &role, NULL);

	/* hide widget */
	pk_statusbar_hide (statusbar);

	if (role == PK_ROLE_ENUM_REFRESH_CACHE) {
		/* hide the details for now */
		widget = glade_xml_get_widget (glade_xml, "frame_details");
		gtk_widget_hide (widget);

		pk_client_reset (client);
		pk_client_set_use_buffer (client, TRUE);
		pk_client_get_updates (client);
		return;
	}

	/* we don't need to do anything here */
	if (role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		return;
	}

	/* make the refresh button clickable now we have completed */
	widget = glade_xml_get_widget (glade_xml, "button_apply");
	gtk_widget_set_sensitive (widget, TRUE);

	widget = glade_xml_get_widget (glade_xml, "button_refresh");
	gtk_widget_set_sensitive (widget, TRUE);

	/* we don't need to do anything here */
	if (role == PK_ROLE_ENUM_UPDATE_PACKAGE) {
		/* clear existing list */
		gtk_list_store_clear (list_store);

		/* hide the details for now */
		widget = glade_xml_get_widget (glade_xml, "frame_details");
		gtk_widget_hide (widget);

		/* get the new update list */
		pk_client_reset (client);
		pk_client_set_use_buffer (client, TRUE);
		pk_client_get_updates (client);
		return;
	}

	length = pk_client_package_buffer_get_size (client);
	if (length == 0) {
		/* clear existing list */
		gtk_list_store_clear (list_store);

		/* put a message in the listbox */
		pk_updates_set_aux_status (client, _("<b>There are no updates available!</b>"));

		/* if no updates then hide apply */
		widget = glade_xml_get_widget (glade_xml, "button_apply");
		gtk_widget_hide (widget);
	} else {
		/* set visible and sensitive */
		widget = glade_xml_get_widget (glade_xml, "button_apply");
		gtk_widget_set_sensitive (widget, TRUE);
		gtk_widget_show (widget);
	}
}

/**
 * pk_updates_progress_changed_cb:
 **/
static void
pk_updates_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				guint elapsed, guint remaining, gpointer data)
{
	pk_statusbar_set_percentage (statusbar, percentage);
	pk_statusbar_set_remaining (statusbar, remaining);
}

/**
 * pk_updates_task_list_changed_cb:
 **/
static void
pk_updates_task_list_changed_cb (PkTaskList *tlist, gpointer data)
{
	GtkWidget *widget;

	/* hide buttons if we are updating */
	if (pk_task_list_contains_role (tlist, PK_ROLE_ENUM_UPDATE_SYSTEM) == TRUE) {
		/* clear existing list */
		gtk_list_store_clear (list_store);

		/* put a message in the listbox */
		pk_updates_set_aux_status (client, _("<b>There is an update already in progress!</b>"));

		/* if doing it then hide apply and refresh */
		widget = glade_xml_get_widget (glade_xml, "button_apply");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "button_refresh");
		gtk_widget_hide (widget);
	}
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

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
                                           PK_DATA G_DIR_SEPARATOR_S "icons");

	loop = g_main_loop_new (NULL, FALSE);

	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE);
	g_signal_connect (client, "package",
			  G_CALLBACK (pk_updates_package_cb), NULL);
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_updates_finished_cb), NULL);
	g_signal_connect (client, "progress-changed",
			  G_CALLBACK (pk_updates_progress_changed_cb), NULL);
	g_signal_connect (client, "update-detail",
			  G_CALLBACK (pk_updates_update_detail_cb), NULL);
	g_signal_connect (client, "transaction-status-changed",
			  G_CALLBACK (pk_updates_transaction_status_changed_cb), NULL);

	/* get actions */
	role_list = pk_client_get_actions (client);

	pconnection = pk_connection_new ();
	g_signal_connect (pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), NULL);

	/* we need to grey out all the buttons if we are in progress */
	tlist = pk_task_list_new ();
	g_signal_connect (tlist, "task-list-changed",
			  G_CALLBACK (pk_updates_task_list_changed_cb), NULL);
	pk_updates_task_list_changed_cb (tlist, NULL);

	glade_xml = glade_xml_new (PK_DATA "/pk-update-viewer.glade", NULL, NULL);
	main_window = glade_xml_get_widget (glade_xml, "window_updates");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), "system-installer");

	/* set apply insensitive until we finished*/
	widget = glade_xml_get_widget (glade_xml, "button_apply");
	gtk_widget_set_sensitive (widget, FALSE);

	/* hide the details for now */
	widget = glade_xml_get_widget (glade_xml, "frame_details");
	gtk_widget_hide (widget);

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (pk_window_delete_event_cb), loop);

	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_close_cb), loop);
	widget = glade_xml_get_widget (glade_xml, "button_apply");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_updates_apply_cb), loop);
	gtk_widget_set_tooltip_text(widget, _("Apply all updates"));
	widget = glade_xml_get_widget (glade_xml, "button_refresh");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_updates_refresh_cb), NULL);
	gtk_widget_set_tooltip_text(widget, _("Refresh package list"));
	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_help_cb), NULL);
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (glade_xml, "button_url");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_url_cb), NULL);
	gtk_widget_set_tooltip_text(widget, _("Visit homepage for selected package"));

	widget = glade_xml_get_widget (glade_xml, "button_update");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_update_cb), NULL);
	gtk_widget_set_tooltip_text(widget, _("Update selected package"));

	gtk_widget_set_size_request (main_window, 500, 300);

	/* create list stores */
	list_store = gtk_list_store_new (PACKAGES_COLUMN_LAST, G_TYPE_STRING,
					     G_TYPE_STRING, G_TYPE_STRING);

	/* create package tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (pk_packages_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	pk_treeview_add_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* use the in-statusbar for progress */
	statusbar = pk_statusbar_new ();
	widget = glade_xml_get_widget (glade_xml, "statusbar_status");
	pk_statusbar_set_widget (statusbar, widget);

	/* make the refresh button non-clickable until we get completion */
	widget = glade_xml_get_widget (glade_xml, "button_refresh");
	gtk_widget_set_sensitive (widget, FALSE);

	/* make the apply button non-clickable until we get completion */
	widget = glade_xml_get_widget (glade_xml, "button_apply");
	gtk_widget_set_sensitive (widget, FALSE);

	/* get the update list */
	pk_client_get_updates (client);
	gtk_widget_show (main_window);

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	g_object_unref (list_store);
	g_object_unref (client);
	g_object_unref (pconnection);
	g_object_unref (role_list);
	g_object_unref (statusbar);
	g_free (package);

	return 0;
}
