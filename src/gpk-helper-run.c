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
#include "gpk-desktop.h"
#include "gpk-enum.h"

#include "egg-debug.h"

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
	GAppLaunchContext *context = NULL;

	/* check have value */
	if (filename == NULL) {
		egg_warning ("no full path");
		goto out;
	}

	/* launch application */
	app = G_APP_INFO(g_desktop_app_info_new_from_filename (filename));
	context = G_APP_LAUNCH_CONTEXT(gdk_app_launch_context_new ());
//	app = (GAppInfo*)g_desktop_app_info_new_from_filename (filename);
//	context = (GAppLaunchContext*)gdk_app_launch_context_new ();
	ret = g_app_info_launch (app, NULL, context, &error);
	if (!ret) {
		egg_warning ("failed to launch: %s", error->message);
		g_error_free (error);
	}
out:
	if (app != NULL)
		g_object_unref (app);
	if (context != NULL)
		g_object_unref (context);
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
		egg_warning ("failed to get selection");
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
 * gpk_helper_run_button_help_cb:
 **/
static void
gpk_helper_run_button_help_cb (GtkWidget *widget, GpkHelperRun *helper)
{
	/* show the help */
	gpk_gnome_help ("run");
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
		egg_debug ("selected row is: %s", filename);
	} else {
		egg_debug ("no row selected");
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
		egg_warning ("failed to get selection");
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
 * gpk_helper_run_add_desktop_file:
 **/
static gboolean
gpk_helper_run_add_desktop_file (GpkHelperRun *helper, const gchar *package_id, const gchar *filename)
{
	gboolean ret = FALSE;
	gchar *icon = NULL;
	gchar *text = NULL;
	gchar *fulltext = NULL;
	gchar *name = NULL;
	gchar *exec = NULL;
	gchar *summary = NULL;
	gchar *joint = NULL;
	gchar *menu_path = NULL;
	GtkTreeIter iter;
	GKeyFile *file = NULL;
	gint weight;
	gboolean hidden;

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

	/* get hidden */
	hidden = g_key_file_get_boolean (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL);
	if (hidden) {
		egg_debug ("hidden, so ignoring %s", filename);
		ret = FALSE;
		goto out;
	}

	/* is WM? */
	ret = !g_key_file_has_group (file, "Window Manager");
	if (!ret) {
		egg_debug ("ignoring Window Manager");
		goto out;
	}

	/* get exec */
	exec = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_TRY_EXEC, NULL);
	if (exec == NULL)
		exec = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, "Exec", NULL);

	/* abandon attempt */
	if (exec == NULL) {
		ret = FALSE;
		goto out;
	}

	/* get name */
	text = g_key_file_get_locale_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NAME, NULL, NULL);
	if (text != NULL)
		name = g_markup_escape_text (text, -1);
	g_free (text);

	/* get icon */
	icon = g_key_file_get_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_ICON, NULL);
	if (icon == NULL || !gpk_desktop_check_icon_valid (icon)) {
		g_free (icon);
		icon = g_strdup (gpk_info_enum_to_icon_name (PK_INFO_ENUM_AVAILABLE));
	}

	/* get summary */
	summary = g_key_file_get_locale_string (file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_COMMENT, NULL, NULL);
	if (summary == NULL)
		summary = g_key_file_get_locale_string (file, G_KEY_FILE_DESKTOP_GROUP, "GenericName", NULL, NULL);

	/* get application path */
	text = gpk_desktop_get_menu_path (filename);
	if (text != NULL)
		menu_path = g_markup_escape_text (text, -1);
	g_free (text);

	/* put formatted text into treeview */
	gtk_list_store_append (helper->priv->list_store, &iter);
	joint = g_strdup_printf ("%s - %s", name, summary);
	text = gpk_package_id_format_twoline (package_id, joint);
	if (menu_path != NULL) {
		/* TRANSLATORS: the path in the menu, e.g. Applications -> Games -> Dave */
		fulltext = g_strdup_printf("%s\n\n<i>%s</i>", text, menu_path);
		g_free (text);
		text = fulltext;
	}

	gtk_list_store_set (helper->priv->list_store, &iter,
			    GPK_CHOOSER_COLUMN_TEXT, fulltext,
			    GPK_CHOOSER_COLUMN_FILENAME, filename,
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
 * gpk_helper_run_add_package_ids:
 **/
static guint
gpk_helper_run_add_package_ids (GpkHelperRun *helper, gchar **package_ids)
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
				ret = gpk_helper_run_add_desktop_file (helper, package_ids[i], filename);
				if (ret)
					added++;
			}
			g_ptr_array_unref (array);
		}
		g_strfreev (parts);
	}
	g_object_unref (desktop);
out:
	return added;
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
		egg_debug ("no executable file for %s", package_ids[0]);
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
		egg_warning ("failed to load ui: %s", error->message);
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
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "button_help"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_helper_run_button_help_cb), helper);

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

