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
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <string.h>
#include <dbus/dbus-glib.h>
#include <locale.h>

#include <polkit-gnome/polkit-gnome.h>

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-control.h>
#include <pk-common.h>
#include <pk-task-list.h>
#include <pk-package-id.h>
#include <pk-package-ids.h>

#include <gpk-common.h>
#include <gpk-gnome.h>
#include <gpk-error.h>

#include "gpk-statusbar.h"
#include "gpk-consolekit.h"
#include "gpk-cell-renderer-uri.h"

static GladeXML *glade_xml = NULL;
static GtkListStore *list_store_preview = NULL;
static GtkListStore *list_store_details = NULL;
static GtkListStore *list_store_description = NULL;
static PkClient *client_action = NULL;
static PkClient *client_query = NULL;
static PkControl *control = NULL;
static PkTaskList *tlist = NULL;
static gchar *cached_package_id = NULL;

static PolKitGnomeAction *refresh_action = NULL;
static PolKitGnomeAction *update_system_action = NULL;
static PolKitGnomeAction *update_packages_action = NULL;
static PolKitGnomeAction *restart_action = NULL;

/* for the preview throbber */
static void pk_update_viewer_add_preview_item (const gchar *icon, const gchar *message, gboolean clear);
static void pk_update_viewer_description_animation_stop (void);
static int animation_timeout = 0;
static int frame_counter = 0;
static int n_frames = 0;
static GdkPixbuf **frames = NULL;

enum {
	PREVIEW_COLUMN_ICON,
	PREVIEW_COLUMN_TEXT,
	PREVIEW_COLUMN_PROGRESS,
	PREVIEW_COLUMN_LAST
};

enum {
	DESC_COLUMN_TITLE,
	DESC_COLUMN_TEXT,
	DESC_COLUMN_URI,
	DESC_COLUMN_PROGRESS,
	DESC_COLUMN_LAST
};

enum {
	HISTORY_COLUMN_ICON,
	HISTORY_COLUMN_TEXT,
	HISTORY_COLUMN_LAST
};

enum {
	PACKAGES_COLUMN_ICON,
	PACKAGES_COLUMN_TEXT,
	PACKAGES_COLUMN_ID,
	PACKAGES_COLUMN_INFO,
	PACKAGES_COLUMN_SELECT,
	PACKAGES_COLUMN_LAST
};

typedef enum {
	PAGE_PREVIEW,
	PAGE_DETAILS,
	PAGE_PROGRESS,
	PAGE_CONFIRM,
	PAGE_ERROR,
	PAGE_LAST
} PkPageEnum;

/**
 * pk_button_help_cb:
 **/
static void
pk_button_help_cb (GtkWidget *widget, gpointer data)
{
	const char *id = data;
	gpk_gnome_help (id);
}

/**
 * pk_update_viewer_set_page:
 **/
static void
pk_update_viewer_set_page (PkPageEnum page)
{
	GList *list, *l;
	GtkWidget *widget;
	GtkRequisition req;
	guint i;

	widget = glade_xml_get_widget (glade_xml, "hbox_hidden");
	list = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l=list, i=0; l; l=l->next, i++) {
		if (i == page) {
			gtk_widget_show (l->data);
		} else {
			gtk_widget_hide (l->data);
		}
	}

	/* some pages are resizeable */
	widget = glade_xml_get_widget (glade_xml, "window_updates");
	if (page == PAGE_DETAILS || page == PAGE_PROGRESS) {
		gtk_window_set_resizable (GTK_WINDOW (widget), TRUE);
		if (page == PAGE_PROGRESS) {
			/* use the natural size unless it's really big */
			gtk_widget_size_request (widget, &req);
			gtk_window_resize (GTK_WINDOW (widget),
				(req.width < 500) ? req.width : 500,
				(req.height < 400) ? req.height : 400);
		}
	} else {
		gtk_window_set_resizable (GTK_WINDOW (widget), FALSE);
	}
}

/**
 * pk_update_viewer_update_system_cb:
 **/
static void
pk_update_viewer_update_system_cb (PolKitGnomeAction *action, gpointer data)
{
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;

	pk_debug ("Doing the system update");

	widget = glade_xml_get_widget (glade_xml, "button_overview2");
	gtk_widget_hide (widget);

	/* set correct view */
	pk_update_viewer_set_page (PAGE_PROGRESS);

	/* reset */
	ret = pk_client_reset (client_action, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return;
	}

	/* update system */
	ret = pk_client_update_system (client_action, &error);
	if (!ret) {
		pk_warning ("failed to update system: %s", error->message);
		g_error_free (error);
	}
}

/**
 * pk_update_viewer_apply_cb:
 **/
static void
pk_update_viewer_apply_cb (PolKitGnomeAction *action, gpointer data)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean valid;
	gboolean update;
	gboolean selected_all = TRUE;
	gboolean selected_any = FALSE;
	gchar *package_id;
	GPtrArray *array;
	gchar **package_ids;
	gboolean ret;
	GError *error = NULL;

	pk_debug ("Doing the package updates");
	array = g_ptr_array_new ();

	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));

	/* get the first iter in the list */
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* find out how many we should update */
	while (valid) {
		gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_SELECT, &update,
				    PACKAGES_COLUMN_ID, &package_id, -1);

		if (!update) {
			selected_all = FALSE;
		} else {
			selected_any = TRUE;
		}

		/* do something with the data */
		if (update) {
			pk_debug ("%s", package_id);
			g_ptr_array_add (array, package_id);
		} else {
			/* need to free the one in the array later */
			g_free (package_id);
		}
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* we have no checkboxes selected */
	if (!selected_any) {
		gpk_error_dialog (_("No updates selected"), _("No updates are selected"), NULL);
		return;
	}

	widget = glade_xml_get_widget (glade_xml, "button_overview2");
	if (selected_all) {
		gtk_widget_hide (widget);
	} else {
		gtk_widget_show (widget);
	}

	/* set correct view */
	pk_update_viewer_set_page (PAGE_PROGRESS);
	package_ids = pk_package_ids_from_array (array);

	/* reset */
	ret = pk_client_reset (client_action, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return;
	}

	/* update a list */
	ret = pk_client_update_packages_strv (client_action, package_ids, &error);
	if (!ret) {
		gpk_error_dialog (_("Failed to update"), _("Individual updates failed"), error->message);
		g_error_free (error);
	}
	g_strfreev (package_ids);

	/* get rid of the array, and free the contents */
	g_ptr_array_free (array, TRUE);
}

/**
 * pk_update_viewer_animation_load_frames:
 **/
static gboolean
pk_update_viewer_animation_load_frames (void)
{
	GtkWidget *widget;
	GdkPixbuf *pixbuf;
	gint w, h;
	gint rows, cols;
	gint r, c, i;

	if (frames == NULL) {
		/* get the process-working animation from the icon theme
		 * and split it into frames.
		 * FIXME reset frames on theme changes
		 */
		widget = glade_xml_get_widget (glade_xml, "window_updates");
		gtk_icon_size_lookup (GTK_ICON_SIZE_DIALOG, &w, &h);
		pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
						   "process-working",
						   w, 0, NULL);
		/* can't load from gnome-icon-theme */
		if (pixbuf == NULL) {
			return FALSE;
		}
		cols = gdk_pixbuf_get_width (pixbuf) / w;
		rows = gdk_pixbuf_get_height (pixbuf) / h;

		n_frames = rows * cols;
		frames = g_new (GdkPixbuf*, n_frames);

		for (i = 0, r = 0; r < rows; r++)
			for (c = 0; c < cols; c++, i++) {
			frames[i] = gdk_pixbuf_new_subpixbuf (pixbuf, c * w, r * h, w, h);
		}

		g_object_unref (pixbuf);
	}
	return TRUE;
}

