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
#include <gtk/gtk.h>

#include <pk-common.h>
#include <pk-enum.h>
#include <pk-extra.h>
#include <pk-package-id.h>
#include <gconf/gconf-client.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-common.h"
#include "gpk-dialog.h"

enum {
	GPK_DIALOG_STORE_IMAGE,
	GPK_DIALOG_STORE_ID,
	GPK_DIALOG_STORE_TEXT,
	GPK_DIALOG_STORE_LAST
};

/**
 * gpk_dialog_package_id_name_join_locale:
 **/
gchar *
gpk_dialog_package_id_name_join_locale (gchar **package_ids)
{
	guint i;
	guint length;
	gchar *text;
	PkPackageId *ident;
	GPtrArray *array;
	gchar **array_strv;

	length = g_strv_length (package_ids);
	array = g_ptr_array_new ();
	for (i=0; i<length; i++) {
		ident = pk_package_id_new_from_string (package_ids[i]);
		if (ident == NULL) {
			egg_warning ("failed to split %s", package_ids[i]);
			continue;
		}
		g_ptr_array_add (array, g_strdup (ident->name));
		pk_package_id_free (ident);
	}
	array_strv = pk_ptr_array_to_argv (array);
	text = gpk_strv_join_locale (array_strv);
	g_strfreev (array_strv);
	if (text == NULL) {
		text = g_strdup (_("many packages"));
	}
	g_ptr_array_free (array, TRUE);
	return text;
}

/**
 * gpk_dialog_package_list_to_list_store:
 **/
static GtkListStore *
gpk_dialog_package_list_to_list_store (PkPackageList *list)
{
	GtkListStore *store;
	GtkTreeIter iter;
	const PkPackageObj *obj;
	PkExtra *extra;
	const gchar *icon;
	gchar *package_id;
	gchar *text;
	guint length;
	guint i;
	gboolean valid;

	extra = pk_extra_new ();
	store = gtk_list_store_new (GPK_DIALOG_STORE_LAST, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	length = pk_package_list_get_size (list);

	/* add each well */
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		text = gpk_package_id_format_twoline (obj->id, obj->summary);
		package_id = pk_package_id_to_string (obj->id);

		/* get the icon */
		icon = pk_extra_get_icon_name (extra, obj->id->name);
		valid = gpk_check_icon_valid (icon);
		if (!valid) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_INSTALLED);
		}

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    GPK_DIALOG_STORE_IMAGE, icon,
				    GPK_DIALOG_STORE_ID, package_id,
				    GPK_DIALOG_STORE_TEXT, text,
				    -1);
		g_free (text);
		g_free (package_id);
	}

	g_object_unref (extra);
	return store;
}

/**
 * gpk_dialog_treeview_for_package_list:
 **/
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

/**
 * gpk_dialog_widget_unrealize_unref_cb:
 **/
static void
gpk_dialog_widget_unrealize_unref_cb (GtkWidget *widget, GObject *obj)
{
	g_object_unref (obj);
}

/**
 * gpk_dialog_embed_package_list_widget:
 **/
gboolean
gpk_dialog_embed_package_list_widget (GtkDialog *dialog, PkPackageList *list)
{
	GtkWidget *scroll;
	GtkListStore *store;
	GtkWidget *widget;
	guint length;

	/* convert to a store */
	store = gpk_dialog_package_list_to_list_store (list);

	/* create a treeview to hold the store */
	widget = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
	gpk_dialog_treeview_for_package_list (GTK_TREE_VIEW (widget));
	gtk_widget_show (widget);

	/* scroll the treeview */
	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scroll), widget);
	gtk_widget_show (scroll);

	/* add some spacing to conform to the GNOME HIG */
	gtk_container_set_border_width (GTK_CONTAINER (scroll), 6);

	length = pk_package_list_get_size (list);
	if (length > 5) {
		gtk_widget_set_size_request (GTK_WIDGET (scroll), -1, 300);
	} else if (length > 1) {
		gtk_widget_set_size_request (GTK_WIDGET (scroll), -1, 150);
	}

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->vbox), scroll);

	/* free the store */
	g_signal_connect (G_OBJECT (dialog), "unrealize",
			  G_CALLBACK (gpk_dialog_widget_unrealize_unref_cb), store);

	return TRUE;
}

/**
 * gpk_dialog_embed_file_list_widget:
 **/
gboolean
gpk_dialog_embed_file_list_widget (GtkDialog *dialog, GPtrArray *files)
{
	GtkWidget *scroll;
	GtkWidget *widget;
	GtkTextBuffer *buffer;
	gchar **array;
	gchar *text;

	/* split and show */
	array = pk_ptr_array_to_argv (files);
	text = g_strjoinv ("\n", array);

	if (egg_strzero (text)) {
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
	gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scroll), widget);
	gtk_widget_show (scroll);

	/* set in buffer */
	buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_set_text (buffer, text, -1);
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);
	gtk_widget_show (widget);

	/* add some spacing to conform to the GNOME HIG */
	gtk_container_set_border_width (GTK_CONTAINER (scroll), 6);
	gtk_widget_set_size_request (GTK_WIDGET (scroll), -1, 300);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->vbox), scroll);
	g_free (text);

	return TRUE;
}

/**
 * gpk_client_checkbutton_show_depends_cb:
 **/
static void
gpk_client_checkbutton_show_depends_cb (GtkWidget *widget, const gchar *key)
{
	gboolean checked;
	GConfClient *gconf_client;

	/* set the policy */
	checked = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	egg_debug ("Changing %s to %i", key, checked);
	gconf_client = gconf_client_get_default ();
	gconf_client_set_bool (gconf_client, key, checked, NULL);
	g_object_unref (gconf_client);
}

/**
 * gpk_dialog_embed_do_not_show_widget:
 **/
gboolean
gpk_dialog_embed_do_not_show_widget (GtkDialog *dialog, const gchar *key)
{
	GtkWidget *widget;

	/* add a checkbutton for deps screen */
	widget = gtk_check_button_new_with_label (_("Do not show this again"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_checkbutton_show_depends_cb), (gpointer) key);
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), widget);
	gtk_widget_show (widget);
	return TRUE;
}

