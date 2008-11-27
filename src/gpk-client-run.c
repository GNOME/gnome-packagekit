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
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-gnome.h"
#include "gpk-client.h"
#include "gpk-common.h"
#include "gpk-enum.h"
#include "gpk-desktop.h"

static GtkListStore *list_store = NULL;
static gchar *full_path = NULL;

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
		egg_debug ("selected row is: %s", full_path);
	} else {
		egg_debug ("no row selected");
	}
}

/**
 * gpk_client_run_row_activated_cb:
 **/
void
gpk_client_run_row_activated_cb (GtkTreeView *treeview, GtkTreePath *path,
				 GtkTreeViewColumn *col, gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean ret;

	/* get selection */
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter (model, &iter, path);
	if (!ret) {
		egg_warning ("failed to get selection");
		return;
	}

	g_free (full_path);
	gtk_tree_model_get (model, &iter, GPK_CHOOSER_COLUMN_FULL_PATH, &full_path, -1);
	gtk_main_quit ();
}

/**
 * gpk_update_viewer_create_custom_widget:
 **/
static GtkWidget *
gpk_update_viewer_create_custom_widget (GladeXML *xml, gchar *func_name, gchar *name,
				        gchar *string1, gchar *string2,
				        gint int1, gint int2, gpointer user_data)
{
	if (egg_strequal (name, "button_action"))
		/* TRANSLATORS: button: execute the application */
		return gtk_button_new_with_mnemonic (_("_Run"));
	egg_warning ("name unknown=%s", name);
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
	/* TRANSLATORS: column for the application icon */
	column = gtk_tree_view_column_new_with_attributes (_("Icon"), renderer,
							   "icon-name", GPK_CHOOSER_COLUMN_ICON, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	/* TRANSLATORS: column for the package name */
	column = gtk_tree_view_column_new_with_attributes (_("Package"), renderer,
							   "markup", GPK_CHOOSER_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_CHOOSER_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, TRUE);
}

/**
 * gpk_client_run_add_desktop_file:
 **/
static gboolean
gpk_client_run_add_desktop_file (const gchar *package_id, const gchar *filename)
{
	gboolean ret;
	gchar *icon = NULL;
	gchar *text = NULL;
	gchar *fulltext = NULL;
	gchar *name = NULL;
	gchar *exec = NULL;
	gchar *summary = NULL;
	gchar *joint = NULL;
	gchar *menu_path;
	GtkTreeIter iter;
	GKeyFile *file;
	PkPackageId *id;
	gint weight;

	/* get weight */
	weight = gpk_desktop_get_file_weight (filename);
	if (weight < 0) {
		egg_debug ("ignoring %s", filename);
		goto out;
	}

	/* get some data from the desktop file */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, filename, G_KEY_FILE_NONE, NULL);
	if (!ret) {
		egg_debug ("failed to load %s", filename);
		goto out;
	}

	exec = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, "TryExec", NULL);
	/* try harder */
	if (exec == NULL)
		exec = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, "Exec", NULL);

	/* abandon attempt */
	if (exec == NULL) {
		ret = FALSE;
		goto out;
	}

	name = g_key_file_get_locale_string (file, G_KEY_FILE_DESKTOP_GROUP, "Name", NULL, NULL);
	icon = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, "Icon", NULL);
	summary = g_key_file_get_locale_string (file, G_KEY_FILE_DESKTOP_GROUP, "Comment", NULL, NULL);
	/* try harder */
	if (summary == NULL)
		summary = g_key_file_get_locale_string (file, G_KEY_FILE_DESKTOP_GROUP, "GenericName", NULL, NULL);

	/* put formatted text into treeview */
	gtk_list_store_append (list_store, &iter);
	joint = g_strdup_printf ("%s - %s", name, summary);
	id = pk_package_id_new_from_string (package_id);
	text = gpk_package_id_format_twoline (id, joint);
	menu_path = gpk_desktop_get_menu_path (filename);
	if (menu_path) {
		/* TRANSLATORS: the path in the menu, e.g. Applications -> Games -> Dave */
		fulltext = g_strdup_printf("%s\n\n<i>%s %s</i>", text, _("Menu:"), menu_path);
		g_free (text);
		text = fulltext;
	}
	pk_package_id_free (id);

	/* might not be valid */
	if (!gpk_desktop_check_icon_valid (icon)) {
		g_free (icon);
		icon = NULL;
	}
	if (icon == NULL)
		icon = g_strdup (gpk_info_enum_to_icon_name (PK_INFO_ENUM_AVAILABLE));
	gtk_list_store_set (list_store, &iter,
			    GPK_CHOOSER_COLUMN_TEXT, fulltext,
			    GPK_CHOOSER_COLUMN_FULL_PATH, exec,
			    GPK_CHOOSER_COLUMN_ICON, icon, -1);