/**
 * pk_update_viewer_animation_update:
 **/
static gboolean
pk_update_viewer_animation_update (gpointer data)
{
	GtkTreeModel *model = data;
	GtkTreeIter iter;
	gint column;

	column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (model), "progress-column"));

	/* have we loaded a file */
	if (frames == NULL) {
		pk_warning ("no frames to process");
		return FALSE;
	}

	gtk_tree_model_get_iter_first (model, &iter);
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    column, frames[frame_counter],
			    -1);

	frame_counter = (frame_counter + 1) % n_frames;

	return TRUE;
}

/**
 * pk_update_viewer_preview_animation_start:
 **/
static void
pk_update_viewer_preview_animation_start (void)
{
	GtkWidget *widget;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeModel *model;
	GList *list;

	/* don't double queue */
	if (animation_timeout != 0) {
		pk_debug ("don't double start");
		return;
	}

	pk_update_viewer_animation_load_frames ();

	widget = glade_xml_get_widget (glade_xml, "treeview_preview");
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (widget), 0);
	list = gtk_tree_view_column_get_cell_renderers (column);
	renderer = list->data;
	g_list_free (list);
	gtk_tree_view_column_clear_attributes (column, renderer);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "pixbuf", PREVIEW_COLUMN_PROGRESS, NULL);

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));
	frame_counter = 0;

	g_object_set_data (G_OBJECT (model), "progress-column",
			   GINT_TO_POINTER (PREVIEW_COLUMN_PROGRESS));

	animation_timeout = g_timeout_add (50, pk_update_viewer_animation_update, model);
	pk_update_viewer_animation_update (model);
}

/**
 * pk_update_viewer_preview_animation_stop:
 **/
static void
pk_update_viewer_preview_animation_stop (void)
{
	GtkWidget *widget;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GList *list;

	if (animation_timeout == 0)
		return;

	g_source_remove (animation_timeout);
	animation_timeout = 0;

	widget = glade_xml_get_widget (glade_xml, "treeview_preview");
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (widget), 0);
	list = gtk_tree_view_column_get_cell_renderers (column);
	renderer = list->data;
	g_list_free (list);
	gtk_tree_view_column_clear_attributes (column, renderer);
	gtk_tree_view_column_set_attributes (column, renderer,
					     "icon-name", PREVIEW_COLUMN_ICON, NULL);
}

/**
 * pk_update_viewer_description_animation_start:
 **/
static void
pk_update_viewer_description_animation_start (void)
{
	GtkWidget *widget;
	GtkTreeViewColumn *column;
	GList *list, *l;
	GtkCellRenderer *renderer;
	GtkTreeIter iter;
	gchar *text;

	/* don't double queue */
	if (animation_timeout != 0) {
		pk_debug ("don't double start");
		return;
	}

	gtk_list_store_clear (list_store_description);

	pk_update_viewer_animation_load_frames ();

	widget = glade_xml_get_widget (glade_xml, "treeview_description");
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (widget), 0);
	list = gtk_tree_view_column_get_cell_renderers (column);
	for (l = list; l; l = l->next) {
		renderer = l->data;
		if (GTK_IS_CELL_RENDERER_PIXBUF (renderer)) {
			g_object_set (renderer, "visible", TRUE, NULL);
		}
	}
	g_list_free (list);

	text = g_strdup_printf ("<b>%s</b>", _("Getting Description..."));
	gtk_list_store_append (list_store_description, &iter);
	gtk_list_store_set (list_store_description, &iter,
			    DESC_COLUMN_TITLE, text,
			    -1);
	g_free (text);

	frame_counter = 0;

	g_object_set_data (G_OBJECT (list_store_description), "progress-column",
		           GINT_TO_POINTER (DESC_COLUMN_PROGRESS));
	animation_timeout = g_timeout_add (50, pk_update_viewer_animation_update, list_store_description);
	pk_update_viewer_animation_update (list_store_description);
}

/**
 * pk_update_viewer_description_animation_stop:
 **/
static void
pk_update_viewer_description_animation_stop (void)
{
	GtkWidget *widget;
	GtkTreeViewColumn *column;
	GList *list, *l;
	GtkCellRenderer *renderer;

	if (animation_timeout == 0)
		return;

	g_source_remove (animation_timeout);
	animation_timeout = 0;

	widget = glade_xml_get_widget (glade_xml, "treeview_description");
	column = gtk_tree_view_get_column (GTK_TREE_VIEW (widget), 0);
	list = gtk_tree_view_column_get_cell_renderers (column);
	for (l = list; l; l = l->next) {
		renderer = list->data;
		if (GTK_IS_CELL_RENDERER_PIXBUF (renderer)) {
			g_object_set (renderer, "visible", FALSE, NULL);
		}
	}
	g_list_free (list);
}

/**
 * pk_update_viewer_refresh_cb:
 **/
static void
pk_update_viewer_refresh_cb (PolKitGnomeAction *action, gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	/* we can't click this if we havn't finished */
	ret = pk_client_reset (client_action, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return;
	}
	ret = pk_client_refresh_cache (client_action, TRUE, &error);
	if (ret == FALSE) {
		gpk_error_dialog (_("Failed to refresh"), _("Method refused"), error->message);
		g_error_free (error);
		return;
	}
}

/**
 * pk_update_viewer_history_cb:
 **/
static void
pk_update_viewer_history_cb (GtkWidget *widget, gpointer data)
{
	GError *error = NULL;

	/* FIXME: do this in process */
	if (!g_spawn_command_line_async ("gpk-log", &error)) {
		gpk_error_dialog (_("Failed to launch"), _("The file was not found"), error->message);
		g_error_free (error);			
	}
}

/**
 * pk_update_viewer_button_cancel_cb:
 **/
static void
pk_update_viewer_button_cancel_cb (GtkWidget *widget, gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	/* we might have a transaction running */
	ret = pk_client_cancel (client_query, &error);
	if (!ret) {
		pk_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
		error = NULL;
	}
	ret = pk_client_cancel (client_action, &error);
	if (!ret) {
		pk_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
	}
}

/**
 * pk_update_viewer_button_close_and_cancel_cb:
 **/
static void
pk_update_viewer_button_close_and_cancel_cb (GtkWidget *widget, gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	GMainLoop *loop = (GMainLoop *) data;

	/* we might have a transaction running */
	ret = pk_client_cancel (client_action, &error);
	if (!ret) {
		pk_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
	}

	g_main_loop_quit (loop);
}

/**
 * pk_update_viewer_close_cb:
 **/
static void
pk_update_viewer_close_cb (GtkWidget *widget, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;

	/* just close the UI, let the installation continue */
	g_main_loop_quit (loop);
}

