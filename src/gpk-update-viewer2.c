/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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
#include <dbus/dbus-glib.h>

#include <gconf/gconf-client.h>
#include <packagekit-glib/packagekit.h>
#include <libnotify/notify.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-unique.h"
#include "egg-markdown.h"

#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-error.h"
#include "gpk-consolekit.h"
#include "gpk-cell-renderer-size.h"
#include "gpk-cell-renderer-info.h"
#include "gpk-cell-renderer-restart.h"
#include "gpk-cell-renderer-percentage.h"
#include "gpk-client.h"
#include "gpk-enum.h"
#include "gpk-repo-signature-helper.h"
#include "gpk-eula-helper.h"

static GMainLoop *loop = NULL;
static GladeXML *glade_xml = NULL;
static GtkListStore *list_store_updates = NULL;
static GtkTextBuffer *text_buffer = NULL;
static PkClient *client_primary = NULL;
static PkClient *client_secondary = NULL;
static PkControl *control = NULL;
static PkPackageList *update_list = NULL;
static GpkRepoSignatureHelper *repo_signature_helper = NULL;
static GpkEulaHelper *eula_helper = NULL;
static EggMarkdown *markdown = NULL;
static PkPackageId *package_id_last = NULL;

enum {
	GPK_UPDATES_COLUMN_TEXT,
	GPK_UPDATES_COLUMN_ID,
	GPK_UPDATES_COLUMN_INFO,
	GPK_UPDATES_COLUMN_SELECT,
	GPK_UPDATES_COLUMN_SENSITIVE,
	GPK_UPDATES_COLUMN_CLICKABLE,
	GPK_UPDATES_COLUMN_RESTART,
	GPK_UPDATES_COLUMN_SIZE,
	GPK_UPDATES_COLUMN_PERCENTAGE,
	GPK_UPDATES_COLUMN_STATUS,
	GPK_UPDATES_COLUMN_DETAILS_OBJ,
	GPK_UPDATES_COLUMN_UPDATE_DETAIL_OBJ,
	GPK_UPDATES_COLUMN_LAST
};

static gboolean gpk_update_viewer_get_new_update_list (void);

/**
 * gpk_update_viewer_button_help_cb:
 **/
static void
gpk_update_viewer_button_help_cb (GtkWidget *widget, gpointer data)
{
	const gchar *id = data;
	gpk_gnome_help (id);
}

/**
 * gpk_update_viewer_button_close_cb:
 **/
static void
gpk_update_viewer_button_close_cb (GtkWidget *widget, gpointer data)
{
	g_main_loop_quit (loop);
}

/**
 * gpk_update_viewer_undisable_packages:
 **/
