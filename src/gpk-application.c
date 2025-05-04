/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2012 Richard Hughes <richard@hughsie.com>
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

#include <errno.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <glib/gi18n.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <math.h>
#include <packagekit-glib2/packagekit.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gpk-common.h"
#include "gpk-common.h"
#include "gpk-dialog.h"
#include "gpk-enum.h"
#include "gpk-error.h"
#include "gpk-task.h"
#include "gpk-debug.h"

typedef enum {
	GPK_SEARCH_NAME,
	GPK_SEARCH_DETAILS,
	GPK_SEARCH_FILE,
	GPK_SEARCH_UNKNOWN
} GpkSearchType;

typedef enum {
	GPK_MODE_NAME_DETAILS_FILE,
	GPK_MODE_GROUP,
	GPK_MODE_ALL_PACKAGES,
	GPK_MODE_SELECTED,
	GPK_MODE_UNKNOWN
} GpkSearchMode;

typedef enum {
	GPK_ACTION_NONE,
	GPK_ACTION_INSTALL,
	GPK_ACTION_REMOVE,
	GPK_ACTION_UNKNOWN
} GpkActionMode;

typedef struct {
	gboolean		 has_package;
	gboolean		 search_in_progress;
	GCancellable		*cancellable;
	gchar			*homepage_url;
	gchar			*search_group;
	gchar			*search_text;
	GHashTable		*repos;
	GpkActionMode		 action;
	GpkSearchMode		 search_mode;
	GpkSearchType		 search_type;
	GtkApplication		*application;
	GSettings		*settings;
	GtkBuilder		*builder;
	GtkListStore		*packages_store;
	GtkTreeStore		*groups_store;
	guint			 details_event_id;
	guint			 status_id;
	PkBitfield		 filters_current;
	PkBitfield		 groups;
	PkBitfield		 roles;
	PkControl		*control;
	PkPackageSack		*package_sack;
	PkStatusEnum		 status_last;
	PkTask			*task;
} GpkApplicationPrivate;

enum {
	GPK_STATE_INSTALLED,
	GPK_STATE_IN_LIST,
	GPK_STATE_COLLECTION,
	GPK_STATE_UNKNOWN
};

enum {
	PACKAGES_COLUMN_IMAGE,
	PACKAGES_COLUMN_STATE,  /* state of the item */
	PACKAGES_COLUMN_CHECKBOX,  /* what we show in the checkbox */
	PACKAGES_COLUMN_CHECKBOX_VISIBLE, /* visible */
	PACKAGES_COLUMN_TEXT,
	PACKAGES_COLUMN_ID,
	PACKAGES_COLUMN_SUMMARY,
	PACKAGES_COLUMN_LAST
};

enum {
	GROUPS_COLUMN_ICON,
	GROUPS_COLUMN_NAME,
	GROUPS_COLUMN_SUMMARY,
	GROUPS_COLUMN_ID,
	GROUPS_COLUMN_ACTIVE,
	GROUPS_COLUMN_LAST
};

static void gpk_application_perform_search (GpkApplicationPrivate *priv);

static void gpk_application_get_requires_cb (PkTask *task, GAsyncResult *res, GpkApplicationPrivate *priv);
static void gpk_application_get_depends_cb (PkTask *task, GAsyncResult *res, GpkApplicationPrivate *priv);

static gboolean
_g_strzero (const gchar *text)
{
	if (text == NULL)
		return TRUE;
	if (text[0] == '\0')
		return TRUE;
	return FALSE;
}

static const gchar *
gpk_application_state_get_icon (PkBitfield state)
{
	if (state == 0)
		return gpk_info_enum_to_icon_name (PK_INFO_ENUM_AVAILABLE);

	if (state == pk_bitfield_value (GPK_STATE_INSTALLED))
		return gpk_info_enum_to_icon_name (PK_INFO_ENUM_INSTALLED);

	if (state == pk_bitfield_value (GPK_STATE_IN_LIST))
		return gpk_info_enum_to_icon_name (PK_INFO_ENUM_INSTALLING);

	if (state == pk_bitfield_from_enums (GPK_STATE_INSTALLED, GPK_STATE_IN_LIST, -1))
		return gpk_info_enum_to_icon_name (PK_INFO_ENUM_REMOVING);

	if (state == pk_bitfield_value (GPK_STATE_COLLECTION))
		return gpk_info_enum_to_icon_name (PK_INFO_ENUM_COLLECTION_AVAILABLE);

	if (state == pk_bitfield_from_enums (GPK_STATE_INSTALLED, GPK_STATE_COLLECTION, -1))
		return gpk_info_enum_to_icon_name (PK_INFO_ENUM_COLLECTION_INSTALLED);

	if (state == pk_bitfield_from_enums (GPK_STATE_IN_LIST, GPK_STATE_INSTALLED, GPK_STATE_COLLECTION, -1))
		return gpk_info_enum_to_icon_name (PK_INFO_ENUM_REMOVING); // need new icon

	if (state == pk_bitfield_from_enums (GPK_STATE_IN_LIST, GPK_STATE_COLLECTION, -1))
		return gpk_info_enum_to_icon_name (PK_INFO_ENUM_INSTALLING); // need new icon

	return NULL;
}

static gboolean
gpk_application_state_get_checkbox (PkBitfield state)
{
	PkBitfield state_local;

	/* remove any we don't care about */
	state_local = state;
	pk_bitfield_remove (state_local, GPK_STATE_COLLECTION);

	/* installed or in array */
	if (state_local == pk_bitfield_value (GPK_STATE_INSTALLED) ||
	    state_local == pk_bitfield_value (GPK_STATE_IN_LIST))
		return TRUE;
	return FALSE;
}

static void
gpk_application_set_text_buffer (GtkWidget *widget, const gchar *text)
{
	GtkTextBuffer *buffer;
	buffer = gtk_text_buffer_new (NULL);
	/* ITS4: ignore, not used for allocation */
	if (_g_strzero (text) == FALSE) {
		gtk_text_buffer_set_text (buffer, text, -1);
	} else {
		/* no information */
		gtk_text_buffer_set_text (buffer, "", -1);
	}
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);
}

static void
gpk_application_allow_install (GpkApplicationPrivate *priv, gboolean allow)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
	gtk_widget_set_visible (widget, allow);
}

static void
gpk_application_allow_remove (GpkApplicationPrivate *priv, gboolean allow)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
	gtk_widget_set_visible (widget, allow);
}

static void
gpk_application_packages_checkbox_invert (GpkApplicationPrivate *priv)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	PkBitfield state;
	gboolean ret;
	g_autofree gchar *package_id = NULL;

	/* get the selection and add */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_packages"));
	selection = gtk_tree_view_get_selection (treeview);
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret) {
		g_warning ("no selection");
		return;
	}

	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_STATE, &state,
			    PACKAGES_COLUMN_ID, &package_id,
			    -1);

	/* do something with the value */
	pk_bitfield_invert (state, GPK_STATE_IN_LIST);

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    PACKAGES_COLUMN_STATE, state,
			    PACKAGES_COLUMN_CHECKBOX, gpk_application_state_get_checkbox (state),
			    PACKAGES_COLUMN_IMAGE, gpk_application_state_get_icon (state),
			    -1);
}

static gboolean
gpk_application_get_checkbox_enable (GpkApplicationPrivate *priv, PkBitfield state)
{
	gboolean enable_installed = TRUE;
	gboolean enable_available = TRUE;

	if (priv->action == GPK_ACTION_INSTALL)
		enable_installed = FALSE;
	else if (priv->action == GPK_ACTION_REMOVE)
		enable_available = FALSE;

	if (pk_bitfield_contain (state, GPK_STATE_INSTALLED))
		return enable_installed;
	return enable_available;
}

static gboolean
gpk_application_get_selected_package (GpkApplicationPrivate *priv, gchar **package_id, gchar **summary)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	gboolean ret;

	/* get the selection and add */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_packages"));
	selection = gtk_tree_view_get_selection (treeview);
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret) {
		g_warning ("no selection");
		return FALSE;
	}

	/* get data */
	if (summary == NULL) {
		gtk_tree_model_get (model, &iter,
				    PACKAGES_COLUMN_ID, package_id,
				    -1);
	} else {
		gtk_tree_model_get (model, &iter,
				    PACKAGES_COLUMN_ID, package_id,
				    PACKAGES_COLUMN_SUMMARY, summary,
				    -1);
	}
	return TRUE;
}

static void
gpk_application_group_add_selected (GpkApplicationPrivate *priv)
{
	gboolean ret;
	g_autofree gchar *id = NULL;
	GtkTreeIter iter;

	ret = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->groups_store), &iter);
	if (!ret)
		return;
	gtk_tree_model_get (GTK_TREE_MODEL (priv->groups_store), &iter,
			    GROUPS_COLUMN_ID, &id,
			    -1);
	if (g_strcmp0 (id, "selected") == 0)
		return;
	gtk_tree_store_insert (priv->groups_store, &iter, NULL, 0);
	gtk_tree_store_set (priv->groups_store, &iter,
			    /* TRANSLATORS: this is a menu group of packages in the queue */
			    GROUPS_COLUMN_NAME, _("Pending"),
			    GROUPS_COLUMN_SUMMARY, NULL,
			    GROUPS_COLUMN_ID, "selected",
			    GROUPS_COLUMN_ICON, "edit-find",
			    GROUPS_COLUMN_ACTIVE, TRUE,
			    -1);
}

static void
gpk_application_group_remove_selected (GpkApplicationPrivate *priv)
{
	gboolean ret;
	g_autofree gchar *id = NULL;
	GtkTreeIter iter;

	ret = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->groups_store), &iter);
	if (!ret)
		return;
	gtk_tree_model_get (GTK_TREE_MODEL (priv->groups_store), &iter,
			    GROUPS_COLUMN_ID, &id,
			    -1);
	if (g_strcmp0 (id, "selected") != 0)
		return;
	gtk_tree_store_remove (priv->groups_store, &iter);
}