/**
 * pk_update_viewer_review_cb:
 **/
static void
pk_update_viewer_review_cb (GtkWidget *widget, gpointer data)
{
	GtkWidget *treeview;
	GtkTreeSelection *selection;
	GtkTreeIter iter;

	treeview = glade_xml_get_widget (glade_xml, "treeview_updates");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	if (!gtk_tree_selection_get_selected (selection, NULL, NULL)) {
		if (gtk_tree_model_get_iter_first (gtk_tree_view_get_model (GTK_TREE_VIEW (treeview)),
						   &iter))
			gtk_tree_selection_select_iter (selection, &iter);
	}

	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	gtk_widget_set_size_request (GTK_WIDGET (widget), 500, 200);

	widget = glade_xml_get_widget (glade_xml, "treeview_description");
	gtk_widget_set_size_request (GTK_WIDGET (widget), 500, 200);

	/* set correct view */
	pk_update_viewer_set_page (PAGE_DETAILS);
}


/**
 * pk_update_viewer_populate_preview:
 **/
static void
pk_update_viewer_populate_preview (void)
{
	GtkWidget *widget;
	guint length;

	length = pk_client_package_buffer_get_size (client_query);
	if (length == 0) {
		pk_update_viewer_add_preview_item ("dialog-information", _("There are no updates available!"), TRUE);

		/* if no updates then hide apply */
		widget = glade_xml_get_widget (glade_xml, "button_review");
		gtk_widget_set_sensitive (widget, FALSE);
		polkit_gnome_action_set_sensitive (update_system_action, FALSE);
		polkit_gnome_action_set_sensitive (update_packages_action, FALSE);
	} else {

		PkPackageItem *item;
		guint i;
		guint num_low = 0;
		guint num_normal = 0;
		guint num_important = 0;
		guint num_security = 0;
		guint num_bugfix = 0;
		guint num_enhancement = 0;
		const gchar *icon;
		gchar *text;

		for (i=0;i<length;i++) {
			item = pk_client_package_buffer_get_item (client_query, i);
			if (item->info == PK_INFO_ENUM_LOW) {
				num_low++;
			} else if (item->info == PK_INFO_ENUM_IMPORTANT) {
				num_important++;
			} else if (item->info == PK_INFO_ENUM_SECURITY) {
				num_security++;
			} else if (item->info == PK_INFO_ENUM_BUGFIX) {
				num_bugfix++;
			} else if (item->info == PK_INFO_ENUM_ENHANCEMENT) {
				num_enhancement++;
			} else {
				num_normal++;
			}
		}

		/* clear existing list */
		gtk_list_store_clear (list_store_preview);

		/* add to preview box in order of priority */
		if (num_security > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_SECURITY);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_SECURITY, num_security);
			pk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}
		if (num_important > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_IMPORTANT);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_IMPORTANT, num_important);
			pk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}
		if (num_bugfix > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_BUGFIX);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_BUGFIX, num_bugfix);
			pk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}
		if (num_enhancement > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_ENHANCEMENT);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_ENHANCEMENT, num_enhancement);
			pk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}
		if (num_low > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_LOW);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_LOW, num_low);
			pk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}
		if (num_normal > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_NORMAL);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_NORMAL, num_normal);
			pk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}

		/* set visible and sensitive */
		widget = glade_xml_get_widget (glade_xml, "button_review");
		gtk_widget_set_sensitive (widget, TRUE);
		polkit_gnome_action_set_sensitive (update_system_action, TRUE);
		polkit_gnome_action_set_sensitive (update_packages_action, TRUE);
	}
}

/**
 * pk_update_viewer_get_new_update_list:
 **/
static void
pk_update_viewer_get_new_update_list (void)
{
	gboolean ret;
	GError *error = NULL;

	/* clear existing list */
	gtk_list_store_clear (list_store_details);

	/* get the new update list */
	ret = pk_client_reset (client_query, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return;
	}
	ret = pk_client_get_updates (client_query, PK_FILTER_ENUM_NONE, &error);
	if (!ret) {
		pk_warning ("failed to get updates: %s", error->message);
		g_error_free (error);
		return;
	}
	pk_update_viewer_populate_preview ();
}

/**
 * pk_update_viewer_overview_cb:
 **/
static void
pk_update_viewer_overview_cb (GtkWidget *widget, gpointer data)
{
	/* set correct view */
	pk_update_viewer_set_page (PAGE_PREVIEW);

	/* get the new update list */
	pk_update_viewer_get_new_update_list ();
}

/**
 * pk_update_viewer_package_cb:
 **/
static void
pk_update_viewer_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id,
			     const gchar *summary, gpointer data)
{
	GtkTreeIter iter;
	gchar *text;
	PkRoleEnum role;
	const gchar *icon_name;
	GtkWidget *widget;

	pk_client_get_role (client, &role, NULL, NULL);
	pk_debug ("role = %s, package = %s:%s:%s", pk_role_enum_to_text (role),
		  pk_info_enum_to_text (info), package_id, summary);

	if (role == PK_ROLE_ENUM_GET_UPDATES) {
		text = gpk_package_id_format_twoline (package_id, summary);
		icon_name = gpk_info_enum_to_icon_name (info);
		gtk_list_store_append (list_store_details, &iter);
		gtk_list_store_set (list_store_details, &iter,
				    PACKAGES_COLUMN_TEXT, text,
				    PACKAGES_COLUMN_ID, package_id,
				    PACKAGES_COLUMN_ICON, icon_name,
				    PACKAGES_COLUMN_INFO, info,
				    PACKAGES_COLUMN_SELECT, TRUE,
				    -1);
		g_free (text);
		return;
	}

	if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		text = gpk_package_id_format_twoline (package_id, summary);
		widget = glade_xml_get_widget (glade_xml, "progress_package_label");
		gtk_label_set_markup (GTK_LABEL (widget), text);

		g_free (text);

		return;
	}
}

/**
 * pk_update_viewer_add_description_item:
 **/
static void
pk_update_viewer_add_description_item (const gchar *title, const gchar *text, const gchar *uri)
{
	gchar *markup;
	GtkWidget *tree_view;
	GtkTreeIter iter;
	GtkTreeSelection *selection;

	/* format */
	markup = g_strdup_printf ("<b>%s:</b>", title);

	pk_debug ("%s %s %s", markup, text, uri);
	gtk_list_store_append (list_store_description, &iter);
	gtk_list_store_set (list_store_description, &iter,
			    DESC_COLUMN_TITLE, markup,
			    DESC_COLUMN_TEXT, text,
			    DESC_COLUMN_URI, uri,
			    -1);

	g_free (markup);

	tree_view = glade_xml_get_widget (glade_xml, "treeview_description");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_NONE);
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (tree_view));
}

/**
 * pk_update_viewer_add_description_link_item:
 **/
static void
pk_update_viewer_add_description_link_item (const gchar *title, const gchar *url_string)
{
	const gchar *text;
	const gchar *uri;
	gchar *title_num;
	gchar **urls;
	guint length;
	gint i;

	urls = g_strsplit (url_string, ";", 0);
	length = g_strv_length (urls);
	for (i = 0; urls[i]; i += 2) {
		uri = urls[i];
		text = urls[i+1];
		if (pk_strzero (text)) {
			text = uri;
		}
		/* no suffix needed */
		if (length == 2) {
			pk_update_viewer_add_description_item (title, text, uri);
		} else {
			title_num = g_strdup_printf ("%s (%i)", title, (i/2) + 1);
			pk_update_viewer_add_description_item (title_num, text, uri);
			g_free (title_num);
		}
	}
	g_strfreev (urls);
}