static void
gpk_update_viewer_undisable_packages ()
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean valid;

	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));

	/* set all the checkboxes sensitive */
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_list_store_set (list_store_updates, &iter,
				    GPK_UPDATES_COLUMN_SENSITIVE, TRUE, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * gpk_update_viewer_button_install_cb:
 **/
static void
gpk_update_viewer_button_install_cb (GtkWidget *widget, gpointer data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean ret;
	gboolean valid;
	gboolean update;
	gboolean selected_all = TRUE;
	gboolean selected_any = FALSE;
	gchar *package_id;
	GError *error = NULL;
	GPtrArray *array;
	gchar **package_ids = NULL;

	egg_debug ("Doing the package updates");
	array = g_ptr_array_new ();

	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));

	/* get the first iter in the list */
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* find out how many we should update */
	while (valid) {
		gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_SELECT, &update,
				    GPK_UPDATES_COLUMN_ID, &package_id, -1);

		/* set all the checkboxes insensitive */
		gtk_list_store_set (list_store_updates, &iter,
				    GPK_UPDATES_COLUMN_SENSITIVE, FALSE, -1);

		/* ay selected? */
		if (!update)
			selected_all = FALSE;
		else
			selected_any = TRUE;

		/* do something with the data */
		if (update) {
			g_ptr_array_add (array, package_id);
		} else {
			/* need to free the one in the array later */
			g_free (package_id);
		}
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* we have no checkboxes selected */
	if (!selected_any) {
		widget = glade_xml_get_widget (glade_xml, "dialog_updates");
		gpk_error_dialog_modal (GTK_WINDOW (widget),
					/* TRANSLATORS: we clicked apply, but had no packages selected */
					_("No updates selected"),
					_("No updates are selected"), NULL);
		return;
	}

	/* reset client */
	ret = pk_client_reset (client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set correct view */
	package_ids = pk_package_ids_from_array (array);
	ret = pk_client_update_packages (client_primary, package_ids, &error);
	if (!ret) {
		egg_warning ("cannot update packages: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_strfreev (package_ids);

	/* get rid of the array, and free the contents */
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);
}

/**
 * gpk_update_viewer_button_cancel_cb:
 **/
static void
gpk_update_viewer_button_cancel_cb (GtkWidget *widget, gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	/* cancel the transaction */
	ret = pk_client_cancel (client_primary, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_update_viewer_button_upgrade_cb:
 **/
static void
gpk_update_viewer_button_upgrade_cb (GtkWidget *widget, gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	ret = g_spawn_command_line_async ("/usr/share/PackageKit/pk-upgrade-distro.sh", NULL);
	if (!ret) {
		egg_warning ("Failure launching pk-upgrade-distro.sh: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_update_viewer_button_delete_event_cb:
 **/
static gboolean
gpk_update_viewer_button_delete_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	g_main_loop_quit (loop);
	return FALSE;
}

/**
 * gpk_update_viewer_find_iter_model_cb:
 **/
static gboolean
gpk_update_viewer_find_iter_model_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, const PkPackageId *id)
{
	gchar *id_tmp = NULL;
	GtkTreePath **_path = NULL;
	PkPackageId *id_new;
	gboolean ret = FALSE;

	_path = (GtkTreePath **) g_object_get_data (G_OBJECT(model), "_path");
	gtk_tree_model_get (model, iter, GPK_UPDATES_COLUMN_ID, &id_tmp, -1);

	/* only match on the name */
	id_new = pk_package_id_new_from_string (id_tmp);
	if (g_strcmp0 (id_new->name, id->name) == 0) {
		*_path = gtk_tree_path_copy (path);
		ret = TRUE;
	}
	pk_package_id_free (id_new);
	return ret;
}

/**
 * gpk_update_viewer_model_get_path:
 **/
static GtkTreePath *
gpk_update_viewer_model_get_path (GtkTreeModel *model, const PkPackageId *id)
{
	GtkTreePath *path = NULL;
	g_object_set_data (G_OBJECT(model), "_path", (gpointer) &path);
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_update_viewer_find_iter_model_cb, (gpointer) id);
	g_object_steal_data (G_OBJECT(model), "_path");
	return path;
}

/**
 * gpk_update_viewer_details_cb:
 **/
static void
gpk_update_viewer_details_cb (PkClient *client, const PkDetailsObj *obj, gpointer data)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;

	treeview = GTK_TREE_VIEW (glade_xml_get_widget (glade_xml, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	path = gpk_update_viewer_model_get_path (model, obj->id);
	if (path == NULL) {
		egg_debug ("not found ID for group");
		return;
	}

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_path_free (path);
	gtk_list_store_set (list_store_updates, &iter,
			    GPK_UPDATES_COLUMN_DETAILS_OBJ, (gpointer) pk_details_obj_copy (obj),
			    GPK_UPDATES_COLUMN_SIZE, (gint)obj->size, -1);
	/* in cache */
	if (obj->size == 0)
		gtk_list_store_set (list_store_updates, &iter,
				    GPK_UPDATES_COLUMN_STATUS, PK_INFO_ENUM_DOWNLOADING, -1);
}

/**
 * gpk_update_viewer_is_update_info:
 **/
static gboolean
gpk_update_viewer_is_update_info (PkInfoEnum info)
{
	if (info == PK_INFO_ENUM_LOW)
		return TRUE;
	if (info == PK_INFO_ENUM_NORMAL)
		return TRUE;
	if (info == PK_INFO_ENUM_IMPORTANT)
		return TRUE;
	if (info == PK_INFO_ENUM_SECURITY)
		return TRUE;
	if (info == PK_INFO_ENUM_BUGFIX)
		return TRUE;
	if (info == PK_INFO_ENUM_ENHANCEMENT)
		return TRUE;
	return FALSE;
}

/**
 * gpk_update_viewer_package_cb:
 **/
static void
gpk_update_viewer_package_cb (PkClient *client, const PkPackageObj *obj, gpointer data)
{
	PkRoleEnum role;
	gchar *text = NULL;
	gchar *package_id;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkWidget *widget;
	GtkTreePath *path;
	gboolean selected;

	pk_client_get_role (client, &role, NULL, NULL);
	egg_debug ("role = %s, package = %s:%s:%s", pk_role_enum_to_text (role),
		  pk_info_enum_to_text (obj->info), obj->id->name, obj->summary);

	/* convert to string */
	package_id = pk_package_id_to_string (obj->id);

	/* used for progress */
	if (!gpk_update_viewer_is_update_info (obj->info)) {
		pk_package_id_free (package_id_last);
		package_id_last = pk_package_id_copy (obj->id);

		/* find model */
		widget = glade_xml_get_widget (glade_xml, "treeview_updates");
		model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));

		/* update icon */
		path = gpk_update_viewer_model_get_path (model, obj->id);
		if (path == NULL) {
			egg_debug ("not found ID for package");
			goto out;
		}

		gtk_tree_model_get_iter (model, &iter, path);
		gtk_list_store_set (list_store_updates, &iter,
				    GPK_UPDATES_COLUMN_STATUS, obj->info, -1);
		gtk_tree_path_free (path);
		goto out;
	}

	/* add to list store */
	text = gpk_package_id_format_twoline (obj->id, obj->summary);
	selected = (obj->info != PK_INFO_ENUM_BLOCKED);
	gtk_list_store_append (list_store_updates, &iter);
	gtk_list_store_set (list_store_updates, &iter,
			    GPK_UPDATES_COLUMN_TEXT, text,
			    GPK_UPDATES_COLUMN_ID, package_id,
			    GPK_UPDATES_COLUMN_INFO, obj->info,
			    GPK_UPDATES_COLUMN_SELECT, selected,
			    GPK_UPDATES_COLUMN_SENSITIVE, selected,
			    GPK_UPDATES_COLUMN_CLICKABLE, selected,
			    GPK_UPDATES_COLUMN_RESTART, PK_RESTART_ENUM_NONE,
			    GPK_UPDATES_COLUMN_STATUS, PK_INFO_ENUM_UNKNOWN,
			    GPK_UPDATES_COLUMN_SIZE, 0,
			    GPK_UPDATES_COLUMN_PERCENTAGE, 0,
			    -1);
out:
	g_free (package_id);
	g_free (text);
}

/**
 * gpk_update_viewer_update_detail_cb:
 **/
static void
gpk_update_viewer_update_detail_cb (PkClient *client, const PkUpdateDetailObj *obj, gpointer data)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;

	treeview = GTK_TREE_VIEW (glade_xml_get_widget (glade_xml, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	path = gpk_update_viewer_model_get_path (model, obj->id);
	if (path == NULL) {
		egg_warning ("not found ID for update detail");
		return;
	}

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_path_free (path);
	gtk_list_store_set (list_store_updates, &iter,
			    GPK_UPDATES_COLUMN_UPDATE_DETAIL_OBJ, (gpointer) pk_update_detail_obj_copy (obj),
			    GPK_UPDATES_COLUMN_RESTART, obj->restart, -1);
}

/**
 * gpk_update_viewer_reconsider_buttons:
 **/
static void
gpk_update_viewer_reconsider_buttons (gpointer data)
{
	GtkWidget *widget;
	PkStatusEnum status;

	/* cancel buttons? */
	pk_client_get_status (client_primary, &status, NULL);
	egg_debug ("status is %s", pk_status_enum_to_text (status));
	if (status == PK_STATUS_ENUM_FINISHED) {
		widget = glade_xml_get_widget (glade_xml, "button_install");
		gtk_widget_show (widget);
		widget = glade_xml_get_widget (glade_xml, "button_cancel");
		gtk_widget_hide (widget);
	} else {
		widget = glade_xml_get_widget (glade_xml, "button_install");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "button_cancel");
		gtk_widget_show (widget);
	}
}

/**
 * gpk_update_viewer_reconsider_info:
 **/
static void
gpk_update_viewer_reconsider_info (GtkTreeModel *model)
{
	GtkTreeIter iter;
	GtkWidget *widget;
	GtkWidget *main_window;
	gboolean valid;
	gboolean selected;
	gboolean any_selected = FALSE;
	guint len;
	guint size;
	guint size_total = 0;
	guint number_total = 0;
	PkRestartEnum restart;
	PkRestartEnum restart_worst = PK_RESTART_ENUM_NONE;
	gchar *text;
	gchar *text_size;

	/* if there are no entries selected, deselect the button */
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_SELECT, &selected,
				    GPK_UPDATES_COLUMN_RESTART, &restart,
				    GPK_UPDATES_COLUMN_SIZE, &size,
				    -1);
		if (selected) {
			any_selected = TRUE;
			size_total += size;
			number_total++;
			if (restart > restart_worst)
				restart_worst = restart;
		}
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* action button */
	widget = glade_xml_get_widget (glade_xml, "button_install");
	gtk_widget_set_sensitive (widget, any_selected);
	main_window = glade_xml_get_widget (glade_xml, "dialog_updates");

	/* have we got any updates */
	len = PK_OBJ_LIST(update_list)->len;
	if (len == 0) {
		widget = glade_xml_get_widget (glade_xml, "vpaned_updates");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "dialog_updates");
		gtk_window_set_resizable (GTK_WINDOW(widget), FALSE);

		/* set state */
		widget = glade_xml_get_widget (glade_xml, "image_progress");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "dialog-information", GTK_ICON_SIZE_DIALOG);
		gtk_widget_show (widget);

		widget = glade_xml_get_widget (glade_xml, "label_summary");
		/* TRANSLATORS: there are no updates */
		text = g_strdup_printf ("<b>%s</b>", _("There are no updates available for your computer"));
		gtk_label_set_label (GTK_LABEL (widget), text);
		g_free (text);
		gtk_widget_show (widget);

		/* close button */
		widget = glade_xml_get_widget (glade_xml, "button_close");
		gtk_widget_show (widget);
		gtk_window_set_focus (GTK_WINDOW(main_window), widget);

		/* header */
		widget = glade_xml_get_widget (glade_xml, "hbox_header");
		gtk_widget_hide (widget);

		widget = glade_xml_get_widget (glade_xml, "button_help");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "button_install");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "label_status");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "label_package");
		gtk_widget_hide (widget);
		goto out;
	}

	/* details */
	widget = glade_xml_get_widget (glade_xml, "vpaned_updates");
	gtk_widget_show (widget);
	widget = glade_xml_get_widget (glade_xml, "dialog_updates");
	gtk_window_set_resizable (GTK_WINDOW(widget), TRUE);
	widget = glade_xml_get_widget (glade_xml, "button_install");
	gtk_widget_show (widget);
	widget = glade_xml_get_widget (glade_xml, "button_help");
	gtk_widget_show (widget);
	gtk_window_set_focus (GTK_WINDOW(main_window), widget);
	widget = glade_xml_get_widget (glade_xml, "button_close");
	gtk_widget_hide (widget);

	/* restart */
	widget = glade_xml_get_widget (glade_xml, "label_package");
	if (restart_worst == PK_RESTART_ENUM_NONE) {
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "image_progress");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "label_summary");
		gtk_label_set_label (GTK_LABEL (widget), "");
	} else {
		gtk_label_set_label (GTK_LABEL (widget), gpk_restart_enum_to_localised_text_future (restart_worst));
		gtk_widget_show (widget);
		widget = glade_xml_get_widget (glade_xml, "image_progress");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), gpk_restart_enum_to_icon_name (restart_worst), GTK_ICON_SIZE_BUTTON);
		gtk_widget_show (widget);
	}

	/* header */
	widget = glade_xml_get_widget (glade_xml, "label_header_title");
	text = g_strdup_printf (ngettext ("There is %i update available",
					  "There are %i updates available", len), len);
	text_size = g_strdup_printf ("<big><b>%s</b></big>", text);
	gtk_label_set_label (GTK_LABEL (widget), text_size);
	g_free (text);
	g_free (text_size);
	widget = glade_xml_get_widget (glade_xml, "hbox_header");
	gtk_widget_show (widget);

	/* total */
	widget = glade_xml_get_widget (glade_xml, "label_summary");
	if (number_total == 0) {
		gtk_label_set_label (GTK_LABEL (widget), "");
	} else {
		text_size = g_format_size_for_display (size_total);
		/* TRANSLATORS: how many updates are selected in the UI */
		text = g_strdup_printf (ngettext ("%i update selected (%s)",
						  "%i updates selected (%s)",
						  number_total), number_total, text_size);
		gtk_label_set_label (GTK_LABEL (widget), text);
		g_free (text);
		g_free (text_size);
	}

	widget = glade_xml_get_widget (glade_xml, "label_summary");
	gtk_widget_show (widget);