static void
gpk_application_change_queue_status (GpkApplicationPrivate *priv)
{
	GtkWidget *widget;
	GtkTreeView *treeview;
	gboolean valid;
	GtkTreeIter iter;
	GtkTreeModel *model;
	PkBitfield state;
	gboolean enabled;

	/* show and hide the action widgets */
	if (pk_package_sack_get_size (priv->package_sack) > 0) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_apply"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_clear"));
		gtk_widget_show (widget);
		gpk_application_group_add_selected (priv);
	} else {
		priv->action = GPK_ACTION_NONE;
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_apply"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_clear"));
		gtk_widget_hide (widget);
		gpk_application_group_remove_selected (priv);
	}

	/* correct the enabled state */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_packages"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* for all current items, reset the state if in the array */
	while (valid) {
		g_autofree gchar *package_id = NULL;
		gtk_tree_model_get (model, &iter,
				    PACKAGES_COLUMN_STATE, &state,
				    PACKAGES_COLUMN_ID, &package_id,
				    -1);

		/* we never show the checkbox for the search helper */
		if (package_id == NULL) {
			enabled = FALSE;
		} else {
			enabled = gpk_application_get_checkbox_enable (priv, state);
		}

		/* set visible */
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, PACKAGES_COLUMN_CHECKBOX_VISIBLE, enabled, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

static gboolean
gpk_application_install (GpkApplicationPrivate *priv)
{
	gboolean ret;
	g_autofree gchar *package_id_selected = NULL;
	g_autofree gchar *summary_selected = NULL;
	g_autoptr(PkPackage) package = NULL;

	/* get selection */
	ret = gpk_application_get_selected_package (priv, &package_id_selected, &summary_selected);
	if (!ret) {
		g_warning ("no package selected");
		goto out;
	}

	/* changed mind, or wrong mode */
	if (priv->action == GPK_ACTION_REMOVE) {
		ret = pk_package_sack_remove_package_by_id (priv->package_sack, package_id_selected);
		if (ret) {
			g_debug ("removed %s from package array", package_id_selected);

			/* correct buttons */
			gpk_application_allow_install (priv, FALSE);
			gpk_application_allow_remove (priv, TRUE);
			gpk_application_packages_checkbox_invert (priv);
			ret = TRUE;
			goto out;
		}
		g_warning ("wrong mode and not in array");
		ret = FALSE;
		goto out;
	}

	/* already added */
	package = pk_package_sack_find_by_id (priv->package_sack, package_id_selected);
	if (package != NULL) {
		g_warning ("already added");
		goto out;
	}

	/* set mode */
	priv->action = GPK_ACTION_INSTALL;

	/* add to array */
	package = pk_package_new ();
	pk_package_set_id (package, package_id_selected, NULL);
	g_object_set (package,
		      "info", PK_INFO_ENUM_AVAILABLE,
		      "summary", summary_selected,
		      NULL);
	pk_package_sack_add_package (priv->package_sack, package);

	/* correct buttons */
	gpk_application_allow_install (priv, FALSE);
	gpk_application_allow_remove (priv, TRUE);
	gpk_application_packages_checkbox_invert (priv);
out:
	/* add the selected group if there are any packages in the queue */
	gpk_application_change_queue_status (priv);
	return ret;
}

static void
gpk_application_menu_homepage_cb (GtkAction *action, GpkApplicationPrivate *priv)
{
	gtk_show_uri_on_window (NULL, priv->homepage_url, GDK_CURRENT_TIME, NULL);
}

static gint
gpk_application_strcmp_indirect (gchar **a, gchar **b)
{
	return strcmp (*a, *b);
}

static void
gpk_application_get_files_cb (PkTask *task, GAsyncResult *res, GpkApplicationPrivate *priv)
{
	gboolean ret;
	g_auto(GStrv) files = NULL;
	g_autofree gchar *package_id_selected = NULL;
	g_auto(GStrv) split = NULL;
	g_autofree gchar *title = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GPtrArray) array_sort = NULL;
	GtkWidget *dialog;
	GtkWindow *window;
	g_autoptr(PkError) error_code = NULL;
	PkFiles *item;
	g_autoptr(PkResults) results = NULL;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		g_warning ("failed to get files: %s", error->message);
		return;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get files: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		return;
	}

	/* get data */
	array = pk_results_get_files_array (results);
	if (array->len != 1)
		return;

	/* assume only one option */
	item = g_ptr_array_index (array, 0);

	/* get selection */
	ret = gpk_application_get_selected_package (priv, &package_id_selected, NULL);
	if (!ret) {
		g_warning ("no package selected");
		return;
	}

	/* get data */
	g_object_get (item,
		      "files", &files,
		      NULL);

	/* convert to pointer array */
	array_sort = pk_strv_to_ptr_array (files);
	g_ptr_array_sort (array_sort, (GCompareFunc) gpk_application_strcmp_indirect);

	/* title */
	split = pk_package_id_split (package_id_selected);
	/* TRANSLATORS: title: how many files are installed by the application */
	title = g_strdup_printf (ngettext ("%u file installed by %s",
					   "%u files installed by %s",
					   array_sort->len), array_sort->len, split[PK_PACKAGE_ID_NAME]);

	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
	dialog = gtk_message_dialog_new (window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
	gpk_dialog_embed_file_list_widget (GTK_DIALOG (dialog), array_sort);
	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 250);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static gboolean
gpk_application_status_changed_timeout_cb (GpkApplicationPrivate *priv)
{
	const gchar *text;
	GtkWidget *widget;

	/* set the text and show */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "headerbar"));
	text = gpk_status_enum_to_localised_text (priv->status_last);
	gtk_header_bar_set_subtitle (GTK_HEADER_BAR(widget), text);

	/* show cancel button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_cancel"));
	gtk_widget_show (widget);

	/* show progressbar */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_progress"));
	gtk_widget_show (widget);

	/* never repeat */
	priv->status_id = 0;
	return FALSE;
}

static void
gpk_application_progress_cb (PkProgress *progress, PkProgressType type, GpkApplicationPrivate *priv)
{
	PkStatusEnum status;
	gint percentage;
	gboolean allow_cancel;
	GtkWidget *widget;

	g_object_get (progress,
		      "status", &status,
		      "percentage", &percentage,
		      "allow-cancel", &allow_cancel,
		      NULL);

	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_debug ("now %s", pk_status_enum_to_string (status));

		if (status == PK_STATUS_ENUM_FINISHED) {
			/* re-enable UI */
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_packages"));
			gtk_widget_set_sensitive (widget, TRUE);

			/* hide the cancel button */
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_cancel"));
			gtk_widget_hide (widget);

			/* make apply button sensitive */
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_apply"));
			gtk_widget_set_sensitive (widget, TRUE);

			/* we've not yet shown, so don't bother */
			if (priv->status_id > 0) {
				g_source_remove (priv->status_id);
				priv->status_id = 0;
			}

			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "headerbar"));
			gtk_header_bar_set_subtitle (GTK_HEADER_BAR(widget), NULL);
			widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_progress"));
			gtk_widget_hide (widget);
			return;
		}

		/* already pending show */
		if (priv->status_id > 0)
			return;

		/* only show after some time in the transaction */
		priv->status_id =
			g_timeout_add (GPK_UI_STATUS_SHOW_DELAY,
				       (GSourceFunc) gpk_application_status_changed_timeout_cb,
				       priv);
		g_source_set_name_by_id (priv->status_id,
					 "[GpkApplication] status-changed");

		/* save for the callback */
		priv->status_last = status;

	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "progressbar_progress"));
		if (percentage > 0) {
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0f);
		} else {
			gtk_widget_hide (widget);
		}

	} else if (type == PK_PROGRESS_TYPE_ALLOW_CANCEL) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_cancel"));
		gtk_widget_set_sensitive (widget, allow_cancel);
	}
}

static void
gpk_application_menu_files_cb (GtkAction *action, GpkApplicationPrivate *priv)
{
	gboolean ret;
	g_auto(GStrv) package_ids = NULL;
	g_autofree gchar *package_id_selected = NULL;

	/* get selection */
	ret = gpk_application_get_selected_package (priv, &package_id_selected, NULL);
	if (!ret) {
		g_warning ("no package selected");
		return;
	}

	/* ensure new action succeeds */
	g_cancellable_reset (priv->cancellable);

	/* set correct view */
	package_ids = pk_package_ids_from_id (package_id_selected);
	pk_task_get_files_async (PK_TASK (priv->task), package_ids, priv->cancellable,
				   (PkProgressCallback) gpk_application_progress_cb, priv,
				   (GAsyncReadyCallback) gpk_application_get_files_cb, priv);
}

static gboolean
gpk_application_remove (GpkApplicationPrivate *priv)
{
	gboolean ret;
	g_autofree gchar *package_id_selected = NULL;
	g_autofree gchar *summary_selected = NULL;
	g_autoptr(PkPackage) package = NULL;

	/* get selection */
	ret = gpk_application_get_selected_package (priv, &package_id_selected, &summary_selected);
	if (!ret) {
		g_warning ("no package selected");
		goto out;
	}

	/* changed mind, or wrong mode */
	if (priv->action == GPK_ACTION_INSTALL) {
		ret = pk_package_sack_remove_package_by_id (priv->package_sack, package_id_selected);
		if (ret) {
			g_debug ("removed %s from package array", package_id_selected);

			/* correct buttons */
			gpk_application_allow_install (priv, TRUE);
			gpk_application_allow_remove (priv, FALSE);
			gpk_application_packages_checkbox_invert (priv);
			ret = TRUE;
			goto out;
		}
		g_warning ("wrong mode and not in array");
		ret = FALSE;
		goto out;
	}

	/* already added */
	ret = (pk_package_sack_find_by_id (priv->package_sack, package_id_selected) == NULL);
	if (!ret) {
		g_warning ("already added");
		goto out;
	}

	priv->action = GPK_ACTION_REMOVE;
	package = pk_package_new ();
	pk_package_set_id (package, package_id_selected, NULL);
	g_object_set (package,
		      "info", PK_INFO_ENUM_INSTALLED,
		      "summary", summary_selected,
		      NULL);
	pk_package_sack_add_package (priv->package_sack, package);

	/* correct buttons */
	gpk_application_allow_install (priv, TRUE);
	gpk_application_allow_remove (priv, FALSE);
	gpk_application_packages_checkbox_invert (priv);
out:
	/* add the selected group if there are any packages in the queue */
	gpk_application_change_queue_status (priv);
	return TRUE;
}

static void
gpk_application_menu_install_cb (GtkAction *action, GpkApplicationPrivate *priv)
{
	gpk_application_install (priv);
}

static void
gpk_application_menu_remove_cb (GtkAction *action, GpkApplicationPrivate *priv)
{
	gpk_application_remove (priv);
}

