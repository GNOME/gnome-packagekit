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

#include <pk-debug.h>
#include <pk-task-client.h>
#include <pk-connection.h>
#include <pk-package-id.h>

#include "pk-common.h"
#include "pk-updates.h"

static void     pk_updates_class_init (PkUpdatesClass *klass);
static void     pk_updates_init       (PkUpdates      *updates);
static void     pk_updates_finalize   (GObject	    *object);

#define PK_UPDATES_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_UPDATES, PkUpdatesPrivate))

struct PkUpdatesPrivate
{
	GladeXML		*glade_xml;
	GtkListStore		*packages_store;
	PkTaskClient		*tclient;
	PkConnection		*pconnection;
	gchar			*package;
	gchar			*actions;
	gboolean		 refresh_in_progress;
	guint			 search_depth;
};

enum {
	ACTION_HELP,
	ACTION_CLOSE,
	LAST_SIGNAL
};

enum
{
	PACKAGES_COLUMN_ICON,
	PACKAGES_COLUMN_TEXT,
	PACKAGES_COLUMN_ID,
	PACKAGES_COLUMN_LAST
};

enum
{
	GROUPS_COLUMN_ICON,
	GROUPS_COLUMN_NAME,
	GROUPS_COLUMN_ID,
	GROUPS_COLUMN_LAST
};

static guint	     signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkUpdates, pk_updates, G_TYPE_OBJECT)

/**
 * pk_updates_class_init:
 * @klass: This graph class instance
 **/
static void
pk_updates_class_init (PkUpdatesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_updates_finalize;
	g_type_class_add_private (klass, sizeof (PkUpdatesPrivate));

	signals [ACTION_HELP] =
		g_signal_new ("action-help",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkUpdatesClass, action_help),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [ACTION_CLOSE] =
		g_signal_new ("action-close",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkUpdatesClass, action_close),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * pk_updates_help_cb:
 **/
static void
pk_updates_help_cb (GtkWidget *widget,
		   PkUpdates  *updates)
{
	pk_debug ("emitting action-help");
	g_signal_emit (updates, signals [ACTION_HELP], 0);
}

/**
 * pk_updates_apply_cb:
 **/
static void
pk_updates_apply_cb (GtkWidget *widget,
		     PkUpdates *updates)
{
	pk_debug ("Doing the system update");
	pk_task_client_reset (updates->priv->tclient);
	pk_task_client_update_system (updates->priv->tclient);

	pk_debug ("emitting action-close");
	g_signal_emit (updates, signals [ACTION_CLOSE], 0);
}

/**
 * pk_updates_refresh_cb:
 **/
static void
pk_updates_refresh_cb (GtkWidget *widget,
		       PkUpdates *updates)
{
	gboolean ret;

	/* don't loop */	
	updates->priv->refresh_in_progress = TRUE;

	/* clear existing list */
	gtk_list_store_clear (updates->priv->packages_store);

	/* make the refresh button non-clickable */
	gtk_widget_set_sensitive (widget, FALSE);

	/* we can't click this if we havn't finished */
	pk_task_client_reset (updates->priv->tclient);
	ret = pk_task_client_refresh_cache (updates->priv->tclient, TRUE);
	if (ret == FALSE) {
		g_object_unref (updates->priv->tclient);
		pk_warning ("failed to refresh cache");
	}
}

/**
 * pk_updates_close_cb:
 **/
static void
pk_updates_close_cb (GtkWidget	*widget,
		    PkUpdates	*updates)
{
	pk_debug ("emitting action-close");
	g_signal_emit (updates, signals [ACTION_CLOSE], 0);
}

/**
 * pk_updates_package_cb:
 **/
static void
pk_updates_package_cb (PkTaskClient *tclient, guint value, const gchar *package_id,
			const gchar *summary, PkUpdates *updates)
{
	PkPackageId *ident;
	GtkTreeIter iter;
	GdkPixbuf *icon;
	gchar *text;
	const gchar *icon_name;

	pk_debug ("package = %i:%s:%s", value, package_id, summary);

	if (updates->priv->refresh_in_progress == TRUE) {
		pk_debug ("ignoring progress reports");
		return;
	}

	/* split by delimeter */
	ident = pk_package_id_new_from_string (package_id);

	text = g_markup_printf_escaped ("<b>%s-%s (%s)</b>\n%s", ident->name, ident->version, ident->arch, summary);

	gtk_list_store_append (updates->priv->packages_store, &iter);
	gtk_list_store_set (updates->priv->packages_store, &iter,
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_ID, package_id,
			    -1);

	if (value == 1) {
		icon_name = "software-update-urgent";
	} else {
		icon_name = "software-update-available";
	}
	icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), icon_name, 48, 0, NULL);
	if (icon) {
		gtk_list_store_set (updates->priv->packages_store, &iter, PACKAGES_COLUMN_ICON, icon, -1);
		gdk_pixbuf_unref (icon);
	}

	pk_package_id_free (ident);
}

/**
 * pk_updates_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
pk_updates_delete_event_cb (GtkWidget	*widget,
				GdkEvent	*event,
				PkUpdates	*updates)
{
	pk_updates_close_cb (widget, updates);
	return FALSE;
}

static void
pk_packages_add_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* image */
	renderer = gtk_cell_renderer_pixbuf_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Severity"), renderer,
							   "pixbuf", PACKAGES_COLUMN_ICON, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Package"), renderer,
							   "markup", PACKAGES_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * pk_packages_treeview_clicked_cb:
 **/
