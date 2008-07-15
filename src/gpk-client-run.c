/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
#include <string.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include <pk-debug.h>
#include <pk-common.h>
#include <pk-client.h>
#include <pk-enum.h>
#include <pk-package-id.h>
#include "gpk-gnome.h"
#include "gpk-client.h"
#include "gpk-common.h"

static GtkListStore *list_store = NULL;
static gchar *full_path = NULL;
static gchar *last_tryexec = NULL;

enum
{
	GPK_CHOOSER_COLUMN_ICON,
	GPK_CHOOSER_COLUMN_TEXT,
	GPK_CHOOSER_COLUMN_FULL_PATH,
	GPK_CHOOSER_COLUMN_LAST
};

/**
 * gpk_client_run_button_help_cb:
 **/
static void
gpk_client_run_button_help_cb (GtkWidget *widget, gpointer data)
{
	gpk_gnome_help ("application-run");
}

/**
 * gpk_client_run_button_close_cb:
 **/
static void
gpk_client_run_button_close_cb (GtkWidget *widget, gpointer data)
{
	/* clear full_path */
	g_free (full_path);
	full_path = NULL;
	gtk_main_quit ();
}

/**
 * gpk_client_run_button_action_cb:
 **/
static void
gpk_client_run_button_action_cb (GtkWidget *widget, gpointer data)
{
	gtk_main_quit ();
}

/**
 * gpk_client_run_treeview_clicked_cb:
 **/
static void
gpk_client_run_treeview_clicked_cb (GtkTreeSelection *selection, gboolean data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (full_path);
		gtk_tree_model_get (model, &iter, GPK_CHOOSER_COLUMN_FULL_PATH, &full_path, -1);

		/* show full_path */
		pk_debug ("selected row is: %s", full_path);
	} else {
		pk_debug ("no row selected");
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
		return gtk_button_new_with_mnemonic (_("_Run"));
	}
	pk_warning ("name unknown=%s", name);
	return NULL;
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
	column = gtk_tree_view_column_new_with_attributes (_("Icon"), renderer,
							   "icon-name", GPK_CHOOSER_COLUMN_ICON, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Package"), renderer,
							   "markup", GPK_CHOOSER_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_CHOOSER_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, TRUE);
}

/**
 * gpk_client_add_executable:
 **/
static void
gpk_client_add_executable (const gchar *package_id, const gchar *path)
{
	gboolean ret;
	gchar *icon = NULL;
	gchar *text = NULL;
	gchar *name = NULL;
	gchar *exec = NULL;
	gchar *summary = NULL;
	gchar *joint = NULL;
	GtkTreeIter iter;
	GKeyFile *file;
	PkPackageId *id;

	/* get some data from the desktop file */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, NULL);
	if (ret) {
		exec = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, "TryExec", NULL);
		/* try harder */
		if (exec == NULL) {
			exec = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, "Exec", NULL);
		}
		/* try harder */
		if (exec == NULL) {
			/* abandon attempt */
			goto out;
		}

		/* have we the same executable name?
		 * this helps when there's "f-spot", "fspot --import %f", and "f-spot --view" in 3
		 * different desktop files */
		if (pk_strequal (exec, last_tryexec)) {
			pk_debug ("same as the last exec '%s' so skipping", exec);
			goto out;
		}

		/* save for next time */
		g_free (last_tryexec);
		last_tryexec = g_strdup (exec);

		name = g_key_file_get_locale_string (file, G_KEY_FILE_DESKTOP_GROUP, "Name", NULL, NULL);
		icon = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, "Icon", NULL);
		summary = g_key_file_get_locale_string (file, G_KEY_FILE_DESKTOP_GROUP, "Comment", NULL, NULL);
		/* try harder */
		if (summary == NULL) {
			summary = g_key_file_get_locale_string (file, G_KEY_FILE_DESKTOP_GROUP, "GenericName", NULL, NULL);
		}
	}

	/* put formatted text into treeview */
	gtk_list_store_append (list_store, &iter);
	joint = g_strdup_printf ("%s - %s", name, summary);
	id = pk_package_id_new_from_string (package_id);
	text = gpk_package_id_format_twoline (id, joint);
	pk_package_id_free (id);

	/* might not be valid */
	if (!gpk_check_icon_valid (icon)) {
		g_free (icon);
		icon = NULL;
	}
	if (icon == NULL) {
		icon = g_strdup (gpk_info_enum_to_icon_name (PK_INFO_ENUM_AVAILABLE));
	}
	gtk_list_store_set (list_store, &iter,
			    GPK_CHOOSER_COLUMN_TEXT, text,
			    GPK_CHOOSER_COLUMN_FULL_PATH, exec, -1);
	gtk_list_store_set (list_store, &iter, GPK_CHOOSER_COLUMN_ICON, icon, -1);