/**
 * pk_update_viewer_update_detail_cb:
 **/
static void
pk_update_viewer_update_detail_cb (PkClient *client, const gchar *package_id,
				    const gchar *updates, const gchar *obsoletes,
				    const gchar *vendor_url, const gchar *bugzilla_url,
				    const gchar *cve_url, PkRestartEnum restart,
				    const gchar *update_text, gpointer data)
{
	GtkWidget *widget;
	PkPackageId *ident;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter treeiter;
	gchar *package_pretty;
	gchar *updates_pretty;
	gchar *obsoletes_pretty;
	const gchar *info_text;
	PkInfoEnum info;

	/* clear existing list */
	pk_update_viewer_description_animation_stop ();
	gtk_list_store_clear (list_store_description);

	/* initially we are hidden */
	widget = glade_xml_get_widget (glade_xml, "scrolledwindow_description");
	gtk_widget_show (widget);

	/* get info  */
	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	if (gtk_tree_selection_get_selected (selection, &model, &treeiter)) {
		gtk_tree_model_get (model, &treeiter,
				    PACKAGES_COLUMN_INFO, &info, -1);
	} else {
		info = PK_INFO_ENUM_NORMAL;
	}

	info_text = gpk_info_enum_to_localised_text (info);
	/* translators: this is the update type, e.g. security */
	pk_update_viewer_add_description_item (_("Type"), info_text, NULL);

	package_pretty = gpk_package_id_name_version (package_id);
	/* translators: this is the package version */
	pk_update_viewer_add_description_item (_("Version"), package_pretty, NULL);
	g_free (package_pretty);

	if (!pk_strzero (updates)) {
		updates_pretty = gpk_package_id_name_version (updates);
		/* translators: this is a list of packages that are updated */
		pk_update_viewer_add_description_item (_("Updates"), updates_pretty, NULL);
		g_free (updates_pretty);
	}

	if (!pk_strzero (obsoletes)) {
		obsoletes_pretty = gpk_package_id_name_version (obsoletes);
		/* translators: this is a list of packages that are obsoleted */
		pk_update_viewer_add_description_item (_("Obsoletes"), obsoletes_pretty, NULL);
		g_free (obsoletes_pretty);
	}

	ident = pk_package_id_new_from_string (package_id);
	/* translators: this is the repository the package has come from */
	pk_update_viewer_add_description_item (_("Repository"), ident->data, NULL);
	pk_package_id_free (ident);

	if (!pk_strzero (update_text)) {
		/* translators: this is the package description */
		pk_update_viewer_add_description_item (_("Description"), update_text, NULL);
	}

	/* add all the links */
	if (!pk_strzero (vendor_url)) {
		/* translators: this is a list of vendor URLs */
		pk_update_viewer_add_description_link_item (_("Vendor"), vendor_url);
	}
	if (!pk_strzero (bugzilla_url)) {
		/* translators: this is a list of bugzilla URLs */
		pk_update_viewer_add_description_link_item (_("Bugzilla"), bugzilla_url);
	}
	if (!pk_strzero (cve_url)) {
		/* translators: this is a list of CVE (security) URLs */
		pk_update_viewer_add_description_link_item (_("CVE"), cve_url);
	}

	/* reboot */
	if (restart == PK_RESTART_ENUM_SESSION ||
	    restart == PK_RESTART_ENUM_SYSTEM) {
		widget = glade_xml_get_widget (glade_xml, "hbox_reboot");
		gtk_widget_show (widget);
	} else {
		widget = glade_xml_get_widget (glade_xml, "hbox_reboot");
		gtk_widget_hide (widget);
	}
}

/**
 * pk_update_viewer_status_changed_cb:
 **/
static void
pk_update_viewer_status_changed_cb (PkClient *client, PkStatusEnum status, gpointer data)
{
	GtkWidget *widget;
	gchar *text;

	widget = glade_xml_get_widget (glade_xml, "progress_part_label");
	text = g_strdup_printf ("<b>%s</b>", gpk_status_enum_to_localised_text (status));
	gtk_label_set_markup (GTK_LABEL (widget), text);
	g_free (text);

	/* when we are testing the transaction, no package should be displayed */
	if (status == PK_STATUS_ENUM_TEST_COMMIT) {
		widget = glade_xml_get_widget (glade_xml, "progress_package_label");
		gtk_label_set_label (GTK_LABEL (widget), "");
	}
}

/**
 * pk_update_viewer_window_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
pk_update_viewer_window_delete_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	g_main_loop_quit (loop);
	return FALSE;
}

/**
 * pk_update_viewer_treeview_update_toggled:
 **/
static void
pk_update_viewer_treeview_update_toggled (GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	GtkTreeModel *model = (GtkTreeModel *) data;
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	gboolean update;
	gchar *package_id;

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_SELECT, &update,
			    PACKAGES_COLUMN_ID, &package_id, -1);

	/* unstage */
	update ^= 1;

	pk_debug ("update %s[%i]", package_id, update);
	g_free (package_id);

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, PACKAGES_COLUMN_SELECT, update, -1);

	/* clean up */
	gtk_tree_path_free (path);
}

/**
 * pk_update_viewer_treeview_add_columns:
 **/
static void
pk_update_viewer_treeview_add_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* image */
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_DIALOG, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Severity"), renderer,
							   "icon-name", PACKAGES_COLUMN_ICON, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_INFO);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Software"), renderer,
							   "markup", PACKAGES_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * pk_update_viewer_treeview_renderer_clicked:
 **/
static void
pk_update_viewer_treeview_renderer_clicked (GtkCellRendererToggle *cell, gchar *uri, gpointer data)
{
	pk_debug ("clicked %s", uri);
	gpk_gnome_open (uri);
}

/**
 * pk_update_viewer_treeview_add_columns_description:
 **/
static void
pk_update_viewer_treeview_add_columns_description (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* image */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "visible", FALSE, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "pixbuf", DESC_COLUMN_PROGRESS);

	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", DESC_COLUMN_TITLE);
	gtk_tree_view_append_column (treeview, column);

	/* column for uris */
	renderer = gpk_cell_renderer_uri_new ();
	g_signal_connect (renderer, "clicked", G_CALLBACK (pk_update_viewer_treeview_renderer_clicked), NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Text"), renderer,
							   "text", DESC_COLUMN_TEXT,
							   "uri", DESC_COLUMN_URI, NULL);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * pk_update_viewer_treeview_add_columns_update:
 **/
