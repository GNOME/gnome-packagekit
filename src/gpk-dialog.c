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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

#include "gpk-common.h"
#include "gpk-dialog.h"
#include "gpk-enum.h"

enum {
	GPK_DIALOG_STORE_IMAGE,
	GPK_DIALOG_STORE_ID,
	GPK_DIALOG_STORE_TEXT,
	GPK_DIALOG_STORE_LAST
};

gchar *
gpk_dialog_package_id_name_join_locale (gchar **package_ids)
{
	guint i;
	guint length;
	gchar *text;
	g_autoptr(GPtrArray) array = NULL;
	g_auto(GStrv) array_strv = NULL;

	length = g_strv_length (package_ids);
	array = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < length; i++) {
		g_auto(GStrv) split = NULL;
		split = pk_package_id_split (package_ids[i]);
		if (split == NULL) {
			g_warning ("failed to split %s", package_ids[i]);
			continue;
		}
		g_ptr_array_add (array, g_strdup (split[0]));
	}
	array_strv = pk_ptr_array_to_strv (array);
	text = gpk_strv_join_locale (array_strv);
	if (text == NULL) {
		/* TRANSLATORS: This is when we have over 5 items, and we're not interested in detail */
		text = g_strdup (_("many packages"));
	}
	return text;
}

static GtkListStore *
gpk_dialog_package_array_to_list_store (GPtrArray *array)
{
	GtkListStore *store;
	GtkTreeIter iter;
	PkPackage *item;
	const gchar *icon;
	guint i;
	PkInfoEnum info;

	store = gtk_list_store_new (GPK_DIALOG_STORE_LAST, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* add each well */
	for (i = 0; i < array->len; i++) {
		g_auto(GStrv) split = NULL;
		g_autofree gchar *text = NULL;
		g_autofree gchar *package_id = NULL;
		g_autofree gchar *summary = NULL;
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);
		text = gpk_package_id_format_twoline (NULL, package_id, summary);

		/* get the icon */
		split = pk_package_id_split (package_id);
		icon = gpk_info_enum_to_icon_name (info);

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    GPK_DIALOG_STORE_IMAGE, icon,
				    GPK_DIALOG_STORE_ID, package_id,
				    GPK_DIALOG_STORE_TEXT, text,
				    -1);
	}

	return store;
}

static gboolean
gpk_dialog_treeview_for_package_list (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;

	/* column for images */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_DND, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "icon-name", GPK_DIALOG_STORE_IMAGE);
	gtk_tree_view_append_column (treeview, column);

	/* column for name */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Name"), renderer,
							   "markup", GPK_DIALOG_STORE_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_DIALOG_STORE_TEXT);
	gtk_tree_view_append_column (treeview, column);

	/* set some common options */
	gtk_tree_view_set_headers_visible (treeview, FALSE);
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_NONE);
	gtk_tree_selection_unselect_all (selection);

	return TRUE;
}

static void
gpk_dialog_widget_unrealize_unref_cb (GtkWidget *widget, GObject *obj)
{
	g_object_unref (obj);
}

gboolean
gpk_dialog_embed_package_list_widget (GtkDialog *dialog, GPtrArray *array)
{
	GtkWidget *scroll;
	GtkListStore *store;
	GtkWidget *widget;
	const guint row_height = 48;

	/* convert to a store */
	store = gpk_dialog_package_array_to_list_store (array);

	/* create a treeview to hold the store */
	widget = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	gpk_dialog_treeview_for_package_list (GTK_TREE_VIEW (widget));
	gtk_widget_show (widget);

	/* scroll the treeview */
	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scroll), widget);
	gtk_widget_show (scroll);

	/* add some spacing to conform to the GNOME HIG */
	gtk_container_set_border_width (GTK_CONTAINER (scroll), 6);

	/* only allow more space if there are a large number of items */
	if (array->len > 5) {
		gtk_widget_set_size_request (GTK_WIDGET (scroll), -1, (row_height * 5) + 8);
	} else if (array->len > 1) {
		gtk_widget_set_size_request (GTK_WIDGET (scroll), -1, (row_height * array->len) + 8);
	}

	/* add scrolled window */
	widget = gtk_dialog_get_content_area (GTK_DIALOG(dialog));
	gtk_container_add_with_properties (GTK_CONTAINER (widget), scroll,
					   "expand", TRUE,
					   "fill", TRUE,
					   NULL);

	/* free the store */
	g_signal_connect (G_OBJECT (dialog), "unrealize",
			  G_CALLBACK (gpk_dialog_widget_unrealize_unref_cb), store);

	return TRUE;
}

