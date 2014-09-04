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
#include <gio/gdesktopappinfo.h>

#include "gpk-helper-run.h"
#include "gpk-marshal.h"
#include "gpk-gnome.h"
#include "gpk-common.h"
#include "gpk-enum.h"

static void     gpk_helper_run_finalize	(GObject	  *object);

#define GPK_HELPER_RUN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_HELPER_RUN, GpkHelperRunPrivate))

struct GpkHelperRunPrivate
{
	GtkBuilder		*builder;
	GtkListStore		*list_store;
};

enum {
	GPK_CHOOSER_COLUMN_ICON,
	GPK_CHOOSER_COLUMN_TEXT,
	GPK_CHOOSER_COLUMN_FILENAME,
	GPK_CHOOSER_COLUMN_LAST
};

G_DEFINE_TYPE (GpkHelperRun, gpk_helper_run, G_TYPE_OBJECT)

/**
 * gpk_helper_run_path:
 **/
static gboolean
gpk_helper_run_path (GpkHelperRun *helper, const gchar *filename)
{
	gboolean ret = FALSE;
	GError *error = NULL;
	GAppInfo *app = NULL;

	/* check have value */
	if (filename == NULL) {
		g_warning ("no full path");
		goto out;
	}

	/* launch application */
	app = G_APP_INFO(g_desktop_app_info_new_from_filename (filename));
	ret = g_app_info_launch (app, NULL, NULL, &error);
	if (!ret) {
		g_warning ("failed to launch: %s", error->message);
		g_error_free (error);
	}
out:
	if (app != NULL)
		g_object_unref (app);
	return ret;
}

/**
 * gpk_helper_run_button_run_cb:
 **/
static void
gpk_helper_run_button_run_cb (GtkWidget *widget, GpkHelperRun *helper)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	gboolean ret;
	gchar *filename;

	/* get selection */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (helper->priv->builder, "treeview_simple"));
	selection = gtk_tree_view_get_selection (treeview);
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret) {
		g_warning ("failed to get selection");
		return;
	}

	gtk_tree_model_get (model, &iter, GPK_CHOOSER_COLUMN_FILENAME, &filename, -1);
	gpk_helper_run_path (helper, filename);
	g_free (filename);
}

/**
 * gpk_helper_run_button_close_cb:
 **/
static void
gpk_helper_run_button_close_cb (GtkWidget *widget, GpkHelperRun *helper)
{
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	gtk_widget_hide (widget);
}

/**
 * gpk_helper_run_delete_event_cb:
 **/
static gboolean
gpk_helper_run_delete_event_cb (GtkWidget *widget, GdkEvent *event, GpkHelperRun *helper)
{
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	gtk_widget_hide (widget);
	return FALSE;
}

/**
 * gpk_helper_run_treeview_clicked_cb:
 **/
static void
gpk_helper_run_treeview_clicked_cb (GtkTreeSelection *selection, GpkHelperRun *helper)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *filename = NULL;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (filename);
		gtk_tree_model_get (model, &iter, GPK_CHOOSER_COLUMN_FILENAME, &filename, -1);

		/* show full path */
		g_debug ("selected row is: %s", filename);
	} else {
		g_debug ("no row selected");
	}
	g_free (filename);
}

/**
 * gpk_helper_run_row_activated_cb:
 **/
static void
gpk_helper_run_row_activated_cb (GtkTreeView *treeview, GtkTreePath *path,
				 GtkTreeViewColumn *col, GpkHelperRun *helper)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean ret;
	gchar *filename;

	/* get selection */
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter (model, &iter, path);
	if (!ret) {
		g_warning ("failed to get selection");
		return;
	}

	gtk_tree_model_get (model, &iter, GPK_CHOOSER_COLUMN_FILENAME, &filename, -1);
	gpk_helper_run_path (helper, filename);
	g_free (filename);
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
 * gpk_helper_run_add_package_ids:
 **/
static guint
gpk_helper_run_add_package_ids (GpkHelperRun *helper, gchar **package_ids)
{
	return 0;
}

/**
 * gpk_helper_run_show:
 *
 * Return value: if we agreed
 **/