out:
	return;
}

/**
 * gpk_update_viewer_status_changed_cb:
 **/
static void
gpk_update_viewer_status_changed_cb (PkClient *client, PkStatusEnum status, gpointer data)
{
	GtkWidget *widget;
	const gchar *text;

	egg_debug ("status %s", pk_status_enum_to_text (status));

	/* clear package */
	if (status == PK_STATUS_ENUM_WAIT) {
		widget = glade_xml_get_widget (glade_xml, "label_package");
		gtk_label_set_label (GTK_LABEL (widget), "");
	}

	/* set status */
	widget = glade_xml_get_widget (glade_xml, "label_status");
	if (status == PK_STATUS_ENUM_FINISHED) {
		gtk_label_set_label (GTK_LABEL (widget), "");
		widget = glade_xml_get_widget (glade_xml, "image_progress");
		gtk_widget_hide (widget);
		goto out;
	}
	if (status == PK_STATUS_ENUM_QUERY) {
		/* TRANSLATORS: querying update list */
		text = _("Getting the list of updates");
	} else {
		text = gpk_status_enum_to_localised_text (status);
	}

	/* set label */
	gtk_label_set_label (GTK_LABEL (widget), text);
	widget = glade_xml_get_widget (glade_xml, "image_progress");

	/* set icon */
	gtk_image_set_from_icon_name (GTK_IMAGE (widget), gpk_status_enum_to_icon_name (status), GTK_ICON_SIZE_BUTTON);
	gtk_widget_show (widget);
out:
	/* set state */
	gpk_update_viewer_reconsider_buttons (NULL);
}

/**
 * gpk_update_viewer_treeview_update_toggled:
 **/
static void
gpk_update_viewer_treeview_update_toggled (GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	GtkTreeModel *model = (GtkTreeModel *) data;
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	gboolean update;
	gchar *package_id;

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_SELECT, &update,
			    GPK_UPDATES_COLUMN_ID, &package_id, -1);

	/* unstage */
	update ^= 1;

	egg_debug ("update %s[%i]", package_id, update);
	g_free (package_id);

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, GPK_UPDATES_COLUMN_SELECT, update, -1);

	/* clean up */
	gtk_tree_path_free (path);

	/* if there are no entries selected, deselect the button */
	gpk_update_viewer_reconsider_info (model);
}

/**
 * gpk_update_viewer_treeview_updates_size_allocate_cb:
 **/
static void
gpk_update_viewer_treeview_updates_size_allocate_cb (GtkWidget *widget, GtkAllocation *allocation, GtkCellRenderer *cell)
{
	GtkTreeViewColumn *column;
	gint width;
	gint wrap_width;

	column = gtk_tree_view_get_column (GTK_TREE_VIEW(widget), 0);
	width = gtk_tree_view_column_get_width (column);
	wrap_width = allocation->width - width - 200;
	if (wrap_width < 10) {
		egg_warning ("wrap_width is impossibly small %i", wrap_width);
		return;
	}
	g_object_set (cell, "wrap-width", wrap_width, NULL);
}

/**
 * gpk_update_viewer_treeview_add_columns_update:
 **/
static void
gpk_update_viewer_treeview_add_columns_update (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeModel *model;

	/* restart */
	renderer = gpk_cell_renderer_restart_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_BUTTON, NULL);
	column = gtk_tree_view_column_new_with_attributes ("", renderer,
							   "value", GPK_UPDATES_COLUMN_RESTART, NULL);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), FALSE);
	gtk_tree_view_append_column (treeview, column);

	/* --- column for image and toggle --- */
	column = gtk_tree_view_column_new ();
	/* TRANSLATORS: if the update should be installed */
	gtk_tree_view_column_set_title (column, _("Install"));
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), FALSE);
	gtk_tree_view_column_set_sort_column_id (column, GPK_UPDATES_COLUMN_INFO);

	/* info */
	renderer = gpk_cell_renderer_info_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_BUTTON, NULL);
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_add_attribute (column, renderer, "value", GPK_UPDATES_COLUMN_INFO);

	/* select toggle */
	renderer = gtk_cell_renderer_toggle_new ();
	model = gtk_tree_view_get_model (treeview);
	g_signal_connect (renderer, "toggled", G_CALLBACK (gpk_update_viewer_treeview_update_toggled), model);
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_add_attribute (column, renderer, "active", GPK_UPDATES_COLUMN_SELECT);
	gtk_tree_view_column_add_attribute (column, renderer, "activatable", GPK_UPDATES_COLUMN_CLICKABLE);
	gtk_tree_view_column_add_attribute (column, renderer, "sensitive", GPK_UPDATES_COLUMN_SENSITIVE);

	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "yalign", 0.0, NULL);
	g_object_set (renderer, "wrap-mode", PANGO_WRAP_WORD, NULL);
	/* TRANSLATORS: a column that has name of the package that will be updated */
	column = gtk_tree_view_column_new_with_attributes (_("Software"), renderer,
							   "markup", GPK_UPDATES_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_UPDATES_COLUMN_TEXT);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_append_column (treeview, column);
	g_signal_connect (treeview, "size-allocate", G_CALLBACK (gpk_update_viewer_treeview_updates_size_allocate_cb), renderer);

	/* column for size */
	renderer = gpk_cell_renderer_size_new ();
	/* TRANSLATORS: a column that has size of the package */
	column = gtk_tree_view_column_new_with_attributes (_("Size"), renderer,
							   "value", GPK_UPDATES_COLUMN_SIZE, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_UPDATES_COLUMN_SIZE);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), FALSE);
	gtk_tree_view_append_column (treeview, column);

	/* --- column for progress --- */
	column = gtk_tree_view_column_new ();
	/* TRANSLATORS: a column that has state of each package */
	gtk_tree_view_column_set_title (column, _("Status"));
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), FALSE);
	gtk_tree_view_column_set_sort_column_id (column, GPK_UPDATES_COLUMN_STATUS);

	/* info */
	renderer = gpk_cell_renderer_info_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_BUTTON, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "value", GPK_UPDATES_COLUMN_STATUS);

	/* column for progress */
	renderer = gpk_cell_renderer_percentage_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "percent", GPK_UPDATES_COLUMN_PERCENTAGE);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), FALSE);

	gtk_tree_view_append_column (treeview, column);
}

/**
 * gpk_update_viewer_add_description_item:
 **/
static void
gpk_update_viewer_add_description_item (const gchar *text)
{
	GtkTextIter start;
	GtkTextIter iter;

	gtk_text_buffer_get_bounds (text_buffer, &start, &iter);
	gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, text, -1, "para", NULL);
	gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
}

/**
 * gpk_update_viewer_add_markup_item:
 **/
