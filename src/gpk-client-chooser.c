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
#include <polkit-gnome/polkit-gnome.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-gnome.h"
#include "gpk-common.h"
#include "gpk-client-chooser.h"
#include "gpk-enum.h"
#include "gpk-desktop.h"

static GtkListStore *list_store = NULL;
static gchar *package_id = NULL;
static PolKitGnomeAction *button_action = NULL;

enum
{
	GPK_CHOOSER_COLUMN_ICON,
	GPK_CHOOSER_COLUMN_TEXT,
	GPK_CHOOSER_COLUMN_ID,
	GPK_CHOOSER_COLUMN_LAST
};

/**
 * gpk_client_chooser_button_help_cb:
 **/
static void
gpk_client_chooser_button_help_cb (GtkWidget *widget, gpointer data)
{
	gpk_gnome_help ("mime-types");
}

/**
 * gpk_client_chooser_button_close_cb:
 **/
static void
gpk_client_chooser_button_close_cb (GtkWidget *widget, gpointer data)
{
	/* clear package_id */
	g_free (package_id);
	package_id = NULL;
	gtk_main_quit ();
}

/**
 * gpk_client_chooser_button_action_cb:
 **/
static void
gpk_client_chooser_button_action_cb (PolKitGnomeAction *action, gpointer data)
{
	gtk_main_quit ();
}

/**
 * gpk_client_chooser_treeview_clicked_cb:
 **/
static void
gpk_client_chooser_treeview_clicked_cb (GtkTreeSelection *selection, gboolean data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (package_id);
		gtk_tree_model_get (model, &iter, GPK_CHOOSER_COLUMN_ID, &package_id, -1);

		/* show package_id */
		egg_debug ("selected row is: %s", package_id);
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
	if (egg_strequal (name, "button_action"))
		return polkit_gnome_action_create_button (button_action);
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
	/* TRANSLATORS: column for the application name */
	column = gtk_tree_view_column_new_with_attributes (_("Package"), renderer,
							   "markup", GPK_CHOOSER_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_CHOOSER_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, TRUE);
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
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.package-install");
	/* TRANSLATORS: button label, install the selected package */
	button_action = polkit_gnome_action_new_default ("install", pk_action, _("_Install"), NULL);
	g_object_set (button_action,
		      "no-icon-name", GTK_STOCK_FLOPPY,
		      "auth-icon-name", GTK_STOCK_FLOPPY,
		      "yes-icon-name", GTK_STOCK_FLOPPY,
		      "self-blocked-icon-name", GTK_STOCK_FLOPPY,
		      NULL);
	polkit_action_unref (pk_action);
}

/**
 * gpk_client_chooser_show:
 *
 * Return value: the package_id of the selected package, or NULL
 **/
gchar *
gpk_client_chooser_show (GtkWindow *window, PkPackageList *list, const gchar *title)
{
	GladeXML *glade_xml;
	GtkWidget *widget;
	GtkTreeSelection *selection;
	const PkPackageObj *obj;
	GtkTreeIter iter;
	PkDesktop *desktop;
	const gchar *icon_name;
	gchar *text;
	guint len;
	guint i;

	g_return_val_if_fail (list != NULL, NULL);
	g_return_val_if_fail (title != NULL, NULL);

	/* we have to do this before we connect up the glade file */
	gpk_update_viewer_setup_policykit ();

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
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_chooser_button_help_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_chooser_button_close_cb), NULL);

	/* set icon name */
	widget = glade_xml_get_widget (glade_xml, "dialog_simple");
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);
	gtk_window_set_title (GTK_WINDOW (widget), title);

	/* make modal if window set */
	if (window != NULL) {
		gtk_window_set_modal (GTK_WINDOW (widget), TRUE);
		gtk_window_set_transient_for (GTK_WINDOW (widget), window);
	}

	/* connect up PolicyKit actions */
	g_signal_connect (button_action, "activate", G_CALLBACK (gpk_client_chooser_button_action_cb), NULL);

	/* create list stores */
	list_store = gtk_list_store_new (GPK_CHOOSER_COLUMN_LAST, G_TYPE_STRING,
						 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* create package_id tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_simple");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_client_chooser_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	pk_treeview_add_general_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* use PkDesktop to get better icon */
	desktop = pk_desktop_new ();

	/* see what we've got already */
	len = pk_package_list_get_size (list);
	for (i=0; i<len; i++) {
		obj = pk_package_list_get_obj (list, i);
		egg_debug ("package '%s' got:", obj->id->name);

		/* put formatted text into treeview */
		gtk_list_store_append (list_store, &iter);
		text = gpk_package_id_format_twoline (obj->id, obj->summary);

		/* get the icon */
		icon_name = gpk_desktop_guess_icon_name (desktop, obj->id->name);
		if (icon_name == NULL)
			icon_name = gpk_info_enum_to_icon_name (obj->info);

		package_id = pk_package_id_to_string (obj->id);
		gtk_list_store_set (list_store, &iter,
				    GPK_CHOOSER_COLUMN_TEXT, text,
				    GPK_CHOOSER_COLUMN_ID, package_id, -1);
		g_free (package_id);
		package_id = NULL;
		gtk_list_store_set (list_store, &iter, GPK_CHOOSER_COLUMN_ICON, icon_name, -1);
		g_free (text);
	}

	g_object_unref (desktop);

	/* show window */
	widget = glade_xml_get_widget (glade_xml, "dialog_simple");
	gtk_widget_show (widget);

	/* wait for button press */
	gtk_main ();

	/* hide window */
	if (GTK_IS_WIDGET (widget))
		gtk_widget_hide (widget);
	g_object_unref (glade_xml);

	return package_id;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpk_client_chooser_self_test (gpointer data)
{
	EggTest *test = (EggTest *) data;

	if (egg_test_start (test, "GpkClientChooser") == FALSE)
		return;
	egg_test_end (test);
}
#endif