static void
gpk_application_get_requires_cb (PkTask *task, GAsyncResult *res, GpkApplicationPrivate *priv)
{
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(GPtrArray) array = NULL;
	GtkWindow *window;
	g_autofree gchar *name = NULL;
	g_autofree gchar *title = NULL;
	g_autofree gchar *message = NULL;
	g_auto(GStrv) package_ids = NULL;
	GtkWidget *dialog;
	g_autofree gchar *package_id_selected = NULL;
	gboolean ret;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		g_warning ("failed to get requires: %s", error->message);
		return;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get requires: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		return;
	}

	/* get selection */
	ret = gpk_application_get_selected_package (priv, &package_id_selected, NULL);
	if (!ret) {
		g_warning ("no package selected");
		return;
	}

	/* get data */
	array = pk_results_get_package_array (results);

	/* empty array */
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
	if (array->len == 0) {
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: no packages returned */
					_("No packages"),
					/* TRANSLATORS: this package is not required by any others */
					_("No other packages require this package"), NULL);
		return;
	}

	package_ids = pk_package_ids_from_id (package_id_selected);
	name = gpk_dialog_package_id_name_join_locale (package_ids);
	/* TRANSLATORS: title: how many packages require this package */
	title = g_strdup_printf (ngettext ("%u package requires %s",
					   "%u packages require %s",
					   array->len), array->len, name);

	/* TRANSLATORS: show a array of packages for the package */
	message = g_strdup_printf (ngettext ("Packages listed below require %s to function correctly.",
					     "Packages listed below require %s to function correctly.",
					     array->len), name);

	dialog = gtk_message_dialog_new (window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gpk_dialog_embed_package_list_widget (GTK_DIALOG (dialog), array);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
gpk_application_menu_requires_cb (GtkAction *action, GpkApplicationPrivate *priv)
{
	gboolean ret;
	g_auto(GStrv) package_ids = NULL;
	g_autofree gchar *package_id_selected = NULL;

	/* get selection */
	ret = gpk_application_get_selected_package (priv, &package_id_selected, NULL);
	if (!ret) {
		g_warning ("no package selected");
		return;
	}

	/* ensure new action succeeds */
	g_cancellable_reset (priv->cancellable);

	/* get the requires */
	package_ids = pk_package_ids_from_id (package_id_selected);

	pk_task_depends_on_async (PK_TASK (priv->task),
				    pk_bitfield_value (PK_FILTER_ENUM_NONE),
				    package_ids, TRUE, priv->cancellable,
				    (PkProgressCallback) gpk_application_progress_cb, priv,
				    (GAsyncReadyCallback) gpk_application_get_depends_cb, priv);
}

static void
gpk_application_get_depends_cb (PkTask *task, GAsyncResult *res, GpkApplicationPrivate *priv)
{
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(GPtrArray) array = NULL;
	GtkWindow *window;
	g_autofree gchar *name = NULL;
	g_autofree gchar *title = NULL;
	g_autofree gchar *message = NULL;
	g_auto(GStrv) package_ids = NULL;
	GtkWidget *dialog;
	g_autofree gchar *package_id_selected = NULL;
	gboolean ret;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		g_warning ("failed to get depends: %s", error->message);
		return;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get depends: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		return;
	}

	/* get data */
	array = pk_results_get_package_array (results);

	/* get selection */
	ret = gpk_application_get_selected_package (priv, &package_id_selected, NULL);
	if (!ret) {
		g_warning ("no package selected");
		return;
	}

	/* empty array */
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
	if (array->len == 0) {
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: no packages returned */
					_("No packages"),
					/* TRANSLATORS: this package does not depend on any others */
					_("This package does not depend on any others"), NULL);
		return;
	}

	package_ids = pk_package_ids_from_id (package_id_selected);
	name = gpk_dialog_package_id_name_join_locale (package_ids);
	/* TRANSLATORS: title: show the number of other packages we depend on */
	title = g_strdup_printf (ngettext ("%u additional package is required for %s",
					   "%u additional packages are required for %s",
					   array->len), array->len, name);

	/* TRANSLATORS: message: show the array of dependent packages for this package */
	message = g_strdup_printf (ngettext ("Packages listed below are required for %s to function correctly.",
					     "Packages listed below are required for %s to function correctly.",
					     array->len), name);

	dialog = gtk_message_dialog_new (window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gpk_dialog_embed_package_list_widget (GTK_DIALOG (dialog), array);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
gpk_application_menu_depends_cb (GtkAction *_action, GpkApplicationPrivate *priv)
{
	gboolean ret;
	g_auto(GStrv) package_ids = NULL;
	g_autofree gchar *package_id_selected = NULL;

	/* get selection */
	ret = gpk_application_get_selected_package (priv, &package_id_selected, NULL);
	if (!ret) {
		g_warning ("no package selected");
		return;
	}

	/* ensure new action succeeds */
	g_cancellable_reset (priv->cancellable);

	/* get the depends */
	package_ids = pk_package_ids_from_id (package_id_selected);

	pk_task_required_by_async (PK_TASK (priv->task),
				     pk_bitfield_value (PK_FILTER_ENUM_NONE),
				     package_ids, TRUE, priv->cancellable,
				     (PkProgressCallback) gpk_application_progress_cb, priv,
				     (GAsyncReadyCallback) gpk_application_get_requires_cb, priv);
}

static const gchar *
gpk_application_get_full_repo_name (GpkApplicationPrivate *priv, const gchar *data)
{
	const gchar *repo_name;

	/* if no data, we can't look up in the hash table */
	if (_g_strzero (data)) {
		g_warning ("no ident data");
		/* TRANSLATORS: the repo name is invalid or not found, fall back to this */
		return _("Invalid");
	}

	/* trim prefix */
	if (g_str_has_prefix (data, "installed:"))
		data += 10;

	/* try to find in cached repo array */
	repo_name = (const gchar *) g_hash_table_lookup (priv->repos, data);
	if (repo_name == NULL) {
		g_warning ("no repo name, falling back to %s", data);
		return data;
	}
	return repo_name;
}

static gboolean
gpk_application_clear_details_cb (GpkApplicationPrivate *priv)
{
	GtkWidget *widget;

	/* hide dead widgets */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_details"));
	gtk_widget_hide (widget);

	/* never repeat */
	priv->details_event_id = 0;
	return FALSE;
}

static void
gpk_application_clear_details (GpkApplicationPrivate *priv)
{
	/* only clear the last data if it takes a little while, else we flicker the display */
	if (priv->details_event_id > 0)
		g_source_remove (priv->details_event_id);
	priv->details_event_id =
		g_timeout_add (200, (GSourceFunc) gpk_application_clear_details_cb, priv);
	g_source_set_name_by_id (priv->details_event_id,
				 "[GpkApplication] clear-details");
}

static void
gpk_application_clear_packages (GpkApplicationPrivate *priv)
{
	/* clear existing array */
	priv->has_package = FALSE;
	gtk_list_store_clear (priv->packages_store);
}

static void
gpk_application_add_item_to_results (GpkApplicationPrivate *priv, PkPackage *item)
{
	GtkTreeIter iter;
	g_autofree gchar *text = NULL;
	gboolean in_queue;
	gboolean installed;
	gboolean enabled;
	PkBitfield state = 0;
	static guint package_cnt = 0;
	PkInfoEnum info;
	g_autofree gchar *package_id = NULL;
	g_autofree gchar *summary = NULL;
	GtkWidget *widget;

	/* get data */
	g_object_get (item,
		      "info", &info,
		      "package-id", &package_id,
		      "summary", &summary,
		      NULL);

	/* mark as got so we don't warn */
	priv->has_package = TRUE;

	/* are we in the package array? */
	in_queue = (pk_package_sack_find_by_id (priv->package_sack, package_id) != NULL);
	installed = (info == PK_INFO_ENUM_INSTALLED) || (info == PK_INFO_ENUM_COLLECTION_INSTALLED);

	if (installed)
		pk_bitfield_add (state, GPK_STATE_INSTALLED);
	if (in_queue)
		pk_bitfield_add (state, GPK_STATE_IN_LIST);

	/* special icon */
	if (info == PK_INFO_ENUM_COLLECTION_INSTALLED || info == PK_INFO_ENUM_COLLECTION_AVAILABLE)
		pk_bitfield_add (state, GPK_STATE_COLLECTION);

	/* use two lines */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_manager"));
	text = gpk_package_id_format_twoline (gtk_widget_get_style_context (widget),
					      package_id,
					      summary);

	/* can we modify this? */
	enabled = gpk_application_get_checkbox_enable (priv, state);

	gtk_list_store_append (priv->packages_store, &iter);
	gtk_list_store_set (priv->packages_store, &iter,
			    PACKAGES_COLUMN_STATE, state,
			    PACKAGES_COLUMN_CHECKBOX, gpk_application_state_get_checkbox (state),
			    PACKAGES_COLUMN_CHECKBOX_VISIBLE, enabled,
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_SUMMARY, summary,
			    PACKAGES_COLUMN_ID, package_id,
			    PACKAGES_COLUMN_IMAGE, gpk_application_state_get_icon (state),
			    -1);

	/* only process every n events else we re-order too many times */
	if (package_cnt++ % 200 == 0) {
		while (gtk_events_pending ())
			gtk_main_iteration ();
	}
}

static void
gpk_application_suggest_better_search (GpkApplicationPrivate *priv)
{
	const gchar *message = NULL;
	/* TRANSLATORS: no results were found for this search */
	const gchar *title = _("No results were found.");
	GtkTreeIter iter;
	g_autofree gchar *text = NULL;
	PkBitfield state = 0;

	if (priv->search_mode == GPK_MODE_GROUP ||
	    priv->search_mode == GPK_MODE_ALL_PACKAGES) {
		/* TRANSLATORS: be helpful, but this shouldn't happen */
		message = _("Try entering a package name in the search bar.");
	}  else if (priv->search_mode == GPK_MODE_SELECTED) {
		/* TRANSLATORS: nothing in the package queue */
		message = _("There are no packages queued to be installed or removed.");
	} else {
		if (priv->search_type == GPK_SEARCH_NAME ||
		    priv->search_type == GPK_SEARCH_FILE)
			/* TRANSLATORS: tell the user to switch to details search mode */
			message = _("Try searching package descriptions by clicking the icon next to the search text.");
		else
			/* TRANSLATORS: tell the user to try harder */
			message = _("Try again with a different search term.");
	}

	text = g_strdup_printf ("%s\n%s", title, message);
	gtk_list_store_append (priv->packages_store, &iter);
	gtk_list_store_set (priv->packages_store, &iter,
			    PACKAGES_COLUMN_STATE, state,
			    PACKAGES_COLUMN_CHECKBOX, FALSE,
			    PACKAGES_COLUMN_CHECKBOX_VISIBLE, FALSE,
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_IMAGE, "system-search",
			    PACKAGES_COLUMN_ID, NULL,
			    -1);
}

static gboolean
gpk_application_perform_search_idle_cb (GpkApplicationPrivate *priv)
{
	gpk_application_perform_search (priv);
	return FALSE;
}

/**
 * gpk_application_select_exact_match:
 *
 * NOTE: we have to do this in the finished_cb, as if we do this as we return
 * results we cancel the search and start getting the package details.
 **/
static void
gpk_application_select_exact_match (GpkApplicationPrivate *priv, const gchar *text)
{
	GtkTreeView *treeview;
	gboolean valid;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeSelection *selection = NULL;

	/* get the first iter in the array */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_packages"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* for all items in treeview */
	while (valid) {
		g_autofree gchar *package_id = NULL;
		gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_ID, &package_id, -1);
		if (package_id != NULL) {
			g_auto(GStrv) split = NULL;
			/* exact match, so select and scroll */
			split = pk_package_id_split (package_id);
			if (g_strcmp0 (split[PK_PACKAGE_ID_NAME], text) == 0) {
				selection = gtk_tree_view_get_selection (treeview);
				gtk_tree_selection_select_iter (selection, &iter);
				path = gtk_tree_model_get_path (model, &iter);
				gtk_tree_view_scroll_to_cell (treeview, path, NULL, FALSE, 0.5f, 0.5f);
				gtk_tree_path_free (path);
			}

			/* no point continuing for a second match */
			if (selection != NULL)
				break;
		}
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

static void
gpk_application_cancel_cb (GtkWidget *button_widget, GpkApplicationPrivate *priv)
{
	g_cancellable_cancel (priv->cancellable);

	/* switch buttons around */
	priv->search_mode = GPK_MODE_UNKNOWN;
}

static void
gpk_application_set_button_find_sensitivity (GpkApplicationPrivate *priv)
{
	GtkWidget *widget;

	/* only sensitive if not in the middle of a search */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_text"));
	gtk_widget_set_sensitive (widget, !priv->search_in_progress);
}

static void
gpk_application_search_cb (PkTask *task, GAsyncResult *res, GpkApplicationPrivate *priv)
{
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(GPtrArray) array = NULL;
	PkPackage *item;
	guint i;
	GtkWidget *widget;
	GtkWindow *window;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		g_warning ("failed to search: %s", error->message);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to search: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		goto out;
	}

	/* get data */
	array = pk_results_get_package_array (results);
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		gpk_application_add_item_to_results (priv, item);
	}

	/* were there no entries found? */
	if (!priv->has_package)
		gpk_application_suggest_better_search (priv);

	/* if there is an exact match, select it */
	gpk_application_select_exact_match (priv, priv->search_text);

	/* focus back to the text extry */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_text"));
	gtk_widget_grab_focus (widget);

	/* reset UI */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_groups"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "textview_description"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_text"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_apply"));
	gtk_widget_set_sensitive (widget, TRUE);
out:
	/* mark find button sensitive */
	priv->search_in_progress = FALSE;
	gpk_application_set_button_find_sensitivity (priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_groups"));
	gtk_widget_set_sensitive (widget, TRUE);
}

static void
gpk_application_perform_search_name_details_file (GpkApplicationPrivate *priv)
{
	GtkEntry *entry;
	GtkWindow *window;
	g_autoptr(GError) error = NULL;
	gboolean ret;
	g_auto(GStrv) searches = NULL;

	entry = GTK_ENTRY (gtk_builder_get_object (priv->builder, "entry_text"));
	g_free (priv->search_text);
	priv->search_text = g_strdup (gtk_entry_get_text (entry));

	/* have we got input? */
	if (_g_strzero (priv->search_text)) {
		g_debug ("no input");
		return;
	}

	ret = !_g_strzero (priv->search_text);
	if (!ret) {
		g_debug ("invalid input text, will fail");
		/* TODO - make the dialog turn red... */
		window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: title: invalid text in the search bar */
					_("Invalid search text"),
					/* TRANSLATORS: message: tell the user that's not allowed */
					_("The search text contains invalid characters"), NULL);
		return;
	}
	g_debug ("find %s", priv->search_text);

	/* mark find button insensitive */
	priv->search_in_progress = TRUE;
	gpk_application_set_button_find_sensitivity (priv);

	/* ensure new action succeeds */
	g_cancellable_reset (priv->cancellable);

	/* do the search */
	searches = g_strsplit (priv->search_text, " ", -1);
	if (priv->search_type == GPK_SEARCH_NAME) {
		pk_task_search_names_async (priv->task,
					     priv->filters_current,
					     searches, priv->cancellable,
					     (PkProgressCallback) gpk_application_progress_cb, priv,
					     (GAsyncReadyCallback) gpk_application_search_cb, priv);
	} else if (priv->search_type == GPK_SEARCH_DETAILS) {
		pk_task_search_details_async (priv->task,
					     priv->filters_current,
					     searches, priv->cancellable,
					     (PkProgressCallback) gpk_application_progress_cb, priv,
					     (GAsyncReadyCallback) gpk_application_search_cb, priv);
	} else if (priv->search_type == GPK_SEARCH_FILE) {
		pk_task_search_files_async (priv->task,
					     priv->filters_current,
					     searches, priv->cancellable,
					     (PkProgressCallback) gpk_application_progress_cb, priv,
					     (GAsyncReadyCallback) gpk_application_search_cb, priv);
	} else {
		g_warning ("invalid search type");
		return;
	}

	if (!ret) {
		window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: title: we failed to execute the method */
					_("The search could not be completed"),
					/* TRANSLATORS: low level failure, details to follow */
					_("Running the transaction failed"), error->message);
		return;
	}
}