gboolean
gpk_helper_run_show (GpkHelperRun *helper, gchar **package_ids)
{
	GtkWidget *widget;
	guint len;

	g_return_val_if_fail (GPK_IS_HELPER_RUN (helper), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* clear old list */
	gtk_list_store_clear (helper->priv->list_store);

	/* add all the apps */
	len = gpk_helper_run_add_package_ids (helper, package_ids);
	if (len == 0) {
		g_debug ("no executable file for %s", package_ids[0]);
		goto out;
	}

	/* show window */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	gtk_widget_show (widget);
out:
	return TRUE;
}

/**
 * gpk_helper_run_set_parent:
 **/
gboolean
gpk_helper_run_set_parent (GpkHelperRun *helper, GtkWindow *window)
{
	GtkWindow *widget;

	g_return_val_if_fail (GPK_IS_HELPER_RUN (helper), FALSE);
	g_return_val_if_fail (window != NULL, FALSE);

	/* make modal if window set */
	widget = GTK_WINDOW (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	gtk_window_set_transient_for (widget, window);
	gtk_window_set_modal (widget, TRUE);

	/* this is a modal popup */
	gtk_window_set_type_hint (widget, GDK_WINDOW_TYPE_HINT_DIALOG);

	return TRUE;
}

/**
 * gpk_helper_run_class_init:
 * @klass: The GpkHelperRunClass
 **/
static void
gpk_helper_run_class_init (GpkHelperRunClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_helper_run_finalize;
	g_type_class_add_private (klass, sizeof (GpkHelperRunPrivate));
}

/**
 * gpk_helper_run_init:
 **/
static void
gpk_helper_run_init (GpkHelperRun *helper)
{
	GtkWidget *widget;
	GtkWidget *button;
	guint retval;
	GError *error = NULL;
	GtkTreeSelection *selection;
	GtkBox *box;

	helper->priv = GPK_HELPER_RUN_GET_PRIVATE (helper);

	/* get UI */
	helper->priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (helper->priv->builder, GPK_DATA "/gpk-log.ui", &error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
	}

	/* connect up default actions */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	g_signal_connect (widget, "delete_event", G_CALLBACK (gpk_helper_run_delete_event_cb), helper);

	/* set icon name */
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* set a size, if the screen allows */
	gpk_window_set_size_request (GTK_WINDOW (widget), 600, 300);

	/* connect up buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "button_close"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_helper_run_button_close_cb), helper);

	/* hide the filter box */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "hbox_filter"));
	gtk_widget_hide (widget);

	/* hide the refresh button */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "button_refresh"));
	gtk_widget_hide (widget);

	/* set icon name */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);
	/* TRANSLATORS: window title: do we want to execute a program we just installed? */
	gtk_window_set_title (GTK_WINDOW (widget), _("Run new application?"));

	/* add run button */
	button = gtk_button_new_with_mnemonic (_("_Run"));
	box = GTK_BOX (gtk_dialog_get_action_area (GTK_DIALOG (widget)));
	gtk_box_pack_start (box, button, FALSE, FALSE, 0);
	gtk_widget_show (button);
	g_signal_connect (button, "clicked", G_CALLBACK (gpk_helper_run_button_run_cb), helper);

	/* create list stores */
	helper->priv->list_store = gtk_list_store_new (GPK_CHOOSER_COLUMN_LAST, G_TYPE_STRING,
						       G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* create package_id tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "treeview_simple"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (helper->priv->list_store));
	g_signal_connect (GTK_TREE_VIEW (widget), "row-activated",
			  G_CALLBACK (gpk_helper_run_row_activated_cb), helper);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_helper_run_treeview_clicked_cb), helper);

	/* add columns to the tree view */
	pk_treeview_add_general_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (widget), FALSE);
}

/**
 * gpk_helper_run_finalize:
 **/
static void
gpk_helper_run_finalize (GObject *object)
{
	GtkWidget *widget;
	GpkHelperRun *helper;

	g_return_if_fail (GPK_IS_HELPER_RUN (object));

	helper = GPK_HELPER_RUN (object);

	/* hide window */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_simple"));
	if (GTK_IS_WIDGET (widget))
		gtk_widget_hide (widget);
	g_object_unref (helper->priv->builder);
	g_object_unref (helper->priv->list_store);

	G_OBJECT_CLASS (gpk_helper_run_parent_class)->finalize (object);
}

/**
 * gpk_helper_run_new:
 **/
GpkHelperRun *
gpk_helper_run_new (void)
{
	GpkHelperRun *helper;
	helper = g_object_new (GPK_TYPE_HELPER_RUN, NULL);
	return GPK_HELPER_RUN (helper);
}