out:
	if (file != NULL)
		g_key_file_free (file);
	g_free (exec);
	g_free (icon);
	g_free (name);
	g_free (text);
	g_free (menu_path);
	g_free (joint);
	g_free (summary);

	return ret;
}

/**
 * gpk_client_run_add_package_ids:
 **/
static guint
gpk_client_run_add_package_ids (gchar **package_ids)
{
	guint i, j;
	guint length;
	guint added = 0;
	const gchar *filename;
	GPtrArray *array;
	gchar **parts;
	gboolean ret;
	PkDesktop *desktop;

	/* open database */
	desktop = pk_desktop_new ();
	ret = pk_desktop_open_database (desktop, NULL);
	if (!ret) {
		egg_debug ("failed to open desktop DB");
		goto out;
	}

	/* add each package */
	length = g_strv_length (package_ids);
	for (i=0; i<length; i++) {
		parts = g_strsplit (package_ids[i], ";", 0);
		array = pk_desktop_get_files_for_package (desktop, parts[0], NULL);
		if (array != NULL) {
			for (j=0; j<array->len; j++) {
				filename = g_ptr_array_index (array, j);
				ret = gpk_client_run_add_desktop_file (package_ids[i], filename);
				if (ret)
					added++;
			}
			g_ptr_array_foreach (array, (GFunc) g_free, NULL);
			g_ptr_array_free (array, TRUE);
		}
		g_strfreev (parts);
	}
	g_object_unref (desktop);
out:
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

	glade_xml = glade_xml_new (GPK_DATA "/gpk-log.glade", NULL, NULL);

	/* connect up default actions */
	widget = glade_xml_get_widget (glade_xml, "dialog_simple");
	g_signal_connect_swapped (widget, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	/* set a size, if the screen allows */
	gpk_window_set_size_request (GTK_WINDOW (widget), 600, 300);

	/* connect up buttons */
	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_run_button_help_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_run_button_close_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_action");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_run_button_action_cb), NULL);
	gtk_widget_show (widget);

	/* hide the filter box */
	widget = glade_xml_get_widget (glade_xml, "hbox_filter");
	gtk_widget_hide (widget);

	/* set icon name */
	widget = glade_xml_get_widget (glade_xml, "dialog_simple");
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);
	/* TRANSLATORS: window title: do we want to execute a program we just installed? */
	gtk_window_set_title (GTK_WINDOW (widget), _("Run new application?"));

	/* create list stores */
	list_store = gtk_list_store_new (GPK_CHOOSER_COLUMN_LAST, G_TYPE_STRING,
					 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* create package_id tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_simple");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store));
	g_signal_connect (GTK_TREE_VIEW (widget), "row-activated",
			  G_CALLBACK (gpk_client_run_row_activated_cb), NULL);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_client_run_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	pk_treeview_add_general_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* add all the apps */
	len = gpk_client_run_add_package_ids (package_ids);
	if (len == 0) {
		egg_debug ("no executable file for %s", package_ids[0]);
		goto out;
	}

	/* show window */
	widget = glade_xml_get_widget (glade_xml, "dialog_simple");
	gtk_widget_show (widget);

	/* wait for button press */
	gtk_main ();

out:
	/* hide window */
	widget = glade_xml_get_widget (glade_xml, "dialog_simple");
	if (GTK_IS_WIDGET (widget))
		gtk_widget_hide (widget);

	g_object_unref (glade_xml);

	return full_path;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpk_client_run_self_test (gpointer data)
{
	EggTest *test = (EggTest *) data;

	if (!egg_test_start (test, "GpkClientRun"))
		return;
	egg_test_end (test);
}
#endif