static void
gpk_application_perform_search_others (GpkApplicationPrivate *priv)
{
	/* ensure new action succeeds */
	g_cancellable_reset (priv->cancellable);

	priv->search_in_progress = TRUE;

	if (priv->search_mode == GPK_MODE_GROUP) {
		g_auto(GStrv) search_groups = NULL;
		search_groups = g_strsplit (priv->search_group, " ", -1);
		pk_task_search_groups_async (PK_TASK(priv->task),
					       priv->filters_current, search_groups, priv->cancellable,
					       (PkProgressCallback) gpk_application_progress_cb, priv,
					       (GAsyncReadyCallback) gpk_application_search_cb, priv);
	} else {
		pk_task_get_packages_async (PK_TASK(priv->task),
					      priv->filters_current, priv->cancellable,
					      (PkProgressCallback) gpk_application_progress_cb, priv,
					      (GAsyncReadyCallback) gpk_application_search_cb, priv);
	}
}

static gboolean
gpk_application_populate_selected (GpkApplicationPrivate *priv)
{
	guint i;
	PkPackage *package;
	g_autoptr(GPtrArray) array = NULL;

	/* get size */
	array = pk_package_sack_get_array (priv->package_sack);

	/* nothing in queue */
	if (array->len == 0) {
		gpk_application_suggest_better_search (priv);
		return TRUE;
	}

	/* dump queue to package window */
	for (i = 0; i < array->len; i++) {
		package = g_ptr_array_index (array, i);
		gpk_application_add_item_to_results (priv, package);
	}
	return TRUE;
}

static void
gpk_application_perform_search (GpkApplicationPrivate *priv)
{
	/*if we are in the middle of a search, just return*/
	if (priv->search_in_progress == TRUE)
		return;

	/* just shown the welcome screen */
	if (priv->search_mode == GPK_MODE_UNKNOWN)
		return;

	g_debug ("CLEAR search");
	gpk_application_clear_details (priv);
	gpk_application_clear_packages (priv);

	if (priv->search_mode == GPK_MODE_NAME_DETAILS_FILE) {
		gpk_application_perform_search_name_details_file (priv);
	} else if (priv->search_mode == GPK_MODE_GROUP ||
		   priv->search_mode == GPK_MODE_ALL_PACKAGES) {
		gpk_application_perform_search_others (priv);
	} else if (priv->search_mode == GPK_MODE_SELECTED) {
		gpk_application_populate_selected (priv);
	} else {
		g_debug ("doing nothing");
	}
}

static void
gpk_application_find_cb (GtkWidget *button_widget, GpkApplicationPrivate *priv)
{
	priv->search_mode = GPK_MODE_NAME_DETAILS_FILE;
	gpk_application_perform_search (priv);
}

static gboolean
gpk_application_quit (GpkApplicationPrivate *priv)
{
	g_autoptr(GPtrArray) array = NULL;
	gint len;
	GtkResponseType result;
	GtkWindow *window;
	GtkWidget *dialog;

	/* do we have any items queued for removal or installation? */
	array = pk_package_sack_get_array (priv->package_sack);
	len = array->len;

	if (len != 0) {
		window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
		dialog = gtk_message_dialog_new (window, GTK_DIALOG_MODAL,
						 GTK_MESSAGE_WARNING, GTK_BUTTONS_CANCEL,
						 /* TRANSLATORS: title: warn the user they are quitting with unapplied changes */
						 "%s", _("Changes not applied"));
		gtk_dialog_add_button (GTK_DIALOG (dialog), _("Close _Anyway"), GTK_RESPONSE_OK);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog),
							  "%s\n%s",
							  /* TRANSLATORS: tell the user the problem */
							  _("You have made changes that have not yet been applied."),
							  _("These changes will be lost if you close this window."));
		gtk_window_set_icon_name (GTK_WINDOW (dialog), GPK_ICON_SOFTWARE_INSTALLER);
		result = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		/* did not agree */
		if (result != GTK_RESPONSE_OK)
			return FALSE;
	}

	/* we might have visual stuff running, close them down */
	g_cancellable_cancel (priv->cancellable);
	g_application_release (G_APPLICATION (priv->application));
	return TRUE;
}

static gboolean
gpk_application_text_changed_cb (GtkEntry *entry, GpkApplicationPrivate *priv)
{
	GtkTreeView *treeview;
	GtkTreeSelection *selection;

	/* clear group selection if we have the tab */
	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_SEARCH_GROUP) &&
	    gtk_entry_get_text_length (entry) > 0) {
		treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_groups"));
		selection = gtk_tree_view_get_selection (treeview);
		gtk_tree_selection_unselect_all (selection);
	}

	/* mark find button sensitive */
	gpk_application_set_button_find_sensitivity (priv);
	return FALSE;
}

static void
gpk_application_packages_installed_clicked_cb (GtkCellRendererToggle *cell, gchar *path_str, GpkApplicationPrivate *priv)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeSelection *selection;
	PkBitfield state;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_packages"));
	model = gtk_tree_view_get_model (treeview);
	path = gtk_tree_path_new_from_string (path_str);

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_STATE, &state,
			    -1);

	/* enforce the selection in case we just fire at the checkbox without selecting */
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_select_iter (selection, &iter);

	if (gpk_application_state_get_checkbox (state)) {
		gpk_application_remove (priv);
	} else {
		gpk_application_install (priv);
	}
	gtk_tree_path_free (path);
}

static void gpk_application_packages_treeview_clicked_cb (GtkTreeSelection *selection, GpkApplicationPrivate *priv);

