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
#include <pk-common.h>

#include "pk-common-gui.h"

static GladeXML *glade_xml = NULL;
static GtkListStore *list_store_general = NULL;
static GtkListStore *list_store_details = NULL;
static PkClient *client = NULL;
static gchar *transaction_id = NULL;
static GHashTable *hash = NULL;

enum
{
	PACKAGES_COLUMN_GENERAL_ICON,
	PACKAGES_COLUMN_GENERAL_TEXT,
	PACKAGES_COLUMN_GENERAL_SUCCEEDED,
	PACKAGES_COLUMN_GENERAL_ID,
	PACKAGES_COLUMN_GENERAL_LAST
};

enum
{
	PACKAGES_COLUMN_DETAILS_ICON,
	PACKAGES_COLUMN_DETAILS_TEXT,
	PACKAGES_COLUMN_DETAILS_LAST
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
pk_button_rollback_cb (GtkWidget *widget, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	pk_client_rollback (client, transaction_id);
	g_main_loop_quit (loop);
}

/**
 * pk_button_close_cb:
 **/
static void
pk_button_close_cb (GtkWidget *widget, gpointer data)
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
		   gboolean succeeded, PkRoleEnum role, guint duration, const gchar *data, gpointer user_data)
{
	GtkTreeIter iter;
	GdkPixbuf *icon;
	gchar *text;
	gchar *pretty;
	const gchar *icon_name;
	const gchar *role_text;

	/* we save this */
	g_hash_table_insert (hash, g_strdup (tid), g_strdup (data));

	pretty = pk_transaction_db_get_pretty_date (timespec);
	pk_debug ("pretty=%s", pretty);
	role_text = pk_role_enum_to_localised_past (role);
	text = g_markup_printf_escaped ("<b>%s</b>\n%s", role_text, pretty);
	g_free (pretty);

	gtk_list_store_append (list_store_general, &iter);
	gtk_list_store_set (list_store_general, &iter,
			    PACKAGES_COLUMN_GENERAL_TEXT, text,
			    PACKAGES_COLUMN_GENERAL_ID, tid,
			    -1);

	icon_name = pk_role_enum_to_icon_name (role);
	icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), icon_name, 48, 0, NULL);
	if (icon) {
		gtk_list_store_set (list_store_general, &iter, PACKAGES_COLUMN_GENERAL_ICON, icon, -1);
		gdk_pixbuf_unref (icon);
	}

	if (succeeded == TRUE) {
		icon_name = "document-new";
	} else {
		icon_name = "dialog-error";
	}
	icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), icon_name, 24, 0, NULL);
	if (icon) {
		gtk_list_store_set (list_store_general, &iter, PACKAGES_COLUMN_GENERAL_SUCCEEDED, icon, -1);
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
			    gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	g_main_loop_quit (loop);
	return FALSE;
}

/**
 * pk_treeview_add_general_columns:
 **/
static void
pk_treeview_add_general_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* image */
	renderer = gtk_cell_renderer_pixbuf_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Role"), renderer,
							   "pixbuf", PACKAGES_COLUMN_GENERAL_ICON, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Transaction"), renderer,
							   "markup", PACKAGES_COLUMN_GENERAL_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_GENERAL_TEXT);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, TRUE);

	/* image */
	renderer = gtk_cell_renderer_pixbuf_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Succeeded"), renderer,
							   "pixbuf", PACKAGES_COLUMN_GENERAL_SUCCEEDED, NULL);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * pk_treeview_add_details_columns:
 **/
static void
pk_treeview_add_details_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* image */
	renderer = gtk_cell_renderer_pixbuf_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Type"), renderer,
							   "pixbuf", PACKAGES_COLUMN_DETAILS_ICON, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Details"), renderer,
							   "markup", PACKAGES_COLUMN_DETAILS_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_DETAILS_TEXT);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, TRUE);
}


/**
 * pk_details_item_add:
 **/