gboolean
gpk_dialog_embed_file_list_widget (GtkDialog *dialog, GPtrArray *files)
{
	GtkWidget *scroll;
	GtkWidget *widget;
	GtkTextBuffer *buffer;
	g_auto(GStrv) array = NULL;
	g_autofree gchar *text = NULL;

	/* split and show */
	array = pk_ptr_array_to_strv (files);
	text = g_strjoinv ("\n", array);
	if (text[0] == '\0') {
		g_free (text);
		text = g_strdup (_("No files"));
	}

	/* create a text view to hold the store */
	widget = gtk_text_view_new ();
	gtk_text_view_set_editable (GTK_TEXT_VIEW (widget), FALSE);
	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (widget), 5);
	gtk_text_view_set_right_margin (GTK_TEXT_VIEW (widget), 5);

	/* scroll the treeview */
	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
					GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scroll), widget);
	gtk_widget_show (scroll);

	/* set in buffer */
	buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_set_text (buffer, text, -1);
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);
	gtk_widget_show (widget);

	/* add some spacing to conform to the GNOME HIG */
	gtk_container_set_border_width (GTK_CONTAINER (scroll), 6);
	gtk_widget_set_size_request (GTK_WIDGET (scroll), -1, 300);

	/* add scrolled window */
	widget = gtk_dialog_get_content_area (GTK_DIALOG(dialog));
	gtk_box_pack_start (GTK_BOX (widget), scroll, TRUE, TRUE, 0);

	return TRUE;
}

static void
gpk_client_checkbutton_show_depends_cb (GtkWidget *widget, const gchar *key)
{
	gboolean checked;
	g_autoptr(GSettings) settings = NULL;

	/* set the policy */
	checked = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	g_debug ("Changing %s to %i", key, checked);
	settings = g_settings_new (GPK_SETTINGS_SCHEMA);
	g_settings_set_boolean (settings, key, !checked);
}

gboolean
gpk_dialog_embed_do_not_show_widget (GtkDialog *dialog, const gchar *key)
{
	GtkWidget *check_button;
	GtkWidget *widget;
	gboolean checked;
	g_autoptr(GSettings) settings = NULL;

	/* add a checkbutton for deps screen */
	check_button = gtk_check_button_new_with_label (_("Do not show this again"));
	g_signal_connect (check_button, "clicked", G_CALLBACK (gpk_client_checkbutton_show_depends_cb), (gpointer) key);
	widget = gtk_dialog_get_content_area (GTK_DIALOG(dialog));
	gtk_container_add_with_properties (GTK_CONTAINER (widget), check_button,
					   "expand", FALSE,
					   "fill", FALSE,
					   NULL);

	/* checked? */
	settings = g_settings_new (GPK_SETTINGS_SCHEMA);
	checked = g_settings_get_boolean (settings, key);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_button), !checked);

	gtk_widget_show (check_button);
	return TRUE;
}

gboolean
gpk_dialog_embed_tabbed_widget (GtkDialog *dialog, GtkNotebook *tabbed_widget)
{
	GtkWidget *widget;

	if (! GTK_IS_NOTEBOOK (tabbed_widget))
		return FALSE;

	widget = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_container_add_with_properties (GTK_CONTAINER (widget),
					   GTK_WIDGET (tabbed_widget),
					   "expand", FALSE,
					   "fill", FALSE,
					   NULL);

	return TRUE;
}

gboolean
gpk_dialog_tabbed_package_list_widget (GtkWidget *tab_page, GPtrArray *array)
{
	GtkWidget *scroll;
	GtkListStore *store;
	GtkWidget *widget;
	const guint row_height = 48;

	/* convert to a store */
	store = gpk_dialog_package_array_to_list_store (array);

	/* create a treeview to hold the store */
	widget = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	gpk_dialog_treeview_for_package_list (GTK_TREE_VIEW (widget));
	gtk_widget_show (widget);

	/* scroll the treeview */
	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scroll), widget);
	gtk_widget_show (scroll);

	/* add some spacing to conform to the GNOME HIG */
	gtk_container_set_border_width (GTK_CONTAINER (scroll), 6);

	/* only allow more space if there are a large number of items */
	if (array->len > 5) {
		gtk_widget_set_size_request (GTK_WIDGET (scroll), -1, (row_height * 5) + 8);
	} else if (array->len > 1) {
		gtk_widget_set_size_request (GTK_WIDGET (scroll), -1, (row_height * array->len) + 8);
	}

	/* add scrolled window */
	gtk_container_add_with_properties (GTK_CONTAINER (tab_page), scroll,
					   "expand", TRUE,
					   "fill", TRUE,
					   NULL);

	/* free the store */
	g_signal_connect (G_OBJECT (tab_page), "unrealize",
			  G_CALLBACK (gpk_dialog_widget_unrealize_unref_cb), store);

	return TRUE;
}

gboolean
gpk_dialog_tabbed_download_size_widget (GtkWidget *tab_page, const gchar *title, guint64 size)
{
	GtkWidget *label;
	GtkWidget *hbox;
	g_autofree gchar *text = NULL;
	g_autofree gchar *size_str = NULL;

	/* size is zero, don't show "0 bytes" */
	if (size == 0) {
		label = gtk_label_new (title);
		gtk_container_add_with_properties (GTK_CONTAINER (tab_page), label,
						   "expand", FALSE,
						   "fill", FALSE,
						   NULL);
		goto out;
	}

	/* add a hbox with the size for deps screen */
	size_str = g_format_size (size);
	text = g_strdup_printf ("%s: %s", title, size_str);
	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_add_with_properties (GTK_CONTAINER (tab_page), hbox,
					   "expand", FALSE,
					   "fill", FALSE,
					   NULL);

	/* add a label */
	label = gtk_label_new (text);
	gtk_box_pack_start (GTK_BOX(hbox), label, FALSE, FALSE, 0);
	gtk_widget_show (hbox);
out:
	gtk_widget_show (label);
	return TRUE;
}