static void
gpk_update_viewer_add_markup_item (const gchar *text)
{
	GtkTextIter start;
	GtkTextIter iter;
	gtk_text_buffer_get_bounds (text_buffer, &start, &iter);
	gtk_text_buffer_insert_markup (text_buffer, &iter, text);
	gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
}

/**
 * gpk_text_buffer_insert_link:
 **/
static void
gpk_text_buffer_insert_link (GtkTextBuffer *buffer, GtkTextIter *iter, const gchar *text, const gchar *href)
{
	GtkTextTag *tag;
	tag = gtk_text_buffer_create_tag (buffer, NULL,
					  "foreground", "blue",
					  "underline", PANGO_UNDERLINE_SINGLE,
					  NULL);
	g_object_set_data (G_OBJECT (tag), "href", g_strdup (href));
	gtk_text_buffer_insert_with_tags (buffer, iter, text, -1, tag, NULL);
}

/**
 * gpk_update_viewer_add_description_link_item:
 **/
static void
gpk_update_viewer_add_description_link_item (const gchar *title, const gchar *url_string)
{
	const gchar *text;
	const gchar *uri;
	gchar **urls;
	guint length;
	gint i;
	GtkTextIter start;
	GtkTextIter iter;

	urls = g_strsplit (url_string, ";", 0);
	length = g_strv_length (urls);

	/* could we have malformed descriptions with ';' in them? */
	if (length % 2 != 0) {
		egg_warning ("length not correct, correcting");
		length--;
	}

	/* insert at end */
	gtk_text_buffer_get_bounds (text_buffer, &start, &iter);
	gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, title, -1, "para", NULL);

	for (i=0; i<length; i+=2) {
		uri = urls[i];
		text = urls[i+1];
		if (egg_strzero (text))
			text = uri;

		if (length == 2) {
			gpk_text_buffer_insert_link (text_buffer, &iter, text, uri);
		} else {
			gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
			gtk_text_buffer_insert (text_buffer, &iter, "• ", -1);
			gpk_text_buffer_insert_link (text_buffer, &iter, text, uri);
//			gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
		}
	}
	gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	g_strfreev (urls);
}

#if 0
/**
 * gpk_update_viewer_get_pretty_from_composite:
 **/
static gchar *
gpk_update_viewer_get_pretty_from_composite (const gchar *package_ids_delimit)
{
	guint i;
	guint length;
	gchar **package_ids;
	gchar *pretty = NULL;
	GString *string;
	PkPackageId *id;

	/* do we have any data? */
	if (egg_strzero (package_ids_delimit))
		goto out;

	string = g_string_new ("");
	package_ids = pk_package_ids_from_text (package_ids_delimit);
	length = g_strv_length (package_ids);
	for (i=0; i<length; i++) {
		id = pk_package_id_new_from_string (package_ids[i]);
		pretty = gpk_package_id_name_version (id);
		pk_package_id_free (id);
		g_string_append (string, pretty);
		g_string_append_c (string, ',');
		g_free (pretty);
	}

	/* remove trailing comma */
	g_string_set_size (string, string->len - 1);
	pretty = g_string_free (string, FALSE);
	g_strfreev (package_ids);
out:
	return pretty;
}
#endif

/**
 * gpk_update_viewer_populate_details:
 **/
static void
gpk_update_viewer_populate_details (const PkUpdateDetailObj *obj)
{
	GtkWidget *widget;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter treeiter;
//	gchar *package_pretty;
	PkInfoEnum info;
	gchar *line;
	gchar *issued;
	gchar *updated;

	/* get info  */
	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	if (gtk_tree_selection_get_selected (selection, &model, &treeiter))
		gtk_tree_model_get (model, &treeiter,
				    GPK_UPDATES_COLUMN_INFO, &info, -1);
	else
		info = PK_INFO_ENUM_NORMAL;

	/* blank */
	gtk_text_buffer_set_text (text_buffer, "", -1);

	if (info == PK_INFO_ENUM_ENHANCEMENT) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gpk_update_viewer_add_description_item (_("This update will add new features and expand functionality."));
	} else if (info == PK_INFO_ENUM_BUGFIX) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gpk_update_viewer_add_description_item (_("This update will fix bugs and other non-critical problems."));
	} else if (info == PK_INFO_ENUM_IMPORTANT) {
		/* TRANSLATORS: this is the update type, e.g. security */
		line = g_strdup_printf ("<b>%s</b>", _("This update is important as it may solve critical problems."));
		gpk_update_viewer_add_markup_item (line);
		g_free (line);
	} else if (info == PK_INFO_ENUM_SECURITY) {
		/* TRANSLATORS: this is the update type, e.g. security */
		line = g_strdup_printf ("<b>%s</b>", _("This update is needed to fix a security vulnerability with this package."));
		gpk_update_viewer_add_markup_item (line);
		g_free (line);
	} else if (info == PK_INFO_ENUM_BLOCKED) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gpk_update_viewer_add_description_item (_("This update is blocked."));
	}

	/* issued and updated */
	if (obj->issued != NULL && obj->updated != NULL) {
		issued = pk_iso8601_from_date (obj->issued);
		updated = pk_iso8601_from_date (obj->updated);
		/* TRANSLATORS: this is when the notification was issued and then updated*/
		line = g_strdup_printf (_("This notification was issued on %s and last updated on %s."), issued, updated);
		gpk_update_viewer_add_description_item (line);
		g_free (issued);
		g_free (updated);
		g_free (line);
	} else if (obj->issued != NULL) {
		issued = pk_iso8601_from_date (obj->issued);
		/* TRANSLATORS: this is when the update was issued */
		line = g_strdup_printf (_("This notification was issued on %s."), issued);
		gpk_update_viewer_add_description_item (line);
		g_free (issued);
		g_free (line);
	}

	/* update text */
	if (!egg_strzero (obj->update_text)) {
		/* convert the bullets */
		line = egg_markdown_parse (markdown, obj->update_text);
		if (!egg_strzero (line))
			gpk_update_viewer_add_markup_item (line);
		g_free (line);
	}

	/* add all the links */
	if (!egg_strzero (obj->vendor_url)) {
		/* TRANSLATORS: this is a list of vendor URLs */
		gpk_update_viewer_add_description_link_item (_("For more information about this update please visit:"), obj->vendor_url);
	}
	if (!egg_strzero (obj->bugzilla_url)) {
		/* TRANSLATORS: this is a list of bugzilla URLs */
		gpk_update_viewer_add_description_link_item (_("For more information about bugs fixed by this this update please visit:"), obj->bugzilla_url);
	}
	if (!egg_strzero (obj->cve_url)) {
		/* TRANSLATORS: this is a list of CVE (security) URLs */
		gpk_update_viewer_add_description_link_item (_("For more information about this security update please visit:"), obj->cve_url);
	}

	/* reboot */
	if (obj->restart == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: reboot required */
		gpk_update_viewer_add_description_item (_("The computer will have to be restarted for the changes to take effect."));
	} else if (obj->restart == PK_RESTART_ENUM_SESSION) {
		/* TRANSLATORS: log out required */
		gpk_update_viewer_add_description_item (_("You will need to log off and back on before the changes will take effect"));
	}

	/* state */
	if (obj->state == PK_UPDATE_STATE_ENUM_UNSTABLE) {
		/* TRANSLATORS: this is the stability status of the update */
		gpk_update_viewer_add_description_item (_("This update is unstable, and should not be used on production systems."));
	} else if (obj->state == PK_UPDATE_STATE_ENUM_TESTING) {
		/* TRANSLATORS: this is the stability status of the update */
		gpk_update_viewer_add_description_item (_("This is a test update, and should not be used on production systems."));
	}

#if 0
	/* split and add */
	package_pretty = gpk_update_viewer_get_pretty_from_composite (obj->updates);
	if (!egg_strzero (package_pretty)) {
		/* TRANSLATORS: this is a list of packages that are updated */
		line = g_strdup_printf (_("This update replaces %s."), package_pretty);
		gpk_update_viewer_add_description_item (line);
		g_free (line);
	}
	g_free (package_pretty);

	/* split and add */
	package_pretty = gpk_update_viewer_get_pretty_from_composite (obj->obsoletes);
	if (!egg_strzero (package_pretty)) {
		/* TRANSLATORS: this is a list of packages that are obsoleted */
		line = g_strdup_printf (_("This update obsoletes %s."), package_pretty);
		gpk_update_viewer_add_description_item (line);
		g_free (line);
	}
	g_free (package_pretty);
