/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

#include "gpk-helper-chooser.h"
#include "gpk-marshal.h"
#include "gpk-gnome.h"
#include "gpk-common.h"
#include "gpk-enum.h"

static void     gpk_helper_chooser_finalize	(GObject	  *object);

#define GPK_HELPER_CHOOSER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_HELPER_CHOOSER, GpkHelperChooserPrivate))

struct GpkHelperChooserPrivate
{
	GtkBuilder		*builder;
	gchar			*package_id;
	GtkListStore		*list_store;
};

enum {
	GPK_HELPER_CHOOSER_EVENT,
	GPK_HELPER_CHOOSER_LAST_SIGNAL
};

enum {
	GPK_CHOOSER_COLUMN_ICON,
	GPK_CHOOSER_COLUMN_TEXT,
	GPK_CHOOSER_COLUMN_ID,
	GPK_CHOOSER_COLUMN_LAST
};

static guint signals [GPK_HELPER_CHOOSER_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkHelperChooser, gpk_helper_chooser, G_TYPE_OBJECT)

/**
 * gpk_helper_chooser_button_install_cb:
 **/
static void
gpk_helper_chooser_button_install_cb (GtkWidget *widget, GpkHelperChooser *helper)
{
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	gtk_widget_hide (widget);
	g_signal_emit (helper, signals [GPK_HELPER_CHOOSER_EVENT], 0, GTK_RESPONSE_YES, helper->priv->package_id);
}

/**
 * gpk_helper_chooser_button_cancel_cb:
 **/
static void
gpk_helper_chooser_button_cancel_cb (GtkWidget *widget, GpkHelperChooser *helper)
{
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	gtk_widget_hide (widget);
	g_signal_emit (helper, signals [GPK_HELPER_CHOOSER_EVENT], 0, GTK_RESPONSE_NO, helper->priv->package_id);
}

/**
 * gpk_helper_chooser_button_response_cb:
 **/
static void
gpk_helper_chooser_button_response_cb (GtkDialog *dialog, GtkResponseType response_id, GpkHelperChooser *helper)
{
	if (response_id == GTK_RESPONSE_DELETE_EVENT) {
		gtk_widget_hide (GTK_WIDGET (dialog));
		g_signal_emit (helper, signals [GPK_HELPER_CHOOSER_EVENT], 0, GTK_RESPONSE_NO, helper->priv->package_id);
	}
}

/**
 * gpk_helper_chooser_treeview_clicked_cb:
 **/
static void
gpk_helper_chooser_treeview_clicked_cb (GtkTreeSelection *selection, GpkHelperChooser *helper)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (helper->priv->package_id);
		gtk_tree_model_get (model, &iter, GPK_CHOOSER_COLUMN_ID, &helper->priv->package_id, -1);

		/* show package_id */
		g_debug ("selected row is: %s", helper->priv->package_id);
	} else {
		g_debug ("no row selected");
	}
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
	/* TRANSLATORS: column for the application name */
	column = gtk_tree_view_column_new_with_attributes (_("Package"), renderer,
							   "markup", GPK_CHOOSER_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_CHOOSER_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, TRUE);
}

/**
 * gpk_helper_chooser_show:
 *
 * Return value: if we agreed
 **/
gboolean
gpk_helper_chooser_show (GpkHelperChooser *helper, GPtrArray *list)
{
	GtkWidget *widget;
	gchar *text;
	const gchar *icon_name;
	guint i;
	PkPackage *item;
	GtkTreeIter iter;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *summary = NULL;

	g_return_val_if_fail (GPK_IS_HELPER_CHOOSER (helper), FALSE);
	g_return_val_if_fail (list != NULL, FALSE);

	/* see what we've got already */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	for (i=0; i<list->len; i++) {
		item = g_ptr_array_index (list, i);
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);
		g_debug ("package '%s' got:", package_id);

		/* put formatted text into treeview */
		gtk_list_store_append (helper->priv->list_store, &iter);
		text = gpk_package_id_format_twoline (gtk_widget_get_style_context (widget),
						      package_id,
						      summary);
		icon_name = gpk_info_enum_to_icon_name (info);
		gtk_list_store_set (helper->priv->list_store, &iter,
				    GPK_CHOOSER_COLUMN_TEXT, text,
				    GPK_CHOOSER_COLUMN_ID, package_id, -1);
		gtk_list_store_set (helper->priv->list_store, &iter, GPK_CHOOSER_COLUMN_ICON, icon_name, -1);
		g_free (package_id);
		g_free (summary);
		g_free (text);
	}

	/* show window */
	gtk_widget_show (widget);

	return TRUE;
}

