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
#include <locale.h>

#include <polkit-gnome/polkit-gnome.h>

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-control.h>
#include <pk-connection.h>
#include <pk-package-id.h>
#include <pk-common.h>

#include <gpk-common.h>
#include <gpk-gnome.h>

static GladeXML *glade_xml = NULL;
static GtkListStore *list_store_general = NULL;
static GtkListStore *list_store_details = NULL;
static PkClient *client = NULL;
static gchar *transaction_id = NULL;
static GHashTable *hash = NULL;
static PolKitGnomeAction *rollback_action = NULL;

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
	gpk_gnome_help ("software-sources");
}

/**
 * pk_button_rollback_cb:
 **/
static void
pk_button_rollback_cb (PolKitGnomeAction *action, gpointer data)
{
	gboolean ret;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) data;

	/* rollback */
	ret = pk_client_rollback (client, transaction_id, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
	}

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
	GTimeVal timeval_now;
	gchar buffer[100];
	guint hours;

	/* the old date */
	g_time_val_from_iso8601 (timespec, &timeval);

	/* the new date */
	g_get_current_time (&timeval_now);

	/* the difference in hours */
	hours = (timeval_now.tv_sec - timeval.tv_sec) / (60 * 60);
	pk_debug ("hours is %i", hours);

	/* get printed string */
	date = g_date_new ();
	g_date_set_time_val (date, &timeval);

	g_date_strftime (buffer, 100, _("%A, %d %B %Y"), date);
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
	gchar *text;
	gchar *pretty;
	const gchar *icon_name;
	const gchar *role_text;

	/* we save this */
	g_hash_table_insert (hash, g_strdup (tid), g_strdup (data));

	pretty = pk_transaction_db_get_pretty_date (timespec);
	pk_debug ("pretty=%s", pretty);
	role_text = gpk_role_enum_to_localised_past (role);
	text = g_markup_printf_escaped ("<b>%s</b>\n%s", role_text, pretty);
	g_free (pretty);

	gtk_list_store_append (list_store_general, &iter);
	gtk_list_store_set (list_store_general, &iter,
			    PACKAGES_COLUMN_GENERAL_TEXT, text,
			    PACKAGES_COLUMN_GENERAL_ID, tid,
			    -1);

	icon_name = gpk_role_enum_to_icon_name (role);
	gtk_list_store_set (list_store_general, &iter, PACKAGES_COLUMN_GENERAL_ICON, icon_name, -1);

	if (succeeded) {
		icon_name = "document-new";
	} else {
		icon_name = "dialog-error";
	}
	gtk_list_store_set (list_store_general, &iter, PACKAGES_COLUMN_GENERAL_SUCCEEDED, icon_name, -1);
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
        g_object_set (renderer, "stock-size", GTK_ICON_SIZE_DIALOG, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Role"), renderer,
							   "icon-name", PACKAGES_COLUMN_GENERAL_ICON, NULL);
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
        g_object_set (renderer, "stock-size", GTK_ICON_SIZE_BUTTON, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Succeeded"), renderer,
							   "icon-name", PACKAGES_COLUMN_GENERAL_SUCCEEDED, NULL);
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
        g_object_set (renderer, "stock-size", GTK_ICON_SIZE_DIALOG, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Type"), renderer,
							   "icon-name", PACKAGES_COLUMN_DETAILS_ICON, NULL);
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
	gchar *text;
	const gchar *icon_name;
	const gchar *info_text;

	info_text = gpk_info_enum_to_localised_text (info);
	text = gpk_package_id_format_twoline (package_id, summary);
	icon_name = gpk_info_enum_to_icon_name (info);

	gtk_list_store_append (list_store_details, &iter);
	gtk_list_store_set (list_store_details, &iter,
			    PACKAGES_COLUMN_DETAILS_TEXT, text, 
			    PACKAGES_COLUMN_DETAILS_ICON, icon_name, -1);
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
	if (pk_strzero (transaction_data)) {
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
		pk_debug ("selected row is: %s", transaction_id);

		/* get the decription */
		pk_treeview_details_populate (transaction_id);
	} else {
		pk_debug ("no row selected");
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
	PkRoleEnum roles;
	PolKitAction *pk_action;
	GtkWidget *button;
	PkControl *control;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  N_("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
		  N_("Show the program version and exit"), NULL },
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
	g_option_context_set_summary (context, _("Software Log Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
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
	control = pk_control_new ();
	roles = pk_control_get_actions (control);
	g_object_unref (control);

	/* save the description in a hash */
	hash = g_hash_table_new (g_str_hash, g_str_equal);

	pconnection = pk_connection_new ();
	g_signal_connect (pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), NULL);

	glade_xml = glade_xml_new (PK_DATA "/gpk-log.glade", NULL, NULL);
	main_window = glade_xml_get_widget (glade_xml, "window_transactions");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), "system-software-update");

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

	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.rollback");
	rollback_action = polkit_gnome_action_new_default ("rollback",
							   pk_action,
							   _("_Rollback"),
							   NULL);
	g_object_set (rollback_action,
		      "no-icon-name", "gtk-go-back-ltr",
		      "auth-icon-name", "gtk-go-back-ltr",
		      "yes-icon-name", "gtk-go-back-ltr",
		      "self-blocked-icon-name", "gtk-go-back-ltr",
		      NULL);
	polkit_action_unref (pk_action);
	g_signal_connect (rollback_action, "activate",
			  G_CALLBACK (pk_button_rollback_cb), loop);
	button = polkit_gnome_action_create_button (rollback_action);
	widget = glade_xml_get_widget (glade_xml, "buttonbox");
        gtk_box_pack_start (GTK_BOX (widget), button, FALSE, FALSE, 0);
        gtk_box_reorder_child (GTK_BOX (widget), button, 1);
	/* hide the rollback button if we can't do the action */
	if (pk_enums_contain (roles, PK_ROLE_ENUM_ROLLBACK)) {
		polkit_gnome_action_set_visible (rollback_action, TRUE);
	} else {
		polkit_gnome_action_set_visible (rollback_action, FALSE);
	}

	gtk_widget_set_size_request (main_window, 500, 300);

	/* create list stores */
	list_store_general = gtk_list_store_new (PACKAGES_COLUMN_GENERAL_LAST, G_TYPE_STRING,
						 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	list_store_details = gtk_list_store_new (PACKAGES_COLUMN_DETAILS_LAST, G_TYPE_STRING, G_TYPE_STRING);

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

	/* get the update list */
	pk_client_get_old_transactions (client, 0, NULL);
	gtk_widget_show (main_window);

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	g_object_unref (glade_xml);
	g_object_unref (list_store_general);
	g_object_unref (list_store_details);
	g_object_unref (client);
	g_object_unref (pconnection);
	g_free (transaction_id);
	g_hash_table_unref (hash);

	return 0;
}