#endif

	/* changelog */
	if (!egg_strzero (obj->changelog)) {
		/* TRANSLATORS: this is a ChangeLog */
		line = g_strdup_printf ("%s\n%s", _("List of changes:"), obj->changelog);
		gpk_update_viewer_add_description_item (line);
		g_free (line);
	}
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
	PkUpdateDetailObj *obj = NULL;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_UPDATE_DETAIL_OBJ, &obj,
				    GPK_UPDATES_COLUMN_ID, &package_id, -1);
		egg_debug ("selected row is: %s, %p", package_id, obj);
		g_free (package_id);
		if (obj != NULL)
			gpk_update_viewer_populate_details (obj);
	} else {
		egg_debug ("no row selected");
	}
}

/**
 * gpk_update_viewer_check_blocked_packages:
 **/
static void
gpk_update_viewer_check_blocked_packages (PkPackageList *list)
{
	guint i;
	guint length;
	const PkPackageObj *obj;
	GString *string;
	gboolean exists = FALSE;
	gchar *text;
	GtkWidget *widget;

	string = g_string_new ("");

	/* find any that are blocked */
	length = pk_package_list_get_size (list);
	for (i=0;i<length;i++) {
		obj = pk_package_list_get_obj (list, i);
		if (obj->info == PK_INFO_ENUM_BLOCKED) {
			text = gpk_package_id_format_oneline (obj->id, obj->summary);
			g_string_append_printf (string, "%s\n", text);
			g_free (text);
			exists = TRUE;
		}
	}

	/* trim off extra newlines */
	if (string->len != 0)
		g_string_set_size (string, string->len-1);

	/* convert to a normal gchar */
	text = g_string_free (string, FALSE);

	/* nothing of interest */
	if (!exists)
		goto out;

	/* throw up dialog */
	widget = glade_xml_get_widget (glade_xml, "dialog_updates");
	/* TRANSLATORS: we failed to install all the updates we requested */
	gpk_error_dialog_modal (GTK_WINDOW (widget), _("Some updates were not installed"), text, NULL);
out:
	g_free (text);
}

/**
 * gpk_update_viewer_finished_get_details_cb:
 **/
static gboolean
gpk_update_viewer_finished_get_details_cb (PkPackageList *list)
{
	gboolean ret;
	gchar **package_ids;
	GError *error = NULL;
	package_ids = pk_package_list_to_strv (list);

	/* get the details of all the packages */
	ret = pk_client_reset (client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = pk_client_get_details (client_primary, package_ids, &error);
	if (!ret) {
		egg_error ("cannot get details: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_strfreev (package_ids);
	g_object_unref (list);
	return FALSE;
}

/**
 * gpk_update_viewer_finished_get_update_details_cb:
 **/
static gboolean
gpk_update_viewer_finished_get_update_details_cb (PkPackageList *list)
{
	gboolean ret;
	gchar **package_ids;
	GError *error = NULL;
	package_ids = pk_package_list_to_strv (list);

	/* get the details of all the packages */
	ret = pk_client_reset (client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = pk_client_get_update_detail (client_primary, package_ids, &error);
	if (!ret) {
		egg_error ("cannot get details: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_strfreev (package_ids);
	g_object_unref (list);
	return FALSE;
}

/**
 * gpk_update_viewer_finished_get_distro_upgrades_cb:
 **/
static gboolean
gpk_update_viewer_finished_get_distro_upgrades_cb (gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	/* get the details of all the packages */
	ret = pk_client_reset (client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = pk_client_get_distro_upgrades (client_primary, &error);
	if (!ret) {
		egg_error ("cannot get details: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return FALSE;
}

/**
 * gpk_update_viewer_requeue:
 **/
static gboolean
gpk_update_viewer_requeue (gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	/* retry new action */
	ret = pk_client_requeue (client_primary, &error);
	if (!ret) {
		egg_warning ("Failed to requeue: %s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * gpk_update_viewer_finished_cb:
 **/
static void
gpk_update_viewer_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, gpointer data)
{
	GtkWidget *widget;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	PkRoleEnum role;
	PkPackageList *list;

	pk_client_get_role (client, &role, NULL, NULL);
	egg_debug ("role: %s, exit: %s", pk_role_enum_to_text (role), pk_exit_enum_to_text (exit));

	/* clear package */
	widget = glade_xml_get_widget (glade_xml, "label_package");
	gtk_label_set_label (GTK_LABEL (widget), "");

	widget = glade_xml_get_widget (glade_xml, "progressbar_progress");
	gtk_widget_hide (widget);

	/* if secondary, ignore */
	if (client == client_primary &&
	    (exit == PK_EXIT_ENUM_KEY_REQUIRED ||
	     exit == PK_EXIT_ENUM_EULA_REQUIRED)) {
		egg_debug ("ignoring primary sig-required or eula");
		return;
	}

	/* get model */
	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));

	/* clicked cancel on get updates screen */
	if (role == PK_ROLE_ENUM_GET_UPDATES &&
	    exit == PK_EXIT_ENUM_CANCELLED) {
		g_main_loop_quit (loop);
		return;
	}

	if (role == PK_ROLE_ENUM_GET_UPDATES) {
		/* get the download sizes */
		if (update_list != NULL)
			g_object_unref (update_list);
		update_list = pk_client_get_package_list (client_primary);

		/* get the download sizes */
		if (PK_OBJ_LIST(update_list)->len > 0)
			g_idle_add ((GSourceFunc) gpk_update_viewer_finished_get_update_details_cb, g_object_ref (update_list));

		/* set info */
		gpk_update_viewer_reconsider_info (model);
	}

	if (role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		/* get the restarts */
		g_idle_add ((GSourceFunc) gpk_update_viewer_finished_get_details_cb, g_object_ref (update_list));

		/* are now able to do action */
		widget = glade_xml_get_widget (glade_xml, "button_install");
		gtk_widget_set_sensitive (widget, TRUE);

		/* set info */
		gpk_update_viewer_reconsider_info (model);
	}

	if (role == PK_ROLE_ENUM_GET_DETAILS) {
		/* get the distro-upgrades */
		g_idle_add ((GSourceFunc) gpk_update_viewer_finished_get_distro_upgrades_cb, NULL);

		/* select the first entry in the updates list now we've got data */
		widget = glade_xml_get_widget (glade_xml, "treeview_updates");
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
		gtk_tree_selection_unselect_all (selection);
		path = gtk_tree_path_new_first ();
		gtk_tree_selection_select_path (selection, path);
		gtk_tree_path_free (path);

		/* set info */
		gpk_update_viewer_reconsider_info (model);
	}

	/* we've just agreed to auth or a EULA */
	if (role == PK_ROLE_ENUM_INSTALL_SIGNATURE ||
	    role == PK_ROLE_ENUM_ACCEPT_EULA) {
		if (exit == PK_EXIT_ENUM_SUCCESS)
			gpk_update_viewer_requeue (NULL);
		else
			gpk_update_viewer_undisable_packages ();
	}

	/* check if we need to display infomation about blocked packages */
	if (exit == PK_EXIT_ENUM_SUCCESS &&
	    (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	     role == PK_ROLE_ENUM_UPDATE_PACKAGES)) {

		/* check blocked */
		list = pk_client_get_package_list (client_primary);
		gpk_update_viewer_check_blocked_packages (list);
		g_object_unref (list);

		/* refresh list */
		gpk_update_viewer_get_new_update_list ();
	}

	/* we pressed cancel */
	if (exit != PK_EXIT_ENUM_SUCCESS) {
		gpk_update_viewer_undisable_packages ();
	}
}

/**
 * gpk_update_viewer_progress_changed_cb:
 **/
static void
gpk_update_viewer_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				guint elapsed, guint remaining, gpointer data)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;

	widget = glade_xml_get_widget (glade_xml, "progressbar_progress");
	gtk_widget_show (widget);
	if (percentage != PK_CLIENT_PERCENTAGE_INVALID)
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);

	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));

	if (package_id_last == NULL) {
		egg_debug ("no last package");
		return;
	}

	path = gpk_update_viewer_model_get_path (model, package_id_last);
	if (path == NULL) {
		egg_debug ("not found ID for package");
		return;
	}

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_path_free (path);
	gtk_list_store_set (list_store_updates, &iter,
			    GPK_UPDATES_COLUMN_PERCENTAGE, subpercentage, -1);
}

/**
 * gpk_update_viewer_error_code_cb:
 **/
static void
gpk_update_viewer_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, gpointer data)
{
	GtkWidget *widget;

	/* ignore some errors */
	if (code == PK_ERROR_ENUM_PROCESS_KILL ||
	    code == PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
		egg_debug ("error ignored %s\n%s", pk_error_enum_to_text (code), details);
		return;
	}

	/* ignore the ones we can handle */
	if (code == PK_ERROR_ENUM_GPG_FAILURE ||
	    code == PK_ERROR_ENUM_NO_LICENSE_AGREEMENT) {
		egg_debug ("error ignored as we're handling %s\n%s", pk_error_enum_to_text (code), details);
		return;
	}

	widget = glade_xml_get_widget (glade_xml, "dialog_updates");
	gpk_error_dialog_modal (GTK_WINDOW (widget), gpk_error_enum_to_localised_text (code),
				gpk_error_enum_to_localised_message (code), details);
}

/**
 * gpk_update_viewer_repo_list_changed_cb:
 **/
static void
gpk_update_viewer_repo_list_changed_cb (PkClient *client, gpointer data)
{
	gpk_update_viewer_get_new_update_list ();
}

/**
 * gpk_update_viewer_detail_popup_menu_select_all:
 **/
static void
gpk_update_viewer_detail_popup_menu_select_all (GtkWidget *menuitem, gpointer userdata)
{
	GtkTreeView *treeview = GTK_TREE_VIEW (userdata);
	gboolean valid;
	GtkTreeIter iter;
	GtkTreeModel *model;
	PkStatusEnum info;

	/* get the first iter in the list */
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_INFO, &info, -1);
		if (info != PK_INFO_ENUM_BLOCKED)
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					    GPK_UPDATES_COLUMN_SELECT, TRUE, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* if there are no entries selected, deselect the button */
	gpk_update_viewer_reconsider_info (model);
}

/**
 * gpk_update_viewer_detail_popup_menu_select_none:
 **/
static void
gpk_update_viewer_detail_popup_menu_select_none (GtkWidget *menuitem, gpointer userdata)
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
				    GPK_UPDATES_COLUMN_SELECT, FALSE, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* if there are no entries selected, deselect the button */
	gpk_update_viewer_reconsider_info (model);
}

/**
 * gpk_update_viewer_get_checked_status:
 **/
static void
gpk_update_viewer_get_checked_status (gboolean *all_checked, gboolean *none_checked)
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
		gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_SELECT, &update, -1);
		if (update)
			*none_checked = FALSE;
		else
			*all_checked = FALSE;
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * gpk_update_viewer_detail_popup_menu_create:
 **/