static void
pk_update_viewer_treeview_add_columns_update (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeModel *model;

	/* column for select toggle */
	renderer = gtk_cell_renderer_toggle_new ();
	model = gtk_tree_view_get_model (treeview);
	g_signal_connect (renderer, "toggled", G_CALLBACK (pk_update_viewer_treeview_update_toggled), model);
	column = gtk_tree_view_column_new_with_attributes ("Update", renderer, "active", PACKAGES_COLUMN_SELECT, NULL);

	/* set this column to a fixed sizing (of 50 pixels) */
	gtk_tree_view_column_set_sizing (GTK_TREE_VIEW_COLUMN (column), GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width (GTK_TREE_VIEW_COLUMN (column), 20);
	gtk_tree_view_append_column (treeview, column);

	/* usual suspects */
	pk_update_viewer_treeview_add_columns (treeview);
}

/**
 * pk_packages_treeview_clicked_cb:
 **/
static void
pk_packages_treeview_clicked_cb (GtkTreeSelection *selection, gpointer data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *package_id;
	GtkWidget *widget;
	GError *error = NULL;
	gboolean ret;

	widget = glade_xml_get_widget (glade_xml, "scrolledwindow_description");

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    PACKAGES_COLUMN_ID, &package_id, -1);

		if (cached_package_id && package_id &&
		    strcmp (cached_package_id, package_id) == 0) {
			g_free (package_id);
			return;
		}
		g_free (cached_package_id);
		/* make back into package ID */
		cached_package_id = g_strdup (package_id);
		g_free (package_id);

		/* clear and display animation until new details come in */
		pk_update_viewer_description_animation_start ();

		widget = glade_xml_get_widget (glade_xml, "hbox_reboot");
		gtk_widget_hide (widget);

		pk_debug ("selected row is: %s", cached_package_id);

		/* reset */
		ret = pk_client_reset (client_query, &error);
		if (!ret) {
			pk_warning ("failed to reset: %s", error->message);
			g_error_free (error);
		}

		/* get the description */
		error = NULL;
		ret = pk_client_get_update_detail (client_query, cached_package_id, &error);
		if (!ret) {
			pk_warning ("failed to get update detail: %s", error->message);
			g_error_free (error);
		}
	} else {
		pk_debug ("no row selected");
	}
}

/**
 * pk_update_viewer_add_preview_item:
 **/
static void
pk_update_viewer_add_preview_item (const gchar *icon, const gchar *message, gboolean clear)
{
	GtkWidget *tree_view;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	gchar *markup;

	/* clear existing list */
	if (clear) {
		gtk_list_store_clear (list_store_preview);
	}

	markup = g_strdup_printf ("<b>%s</b>", message);
	gtk_list_store_append (list_store_preview, &iter);
	gtk_list_store_set (list_store_preview, &iter,
			    PACKAGES_COLUMN_TEXT, markup,
			    PACKAGES_COLUMN_ICON, icon,
			    -1);
	g_free (markup);

	tree_view = glade_xml_get_widget (glade_xml, "treeview_preview");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_NONE);
}

/**
 * pk_update_get_approx_time:
 **/
static const gchar *
pk_update_get_approx_time (guint time)
{
	if (time < 60) {
		return _("Less than a minute ago");
	} else if (time < 60*60) {
		return _("Less than an hour ago");
	} else if (time < 24*60*60) {
		return _("A few hours ago");
	} else if (time < 7*24*60*60) {
		return _("A few days ago");
	}
	return _("Over a week ago");
}

/**
 * pk_update_update_last_refreshed_time:
 **/
static gboolean
pk_update_update_last_refreshed_time (void)
{
	GtkWidget *widget;
	guint time;
	const gchar *time_text;

	/* get times from the daemon */
	pk_control_get_time_since_action (control, PK_ROLE_ENUM_REFRESH_CACHE, &time, NULL);
	time_text = pk_update_get_approx_time (time);
	widget = glade_xml_get_widget (glade_xml, "label_last_refresh");
	gtk_label_set_label (GTK_LABEL (widget), time_text);
	return TRUE;
}

/**
 * pk_update_update_last_updated_time:
 **/
static gboolean
pk_update_update_last_updated_time (void)
{
	GtkWidget *widget;
	guint time;
	guint time_new;
	const gchar *time_text;

	/* get times from the daemon */
	pk_control_get_time_since_action (control, PK_ROLE_ENUM_UPDATE_SYSTEM, &time, NULL);
	pk_control_get_time_since_action (control, PK_ROLE_ENUM_UPDATE_PACKAGES, &time_new, NULL);

	/* always use the shortest time */
	if (time_new < time) {
		time = time_new;
	}
	time_text = pk_update_get_approx_time (time);
	widget = glade_xml_get_widget (glade_xml, "label_last_update");
	gtk_label_set_label (GTK_LABEL (widget), time_text);
	return TRUE;
}

static void
pk_update_viewer_restart_cb (GtkWidget *widget, gpointer data)
{
	gpk_restart_system ();
}

static void pk_update_viewer_populate_preview (void);

/**
 * pk_update_viewer_check_blocked_packages:
 **/
static void
pk_update_viewer_check_blocked_packages (PkClient *client)
{
	guint i;
	guint length;
	PkPackageItem *item;
	GString *string;
	gboolean exists = FALSE;
	gchar *text;
	gchar *title_bold;
	GtkWidget *widget;

	string = g_string_new ("");

	/* find any that are blocked */
	length = pk_client_package_buffer_get_size (client);
	for (i=0;i<length;i++) {
		item = pk_client_package_buffer_get_item (client, i);
		if (item->info == PK_INFO_ENUM_BLOCKED) {
			text = gpk_package_id_format_oneline (item->package_id, item->summary);
			g_string_append_printf (string, "%s\n", text);
			g_free (text);
			exists = TRUE;
		}
	}

	/* trim off extra newlines */
	if (string->len != 0) {
		g_string_set_size (string, string->len-1);
	}

	/* convert to a normal gchar */
	text = g_string_free (string, FALSE);

	/* set the widget text */
	if (exists) {
		widget = glade_xml_get_widget (glade_xml, "label_update_title");
		title_bold = g_strdup_printf ("<b>%s</b>", _("Some updates were not updated"));
		gtk_label_set_markup (GTK_LABEL (widget), title_bold);
		g_free (title_bold);

		widget = glade_xml_get_widget (glade_xml, "label_update_notice");
		gtk_label_set_markup (GTK_LABEL (widget), text);
		gtk_widget_show (widget);
	} else {
		widget = glade_xml_get_widget (glade_xml, "label_update_title");
		title_bold = g_strdup_printf ("<b>%s</b>", _("System update completed"));
		gtk_label_set_markup (GTK_LABEL (widget), title_bold);
		g_free (title_bold);

		widget = glade_xml_get_widget (glade_xml, "label_update_notice");
		gtk_widget_hide (widget);
	}

	g_free (text);
}

/**
 * pk_update_viewer_finished_cb:
 **/
