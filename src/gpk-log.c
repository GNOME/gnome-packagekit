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

/* local .la */
#include <libunique.h>

#include "egg-debug.h"
#include <pk-client.h>
#include <pk-control.h>
#include <pk-package-id.h>
#include <pk-common.h>

#include <gpk-common.h>
#include <gpk-gnome.h>

static GladeXML *glade_xml = NULL;
static GtkListStore *list_store = NULL;
static PkClient *client = NULL;
static gchar *transaction_id = NULL;
static PolKitGnomeAction *button_action = NULL;

enum
{
	GPK_LOG_COLUMN_ICON,
	GPK_LOG_COLUMN_TEXT,
	GPK_LOG_COLUMN_ID,
	GPK_LOG_COLUMN_LAST
};

/**
 * gpk_log_button_help_cb:
 **/
static void
gpk_log_button_help_cb (GtkWidget *widget, gboolean data)
{
	gpk_gnome_help ("update-log");
}

/**
 * gpk_log_button_rollback_cb:
 **/
static void
gpk_log_button_rollback_cb (PolKitGnomeAction *action, gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	/* rollback */
	ret = pk_client_rollback (client, transaction_id, &error);
	if (!ret) {
		egg_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
	}
	gtk_main_quit ();
}

/**
 * gpk_log_get_localised_date:
 **/
static gchar *
gpk_log_get_localised_date (const gchar *timespec)
{
	GDate *date;
	GTimeVal timeval;
	gchar buffer[100];

	/* the old date */
	g_time_val_from_iso8601 (timespec, &timeval);

	/* get printed string */
	date = g_date_new ();
	g_date_set_time_val (date, &timeval);

	g_date_strftime (buffer, 100, _("%A, %d %B %Y"), date);
	g_date_free (date);
	return g_strdup (buffer);
}

/**
 * gpk_log_get_type_line:
 **/
static gchar *
gpk_log_get_type_line (gchar **array, PkInfoEnum info)
{
	guint i;
	guint size;
	PkInfoEnum info_local;
	GString *string;
	gchar *text;
	gchar *whole;
	gchar **sections;
	PkPackageId *id;

	string = g_string_new ("");
	size = g_strv_length (array);
	for (i=0; i<size; i++) {
		sections = g_strsplit (array[i], "\t", 0);
		info_local = pk_info_enum_from_text (sections[0]);
		if (info_local == info) {
			id = pk_package_id_new_from_string (sections[1]);
			text = gpk_package_id_format_oneline (id, NULL);
			g_string_append_printf (string, "%s, ", text);
			g_free (text);
			pk_package_id_free (id);
		}
		g_strfreev (sections);
	}

	/* nothing, so return NULL */
	if (string->len == 0) {
		g_string_free (string, TRUE);
		return NULL;
	}

	/* remove last comma space */
	g_string_set_size (string, string->len - 2);

	/* add a nice header, and make text italic */
	text = g_string_free (string, FALSE);
	whole = g_strdup_printf ("<b>%s</b>: %s\n", gpk_info_enum_to_localised_past (info), text);
	g_free (text);
	return whole;
}

/**
 * gpk_log_get_transaction_item:
 **/
static gchar *
gpk_log_get_transaction_item (const gchar *timespec, const gchar *data)
{
	gchar *pretty;
	GString *string;
	gchar *text;
	gchar **array;

	pretty = gpk_log_get_localised_date (timespec);
	string = g_string_new ("");
	g_string_append_printf (string, "<big><b>%s</b></big>\n", pretty);
	g_free (pretty);

	array = g_strsplit (data, "\n", 0);

	/* get each type */
	text = gpk_log_get_type_line (array, PK_INFO_ENUM_INSTALLING);
	if (text != NULL) {
		g_string_append (string, text);
	}
	g_free (text);
	text = gpk_log_get_type_line (array, PK_INFO_ENUM_REMOVING);
	if (text != NULL) {
		g_string_append (string, text);
	}
	g_free (text);
	text = gpk_log_get_type_line (array, PK_INFO_ENUM_UPDATING);
	if (text != NULL) {
		g_string_append (string, text);
	}
	g_free (text);
	g_strfreev (array);

	/* remove last \n */
	if (string->len > 0) {
		g_string_set_size (string, string->len - 1);
	}

	return g_string_free (string, FALSE);
}

/**
 * gpk_log_transaction_cb:
 **/
static void
gpk_log_transaction_cb (PkClient *client, const gchar *tid, const gchar *timespec,
			gboolean succeeded, PkRoleEnum role, guint duration, const gchar *data, gpointer user_data)
{
	GtkTreeIter iter;
	gchar *text;
	const gchar *icon_name;

	/* only show transactions that succeeded */
	if (!succeeded) {
		egg_debug ("tid %s did not succeed, so not adding", tid);
		return;
	}

	/* put formatted text into treeview */
	text = gpk_log_get_transaction_item (timespec, data);
	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (list_store, &iter,
			    GPK_LOG_COLUMN_TEXT, text,
			    GPK_LOG_COLUMN_ID, tid, -1);
	g_free (text);

	icon_name = gpk_role_enum_to_icon_name (role);
	gtk_list_store_set (list_store, &iter, GPK_LOG_COLUMN_ICON, icon_name, -1);
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
							   "icon-name", GPK_LOG_COLUMN_ICON, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Transaction"), renderer,
							   "markup", GPK_LOG_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_LOG_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, TRUE);
}