out:
	g_key_file_free (file);
	g_free (exec);
	g_free (icon);
	g_free (name);
	g_free (text);
	g_free (joint);
	g_free (summary);
}

/**
 * gpk_client_add_package_ids:
 **/
static guint
gpk_client_add_package_ids (gchar **package_ids)
{
	guint i, j;
	guint length;
	guint added = 0;
	guint files_len;
	gchar **files;
	const gchar *package_id;
	GError *error = NULL;
	GpkClient *gclient;

	length = g_strv_length (package_ids);
	gclient = gpk_client_new ();
	/* only show if we need to download a cache */
	gpk_client_set_interaction (gclient, GPK_CLIENT_INTERACT_SOMETIMES);
	gpk_client_show_finished (gclient, FALSE);

	for (i=0; i<length; i++) {
		package_id = package_ids[i];
		pk_debug ("package_id=%s", package_id);
		files = gpk_client_get_file_list (gclient, package_ids[0], &error);
		if (files == NULL) {
			pk_warning ("could not get file list: %s", error->message);
			g_error_free (error);
			error = NULL;
			continue;
		}
		files_len = g_strv_length (files);
		for (j=0; j<files_len; j++) {
			if (g_str_has_suffix (files[j], ".desktop")) {
				pk_debug ("package=%s, file=%s", package_id, files[j]);
				gpk_client_add_executable (package_id, files[j]);
				added++;
			}
		}
	}
	g_object_unref (gclient);
	return added;
}

/**
 * gpk_client_run_show:
 *
 * Return value: the package_id of the selected package, or NULL
 **/
gchar *
gpk_client_run_show (gchar **package_ids)
{
	GladeXML *glade_xml;
	GtkWidget *widget;
	GtkTreeSelection *selection;
	guint len;

	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* use custom widgets */
	glade_set_custom_handler (gpk_update_viewer_create_custom_widget, NULL);

	glade_xml = glade_xml_new (PK_DATA "/gpk-log.glade", NULL, NULL);

	/* connect up default actions */
	widget = glade_xml_get_widget (glade_xml, "window_simple");
	g_signal_connect_swapped (widget, "delete_event", G_CALLBACK (gtk_main_quit), NULL);
	gtk_widget_set_size_request (widget, 600, 300);

	/* connect up buttons */
	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_run_button_help_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_run_button_close_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_action");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_run_button_action_cb), NULL);
	gtk_widget_show (widget);

	/* set icon name */
	widget = glade_xml_get_widget (glade_xml, "window_simple");
	gtk_window_set_icon_name (GTK_WINDOW (widget), "system-software-installer");
	gtk_window_set_title (GTK_WINDOW (widget), _("Run new application?"));

	/* create list stores */
	list_store = gtk_list_store_new (GPK_CHOOSER_COLUMN_LAST, G_TYPE_STRING,
					 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* create package_id tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_simple");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_client_run_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	pk_treeview_add_general_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* add all the apps */
	len = gpk_client_add_package_ids (package_ids);
	if (len == 0) {
		goto out;
	}

	/* show window */
	widget = glade_xml_get_widget (glade_xml, "window_simple");
	gtk_widget_show (widget);

	/* wait for button press */
	gtk_main ();

out:
	g_free (last_tryexec);

	/* hide window */
	widget = glade_xml_get_widget (glade_xml, "window_simple");
	if (GTK_IS_WIDGET (widget)) {
		gtk_widget_hide (widget);
	}

	g_object_unref (glade_xml);

	return full_path;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
gpk_client_run_self_test (gpointer data)
{
	LibSelfTest *test = (LibSelfTest *) data;

	if (libst_start (test, "GpkClientRun", CLASS_AUTO) == FALSE) {
		return;
	}
	libst_end (test);
}
#endif