static void
pk_packages_treeview_clicked_cb (GtkTreeSelection *selection,
				    PkUpdates *updates)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *package_id;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (updates->priv->package);
		gtk_tree_model_get (model, &iter,
				    PACKAGES_COLUMN_ID, &package_id, -1);

		/* make back into package ID */
		updates->priv->package = g_strdup (package_id);
		g_free (package_id);
		g_print ("selected row is: %s\n", updates->priv->package);
		/* get the decription */
		pk_task_client_get_description (updates->priv->tclient, updates->priv->package);
	} else {
		g_print ("no row selected.\n");
	}
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkUpdates *updates)
{
	pk_debug ("connected=%i", connected);
}

/**
 * pk_updates_finished_cb:
 **/
static void
pk_updates_finished_cb (PkTaskClient *tclient, PkTaskStatus status, guint runtime, PkUpdates *updates)
{
	GtkWidget *widget;

	/* make the refresh button clickable until we get completion */
	widget = glade_xml_get_widget (updates->priv->glade_xml, "button_refresh");
	gtk_widget_set_sensitive (widget, TRUE);

	if (updates->priv->refresh_in_progress == FALSE) {
		pk_debug ("just the GetUpdates finishing");
		return;
	}

	/* don't do this again */
	updates->priv->refresh_in_progress = FALSE;

	/* get the update list */
	pk_task_client_reset (updates->priv->tclient);
	pk_task_client_get_updates (updates->priv->tclient);
}

/**
 * pk_updates_init:
 **/
static void
pk_updates_init (PkUpdates *updates)
{
	GtkWidget *main_window;
	GtkWidget *widget;

	updates->priv = PK_UPDATES_GET_PRIVATE (updates);
	updates->priv->package = NULL;
	updates->priv->refresh_in_progress = FALSE;

	updates->priv->search_depth = 0;

	updates->priv->tclient = pk_task_client_new ();
	g_signal_connect (updates->priv->tclient, "package",
			  G_CALLBACK (pk_updates_package_cb), updates);
	g_signal_connect (updates->priv->tclient, "finished",
			  G_CALLBACK (pk_updates_finished_cb), updates);

	/* get actions */
	updates->priv->actions = pk_task_client_get_actions (updates->priv->tclient);

	updates->priv->pconnection = pk_connection_new ();
	g_signal_connect (updates->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), updates);

	updates->priv->glade_xml = glade_xml_new (PK_DATA "/pk-updates.glade", NULL, NULL);
	main_window = glade_xml_get_widget (updates->priv->glade_xml, "window_updates");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), "system-installer");

	/* hide the details for now */
	widget = glade_xml_get_widget (updates->priv->glade_xml, "frame_details");
	gtk_widget_hide (widget);

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (pk_updates_delete_event_cb), updates);

	widget = glade_xml_get_widget (updates->priv->glade_xml, "button_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_updates_close_cb), updates);
	widget = glade_xml_get_widget (updates->priv->glade_xml, "button_apply");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_updates_apply_cb), updates);
	widget = glade_xml_get_widget (updates->priv->glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_updates_help_cb), updates);
	widget = glade_xml_get_widget (updates->priv->glade_xml, "button_refresh");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_updates_refresh_cb), updates);

	gtk_widget_set_size_request (main_window, 500, 300);

	GtkTreeSelection *selection;

	/* create list stores */
	updates->priv->packages_store = gtk_list_store_new (PACKAGES_COLUMN_LAST,
						       GDK_TYPE_PIXBUF,
						       G_TYPE_STRING,
						       G_TYPE_STRING);

	/* create package tree view */
	widget = glade_xml_get_widget (updates->priv->glade_xml, "treeview_updates");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (updates->priv->packages_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (pk_packages_treeview_clicked_cb), updates);

	/* add columns to the tree view */
	pk_packages_add_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* make the refresh button non-clickable until we get completion */
	widget = glade_xml_get_widget (updates->priv->glade_xml, "button_refresh");
	gtk_widget_set_sensitive (widget, FALSE);

	/* get the update list */
	pk_task_client_get_updates (updates->priv->tclient);

	gtk_widget_show (main_window);
}

/**
 * pk_updates_finalize:
 * @object: This graph class instance
 **/
static void
pk_updates_finalize (GObject *object)
{
	PkUpdates *updates;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_UPDATES (object));

	updates = PK_UPDATES (object);
	updates->priv = PK_UPDATES_GET_PRIVATE (updates);

	g_object_unref (updates->priv->packages_store);
	g_object_unref (updates->priv->tclient);
	g_object_unref (updates->priv->pconnection);
	g_free (updates->priv->package);
	g_free (updates->priv->actions);

	G_OBJECT_CLASS (pk_updates_parent_class)->finalize (object);
}

/**
 * pk_updates_new:
 * Return value: new PkUpdates instance.
 **/
PkUpdates *
pk_updates_new (void)
{
	PkUpdates *updates;
	updates = g_object_new (PK_TYPE_UPDATES, NULL);
	return PK_UPDATES (updates);
}

