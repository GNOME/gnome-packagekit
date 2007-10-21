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
#include <pk-package-id.h>
#include <pk-enum-list.h>
#include "pk-common-gui.h"

static GladeXML *glade_xml = NULL;
static GtkWidget *progress_bar = NULL;
static GtkListStore *list_store = NULL;
static PkClient *client = NULL;
static gchar *package = NULL;
static guint timer_id = 0;

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
pk_button_help_cb (GtkWidget *widget,
		   gboolean  data)
{
	pk_debug ("emitting action-help");
}

/**
 * pk_updates_apply_cb:
 **/
static void
pk_updates_apply_cb (GtkWidget *widget,
		     gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	pk_debug ("Doing the system update");

	/* don't spin anymore */
	if (timer_id != 0) {
		g_source_remove (timer_id);
	}

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

	/* don't spin anymore */
	if (timer_id != 0) {
		g_source_remove (timer_id);
	}

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
	GdkPixbuf *icon;
	gchar *text;
	const gchar *icon_name;

	pk_debug ("package = %s:%s:%s", pk_info_enum_to_text (info), package_id, summary);

	/* ignore metadata updates */
	if (info == PK_INFO_ENUM_DOWNLOADING) {
		return;
	}

	text = pk_package_id_pretty (package_id, summary);
	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (list_store, &iter,
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_ID, package_id,
			    -1);

	icon_name = pk_info_enum_to_icon_name (info);
	icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), icon_name, 48, 0, NULL);
	if (icon != NULL) {
		gtk_list_store_set (list_store, &iter, PACKAGES_COLUMN_ICON, icon, -1);
		gdk_pixbuf_unref (icon);
	}
	g_free (text);
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
	column = gtk_tree_view_column_new_with_attributes (_("Severity"), renderer,
							   "pixbuf", PACKAGES_COLUMN_ICON, NULL);
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
 * pk_updates_finished_cb:
 **/
static void
pk_updates_finished_cb (PkClient *client, PkStatusEnum status, guint runtime, gpointer data)
{
	GtkWidget *widget;
	PkRoleEnum role;
	guint length;

	pk_client_get_role (client, &role, NULL);

	/* hide the progress bar */
	gtk_widget_hide (progress_bar);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar), 0.0);

	if (role == PK_ROLE_ENUM_REFRESH_CACHE) {
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

	length = pk_client_package_buffer_get_size (client);
	if (length == 0) {
		GtkTreeIter iter;
		GdkPixbuf *icon;

		/* if no updates then hide apply and add this to the box */
		widget = glade_xml_get_widget (glade_xml, "button_apply");
		gtk_widget_hide (widget);

		gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter, PACKAGES_COLUMN_TEXT, _("<b>There are no updates available!</b>"), -1);
		icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), "dialog-information", 48, 0, NULL);
		if (icon != NULL) {
			gtk_list_store_set (list_store, &iter, PACKAGES_COLUMN_ICON, icon, -1);
			gdk_pixbuf_unref (icon);
		}
	} else {
		/* set visible and sensitive */
		widget = glade_xml_get_widget (glade_xml, "button_apply");
		gtk_widget_set_sensitive (widget, TRUE);
		gtk_widget_show (widget);
	}
}

/**
 * pk_updates_percentage_changed_cb:
 **/
static void
pk_updates_percentage_changed_cb (PkClient *client, guint percentage, gpointer data)
{
	if (percentage == 0) {
		return;
	}
	gtk_widget_show (progress_bar);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar), (gfloat) percentage / 100.0);
}

/**
 * pk_updates_no_percentage_updates_timeout:
 **/
gboolean
pk_updates_no_percentage_updates_timeout (gpointer data)
{
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (progress_bar));
	return TRUE;
}

/**
 * pk_application_no_percentage_updates_cb:
 **/
static void
pk_updates_no_percentage_updates_cb (PkClient *client, gpointer data)
{
	timer_id = g_timeout_add (40, pk_updates_no_percentage_updates_timeout, data);
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
	pk_client_set_use_buffer (client, TRUE);
	g_signal_connect (client, "package",
			  G_CALLBACK (pk_updates_package_cb), NULL);
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_updates_finished_cb), NULL);
	g_signal_connect (client, "percentage-changed",
			  G_CALLBACK (pk_updates_percentage_changed_cb), NULL);
	g_signal_connect (client, "no-percentage-updates",
			  G_CALLBACK (pk_updates_no_percentage_updates_cb), NULL);

	/* get actions */
	role_list = pk_client_get_actions (client);

	pconnection = pk_connection_new ();
	g_signal_connect (pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), NULL);

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
	widget = glade_xml_get_widget (glade_xml, "button_refresh");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_updates_refresh_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_help_cb), NULL);

	gtk_widget_set_size_request (main_window, 500, 300);

	/* create list stores */
	list_store = gtk_list_store_new (PACKAGES_COLUMN_LAST, GDK_TYPE_PIXBUF,
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

	widget = glade_xml_get_widget (glade_xml, "statusbar_status");
	progress_bar = gtk_progress_bar_new ();
	gtk_box_pack_end (GTK_BOX (widget), progress_bar, TRUE, TRUE, 0);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar), 0.0);

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
	g_free (package);

	return 0;
}