static void
gpk_update_viewer_detail_popup_menu_create (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
	GtkWidget *menu;
	GtkWidget *menuitem;
	gboolean all_checked;
	gboolean none_checked;

	menu = gtk_menu_new();

	/* we don't want to show 'Select all' if they are all checked */
	gpk_update_viewer_get_checked_status (&all_checked, &none_checked);

	if (!all_checked) {
		/* TRANSLATORS: right click menu, select all the updates */
		menuitem = gtk_menu_item_new_with_label (_("Select all"));
		g_signal_connect (menuitem, "activate",
				  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_all), treeview);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	}

	if (!none_checked) {
		/* TRANSLATORS: right click menu, unselect all the updates */
		menuitem = gtk_menu_item_new_with_label (_("Unselect all"));
		g_signal_connect (menuitem, "activate",
				  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_none), treeview);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	}

	/* TRANSLATORS: right click option, ignore this update name, not currently used */
	menuitem = gtk_menu_item_new_with_label (_("Ignore this update"));
	gtk_widget_set_sensitive (GTK_WIDGET (menuitem), FALSE);
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_all), treeview);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);

	gtk_widget_show_all (menu);
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
		        (event != NULL) ? event->button : 0,
		        gdk_event_get_time((GdkEvent*)event));
}

/**
 * gpk_update_viewer_detail_button_pressed:
 **/
static gboolean
gpk_update_viewer_detail_button_pressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
	GtkTreeSelection *selection;
	GtkTreePath *path;

	/* single click with the right mouse button? */
	if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
		/* we did not handle this */
		return FALSE;
	}

	egg_debug ("Single right click on the tree view");

	/* select the row */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	if (gtk_tree_selection_count_selected_rows (selection) <= 1) {
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
	gpk_update_viewer_detail_popup_menu_create (treeview, event, userdata);
	return TRUE;
}

/**
 * gpk_update_viewer_detail_popup_menu:
 **/
static gboolean
gpk_update_viewer_detail_popup_menu (GtkWidget *treeview, gpointer userdata)
{
	gpk_update_viewer_detail_popup_menu_create (treeview, NULL, userdata);
	return TRUE;
}

/**
 * gpk_update_viewer_activated_cb
 **/
static void
gpk_update_viewer_activated_cb (EggUnique *egg_unique, gpointer data)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (glade_xml, "dialog_updates");
	gtk_window_present (GTK_WINDOW (widget));
}

/**
 * gpk_update_viewer_get_new_update_list
 **/
static gboolean
gpk_update_viewer_get_new_update_list (void)
{
	gboolean ret;
	GError *error = NULL;

	/* clear all widgets */
	gtk_list_store_clear (list_store_updates);
	gtk_text_buffer_set_text (text_buffer, "", -1);

	/* reset client */
	ret = pk_client_reset (client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get new list */
	ret = pk_client_get_updates (client_primary, PK_FILTER_ENUM_NONE, &error);
	if (!ret) {
		egg_warning ("Failed to get updates: %s", error->message);
		g_error_free (error);
	}
out:
	return ret;
}

/**
 * gpk_update_viewer_allow_cancel_cb:
 **/
static void
gpk_update_viewer_allow_cancel_cb (PkClient *client, gboolean allow_cancel, gpointer data)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (glade_xml, "button_cancel");
	gtk_widget_set_sensitive (widget, allow_cancel);
}

/**
 * gpk_update_viewer_eula_cb:
 **/
static void
gpk_update_viewer_eula_required_cb (PkClient *client, const gchar *eula_id, const gchar *package_id,
				    const gchar *vendor_name, const gchar *license_agreement, gpointer data)
{
	/* use the helper */
	gpk_eula_helper_show (eula_helper, eula_id, package_id, vendor_name, license_agreement);
}

/**
 * gpk_update_viewer_repo_signature_event_cb:
 **/
static void
gpk_update_viewer_repo_signature_event_cb (GpkRepoSignatureHelper *_repo_signature_helper, GtkResponseType type, const gchar *key_id, const gchar *package_id, gpointer data)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	gboolean ret;
	GError *error = NULL;

	if (type != GTK_RESPONSE_YES) {
		/* we've ruined the old one by making the checkboxes insensitive */
		gpk_update_viewer_get_new_update_list ();
		gpk_update_viewer_reconsider_buttons (NULL);
		goto out;
	}

	/* reset client */
	ret = pk_client_reset (client_secondary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* install signature */
	ret = pk_client_install_signature (client_secondary, PK_SIGTYPE_ENUM_GPG, key_id, package_id, &error);
	if (!ret) {
		egg_warning ("cannot install signature: %s", error->message);
		g_error_free (error);
		/* we've ruined the old one by making the checkboxes insensitive */
		gpk_update_viewer_get_new_update_list ();
		goto out;
	}
out:
	/* set state */
	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));
	gpk_update_viewer_reconsider_info (model);
}

