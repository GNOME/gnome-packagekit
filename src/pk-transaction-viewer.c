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
#include "pk-common.h"

static GladeXML *glade_xml = NULL;
static GtkListStore *list_store = NULL;
static PkClient *client = NULL;
static gchar *transaction_id = NULL;

enum
{
	PACKAGES_COLUMN_ICON,
	PACKAGES_COLUMN_TEXT,
	PACKAGES_COLUMN_SUCCEEDED,
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
 * pk_button_rollback_cb:
 **/
static void
pk_button_rollback_cb (GtkWidget *widget, gboolean data)
{
	pk_debug ("moo");
}

/**
 * pk_button_close_cb:
 **/
static void
pk_button_close_cb (GtkWidget	*widget,
		     gboolean	data)
{
	GMainLoop *loop = (GMainLoop *) data;
	g_main_loop_quit (loop);
	pk_debug ("emitting action-close");
}

/**
 * pk_transaction_db_get_pretty_date:
 **/
static gchar *
pk_transaction_db_get_pretty_date (const gchar *timespec)
{
	GDate *date;
	GTimeVal timeval;
	gchar buffer[100];

	g_time_val_from_iso8601 (timespec, &timeval);

	/* get printed string */
	date = g_date_new ();
	g_date_set_time_val (date, &timeval);

	g_date_strftime (buffer, 100, "%A, %d %B %Y", date);
	g_date_free (date);
	return g_strdup (buffer);
}


/**
 * pk_transaction_cb:
 **/
static void
pk_transaction_cb (PkClient *client, const gchar *tid, const gchar *timespec,
		   gboolean succeeded, PkRoleEnum role, guint duration, gpointer data)
{
	GtkTreeIter iter;
	GdkPixbuf *icon;
	gchar *text;
	gchar *pretty;
	const gchar *icon_name;
	const gchar *role_text;

	pretty = pk_transaction_db_get_pretty_date (timespec);
	pk_debug ("pretty=%s", pretty);
	role_text = pk_role_enum_to_localised_past (role);
	text = g_markup_printf_escaped ("<b>%s</b>\n%s", role_text, pretty);
	g_free (pretty);

	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (list_store, &iter,
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_ID, tid,
			    -1);

	icon_name = pk_role_enum_to_icon_name (role);
	icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), icon_name, 48, 0, NULL);
	if (icon) {
		gtk_list_store_set (list_store, &iter, PACKAGES_COLUMN_ICON, icon, -1);
		gdk_pixbuf_unref (icon);
	}

	if (succeeded == TRUE) {
		icon_name = "document-new";
	} else {
		icon_name = "dialog-error";
	}
	icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), icon_name, 24, 0, NULL);
	if (icon) {
		gtk_list_store_set (list_store, &iter, PACKAGES_COLUMN_SUCCEEDED, icon, -1);
		gdk_pixbuf_unref (icon);
	}
}

/**
 * pk_window_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
pk_window_delete_event_cb (GtkWidget	*widget,
			    GdkEvent	*event,
			    gboolean	 data)
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

	/* image */
	renderer = gtk_cell_renderer_pixbuf_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Role"), renderer,
							   "pixbuf", PACKAGES_COLUMN_ICON, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Transaction"), renderer,
							   "markup", PACKAGES_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, TRUE);

	/* image */
	renderer = gtk_cell_renderer_pixbuf_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Succeeded"), renderer,
							   "pixbuf", PACKAGES_COLUMN_SUCCEEDED, NULL);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * pk_treeview_clicked_cb:
 **/
static void
pk_treeview_clicked_cb (GtkTreeSelection *selection, gboolean data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *id;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (transaction_id);
		gtk_tree_model_get (model, &iter,
				    PACKAGES_COLUMN_ID, &id, -1);

		/* make back into transaction_id */
		transaction_id = g_strdup (id);
		g_free (id);
		g_print ("selected row is: %s\n", id);
		/* get the decription */
		pk_client_get_description (client, id);
	} else {
		g_print ("no row selected.\n");
	}
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, gboolean data)
{
	pk_debug ("connected=%i", connected);
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
	PkConnection *pconnection;
	PkEnumList *role_list;

	const GOptionEntry options[] = {
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  "Show extra debugging information", NULL },
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (_("Transaction Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);
	pk_debug_init (verbose);
	gtk_init (&argc, &argv);

	loop = g_main_loop_new (NULL, FALSE);

	client = pk_client_new ();
	g_signal_connect (client, "transaction",
			  G_CALLBACK (pk_transaction_cb), NULL);

	/* get actions */
	role_list = pk_client_get_actions (client);

	pconnection = pk_connection_new ();
	g_signal_connect (pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), NULL);

	glade_xml = glade_xml_new (PK_DATA "/pk-transactions.glade", NULL, NULL);
	main_window = glade_xml_get_widget (glade_xml, "window_transactions");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), "system-installer");

	/* hide the details for now */
	widget = glade_xml_get_widget (glade_xml, "frame_details");
	gtk_widget_hide (widget);

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (pk_window_delete_event_cb), loop);

	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_close_cb), loop);
	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_help_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_rollback");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_rollback_cb), NULL);

	gtk_widget_set_size_request (main_window, 500, 300);

	/* create list stores */
	list_store = gtk_list_store_new (PACKAGES_COLUMN_LAST, GDK_TYPE_PIXBUF,
					 G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_STRING);

	/* create transaction_id tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_transactions");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (pk_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	pk_treeview_add_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* make the refresh button non-clickable until we get completion */
	widget = glade_xml_get_widget (glade_xml, "button_rollback");
	gtk_widget_set_sensitive (widget, FALSE);

	/* get the update list */
	pk_client_get_old_transactions (client, 50);
	gtk_widget_show (main_window);

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	g_object_unref (list_store);
	g_object_unref (client);
	g_object_unref (pconnection);
	g_object_unref (role_list);
	g_free (transaction_id);

	return 0;
}