static void
pk_update_viewer_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, gpointer data)
{
	GtkWidget *widget;
	PkRoleEnum role;
	PkRestartEnum restart;

	pk_client_get_role (client, &role, NULL, NULL);

	/* we don't need to do anything here */
	if (role == PK_ROLE_ENUM_GET_UPDATE_DETAIL ||
	    role == PK_ROLE_ENUM_REFRESH_CACHE) {
		return;
	}

	/* stop the throbber */
	pk_update_viewer_preview_animation_stop ();

	/* check if we need to display infomation about blocked packages */
	if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		pk_update_viewer_check_blocked_packages (client);
	}

	/* hide the cancel */
	if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		widget = glade_xml_get_widget (glade_xml, "button_cancel");
		gtk_widget_hide (widget);

		/* go onto the success page */
		if (exit == PK_EXIT_ENUM_SUCCESS) {

			/* do we have to show any widgets? */
			restart = pk_client_get_require_restart (client);
			if (restart == PK_RESTART_ENUM_SYSTEM ||
			    restart == PK_RESTART_ENUM_SESSION) {
				pk_debug ("showing reboot widgets");
				widget = glade_xml_get_widget (glade_xml, "hbox_restart");
				gtk_widget_show (widget);
				polkit_gnome_action_set_visible (restart_action, TRUE);
			}

			/* set correct view */
			pk_update_viewer_set_page (PAGE_CONFIRM);
		}
	}

	pk_update_viewer_populate_preview ();
}

static void
pk_button_more_installs_cb (GtkWidget *button, gpointer data)
{
	/* set correct view */
	pk_update_viewer_set_page (PAGE_DETAILS);

	pk_update_viewer_get_new_update_list ();
}

/**
 * pk_update_viewer_progress_changed_cb:
 **/
static void
pk_update_viewer_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				guint elapsed, guint remaining, gpointer data)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (glade_xml, "progressbar_percent");

	if (percentage != PK_CLIENT_PERCENTAGE_INVALID) {
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);
	}
}

/**
 * pk_update_viewer_allow_cancel_cb:
 **/
static void
pk_update_viewer_allow_cancel_cb (PkClient *client, gboolean allow_cancel, gpointer data)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (glade_xml, "button_cancel");
	gtk_widget_set_sensitive (widget, allow_cancel);
}

/**
 * pk_update_viewer_preview_set_animation:
 **/
static void
pk_update_viewer_preview_set_animation (const gchar *text)
{
	GtkWidget *widget;

	/* put a message in the listbox */
	pk_update_viewer_add_preview_item ("dialog-information", text, TRUE);

	/* hide apply, review and refresh */
	polkit_gnome_action_set_sensitive (update_system_action, FALSE);
	polkit_gnome_action_set_sensitive (update_packages_action, FALSE);
	polkit_gnome_action_set_sensitive (refresh_action, FALSE);
	widget = glade_xml_get_widget (glade_xml, "button_review");
	gtk_widget_set_sensitive (widget, FALSE);

	/* start the spinning preview */
	pk_update_viewer_preview_animation_start ();
}

/**
 * pk_update_viewer_task_list_changed_cb:
 **/
static void
pk_update_viewer_task_list_changed_cb (PkTaskList *tlist, gpointer data)
{
	GtkWidget *widget;

	/* hide buttons if we are updating */
	if (pk_task_list_contains_role (tlist, PK_ROLE_ENUM_UPDATE_SYSTEM) ||
	    pk_task_list_contains_role (tlist, PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		pk_update_viewer_preview_set_animation (_("A system update is already in progress"));

	} else if (pk_task_list_contains_role (tlist, PK_ROLE_ENUM_GET_UPDATES)) {
		pk_update_viewer_preview_set_animation (_("Getting updates"));

	} else if (pk_task_list_contains_role (tlist, PK_ROLE_ENUM_REFRESH_CACHE)) {
		pk_update_viewer_preview_set_animation (_("Refreshing package cache"));

	} else {
		pk_update_viewer_preview_animation_stop ();

		/* show apply, review and refresh */
		polkit_gnome_action_set_sensitive (update_system_action, TRUE);
		polkit_gnome_action_set_sensitive (update_packages_action, TRUE);
		polkit_gnome_action_set_sensitive (refresh_action, TRUE);
		widget = glade_xml_get_widget (glade_xml, "button_review");
		gtk_widget_set_sensitive (widget, TRUE);
	}
}

/**
 * pk_update_viewer_error_code_cb:
 **/
static void
pk_update_viewer_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, gpointer data)
{
	GtkWidget *widget;
	const gchar *title;
	gchar *title_bold;
	gchar *details_safe;

	/* set correct view */
	pk_update_viewer_set_page (PAGE_ERROR);

	/* set bold title */
	widget = glade_xml_get_widget (glade_xml, "label_error_title");
	title = gpk_error_enum_to_localised_text (code);
	title_bold = g_strdup_printf ("<b>%s</b>", title);
	gtk_label_set_label (GTK_LABEL (widget), title_bold);
	g_free (title_bold);

	widget = glade_xml_get_widget (glade_xml, "label_error_message");
	gtk_label_set_label (GTK_LABEL (widget), gpk_error_enum_to_localised_message (code));

	widget = glade_xml_get_widget (glade_xml, "label_error_details");
	details_safe = g_markup_escape_text (details, -1);
	gtk_label_set_label (GTK_LABEL (widget), details_safe);
	g_free (details_safe);
}

/**
 * pk_update_viewer_repo_list_changed_cb:
 **/
static void
pk_update_viewer_repo_list_changed_cb (PkClient *client, gpointer data)
{
	pk_update_viewer_get_new_update_list ();
}

/**
 * pk_update_viewer_detail_popup_menu_select_all:
 **/
void
pk_update_viewer_detail_popup_menu_select_all (GtkWidget *menuitem, gpointer userdata)
{
	GtkTreeView *treeview = GTK_TREE_VIEW (userdata);
	gboolean valid;
	GtkTreeIter iter;
	GtkTreeModel *model;

	/* get the first iter in the list */
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter, -1);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    PACKAGES_COLUMN_SELECT, TRUE, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * pk_update_viewer_detail_popup_menu_select_none:
 **/
void
pk_update_viewer_detail_popup_menu_select_none (GtkWidget *menuitem, gpointer userdata)
{
	GtkTreeView *treeview = GTK_TREE_VIEW (userdata);
	gboolean valid;
	GtkTreeIter iter;
	GtkTreeModel *model;

	/* get the first iter in the list */
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter, -1);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    PACKAGES_COLUMN_SELECT, FALSE, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * pk_update_viewer_get_checked_status:
 **/
void
pk_update_viewer_get_checked_status (gboolean *all_checked, gboolean *none_checked)
{
	GtkTreeView *treeview;
	gboolean valid;
	gboolean update;
	GtkTreeIter iter;
	GtkTreeModel *model;

	/* get the first iter in the list */
	treeview = GTK_TREE_VIEW (glade_xml_get_widget (glade_xml, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	*all_checked = TRUE;
	*none_checked = TRUE;
	while (valid) {
		gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_SELECT, &update, -1);
		if (update) {
			*none_checked = FALSE;
		} else {
			*all_checked = FALSE;
		}
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * pk_update_viewer_detail_popup_menu_create:
 **/
void
pk_update_viewer_detail_popup_menu_create (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
	GtkWidget *menu;
	GtkWidget *menuitem;
	gboolean all_checked;
	gboolean none_checked;

	menu = gtk_menu_new();

	/* we don't want to show 'Select all' if they are all checked */
	pk_update_viewer_get_checked_status (&all_checked, &none_checked);

	if (!all_checked) {
		menuitem = gtk_menu_item_new_with_label ("Select all");
		g_signal_connect (menuitem, "activate",
				  G_CALLBACK (pk_update_viewer_detail_popup_menu_select_all), treeview);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	}

	if (!none_checked) {
		menuitem = gtk_menu_item_new_with_label ("Unselect all");
		g_signal_connect (menuitem, "activate",
				  G_CALLBACK (pk_update_viewer_detail_popup_menu_select_none), treeview);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	}

	menuitem = gtk_menu_item_new_with_label ("Ignore this package");
	gtk_widget_set_sensitive (GTK_WIDGET (menuitem), FALSE);
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (pk_update_viewer_detail_popup_menu_select_all), treeview);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);

	gtk_widget_show_all (menu);
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
		        (event != NULL) ? event->button : 0,
		        gdk_event_get_time((GdkEvent*)event));
}