/**
 * gpk_log_treeview_clicked_cb:
 **/
static void
gpk_log_treeview_clicked_cb (GtkTreeSelection *selection, gboolean data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *id;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (transaction_id);
		gtk_tree_model_get (model, &iter, GPK_LOG_COLUMN_ID, &id, -1);

		/* show transaction_id */
		egg_debug ("selected row is: %s", id);
		g_free (id);
	} else {
		egg_debug ("no row selected");
	}
}

/**
 * gpk_update_viewer_create_custom_widget:
 **/
static GtkWidget *
gpk_update_viewer_create_custom_widget (GladeXML *xml, gchar *func_name, gchar *name,
				        gchar *string1, gchar *string2,
				        gint int1, gint int2, gpointer user_data)
{
	if (pk_strequal (name, "button_action")) {
		return polkit_gnome_action_create_button (button_action);
	}
	egg_warning ("name unknown=%s", name);
	return NULL;
}

/**
 * gpk_update_viewer_setup_policykit:
 *
 * We have to do this before the glade stuff if done as the custom handler needs the actions setup
 **/
static void
gpk_update_viewer_setup_policykit (void)
{
	PolKitAction *pk_action;
	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.system-rollback");
	button_action = polkit_gnome_action_new_default ("rollback", pk_action, _("_Rollback"), NULL);
	g_object_set (button_action,
		      "no-icon-name", "gtk-go-back-ltr",
		      "auth-icon-name", "gtk-go-back-ltr",
		      "yes-icon-name", "gtk-go-back-ltr",
		      "self-blocked-icon-name", "gtk-go-back-ltr",
		      NULL);
	polkit_action_unref (pk_action);
}

/**
 * gpk_log_activated_cb
 **/
static void
gpk_log_activated_cb (LibUnique *libunique, gpointer data)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (glade_xml, "window_simple");
	gtk_window_present (GTK_WINDOW (widget));
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	GOptionContext *context;
	GtkWidget *widget;
	GtkTreeSelection *selection;
	PkBitfield roles;
	PkControl *control;
	LibUnique *libunique;
	gboolean ret;

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

	egg_debug_init (verbose);
	gtk_init (&argc, &argv);

	/* are we running privileged */
	ret = gpk_check_privileged_user (_("Log viewer"));
	if (!ret) {
		return 1;
	}

	/* are we already activated? */
	libunique = libunique_new ();
	ret = libunique_assign (libunique, "org.freedesktop.PackageKit.LogViewer");
	if (!ret) {
		goto unique_out;
	}
	g_signal_connect (libunique, "activated",
			  G_CALLBACK (gpk_log_activated_cb), NULL);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
                                           PK_DATA G_DIR_SEPARATOR_S "icons");

	/* we have to do this before we connect up the glade file */
	gpk_update_viewer_setup_policykit ();

	/* use custom widgets */
	glade_set_custom_handler (gpk_update_viewer_create_custom_widget, NULL);

	client = pk_client_new ();
	g_signal_connect (client, "transaction",
			  G_CALLBACK (gpk_log_transaction_cb), NULL);

	/* get actions */
	control = pk_control_new ();
	roles = pk_control_get_actions (control);
	g_object_unref (control);

	glade_xml = glade_xml_new (PK_DATA "/gpk-log.glade", NULL, NULL);
	widget = glade_xml_get_widget (glade_xml, "window_simple");
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_LOG);
	gtk_widget_set_size_request (widget, 500, 400);

	/* Get the main window quit */
	g_signal_connect_swapped (widget, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);
	gtk_widget_grab_default (widget);

	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_log_button_help_cb), NULL);

	/* connect up PolicyKit actions */
	g_signal_connect (button_action, "activate", G_CALLBACK (gpk_log_button_rollback_cb), NULL);

	/* hide the rollback button if we can't do the action */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_ROLLBACK)) {
		polkit_gnome_action_set_visible (button_action, TRUE);
	} else {
		polkit_gnome_action_set_visible (button_action, FALSE);
	}

	/* create list stores */
	list_store = gtk_list_store_new (GPK_LOG_COLUMN_LAST, G_TYPE_STRING,
						 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* create transaction_id tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_simple");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_log_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	pk_treeview_add_general_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* get the update list */
	pk_client_get_old_transactions (client, 0, NULL);

	/* show */
	widget = glade_xml_get_widget (glade_xml, "window_simple");
	gtk_widget_show (widget);
	gtk_main ();

	g_object_unref (glade_xml);
	g_object_unref (list_store);
	g_object_unref (client);
	g_free (transaction_id);
unique_out:
	g_object_unref (libunique);
	return 0;
}