/**
 * gpk_helper_chooser_set_parent:
 **/
gboolean
gpk_helper_chooser_set_parent (GpkHelperChooser *helper, GtkWindow *window)
{
	GtkWindow *widget;

	g_return_val_if_fail (GPK_IS_HELPER_CHOOSER (helper), FALSE);
	g_return_val_if_fail (window != NULL, FALSE);

	/* make modal if window set */
	widget = GTK_WINDOW (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	gtk_window_set_transient_for (widget, window);
	gtk_window_set_modal (widget, TRUE);

	/* this is a modal popup, so don't show a window title */
	gtk_window_set_title (widget, "");

	return TRUE;
}

/**
 * gpk_helper_chooser_class_init:
 * @klass: The GpkHelperChooserClass
 **/
static void
gpk_helper_chooser_class_init (GpkHelperChooserClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_helper_chooser_finalize;
	g_type_class_add_private (klass, sizeof (GpkHelperChooserPrivate));
	signals [GPK_HELPER_CHOOSER_EVENT] =
		g_signal_new ("event",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkHelperChooserClass, event),
			      NULL, NULL, gpk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
}

/**
 * gpk_helper_chooser_init:
 **/
static void
gpk_helper_chooser_init (GpkHelperChooser *helper)
{
	GtkWidget *widget;
	guint retval;
	GError *error = NULL;
	GtkWidget *button;
	GtkTreeSelection *selection;

	helper->priv = GPK_HELPER_CHOOSER_GET_PRIVATE (helper);

	helper->priv->package_id = NULL;

	/* get UI */
	helper->priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (helper->priv->builder, GPK_DATA "/gpk-log.ui", &error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
	}

	/* connect up default actions */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	g_signal_connect (GTK_DIALOG (widget), "response", G_CALLBACK (gpk_helper_chooser_button_response_cb), helper);

	/* set icon name */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);
	gtk_window_set_title (GTK_WINDOW (widget), _("Applications that can open this type of file"));

	/* set a size, if the screen allows */
	gpk_window_set_size_request (GTK_WINDOW (widget), 600, 300);

	/* connect up buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "button_close"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_helper_chooser_button_cancel_cb), helper);

	/* TRANSLATORS: button label, install */
	button = gtk_button_new_with_mnemonic (_("_Install"));
	g_signal_connect (button, "clicked", G_CALLBACK (gpk_helper_chooser_button_install_cb), helper);

	/* TRANSLATORS: button tooltip */
	gtk_widget_set_tooltip_text (button, _("Install package"));

	/* add to box */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	widget = gtk_dialog_get_action_area (GTK_DIALOG(widget));
	gtk_box_pack_start (GTK_BOX (widget), button, FALSE, FALSE, 0);
	gtk_widget_show (button);

	/* hide the filter box */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "hbox_filter"));
	gtk_widget_hide (widget);

	/* hide the refresh button */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "button_refresh"));
	gtk_widget_hide (widget);

	/* create list stores */
	helper->priv->list_store = gtk_list_store_new (GPK_CHOOSER_COLUMN_LAST, G_TYPE_STRING,
						       G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* create package_id tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "treeview_simple"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (helper->priv->list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_helper_chooser_treeview_clicked_cb), helper);

	/* add columns to the tree view */
	pk_treeview_add_general_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (widget), FALSE);
}

/**
 * gpk_helper_chooser_finalize:
 **/
static void
gpk_helper_chooser_finalize (GObject *object)
{
	GtkWidget *widget;
	GpkHelperChooser *helper;

	g_return_if_fail (GPK_IS_HELPER_CHOOSER (object));

	helper = GPK_HELPER_CHOOSER (object);

	/* hide window */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	if (GTK_IS_WIDGET (widget))
		gtk_widget_hide (widget);
	g_free (helper->priv->package_id);
	g_object_unref (helper->priv->builder);

	G_OBJECT_CLASS (gpk_helper_chooser_parent_class)->finalize (object);
}

/**
 * gpk_helper_chooser_new:
 **/
GpkHelperChooser *
gpk_helper_chooser_new (void)
{
	GpkHelperChooser *helper;
	helper = g_object_new (GPK_TYPE_HELPER_CHOOSER, NULL);
	return GPK_HELPER_CHOOSER (helper);
}