/**
 * pk_update_viewer_detail_button_pressed:
 **/
gboolean
pk_update_viewer_detail_button_pressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
	GtkTreeSelection *selection;

	/* single click with the right mouse button? */
	if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
		/* we did not handle this */
		return FALSE;
	}

	pk_debug ("Single right click on the tree view");

	/* select the row */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	if (gtk_tree_selection_count_selected_rows (selection) <= 1) {
		GtkTreePath *path;
		/* Get tree path for row that was clicked */
		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview),
						   (gint) event->x, (gint) event->y, &path,
						   NULL, NULL, NULL)) {
			gtk_tree_selection_unselect_all (selection);
			gtk_tree_selection_select_path (selection, path);
			gtk_tree_path_free (path);
		}
	}

	/* create */
	pk_update_viewer_detail_popup_menu_create (treeview, event, userdata);
	return TRUE;
}

/**
 * pk_update_viewer_detail_popup_menu:
 **/
gboolean
pk_update_viewer_detail_popup_menu (GtkWidget *treeview, gpointer userdata)
{
	pk_update_viewer_detail_popup_menu_create (treeview, NULL, userdata);
	return TRUE;
}

/**
 * pk_update_viewer_task_list_finished_cb:
 **/
static void
pk_update_viewer_task_list_finished_cb (PkTaskList *tlist, PkClient *client, PkExitEnum exit,
					guint runtime, gpointer userdata)
{
	PkRoleEnum role;
	gboolean ret;

	/* get the role */
	ret = pk_client_get_role (client, &role, NULL, NULL);
	if (!ret) {
		pk_warning ("cannot get role");
		return;
	}
	pk_debug ("%s", pk_role_enum_to_text (role));

	/* update last time in the UI */
	if (role == PK_ROLE_ENUM_REFRESH_CACHE) {
		pk_update_update_last_refreshed_time ();
	} else if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
		   role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		pk_update_update_last_updated_time ();
	}

	/* do we need to repopulate the preview widget */
	if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    role == PK_ROLE_ENUM_REFRESH_CACHE) {
		pk_update_viewer_get_new_update_list ();
	}
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	GOptionContext *context;
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkTreeSelection *selection;
	PkRoleEnum roles;
	gboolean ret;
	GtkSizeGroup *size_group;
	GtkWidget *button;
	PolKitAction *pk_action;
	GError *error = NULL;

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
	g_option_context_set_summary (context, _("Software Update Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	pk_debug_init (verbose);
	gtk_init (&argc, &argv);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   PK_DATA G_DIR_SEPARATOR_S "icons");

	loop = g_main_loop_new (NULL, FALSE);

	control = pk_control_new ();
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (pk_update_viewer_repo_list_changed_cb), NULL);

	/* this is stuff we don't care about */
	client_query = pk_client_new ();
	pk_client_set_use_buffer (client_query, TRUE, NULL);
	g_signal_connect (client_query, "package",
			  G_CALLBACK (pk_update_viewer_package_cb), NULL);
	g_signal_connect (client_query, "finished",
			  G_CALLBACK (pk_update_viewer_finished_cb), NULL);
	g_signal_connect (client_query, "progress-changed",
			  G_CALLBACK (pk_update_viewer_progress_changed_cb), NULL);
	g_signal_connect (client_query, "update-detail",
			  G_CALLBACK (pk_update_viewer_update_detail_cb), NULL);
	g_signal_connect (client_query, "status-changed",
			  G_CALLBACK (pk_update_viewer_status_changed_cb), NULL);
	g_signal_connect (client_query, "error-code",
			  G_CALLBACK (pk_update_viewer_error_code_cb), NULL);
	g_signal_connect (client_query, "allow-cancel",
			  G_CALLBACK (pk_update_viewer_allow_cancel_cb), NULL);

	client_action = pk_client_new ();
	pk_client_set_use_buffer (client_action, TRUE, NULL);
	g_signal_connect (client_action, "package",
			  G_CALLBACK (pk_update_viewer_package_cb), NULL);
	g_signal_connect (client_action, "finished",
			  G_CALLBACK (pk_update_viewer_finished_cb), NULL);
	g_signal_connect (client_action, "progress-changed",
			  G_CALLBACK (pk_update_viewer_progress_changed_cb), NULL);
	g_signal_connect (client_action, "status-changed",
			  G_CALLBACK (pk_update_viewer_status_changed_cb), NULL);
	g_signal_connect (client_action, "error-code",
			  G_CALLBACK (pk_update_viewer_error_code_cb), NULL);
	g_signal_connect (client_action, "allow-cancel",
			  G_CALLBACK (pk_update_viewer_allow_cancel_cb), NULL);

	/* get actions */
	roles = pk_control_get_actions (control);

	/* monitor for other updates in progress */
	tlist = pk_task_list_new ();

	glade_xml = glade_xml_new (PK_DATA "/gpk-update-viewer.glade", NULL, NULL);
	main_window = glade_xml_get_widget (glade_xml, "window_updates");

	/* hide until we have updates */
	widget = glade_xml_get_widget (glade_xml, "hbox_reboot");
	gtk_widget_hide (widget);

	/* hide from finished page until we have updates */
	widget = glade_xml_get_widget (glade_xml, "hbox_restart");
	gtk_widget_hide (widget);

	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.consolekit.system.restart");
	restart_action = polkit_gnome_action_new_default ("restart-system", pk_action,
							  _("_Restart computer now"), NULL);
	g_object_set (restart_action,
		      "no-icon-name", GTK_STOCK_REFRESH,
		      "auth-icon-name", GTK_STOCK_REFRESH,
		      "yes-icon-name", GTK_STOCK_REFRESH,
		      "self-blocked-icon-name", GTK_STOCK_REFRESH,
		      "no-visible", FALSE,
		      "master-visible", FALSE,
		      NULL);
	polkit_action_unref (pk_action);
	g_signal_connect (restart_action, "activate",
			  G_CALLBACK (pk_update_viewer_restart_cb), loop);
	button = polkit_gnome_action_create_button (restart_action);
	widget = glade_xml_get_widget (glade_xml, "buttonbox_confirm");
	gtk_box_pack_start (GTK_BOX (widget), button, FALSE, FALSE, 0);
	gtk_box_reorder_child (GTK_BOX (widget), button, 1);

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (pk_update_viewer_window_delete_event_cb), loop);

	/* button_close2 and button_close3 are on the overview/review
	 * screens, where we want to cancel transactions when closing
	 */
	widget = glade_xml_get_widget (glade_xml, "button_close2");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_update_viewer_button_close_and_cancel_cb), loop);
	widget = glade_xml_get_widget (glade_xml, "button_close3");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_update_viewer_button_close_and_cancel_cb), loop);
	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_update_viewer_close_cb), loop);
	widget = glade_xml_get_widget (glade_xml, "button_close4");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_update_viewer_close_cb), loop);
	widget = glade_xml_get_widget (glade_xml, "button_close5");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_update_viewer_close_cb), loop);
	widget = glade_xml_get_widget (glade_xml, "button_cancel");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_update_viewer_button_cancel_cb), loop);
	gtk_widget_set_sensitive (widget, FALSE);

	/* can we ever do the action? */
	if (pk_enums_contain (roles, PK_ROLE_ENUM_CANCEL) == FALSE) {
		gtk_widget_hide (widget);
	}

	widget = glade_xml_get_widget (glade_xml, "button_review");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_update_viewer_review_cb), loop);
	gtk_widget_set_tooltip_text(widget, _("Review the update list"));

	widget = glade_xml_get_widget (glade_xml, "button_overview");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_update_viewer_overview_cb), loop);
	gtk_widget_set_tooltip_text(widget, _("Back to overview"));

	widget = glade_xml_get_widget (glade_xml, "button_overview2");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_more_installs_cb), loop);
	gtk_widget_set_tooltip_text (widget, _("Back to overview"));

	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.update-package");
	update_packages_action = polkit_gnome_action_new_default ("update-package",
								pk_action,
								_("_Apply Updates"),
								_("Apply the selected updates"));
	g_object_set (update_packages_action,
		      "no-icon-name", GTK_STOCK_APPLY,
		      "auth-icon-name", GTK_STOCK_APPLY,
		      "yes-icon-name", GTK_STOCK_APPLY,
		      "self-blocked-icon-name", GTK_STOCK_APPLY,
		      NULL);
	polkit_action_unref (pk_action);
	g_signal_connect (update_packages_action, "activate",
			  G_CALLBACK (pk_update_viewer_apply_cb), loop);
	button = polkit_gnome_action_create_button (update_packages_action);
	widget = glade_xml_get_widget (glade_xml, "buttonbox_review");
	gtk_box_pack_start (GTK_BOX (widget), button, FALSE, FALSE, 0);
	gtk_box_reorder_child (GTK_BOX (widget), button, 2);

	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.update-system");
	update_system_action = polkit_gnome_action_new_default ("update-system",
								pk_action,
								_("_Update System"),
								_("Apply all updates"));
	g_object_set (update_system_action,
		      "no-icon-name", GTK_STOCK_APPLY,
		      "auth-icon-name", GTK_STOCK_APPLY,
		      "yes-icon-name", GTK_STOCK_APPLY,
		      "self-blocked-icon-name", GTK_STOCK_APPLY,
		      NULL);
	polkit_action_unref (pk_action);
	g_signal_connect (update_system_action, "activate",
			  G_CALLBACK (pk_update_viewer_update_system_cb), loop);
	button = polkit_gnome_action_create_button (update_system_action);
	widget = glade_xml_get_widget (glade_xml, "buttonbox_overview");
	gtk_box_pack_start (GTK_BOX (widget), button, FALSE, FALSE, 0);
	gtk_box_reorder_child (GTK_BOX (widget), button, 1);

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_BOTH);

	widget = glade_xml_get_widget (glade_xml, "alignment_refresh");
	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.refresh-cache");
	refresh_action = polkit_gnome_action_new_default ("refresh",
						          pk_action,
							  _("Refresh"),
							  _("Refreshing is not normally required but will retrieve the latest application and update lists"));
	g_object_set (refresh_action, "auth-icon-name", NULL, NULL);
	polkit_action_unref (pk_action);

	g_signal_connect (refresh_action, "activate",
			  G_CALLBACK (pk_update_viewer_refresh_cb), NULL);

	button = polkit_gnome_action_create_button (refresh_action);
	gtk_container_add (GTK_CONTAINER (widget), button);
	gtk_size_group_add_widget (size_group, button);

	widget = glade_xml_get_widget (glade_xml, "button_history");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_update_viewer_history_cb), NULL);
	gtk_size_group_add_widget (size_group, widget);

	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_help_cb), "update-viewer");

	widget = glade_xml_get_widget (glade_xml, "button_help2");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_help_cb), "update-viewer-details");

	/* create list stores */
	list_store_details = gtk_list_store_new (PACKAGES_COLUMN_LAST, G_TYPE_STRING,
						 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN);
	list_store_preview = gtk_list_store_new (PREVIEW_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF);
	list_store_description = gtk_list_store_new (DESC_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF);

	/* sorted */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (list_store_details),
					      PACKAGES_COLUMN_TEXT, GTK_SORT_ASCENDING);

	/* create preview tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_preview");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store_preview));

	/* add columns to the tree view */
	pk_update_viewer_treeview_add_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* create description tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_description");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store_description));

	/* add columns to the tree view */
	pk_update_viewer_treeview_add_columns_description (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* create package tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store_details));
	g_signal_connect (widget, "popup-menu",
			  G_CALLBACK (pk_update_viewer_detail_popup_menu), NULL);
	g_signal_connect (widget, "button-press-event",
			  G_CALLBACK (pk_update_viewer_detail_button_pressed), NULL);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	g_signal_connect (selection, "changed",
			  G_CALLBACK (pk_packages_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	pk_update_viewer_treeview_add_columns_update (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* make the buttons non-clickable until we get completion */
	polkit_gnome_action_set_sensitive (refresh_action, FALSE);
	polkit_gnome_action_set_sensitive (update_system_action, FALSE);
	polkit_gnome_action_set_sensitive (update_packages_action, FALSE);
	widget = glade_xml_get_widget (glade_xml, "button_review");
	gtk_widget_set_sensitive (widget, FALSE);

	/* set the last updated text */
	pk_update_update_last_refreshed_time ();
	pk_update_update_last_updated_time ();

	/* set the labels blank until we get a package */
	widget = glade_xml_get_widget (glade_xml, "progress_part_label");
	gtk_label_set_label (GTK_LABEL (widget), "");
	widget = glade_xml_get_widget (glade_xml, "progress_package_label");
	gtk_label_set_label (GTK_LABEL (widget), "");

	/* we need to grey out all the buttons if we are in progress */
	g_signal_connect (tlist, "changed",
			  G_CALLBACK (pk_update_viewer_task_list_changed_cb), NULL);
	pk_update_viewer_task_list_changed_cb (tlist, NULL);
	g_signal_connect (tlist, "finished",
			  G_CALLBACK (pk_update_viewer_task_list_finished_cb), NULL);

	/* show window */
	gtk_widget_show (main_window);

	/* coldplug */
	pk_update_viewer_get_new_update_list ();

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	/* we might have visual stuff running, close it down */
	ret = pk_client_cancel (client_query, &error);
	if (!ret) {
		pk_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (glade_xml);
	g_object_unref (list_store_preview);
	g_object_unref (list_store_description);
	g_object_unref (list_store_details);
	g_object_unref (control);
	g_object_unref (client_query);
	g_object_unref (client_action);
	g_free (cached_package_id);

	return 0;
}