static void
gpk_application_button_clear_cb (GtkWidget *widget_button, GpkApplicationPrivate *priv)
{
	GtkTreeView *treeview;
	gboolean valid;
	gboolean checkbox;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	const gchar *icon;
	PkBitfield state;
	gboolean ret;

	/* get the first iter in the array */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_packages"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* for all current items, reset the state if in the array */
	while (valid) {
		gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_STATE, &state, -1);
		ret = pk_bitfield_contain (state, GPK_STATE_IN_LIST);
		if (ret) {
			pk_bitfield_remove (state, GPK_STATE_IN_LIST);
			/* get the new icon */
			icon = gpk_application_state_get_icon (state);
			checkbox = gpk_application_state_get_checkbox (state);

			/* set new value */
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					    PACKAGES_COLUMN_STATE, state,
					    PACKAGES_COLUMN_CHECKBOX, checkbox,
					    PACKAGES_COLUMN_IMAGE, icon,
					    -1);
		}
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* clear queue */
	pk_package_sack_clear (priv->package_sack);

	/* force a button refresh */
	selection = gtk_tree_view_get_selection (treeview);
	gpk_application_packages_treeview_clicked_cb (selection, priv);

	priv->action = GPK_ACTION_NONE;
	gpk_application_change_queue_status (priv);
}

static void
gpk_application_install_packages_cb (PkTask *task, GAsyncResult *res, GpkApplicationPrivate *priv)
{
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkError) error_code = NULL;
	GtkWindow *window;
	guint idle_id;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		g_warning ("failed to install packages: %s", error->message);
		return;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to install packages: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		return;
	}

	/* idle add in the background */
	idle_id = g_idle_add ((GSourceFunc) gpk_application_perform_search_idle_cb, priv);
	g_source_set_name_by_id (idle_id, "[GpkApplication] search");

	/* clear if success */
	pk_package_sack_clear (priv->package_sack);
	priv->action = GPK_ACTION_NONE;
	gpk_application_change_queue_status (priv);
}

static void
gpk_application_remove_packages_cb (PkTask *task, GAsyncResult *res, GpkApplicationPrivate *priv)
{
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkError) error_code = NULL;
	GtkWindow *window;
	guint idle_id;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		g_warning ("failed to remove packages: %s", error->message);
		return;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to remove packages: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		return;
	}

	/* idle add in the background */
	idle_id = g_idle_add ((GSourceFunc) gpk_application_perform_search_idle_cb, priv);
	g_source_set_name_by_id (idle_id, "[GpkApplication] search");

	/* clear if success */
	pk_package_sack_clear (priv->package_sack);
	priv->action = GPK_ACTION_NONE;
	gpk_application_change_queue_status (priv);
}

static void
gpk_application_button_apply_cb (GtkWidget *widget, GpkApplicationPrivate *priv)
{
	g_auto(GStrv) package_ids = NULL;
	gboolean autoremove;

	/* ensure new action succeeds */
	g_cancellable_reset (priv->cancellable);

	package_ids = pk_package_sack_get_ids (priv->package_sack);
	if (priv->action == GPK_ACTION_INSTALL) {
		/* install */
		pk_task_install_packages_async (priv->task, package_ids, priv->cancellable,
						(PkProgressCallback) gpk_application_progress_cb, priv,
						(GAsyncReadyCallback) gpk_application_install_packages_cb, priv);

		/* make package array insensitive */
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_packages"));
		gtk_widget_set_sensitive (widget, FALSE);

		/* make apply button insensitive */
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_apply"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_clear"));
		gtk_widget_set_visible (widget, FALSE);

	} else if (priv->action == GPK_ACTION_REMOVE) {
		autoremove = g_settings_get_boolean (priv->settings, GPK_SETTINGS_ENABLE_AUTOREMOVE);

		/* remove */
		pk_task_remove_packages_async (priv->task, package_ids, TRUE, autoremove, priv->cancellable,
					       (PkProgressCallback) gpk_application_progress_cb, priv,
					       (GAsyncReadyCallback) gpk_application_remove_packages_cb, priv);

		/* make package array insensitive */
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_packages"));
		gtk_widget_set_sensitive (widget, FALSE);

		/* make apply button insensitive */
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_apply"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_clear"));
		gtk_widget_set_visible (widget, FALSE);
	}
}

static void
gpk_application_packages_add_columns (GpkApplicationPrivate *priv)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_packages"));

	/* column for installed toggles */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (gpk_application_packages_installed_clicked_cb), priv);

	/* TRANSLATORS: column for installed status */
	column = gtk_tree_view_column_new_with_attributes (_("Installed"), renderer,
							   "active", PACKAGES_COLUMN_CHECKBOX,
							   "visible", PACKAGES_COLUMN_CHECKBOX_VISIBLE, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for images */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_DIALOG, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "icon-name", PACKAGES_COLUMN_IMAGE);
	gtk_tree_view_append_column (treeview, column);

	/* column for name */
	renderer = gtk_cell_renderer_text_new ();
	/* TRANSLATORS: column for package name */
	column = gtk_tree_view_column_new_with_attributes (_("Name"), renderer,
							   "markup", PACKAGES_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
}

static void
gpk_application_groups_add_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_LARGE_TOOLBAR, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "icon-name", GROUPS_COLUMN_ICON);
	gtk_tree_view_append_column (treeview, column);

	/* column for name */
	renderer = gtk_cell_renderer_text_new ();
	/* TRANSLATORS: column for group name */
	column = gtk_tree_view_column_new_with_attributes (_("Name"), renderer,
							   "text", GROUPS_COLUMN_NAME,
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, GROUPS_COLUMN_NAME);
	gtk_tree_view_append_column (treeview, column);

}

static void
gpk_application_groups_treeview_changed_cb (GtkTreeSelection *selection, GpkApplicationPrivate *priv)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkEntry *entry;
	GtkTreePath *path;
	gboolean active;

	/* hide details */
	g_debug ("CLEAR tv changed");
	gpk_application_clear_details (priv);
	gpk_application_clear_packages (priv);

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		/* clear the search text if we clicked the group array */
		entry = GTK_ENTRY (gtk_builder_get_object (priv->builder, "entry_text"));
		gtk_entry_set_text (entry, "");

		g_free (priv->search_group);
		gtk_tree_model_get (model, &iter,
				    GROUPS_COLUMN_ID, &priv->search_group,
				    GROUPS_COLUMN_ACTIVE, &active, -1);
		g_debug ("selected row is: %s (%i)", priv->search_group, active);

		/* don't search parent groups */
		if (!active) {
			path = gtk_tree_model_get_path (model, &iter);

			/* select the parent group */
			gtk_tree_selection_select_path (selection, path);
			gtk_tree_path_free (path);
			return;
		}

		/* GetPackages? */
		if (g_strcmp0 (priv->search_group, "all-packages") == 0)
			priv->search_mode = GPK_MODE_ALL_PACKAGES;
		else if (g_strcmp0 (priv->search_group, "selected") == 0)
			priv->search_mode = GPK_MODE_SELECTED;
		else
			priv->search_mode = GPK_MODE_GROUP;

		/* actually do the search */
		gpk_application_perform_search (priv);
	}
}

static void
gpk_application_get_details_cb (PkTask *task, GAsyncResult *res, GpkApplicationPrivate *priv)
{
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(GPtrArray) array = NULL;
	PkDetails *item;
	GtkWidget *widget;
	gchar *value;
	const gchar *repo_name;
	gboolean installed;
	g_auto(GStrv) split = NULL;
	GtkWindow *window;
	g_autofree gchar *package_id = NULL;
	g_autofree gchar *url = NULL;
	PkGroupEnum group;
	g_autofree gchar *license = NULL;
	g_autofree gchar *description = NULL;
	guint64 size;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		g_warning ("failed to get list of categories: %s", error->message);
		return;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get details: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		return;
	}

	/* get data */
	array = pk_results_get_details_array (results);
	if (array->len != 1) {
		g_warning ("not one entry %u", array->len);
		return;
	}

	/* only choose the first item */
	item = g_ptr_array_index (array, 0);

	/* show to start */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "grid_details"));
	gtk_widget_show (widget);

	/* get data */
	g_object_get (item,
		      "package-id", &package_id,
		      "url", &url,
		      "group", &group,
		      "license", &license,
		      "description", &description,
		      "size", &size,
		      NULL);

	split = pk_package_id_split (package_id);
	installed = g_str_has_prefix (split[PK_PACKAGE_ID_DATA], "installed");

	/* homepage */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_homepage"));
	g_free (priv->homepage_url);
	priv->homepage_url = g_strdup (url);
	gtk_widget_set_visible (widget, url != NULL);

	/* licence */
	if (license != NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_licence_title"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_licence"));
		gtk_label_set_label (GTK_LABEL (widget), license);
		gtk_label_set_line_wrap (GTK_LABEL (widget), TRUE);
		gtk_widget_show (widget);
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_licence_title"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_licence"));
		gtk_widget_hide (widget);
	}

	/* set the description */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "textview_description"));
	gpk_application_set_text_buffer (widget, description);

	/* if non-zero, set the size */
	if (size > 0) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_size_title"));
		/* set the size */
		if (g_strcmp0 (split[PK_PACKAGE_ID_DATA], "meta") == 0) {
			/* TRANSLATORS: the size of the meta package */
			gtk_label_set_label (GTK_LABEL (widget), _("Size"));
		} else if (installed) {
			/* TRANSLATORS: the installed size in bytes of the package */
			gtk_label_set_label (GTK_LABEL (widget), _("Installed size"));
		} else {
			/* TRANSLATORS: the download size of the package */
			gtk_label_set_label (GTK_LABEL (widget), _("Download size"));
		}
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_size"));
		value = g_format_size (size);
		gtk_label_set_label (GTK_LABEL (widget), value);
		gtk_widget_show (widget);
		g_free (value);
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_size_title"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_size"));
		gtk_widget_hide (widget);
	}

	/* set the repo text */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "label_source"));
	/* get the full name of the repo from the repo_id */
	repo_name = gpk_application_get_full_repo_name (priv, split[PK_PACKAGE_ID_DATA]);
	gtk_label_set_label (GTK_LABEL (widget), repo_name);
}