static void
pk_details_item_add (GtkListStore *list_store, PkInfoEnum info, const gchar *package_id, const gchar *summary)
{
	GtkTreeIter iter;
	GdkPixbuf *icon;
	gchar *text;
	const gchar *icon_name;
	const gchar *info_text;

	info_text = pk_info_enum_to_localised_text (info);
	text = pk_package_id_pretty (package_id, summary);
	icon_name = pk_info_enum_to_icon_name (info);

	gtk_list_store_append (list_store_details, &iter);
	gtk_list_store_set (list_store_details, &iter,
			    PACKAGES_COLUMN_DETAILS_TEXT, text, -1);
	icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), icon_name, 24, 0, NULL);
	if (icon) {
		gtk_list_store_set (list_store_details, &iter, PACKAGES_COLUMN_DETAILS_ICON, icon, -1);
		gdk_pixbuf_unref (icon);
	}
	g_free (text);
}

/**
 * pk_treeview_details_populate:
 **/
static void
pk_treeview_details_populate (const gchar *tid)
{
	GtkWidget *widget;
	gchar **array;
	gchar **sections;
	guint i;
	guint size;
	PkInfoEnum info;
	gchar *transaction_data;

	/* get from hash */
	transaction_data = (gchar *) g_hash_table_lookup (hash, tid);

	/* no details? */
	if (pk_strzero (transaction_data) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "frame_details");
		gtk_widget_hide (widget);
		return;
	}

	widget = glade_xml_get_widget (glade_xml, "treeview_details");
	gtk_list_store_clear (list_store_details);

	array = g_strsplit (transaction_data, "\n", 0);
	size = g_strv_length (array);
	for (i=0; i<size; i++) {
		sections = g_strsplit (array[i], "\t", 0);
		info = pk_info_enum_from_text (sections[0]);
		pk_details_item_add (list_store_details, info, sections[1], sections[2]);
		g_strfreev (sections);
	}
	g_strfreev (array);

	widget = glade_xml_get_widget (glade_xml, "frame_details");
	gtk_widget_show (widget);
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
				    PACKAGES_COLUMN_GENERAL_ID, &id, -1);

		/* make back into transaction_id */
		transaction_id = g_strdup (id);
		g_free (id);
		g_print ("selected row is: %s\n", transaction_id);

		/* get the decription */
		pk_treeview_details_populate (transaction_id);
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
	g_option_context_set_summary (context, _("Transaction Viewer"));
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
	g_signal_connect (client, "transaction",
			  G_CALLBACK (pk_transaction_cb), NULL);

	/* get actions */
	role_list = pk_client_get_actions (client);

	/* save the description in a hash */
	hash = g_hash_table_new (g_str_hash, g_str_equal);

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
			  G_CALLBACK (pk_button_rollback_cb), loop);

	gtk_widget_set_size_request (main_window, 500, 300);

	/* create list stores */
	list_store_general = gtk_list_store_new (PACKAGES_COLUMN_GENERAL_LAST, GDK_TYPE_PIXBUF,
						 G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_STRING);
	list_store_details = gtk_list_store_new (PACKAGES_COLUMN_DETAILS_LAST, GDK_TYPE_PIXBUF, G_TYPE_STRING);

	/* create transaction_id tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_transactions");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store_general));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (pk_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	pk_treeview_add_general_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* create transaction_id tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_details");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store_details));
	pk_treeview_add_details_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* make the refresh button non-clickable if we can't do the action */
	widget = glade_xml_get_widget (glade_xml, "button_rollback");
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_ROLLBACK) == TRUE) {
		gtk_widget_set_sensitive (widget, TRUE);
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
	}

	/* get the update list */
	pk_client_get_old_transactions (client, 0);
	gtk_widget_show (main_window);

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	g_object_unref (list_store_general);
	g_object_unref (list_store_details);
	g_object_unref (client);
	g_object_unref (pconnection);
	g_object_unref (role_list);
	g_free (transaction_id);
	g_hash_table_unref (hash);

	return 0;
}