/**
 * gpk_update_viewer_eula_event_cb:
 **/
static void
gpk_update_viewer_eula_event_cb (GpkRepoSignatureHelper *_eula_helper, GtkResponseType type, const gchar *eula_id, gpointer data)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	gboolean ret;
	GError *error = NULL;

	if (type != GTK_RESPONSE_YES) {
		/* we've ruined the old one by making the checkboxes insensitive */
		gpk_update_viewer_get_new_update_list ();
		gpk_update_viewer_reconsider_buttons (NULL);
		goto out;
	}

	/* reset client */
	ret = pk_client_reset (client_secondary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* install signature */
	ret = pk_client_accept_eula (client_secondary, eula_id, &error);
	if (!ret) {
		egg_warning ("cannot accept eula: %s", error->message);
		g_error_free (error);
		/* we've ruined the old one by making the checkboxes insensitive */
		gpk_update_viewer_get_new_update_list ();
		goto out;
	}
out:
	/* set state */
	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));
	gpk_update_viewer_reconsider_info (model);
}

/**
 * gpk_update_viewer_repo_signature_required_cb:
 **/
static void
gpk_update_viewer_repo_signature_required_cb (PkClient *client, const gchar *package_id, const gchar *repository_name,
					      const gchar *key_url, const gchar *key_userid, const gchar *key_id,
					      const gchar *key_fingerprint, const gchar *key_timestamp,
					      PkSigTypeEnum type, gpointer data)
{
	/* use the helper */
	gpk_repo_signature_helper_show (repo_signature_helper, package_id, repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp);
}

/**
 * pk_client_distro_upgrade_cb:
 **/
static void
pk_client_distro_upgrade_cb (PkClient *client, const PkDistroUpgradeObj *obj, gpointer data)
{
	gchar *text;
	gchar *text_format;
	GtkWidget *widget;

	if (obj->state != PK_UPDATE_STATE_ENUM_STABLE)
		return;

	/* only display last (newest) distro */
	widget = glade_xml_get_widget (glade_xml, "label_upgrade");
	/* TRANSLATORS: new distro available, e.g. F9 to F10 */
	text = g_strdup_printf (_("New distribution upgrade release '%s' is available"), obj->summary);
	text_format = g_strdup_printf ("<b>%s</b>", text);
	gtk_label_set_label (GTK_LABEL (widget), text_format);
	g_free (text);
	g_free (text_format);
	widget = glade_xml_get_widget (glade_xml, "viewport_upgrade");
	gtk_widget_show (widget);
}

/**
 * gpk_update_viewer_textview_follow_link:
 *
 * Looks at all tags covering the position of iter in the text view,
 * and if one of them is a link, follow it by showing the page identified
 * by the data attached to it.
 **/
static void
gpk_update_viewer_textview_follow_link (GtkWidget *text_view, GtkTextIter *iter)
{
	GSList *tags = NULL, *tagp = NULL;

	tags = gtk_text_iter_get_tags (iter);
	for (tagp = tags; tagp != NULL; tagp = tagp->next) {
		GtkTextTag *tag = tagp->data;
		const gchar *href = (const gchar *) (g_object_get_data (G_OBJECT (tag), "href"));
		gpk_gnome_open (href);
	}

	if (tags != NULL)
		g_slist_free (tags);
}

/**
 * gpk_update_viewer_textview_key_press_event:
 *
 * Links can be activated by pressing Enter
 **/
static gboolean
gpk_update_viewer_textview_key_press_event (GtkWidget *text_view, GdkEventKey *event)
{
	GtkTextIter iter;
	GtkTextBuffer *buffer;

	switch (event->keyval) {
		case GDK_Return:
		case GDK_KP_Enter:
			buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
			gtk_text_buffer_get_iter_at_mark (buffer, &iter, gtk_text_buffer_get_insert (buffer));
			gpk_update_viewer_textview_follow_link (text_view, &iter);
			break;
		default:
		break;
	}

	return FALSE;
}

/**
 * gpk_update_viewer_textview_event_after:
 *
 * Links can also be activated by clicking
 **/
static gboolean
gpk_update_viewer_textview_event_after (GtkWidget *text_view, GdkEvent *ev)
{
	GtkTextIter start, end, iter;
	GtkTextBuffer *buffer;
	GdkEventButton *event;
	gint x, y;

	if (ev->type != GDK_BUTTON_RELEASE)
		return FALSE;

	event = (GdkEventButton *)ev;
	if (event->button != 1)
		return FALSE;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));

	/* we shouldn't follow a link if the user has selected something */
	gtk_text_buffer_get_selection_bounds (buffer, &start, &end);
	if (gtk_text_iter_get_offset (&start) != gtk_text_iter_get_offset (&end))
		return FALSE;

	gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (text_view), GTK_TEXT_WINDOW_WIDGET, event->x, event->y, &x, &y);
	gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (text_view), &iter, x, y);
	gpk_update_viewer_textview_follow_link (text_view, &iter);

	return FALSE;
}

/**
 * gpk_update_viewer_textview_set_cursor:
 *
 * Looks at all tags covering the position (x, y) in the text view,
 * and if one of them is a link, change the cursor to the "hands" cursor
 * typically used by web browsers.
 **/
static void
gpk_update_viewer_textview_set_cursor (GtkTextView *text_view, gint x, gint y)
{
	GSList *tags = NULL, *tagp = NULL;
	GtkTextIter iter;
	GdkCursor *cursor;
	gboolean hovering = FALSE;
	gboolean hovering_over_link = FALSE;

	hovering_over_link = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT(text_view), "hovering"));
	gtk_text_view_get_iter_at_location (text_view, &iter, x, y);

	tags = gtk_text_iter_get_tags (&iter);
	for (tagp = tags; tagp != NULL; tagp = tagp->next) {
		GtkTextTag *tag = tagp->data;
		const gchar *href = (const gchar *) g_object_get_data (G_OBJECT (tag), "href");
		if (href != NULL) {
			hovering = TRUE;
			break;
		}
	}

	/* already set same state */
	if (hovering != hovering_over_link) {
		g_object_set_data (G_OBJECT(text_view), "hovering", GUINT_TO_POINTER (hovering));
		if (hovering)
			cursor = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_HAND2);
		else
			cursor = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_XTERM);
		gdk_window_set_cursor (gtk_text_view_get_window (text_view, GTK_TEXT_WINDOW_TEXT), cursor);
		gdk_cursor_unref (cursor);
	}

	if (tags != NULL)
		g_slist_free (tags);
}

/**
 * gpk_update_viewer_textview_motion_notify_event:
 *
 * Update the cursor image if the pointer moved.
 **/
static gboolean
gpk_update_viewer_textview_motion_notify_event (GtkWidget *text_view, GdkEventMotion *event)
{
	gint x, y;

	gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (text_view), GTK_TEXT_WINDOW_WIDGET, event->x, event->y, &x, &y);
	gpk_update_viewer_textview_set_cursor (GTK_TEXT_VIEW (text_view), x, y);
	gdk_window_get_pointer (text_view->window, NULL, NULL, NULL);
	return FALSE;
}

/**
 * gpk_update_viewer_textview_visibility_notify_event:
 *
 * Also update the cursor image if the window becomes visible
 * (e.g. when a window covering it got iconified).
 **/