static void
gpk_application_packages_treeview_clicked_cb (GtkTreeSelection *selection, GpkApplicationPrivate *priv)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean show_install = TRUE;
	gboolean show_remove = TRUE;
	PkBitfield state;
	g_auto(GStrv) package_ids = NULL;
	g_autofree gchar *package_id = NULL;
	g_autofree gchar *summary = NULL;

	/* ignore selection changed if we've just cleared the package list */
	if (!priv->has_package)
		return;

	/* This will only work in single or browse selection mode! */
	if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_debug ("no row selected");

		/* we cannot now add it */
		gpk_application_allow_install (priv, FALSE);
		gpk_application_allow_remove (priv, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "hbox_packages"));
		gtk_widget_hide (widget);

		/* hide details */
		gpk_application_clear_details (priv);
		return;
	}

	/* check we aren't a help line */
	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_STATE, &state,
			    PACKAGES_COLUMN_ID, &package_id,
			    PACKAGES_COLUMN_SUMMARY, &summary,
			    -1);
	if (package_id == NULL) {
		g_debug ("ignoring help click");
		return;
	}

	/* show the menu item */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "hbox_packages"));
	gtk_widget_show (widget);

	show_install = (state == 0 ||
			state == pk_bitfield_from_enums (GPK_STATE_INSTALLED, GPK_STATE_IN_LIST, -1));
	show_remove = (state == pk_bitfield_value (GPK_STATE_INSTALLED) ||
		       state == pk_bitfield_value (GPK_STATE_IN_LIST));

	if (priv->action == GPK_ACTION_INSTALL && !pk_bitfield_contain (state, GPK_STATE_IN_LIST))
		show_remove = FALSE;
	if (priv->action == GPK_ACTION_REMOVE && !pk_bitfield_contain (state, GPK_STATE_IN_LIST))
		show_install = FALSE;

	/* only show buttons if we are in the correct mode */
	gpk_application_allow_install (priv, show_install);
	gpk_application_allow_remove (priv, show_remove);

	/* clear the description text */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "textview_description"));
	gpk_application_set_text_buffer (widget, NULL);

	/* ensure new action succeeds */
	g_cancellable_reset (priv->cancellable);

	/* get the details */
	package_ids = pk_package_ids_from_id (package_id);
	pk_task_get_details_async (PK_TASK(priv->task), package_ids, priv->cancellable,
				     (PkProgressCallback) gpk_application_progress_cb, priv,
				     (GAsyncReadyCallback) gpk_application_get_details_cb, priv);
}

static void
gpk_application_notify_network_state_cb (PkControl *_control, GParamSpec *pspec, GpkApplicationPrivate *priv)
{
	PkNetworkEnum state;

	/* show icon? */
	g_object_get (priv->control,
		      "network-state", &state,
		      NULL);
	g_debug ("state=%u", state);
}

static void
gpk_application_group_add_data (GpkApplicationPrivate *priv, PkGroupEnum group)
{
	GtkTreeIter iter;
	const gchar *icon_name;
	const gchar *text;

	gtk_tree_store_append (priv->groups_store, &iter, NULL);

	text = gpk_group_enum_to_localised_text (group);
	icon_name = gpk_group_enum_to_icon_name (group);
	gtk_tree_store_set (priv->groups_store, &iter,
			    GROUPS_COLUMN_NAME, text,
			    GROUPS_COLUMN_SUMMARY, NULL,
			    GROUPS_COLUMN_ID, pk_group_enum_to_string (group),
			    GROUPS_COLUMN_ICON, icon_name,
			    GROUPS_COLUMN_ACTIVE, TRUE,
			    -1);
}

static void
gpk_application_menu_search_by_name (GtkMenuItem *item, GpkApplicationPrivate *priv)
{
	GtkWidget *widget;

	/* change type */
	priv->search_type = GPK_SEARCH_NAME;
	g_debug ("set search type=%u", priv->search_type);

	/* save default to GSettings */
	g_settings_set_enum (priv->settings,
			     GPK_SETTINGS_SEARCH_MODE,
			     priv->search_type);

	/* set the new icon */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_text"));
	/* TRANSLATORS: entry tooltip: basic search */
	gtk_widget_set_tooltip_text (widget, _("Searching by name"));
	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (widget),
					   GTK_ENTRY_ICON_PRIMARY,
					   "edit-find");
}

static void
gpk_application_menu_search_by_description (GtkMenuItem *item, GpkApplicationPrivate *priv)
{
	GtkWidget *widget;

	/* set type */
	priv->search_type = GPK_SEARCH_DETAILS;
	g_debug ("set search type=%u", priv->search_type);

	/* save default to GSettings */
	g_settings_set_enum (priv->settings,
			     GPK_SETTINGS_SEARCH_MODE,
			     priv->search_type);

	/* set the new icon */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_text"));
	/* TRANSLATORS: entry tooltip: detailed search */
	gtk_widget_set_tooltip_text (widget, _("Searching by description"));
	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (widget),
					   GTK_ENTRY_ICON_PRIMARY,
					   "edit-find-replace");
}

static void
gpk_application_menu_search_by_file (GtkMenuItem *item, GpkApplicationPrivate *priv)
{
	GtkWidget *widget;

	/* set type */
	priv->search_type = GPK_SEARCH_FILE;
	g_debug ("set search type=%u", priv->search_type);

	/* save default to GSettings */
	g_settings_set_enum (priv->settings,
			     GPK_SETTINGS_SEARCH_MODE,
			     priv->search_type);

	/* set the new icon */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_text"));
	/* TRANSLATORS: entry tooltip: file search */
	gtk_widget_set_tooltip_text (widget, _("Searching by file"));
	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (widget),
					   GTK_ENTRY_ICON_PRIMARY,
					   "folder-open");
}

static void
gpk_application_entry_text_icon_press_cb (GtkEntry *entry, GtkEntryIconPosition icon_pos, GdkEventButton *event, GpkApplicationPrivate *priv)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;

	/* only respond to left button */
	if (event->button != 1)
		return;

	g_debug ("icon_pos=%u", icon_pos);

	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_SEARCH_NAME)) {
		/* TRANSLATORS: context menu item for the search type icon */
		item = gtk_menu_item_new_with_mnemonic (_("Search by name"));
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_name), priv);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_SEARCH_DETAILS)) {
		/* TRANSLATORS: context menu item for the search type icon */
		item = gtk_menu_item_new_with_mnemonic (_("Search by description"));
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_description), priv);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_SEARCH_FILE)) {
		/* TRANSLATORS: context menu item for the search type icon */
		item = gtk_menu_item_new_with_mnemonic (_("Search by file name"));
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_file), priv);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup_at_pointer (GTK_MENU (menu), (GdkEvent*) event);
}

static void
gpk_application_activate_about_cb (GSimpleAction *action,
				   GVariant *parameter,
				   gpointer user_data)
{
	GpkApplicationPrivate *priv = user_data;
	GtkWidget *main_window;
	const char *authors[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *artists[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *license[] = {
		N_("Licensed under the GNU General Public License Version 2"),
		N_("PackageKit is free software; you can redistribute it and/or "
		   "modify it under the terms of the GNU General Public License "
		   "as published by the Free Software Foundation; either version 2 "
		   "of the License, or (at your option) any later version."),
		N_("PackageKit is distributed in the hope that it will be useful, "
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of "
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
		   "GNU General Public License for more details."),
		N_("You should have received a copy of the GNU General Public License "
		   "along with this program; if not, write to the Free Software "
		   "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA "
		   "02110-1301, USA.")
	};
	/* TRANSLATORS: put your own name here -- you deserve credit! */
	const char  *translators = _("translator-credits");
	g_autofree gchar *license_trans = NULL;

	/* Translators comment: put your own name here to appear in the about dialog. */
	if (!strcmp (translators, "translator-credits")) {
		translators = NULL;
	}

	license_trans = g_strconcat (_(license[0]), "\n\n", _(license[1]), "\n\n",
				     _(license[2]), "\n\n", _(license[3]), "\n",  NULL);

	/* use parent */
	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_manager"));

	gtk_window_set_default_icon_name (GPK_ICON_SOFTWARE_INSTALLER);
	gtk_show_about_dialog (GTK_WINDOW (main_window),
                               "program-name", _("Packages"),
			       "version", PACKAGE_VERSION,
			       "copyright", "Copyright \xc2\xa9 2007-2009 Richard Hughes",
			       "license", license_trans,
			       "wrap-license", TRUE,
			       "website-label", _("PackageKit Website"),
			       "website", "https://www.freedesktop.org/software/PackageKit/",
				/* TRANSLATORS: description of NULL, gpk-application that is */
			       "comments", _("Package Manager for GNOME"),
			       "authors", authors,
			       "artists", artists,
			       "translator-credits", translators,
			       "logo-icon-name", GPK_ICON_SOFTWARE_INSTALLER,
			       NULL);
}

static void
gpk_application_activate_sources_cb (GSimpleAction *action,
				     GVariant *parameter,
				     gpointer user_data)
{
	gboolean ret;
	g_autofree gchar *command = NULL;
	GpkApplicationPrivate *priv = user_data;
	GtkWidget *window;
	guint xid;

	/* get xid */
	window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_manager"));
	xid = gdk_x11_window_get_xid (gtk_widget_get_window (window));

	command = g_strdup_printf ("%s/gpk-prefs --parent-window %u", BINDIR, xid);
	g_debug ("running: %s", command);
	ret = g_spawn_command_line_async (command, NULL);
	if (!ret)
		g_warning ("spawn of %s failed", command);
}

static void
gpk_application_activate_log_cb (GSimpleAction *action,
				 GVariant *parameter,
				 gpointer user_data)
{
	gboolean ret;
	g_autofree gchar *command = NULL;
	GpkApplicationPrivate *priv = user_data;
	GtkWidget *window;
	guint xid;

	/* get xid */
	window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_manager"));
	xid = gdk_x11_window_get_xid (gtk_widget_get_window (window));

	command = g_strdup_printf ("%s/gpk-log --parent-window %u", BINDIR, xid);
	g_debug ("running: %s", command);
	ret = g_spawn_command_line_async (command, NULL);
	if (!ret)
		g_warning ("spawn of %s failed", command);
}

static void
gpk_application_refresh_cache_cb (PkTask *task, GAsyncResult *res, GpkApplicationPrivate *priv)
{
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkError) error_code = NULL;
	GtkWindow *window;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		g_warning ("failed to refresh: %s", error->message);
		return;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to refresh: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		return;
	}
}

static void
gpk_application_activate_refresh_cb (GSimpleAction *action,
				     GVariant *parameter,
				     gpointer user_data)
{
	GpkApplicationPrivate *priv = user_data;

	/* ensure new action succeeds */
	g_cancellable_reset (priv->cancellable);

	pk_task_refresh_cache_async (priv->task, TRUE, priv->cancellable,
				     (PkProgressCallback) gpk_application_progress_cb, priv,
				     (GAsyncReadyCallback) gpk_application_refresh_cache_cb, priv);
}

static void
gpk_application_package_row_activated_cb (GtkTreeView *treeview, GtkTreePath *path,
					  GtkTreeViewColumn *col, GpkApplicationPrivate *priv)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean ret;
	PkBitfield state;
	g_autofree gchar *package_id = NULL;

	/* get selection */
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter (model, &iter, path);
	if (!ret) {
		g_warning ("failed to get selection");
		return;
	}

	/* get data */
	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_STATE, &state,
			    PACKAGES_COLUMN_ID, &package_id,
			    -1);

	/* check we aren't a help line */
	if (package_id == NULL) {
		g_debug ("ignoring help click");
		return;
	}

	if (gpk_application_state_get_checkbox (state))
		gpk_application_remove (priv);
	else
		gpk_application_install (priv);
}

static gboolean
gpk_application_group_row_separator_func (GtkTreeModel *model, GtkTreeIter *iter, GpkApplicationPrivate *priv)
{
	g_autofree gchar *name = NULL;
	gtk_tree_model_get (model, iter, GROUPS_COLUMN_ID, &name, -1);
	return g_strcmp0 (name, "separator") == 0;
}

static void
gpk_application_add_welcome (GpkApplicationPrivate *priv)
{
	GtkTreeIter iter;
	const gchar *welcome;
	PkBitfield state = 0;

	g_debug ("CLEAR welcome");
	gpk_application_clear_packages (priv);
	gtk_list_store_append (priv->packages_store, &iter);

	/* enter something nice */
	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		/* TRANSLATORS: welcome text if we can click the group array */
		welcome = _("Enter a search word or click a category to get started.");
	} else {
		/* TRANSLATORS: welcome text if we have to search by name */
		welcome = _("Enter a search word to get started.");
	}
	gtk_list_store_set (priv->packages_store, &iter,
			    PACKAGES_COLUMN_STATE, state,
			    PACKAGES_COLUMN_CHECKBOX, FALSE,
			    PACKAGES_COLUMN_CHECKBOX_VISIBLE, FALSE,
			    PACKAGES_COLUMN_TEXT, welcome,
			    PACKAGES_COLUMN_IMAGE, "system-search",
			    PACKAGES_COLUMN_SUMMARY, NULL,
			    PACKAGES_COLUMN_ID, NULL,
			    -1);
}

static void
gpk_application_create_group_array_enum (GpkApplicationPrivate *priv)
{
	GtkWidget *widget;
	guint i;

	/* set to no indent */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_groups"));
	gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (widget), FALSE);
	gtk_tree_view_set_level_indentation  (GTK_TREE_VIEW (widget), 0);

	/* create group tree view if we can search by group */
	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		/* add all the groups supported (except collections, which we handled above */
		for (i = 0; i < PK_GROUP_ENUM_LAST; i++) {
			if (pk_bitfield_contain (priv->groups, i) &&
			    i != PK_GROUP_ENUM_COLLECTIONS && i != PK_GROUP_ENUM_NEWEST)
				gpk_application_group_add_data (priv, i);
		}
	}
}

static void
gpk_application_get_categories_cb (PkTask *task, GAsyncResult *res, GpkApplicationPrivate *priv)
{
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(GPtrArray) array = NULL;
	GtkTreeIter iter;
	GtkTreeIter iter2;
	guint i, j;
	GtkTreeView *treeview;
	PkCategory *item;
	PkCategory *item2;
	GtkWindow *window;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		g_warning ("failed to get list of categories: %s", error->message);
		return;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get cats: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		return;
	}

	/* set to expanders with indent */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (priv->builder, "treeview_groups"));
	gtk_tree_view_set_show_expanders (treeview, TRUE);
	gtk_tree_view_set_level_indentation  (treeview, 3);

	/* add repos with descriptions */
	array = pk_results_get_category_array (results);
	for (i = 0; i < array->len; i++) {
		g_autofree gchar *package_id = NULL;
		g_autofree gchar *name = NULL;
		g_autofree gchar *summary = NULL;
		g_autofree gchar *cat_id = NULL;
		g_autofree gchar *icon = NULL;

		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "name", &name,
			      "summary", &summary,
			      "cat-id", &cat_id,
			      "icon", &icon,
			      NULL);

		gtk_tree_store_append (priv->groups_store, &iter, NULL);
		gtk_tree_store_set (priv->groups_store, &iter,
				    GROUPS_COLUMN_NAME, name,
				    GROUPS_COLUMN_SUMMARY, summary,
				    GROUPS_COLUMN_ID, cat_id,
				    GROUPS_COLUMN_ICON, icon,
				    GROUPS_COLUMN_ACTIVE, FALSE,
				    -1);
		j = 0;
		do {
			g_autofree gchar *cat_id_tmp = NULL;
			g_autofree gchar *icon_tmp = NULL;
			g_autofree gchar *name_tmp = NULL;
			g_autofree gchar *parent_id_tmp = NULL;
			g_autofree gchar *summary_tmp = NULL;

			/* only allows groups two layers deep */
			item2 = g_ptr_array_index (array, j);
			g_object_get (item2,
				      "parent-id", &parent_id_tmp,
				      "cat-id", &cat_id_tmp,
				      "name", &name_tmp,
				      "summary", &summary_tmp,
				      "icon", &icon_tmp,
				      NULL);
			if (g_strcmp0 (parent_id_tmp, cat_id) == 0) {
				gtk_tree_store_append (priv->groups_store, &iter2, &iter);
				gtk_tree_store_set (priv->groups_store, &iter2,
						    GROUPS_COLUMN_NAME, name_tmp,
						    GROUPS_COLUMN_SUMMARY, summary_tmp,
						    GROUPS_COLUMN_ID, cat_id_tmp,
						    GROUPS_COLUMN_ICON, icon_tmp,
						    GROUPS_COLUMN_ACTIVE, TRUE,
						    -1);
				g_ptr_array_remove (array, item2);
			} else
				j++;
		} while (j < array->len);
	}

	/* open all expanders */
	gtk_tree_view_collapse_all (treeview);
}

static void
gpk_application_create_group_array_categories (GpkApplicationPrivate *priv)
{
	/* ensure new action succeeds */
	g_cancellable_reset (priv->cancellable);

	/* get categories supported */
	pk_task_get_categories_async (PK_TASK(priv->task), priv->cancellable,
				        (PkProgressCallback) gpk_application_progress_cb, priv,
				        (GAsyncReadyCallback) gpk_application_get_categories_cb, priv);
}

static void
gpk_application_key_changed_cb (GSettings *settings, const gchar *key, GpkApplicationPrivate *priv)
{
	gboolean ret;

	if (g_strcmp0 (key, GPK_SETTINGS_CATEGORY_GROUPS) == 0) {
		ret = g_settings_get_boolean (priv->settings, key);
		gtk_tree_store_clear (priv->groups_store);
		if (ret)
			gpk_application_create_group_array_categories (priv);
		else
			gpk_application_create_group_array_enum (priv);
	} else if (g_strcmp0 (key, "filter-newest") == 0) {
		/* refresh the search results */
		if (g_settings_get_boolean (priv->settings, key))
			pk_bitfield_add (priv->filters_current, PK_FILTER_ENUM_NEWEST);
		else
			pk_bitfield_remove (priv->filters_current, PK_FILTER_ENUM_NEWEST);
		gpk_application_perform_search (priv);
	} else if (g_strcmp0 (key, "filter-arch") == 0) {
		/* refresh the search results */
		if (g_settings_get_boolean (priv->settings, key))
			pk_bitfield_add (priv->filters_current, PK_FILTER_ENUM_ARCH);
		else
			pk_bitfield_remove (priv->filters_current, PK_FILTER_ENUM_ARCH);
		gpk_application_perform_search (priv);
	}
}

static void
pk_backend_status_get_properties_cb (GObject *object, GAsyncResult *res, GpkApplicationPrivate *priv)
{
	GtkWidget *widget;
	g_autoptr(GError) error = NULL;
	PkControl *control = PK_CONTROL(object);
	gboolean ret;
	PkBitfield filters;
	GtkTreeIter iter;
	const gchar *icon_name;

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	if (!ret) {
		/* TRANSLATORS: daemon is broken */
		g_print ("%s: %s\n", _("Exiting as properties could not be retrieved"), error->message);
		return;
	}

	/* get values */
	g_object_get (control,
		      "roles", &priv->roles,
		      "filters", &filters,
		      "groups", &priv->groups,
		      NULL);

	/* Remove description/file array if needed. */
	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_GET_DETAILS) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow2"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_GET_FILES) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_files"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_DEPENDS_ON) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_depends"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_REQUIRED_BY) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_requires"));
		gtk_widget_hide (widget);
	}

	/* hide the group selector if we don't support search-groups */
	if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_SEARCH_GROUP) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "scrolledwindow_groups"));
		gtk_widget_hide (widget);
	}

	/* add an "all" entry if we can GetPackages */
	ret = g_settings_get_boolean (priv->settings, GPK_SETTINGS_SHOW_ALL_PACKAGES);
	if (ret && pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_GET_PACKAGES)) {
		gtk_tree_store_append (priv->groups_store, &iter, NULL);
		icon_name = gpk_role_enum_to_icon_name (PK_ROLE_ENUM_GET_PACKAGES);
		gtk_tree_store_set (priv->groups_store, &iter,
				    /* TRANSLATORS: title: all of the packages on the system and available in sources */
				    GROUPS_COLUMN_NAME, _("All packages"),
				    /* TRANSLATORS: tooltip: all packages */
				    GROUPS_COLUMN_SUMMARY, _("Show all packages"),
				    GROUPS_COLUMN_ID, "all-packages",
				    GROUPS_COLUMN_ACTIVE, TRUE,
				    GROUPS_COLUMN_ICON, icon_name, -1);
	}

	/* add these at the top of the array */
	if (pk_bitfield_contain (priv->groups, PK_GROUP_ENUM_COLLECTIONS))
		gpk_application_group_add_data (priv, PK_GROUP_ENUM_COLLECTIONS);

	/* add a separator */
	gtk_tree_store_append (priv->groups_store, &iter, NULL);
	gtk_tree_store_set (priv->groups_store, &iter,
			    GROUPS_COLUMN_ID, "separator", -1);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_groups"));
	gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (widget),
					      (GtkTreeViewRowSeparatorFunc) gpk_application_group_row_separator_func,
					      priv, NULL);

	/* simple array or category tree? */
	ret = g_settings_get_boolean (priv->settings, GPK_SETTINGS_CATEGORY_GROUPS);
	if (ret && pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_GET_CATEGORIES))
		gpk_application_create_group_array_categories (priv);
	else
		gpk_application_create_group_array_enum (priv);

	/* set the search mode */
	priv->search_type = g_settings_get_enum (priv->settings, GPK_SETTINGS_SEARCH_MODE);

	/* search by name */
	if (priv->search_type == GPK_SEARCH_NAME) {
		gpk_application_menu_search_by_name (NULL, priv);

	/* set to details if we can we do the action? */
	} else if (priv->search_type == GPK_SEARCH_DETAILS) {
		if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_SEARCH_DETAILS)) {
			gpk_application_menu_search_by_description (NULL, priv);
		} else {
			g_warning ("cannot use mode %u as not capable, using name", priv->search_type);
			gpk_application_menu_search_by_name (NULL, priv);
		}

	/* set to file if we can we do the action? */
	} else if (priv->search_type == GPK_SEARCH_FILE) {
		gpk_application_menu_search_by_file (NULL, priv);

		if (pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_SEARCH_FILE)) {
			gpk_application_menu_search_by_file (NULL, priv);
		} else {
			g_warning ("cannot use mode %u as not capable, using name", priv->search_type);
			gpk_application_menu_search_by_name (NULL, priv);
		}

	/* mode not recognized */
	} else {
		g_warning ("cannot recognize mode %u, using name", priv->search_type);
		gpk_application_menu_search_by_name (NULL, priv);
	}

	/* welcome */
	gpk_application_add_welcome (priv);
}