static gboolean
gpk_update_viewer_textview_visibility_notify_event (GtkWidget *text_view, GdkEventVisibility *event)
{
	gint wx, wy, bx, by;

	gdk_window_get_pointer (text_view->window, &wx, &wy, NULL);
	gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (text_view), GTK_TEXT_WINDOW_WIDGET, wx, wy, &bx, &by);
	gpk_update_viewer_textview_set_cursor (GTK_TEXT_VIEW (text_view), bx, by);
	return FALSE;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	GOptionContext *context;
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkTreeSelection *selection;
	PkBitfield roles;
	gboolean ret;
	GError *error = NULL;
	EggUnique *egg_unique;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
		  _("Show the program version and exit"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (NULL);
	/* TRANSLATORS: program name, a simple app to view pending updates */
	g_option_context_set_summary (context, _("Software Update Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	egg_debug_init (verbose);
	notify_init ("gpk-update-viewer");
	gtk_init (&argc, &argv);

	/* TRANSLATORS: title to pass to to the user if there are not enough privs */
	ret = gpk_check_privileged_user (_("Software Update Viewer"), TRUE);
	if (!ret)
		return 1;

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* are we already activated? */
	egg_unique = egg_unique_new ();
	ret = egg_unique_assign (egg_unique, "org.freedesktop.PackageKit.UpdateViewer2");
	if (!ret)
		goto unique_out;

	g_signal_connect (egg_unique, "activated", G_CALLBACK (gpk_update_viewer_activated_cb), NULL);

	markdown = egg_markdown_new ();
	egg_markdown_set_output (markdown, EGG_MARKDOWN_OUTPUT_PANGO);
	egg_markdown_set_escape (markdown, TRUE);

	control = pk_control_new ();
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (gpk_update_viewer_repo_list_changed_cb), NULL);

	/* this is what we use mainly */
	client_primary = pk_client_new ();
	pk_client_set_use_buffer (client_primary, TRUE, NULL);
	g_signal_connect (client_primary, "package",
			  G_CALLBACK (gpk_update_viewer_package_cb), NULL);
	g_signal_connect (client_primary, "details",
			  G_CALLBACK (gpk_update_viewer_details_cb), NULL);
	g_signal_connect (client_primary, "finished",
			  G_CALLBACK (gpk_update_viewer_finished_cb), NULL);
	g_signal_connect (client_primary, "progress-changed",
			  G_CALLBACK (gpk_update_viewer_progress_changed_cb), NULL);
	g_signal_connect (client_primary, "update-detail",
			  G_CALLBACK (gpk_update_viewer_update_detail_cb), NULL);
	g_signal_connect (client_primary, "status-changed",
			  G_CALLBACK (gpk_update_viewer_status_changed_cb), NULL);
	g_signal_connect (client_primary, "error-code",
			  G_CALLBACK (gpk_update_viewer_error_code_cb), NULL);
	g_signal_connect (client_primary, "allow-cancel",
			  G_CALLBACK (gpk_update_viewer_allow_cancel_cb), NULL);
	g_signal_connect (client_primary, "repo-signature-required",
			  G_CALLBACK (gpk_update_viewer_repo_signature_required_cb), NULL);
	g_signal_connect (client_primary, "eula-required",
			  G_CALLBACK (gpk_update_viewer_eula_required_cb), NULL);
	g_signal_connect (client_primary, "distro-upgrade",
			  G_CALLBACK (pk_client_distro_upgrade_cb), NULL);

	/* this is for auth and eula callbacks */
	client_secondary = pk_client_new ();
	g_signal_connect (client_secondary, "error-code",
			  G_CALLBACK (gpk_update_viewer_error_code_cb), NULL);
	g_signal_connect (client_secondary, "finished",
			  G_CALLBACK (gpk_update_viewer_finished_cb), NULL);

	/* get actions */
	roles = pk_control_get_actions (control, NULL);

	glade_xml = glade_xml_new (GPK_DATA "/gpk-update-viewer2.glade", NULL, NULL);
	main_window = glade_xml_get_widget (glade_xml, "dialog_updates");
	g_signal_connect (main_window, "delete_event", G_CALLBACK (gpk_update_viewer_button_delete_event_cb), NULL);

	/* helpers */
	repo_signature_helper = gpk_repo_signature_helper_new ();
	g_signal_connect (repo_signature_helper, "event", G_CALLBACK (gpk_update_viewer_repo_signature_event_cb), NULL);
	gpk_repo_signature_helper_set_parent (repo_signature_helper, GTK_WINDOW (main_window));

	eula_helper = gpk_eula_helper_new ();
	g_signal_connect (eula_helper, "event", G_CALLBACK (gpk_update_viewer_eula_event_cb), NULL);
	gpk_eula_helper_set_parent (eula_helper, GTK_WINDOW (main_window));

	/* make GpkClient windows modal */
	gtk_widget_realize (main_window);

	/* create list stores */
	list_store_updates = gtk_list_store_new (GPK_UPDATES_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT,
						 G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
						 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_POINTER);
	text_buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_create_tag (text_buffer, "para",
				    "pixels_below_lines", 3,
				    "pixels_above_lines", 3, NULL);

	/* no upgrades yet */
	widget = glade_xml_get_widget (glade_xml, "viewport_upgrade");
	gtk_widget_hide (widget);

	/* header */
	widget = glade_xml_get_widget (glade_xml, "hbox_header");
	gtk_widget_hide (widget);

	/* description */
	widget = glade_xml_get_widget (glade_xml, "textview_details");
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), text_buffer);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (widget), FALSE);
	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (widget), 5);
	g_signal_connect (GTK_TEXT_VIEW (widget), "key-press-event", G_CALLBACK (gpk_update_viewer_textview_key_press_event), NULL);
	g_signal_connect (GTK_TEXT_VIEW (widget), "event-after", G_CALLBACK (gpk_update_viewer_textview_event_after), NULL);
	g_signal_connect (GTK_TEXT_VIEW (widget), "motion-notify-event", G_CALLBACK (gpk_update_viewer_textview_motion_notify_event), NULL);
	g_signal_connect (GTK_TEXT_VIEW (widget), "visibility-notify-event", G_CALLBACK (gpk_update_viewer_textview_visibility_notify_event), NULL);

	/* updates */
	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	gtk_widget_set_size_request (GTK_WIDGET (widget), 800, 200);
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store_updates));
	gpk_update_viewer_treeview_add_columns_update (GTK_TREE_VIEW (widget));
	g_signal_connect (widget, "popup-menu",
			  G_CALLBACK (gpk_update_viewer_detail_popup_menu), NULL);
	g_signal_connect (widget, "button-press-event",
			  G_CALLBACK (gpk_update_viewer_detail_button_pressed), NULL);

	/* selection */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	g_signal_connect (selection, "changed",
			  G_CALLBACK (pk_packages_treeview_clicked_cb), NULL);

	/* bottom UI */
	widget = glade_xml_get_widget (glade_xml, "progressbar_progress");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (glade_xml, "label_summary");
	gtk_widget_hide (widget);

	/* help button */
	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_help_cb), (gpointer) "update-viewer");

	/* set install button insensitive */
	widget = glade_xml_get_widget (glade_xml, "button_install");
	gtk_widget_set_sensitive (widget, FALSE);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_install_cb), NULL);

	/* close button */
	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_close_cb), NULL);
	gtk_widget_hide (widget);
	gtk_window_set_focus (GTK_WINDOW(main_window), widget);

	/* hide cancel button */
	widget = glade_xml_get_widget (glade_xml, "button_cancel");
	gtk_widget_hide (widget);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_cancel_cb), NULL);

	/* upgrade button */
	widget = glade_xml_get_widget (glade_xml, "button_upgrade");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_upgrade_cb), NULL);

	/* show window */
	gtk_widget_show (main_window);

	/* coldplug */
	gpk_update_viewer_get_new_update_list ();

	/* wait */
	loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (loop);

	/* we might have visual stuff running, close it down */
	ret = pk_client_cancel (client_primary, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_clear_error (&error);
	}

	/* we might have visual stuff running, close it down */
	ret = pk_client_cancel (client_secondary, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
	}

	g_main_loop_unref (loop);

	if (update_list != NULL)
		g_object_unref (update_list);

	g_object_unref (eula_helper);
	g_object_unref (repo_signature_helper);
	g_object_unref (glade_xml);
	g_object_unref (list_store_updates);
	g_object_unref (control);
	g_object_unref (markdown);
	g_object_unref (client_primary);
	g_object_unref (client_secondary);
	g_object_unref (text_buffer);
	pk_package_id_free (package_id_last);
unique_out:
	g_object_unref (egg_unique);

	return 0;
}