static void
gpk_application_get_repo_list_cb (PkTask *task, GAsyncResult *res, GpkApplicationPrivate *priv)
{
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(GPtrArray) array = NULL;
	PkRepoDetail *item;
	guint i;
	GtkWindow *window;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		g_warning ("failed to get list of repos: %s", error->message);
		return;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to repo list: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		return;
	}

	/* add repos with descriptions */
	array = pk_results_get_repo_detail_array (results);
	for (i = 0; i < array->len; i++) {
		g_autofree gchar *repo_id = NULL;
		g_autofree gchar *description = NULL;
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "repo-id", &repo_id,
			      "description", &description,
			      NULL);

		g_debug ("repo = %s:%s", repo_id, description);
		/* no problem, just no point adding as we will fallback to the repo_id */
		if (description != NULL)
			g_hash_table_insert (priv->repos, g_strdup (repo_id), g_strdup (description));
	}
}

static void
gpk_application_activate_cb (GtkApplication *_application, GpkApplicationPrivate *priv)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (priv->builder, "window_manager"));
	gtk_window_present (window);
}

static void
gpk_application_startup_cb (GtkApplication *application, GpkApplicationPrivate *priv)
{
	GAction *action;
	g_autoptr(GError) error = NULL;
	GMenuModel *menu;
	GtkTreeSelection *selection;
	GtkWidget *main_window;
	GtkWidget *widget;
	guint retval;

	priv->package_sack = pk_package_sack_new ();
	priv->settings = g_settings_new (GPK_SETTINGS_SCHEMA);
	priv->cancellable = g_cancellable_new ();
	priv->repos = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	/* watch gnome-packagekit keys */
	g_signal_connect (priv->settings, "changed", G_CALLBACK (gpk_application_key_changed_cb), priv);

	/* create array stores */
	priv->packages_store = gtk_list_store_new (PACKAGES_COLUMN_LAST,
					      G_TYPE_STRING,
					      G_TYPE_UINT64,
					      G_TYPE_BOOLEAN,
					      G_TYPE_BOOLEAN,
					      G_TYPE_STRING,
					      G_TYPE_STRING,
					      G_TYPE_STRING);
	priv->groups_store = gtk_tree_store_new (GROUPS_COLUMN_LAST,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_BOOLEAN);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   PKGDATADIR G_DIR_SEPARATOR_S "icons");
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   "/usr/share/gnome-packagekit/icons");

	priv->control = pk_control_new ();

	/* this is what we use mainly */
	priv->task = PK_TASK (gpk_task_new ());
	g_object_set (priv->task,
		      "background", FALSE,
		      NULL);

	/* get properties */
	pk_control_get_properties_async (priv->control, NULL, (GAsyncReadyCallback) pk_backend_status_get_properties_cb, priv);
	g_signal_connect (priv->control, "notify::network-state",
			  G_CALLBACK (gpk_application_notify_network_state_cb), priv);

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_resource (priv->builder,
						"/org/gnome/packagekit/gpk-application.ui",
						&error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		return;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_manager"));
	gtk_application_add_window (application, GTK_WINDOW (main_window));
	gtk_window_set_application (GTK_WINDOW (main_window), application);

	/* setup the application menu */
	menu = G_MENU_MODEL (gtk_builder_get_object (priv->builder, "appmenu"));
	gtk_application_set_app_menu (priv->application, menu);
	action = g_settings_create_action (priv->settings, "filter-newest");
	g_action_map_add_action (G_ACTION_MAP (priv->application), action);
	action = g_settings_create_action (priv->settings, "filter-arch");
	g_action_map_add_action (G_ACTION_MAP (priv->application), action);

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SOFTWARE_INSTALLER);
	gtk_window_set_default_icon_name (GPK_ICON_SOFTWARE_INSTALLER);

	/* clear */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_clear"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_button_clear_cb), priv);

	/* install */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_apply"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_button_apply_cb), priv);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_homepage"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_menu_homepage_cb), priv);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_files"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_menu_files_cb), priv);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_menu_install_cb), priv);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_remove"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_menu_remove_cb), priv);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_depends"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_menu_depends_cb), priv);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_requires"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_menu_requires_cb), priv);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "hbox_packages"));
	gtk_widget_hide (widget);

	/* search cancel button */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_cancel"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_cancel_cb), priv);
	gtk_widget_hide (widget);

	/* the fancy text entry widget */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_text"));

	/* set focus on entry text */
	gtk_widget_grab_focus (widget);
	gtk_widget_show (widget);
	gtk_entry_set_icon_sensitive (GTK_ENTRY (widget), GTK_ENTRY_ICON_PRIMARY, TRUE);

	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_find_cb), priv);
	g_signal_connect (widget, "paste-clipboard",
			  G_CALLBACK (gpk_application_find_cb), priv);
	g_signal_connect (widget, "icon-press",
			  G_CALLBACK (gpk_application_entry_text_icon_press_cb), priv);

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "entry_text"));
	g_signal_connect (GTK_EDITABLE (widget), "changed",
			  G_CALLBACK (gpk_application_text_changed_cb), priv);

	/* mark find button insensitive */
	gpk_application_set_button_find_sensitivity (priv);

	/* set a size, as much as the screen allows */
	gtk_window_set_default_size (GTK_WINDOW (main_window), 1000, 600);
	gtk_widget_show (GTK_WIDGET(main_window));

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_packages"));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));
	g_signal_connect (GTK_TREE_VIEW (widget), "row-activated",
			  G_CALLBACK (gpk_application_package_row_activated_cb), priv);

	/* sorted */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->packages_store),
					      PACKAGES_COLUMN_ID, GTK_SORT_ASCENDING);

	/* create package tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_packages"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (priv->packages_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_application_packages_treeview_clicked_cb), priv);

	/* add columns to the tree view */
	gpk_application_packages_add_columns (priv);

	/* set up the groups checkbox */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "treeview_groups"));

	/* add columns to the tree view */
	gpk_application_groups_add_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (widget), GROUPS_COLUMN_SUMMARY);
	gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (widget), FALSE);
	gtk_tree_view_set_level_indentation  (GTK_TREE_VIEW (widget), 9);
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (priv->groups_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_application_groups_treeview_changed_cb), priv);

	/* get repos, so we can show the full name in the package source box */
	pk_task_get_repo_list_async (PK_TASK (priv->task),
				       pk_bitfield_value (PK_FILTER_ENUM_NONE),
				       priv->cancellable,
				       (PkProgressCallback) gpk_application_progress_cb, priv,
				       (GAsyncReadyCallback) gpk_application_get_repo_list_cb, priv);

	/* set current action */
	priv->action = GPK_ACTION_NONE;
	gpk_application_change_queue_status (priv);

	/* sync toggles */
	gpk_application_key_changed_cb (priv->settings, "filter-newest", priv);
	gpk_application_key_changed_cb (priv->settings, "filter-arch", priv);

	/* hide details */
	gpk_application_clear_details (priv);
}

static void
gpk_application_activate_quit_cb (GSimpleAction *action,
				  GVariant *parameter,
				  gpointer user_data)
{
	GpkApplicationPrivate *priv = user_data;
	gpk_application_quit (priv);
}

static void
gpk_application_activate_updates_cb (GSimpleAction *action,
				     GVariant *parameter,
				     gpointer user_data)
{
	gboolean ret;
	g_autofree gchar *command = NULL;
	g_autoptr(GError) error = NULL;

	command = g_build_filename (BINDIR, "gpk-update-viewer", NULL);
	g_debug ("running: %s", command);
	ret = g_spawn_command_line_async (command, &error);
	if (!ret)
		g_warning ("spawn of %s failed: %s", command, error->message);

}

static GActionEntry gpk_menu_app_entries[] = {
	{ "updates",		gpk_application_activate_updates_cb, NULL, NULL, NULL },
	{ "sources",		gpk_application_activate_sources_cb, NULL, NULL, NULL },
	{ "refresh",		gpk_application_activate_refresh_cb, NULL, NULL, NULL },
	{ "log",		gpk_application_activate_log_cb, NULL, NULL, NULL },
	{ "quit",		gpk_application_activate_quit_cb, NULL, NULL, NULL },
	{ "about",		gpk_application_activate_about_cb, NULL, NULL, NULL },
};

int
main (int argc, char *argv[])
{
	gboolean program_version = FALSE;
	GOptionContext *context;
	gboolean ret;
	gint status = 0;
	g_autofree gchar *filename = NULL;
	GpkApplicationPrivate *priv;

	const GOptionEntry options[] = {
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
		  /* TRANSLATORS: show the program version */
		  _("Show the program version and exit"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("Install Software"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gpk_debug_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	/* add PackageKit */
	gpk_debug_add_log_domain ("PackageKit");

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	/* are we running privileged */
	ret = gpk_check_privileged_user (_("Package installer"), TRUE);
	if (!ret)
		return 1;

	priv = g_new0 (GpkApplicationPrivate, 1);

	/* are we already activated? */
	priv->application = gtk_application_new ("org.gnome.Packages", 0);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (gpk_application_startup_cb), priv);
	g_signal_connect (priv->application, "activate",
			  G_CALLBACK (gpk_application_activate_cb), priv);
	g_action_map_add_action_entries (G_ACTION_MAP (priv->application),
					 gpk_menu_app_entries,
					 G_N_ELEMENTS (gpk_menu_app_entries),
					 priv);

	filename = g_build_filename (BINDIR, "gpk-update-viewer", NULL);
	if (!g_file_test (filename, G_FILE_TEST_IS_EXECUTABLE)) {
		GAction *action = g_action_map_lookup_action (G_ACTION_MAP (priv->application), "updates");
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), FALSE);
	}

	/* run */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);
	g_object_unref (priv->application);

	if (priv->details_event_id > 0)
		g_source_remove (priv->details_event_id);

	if (priv->packages_store != NULL)
		g_object_unref (priv->packages_store);
	if (priv->control != NULL)
		g_object_unref (priv->control);
	if (priv->task != NULL)
		g_object_unref (priv->task);
	if (priv->settings != NULL)
		g_object_unref (priv->settings);
	if (priv->builder != NULL)
		g_object_unref (priv->builder);
	if (priv->cancellable != NULL)
		g_object_unref (priv->cancellable);
	if (priv->package_sack != NULL)
		g_object_unref (priv->package_sack);
	if (priv->repos != NULL)
		g_hash_table_destroy (priv->repos);
	if (priv->status_id > 0)
		g_source_remove (priv->status_id);
	g_free (priv->homepage_url);
	g_free (priv->search_group);
	g_free (priv->search_text);
	g_free (priv);

	return status;
}
