/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2010 Richard Hughes <richard@hughsie.com>
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

#include <dbus/dbus-glib.h>
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

#include "egg-markdown.h"
#include "egg-string.h"

#include "gpk-cell-renderer-uri.h"
#include "gpk-common.h"
#include "gpk-common.h"
#include "gpk-desktop.h"
#include "gpk-dialog.h"
#include "gpk-enum.h"
#include "gpk-error.h"
#include "gpk-gnome.h"
#include "gpk-helper-run.h"
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

static EggMarkdown	*markdown = NULL;
static gboolean		 has_package = FALSE; /* if we got a package in the search */
static gboolean		 search_in_progress = FALSE;
static GCancellable	*cancellable = NULL;
static gchar		*homepage_url = NULL;
static gchar		*search_group = NULL;
static gchar		*search_text = NULL;
static GHashTable	*repos = NULL;
static GpkActionMode	 action = GPK_ACTION_UNKNOWN;
static GpkHelperRun	*helper_run = NULL;
static GpkSearchMode	 search_mode = GPK_MODE_UNKNOWN;
static GpkSearchType	 search_type = GPK_SEARCH_UNKNOWN;
static GSettings	*settings = NULL;
static GtkBuilder	*builder = NULL;
static GtkListStore	*details_store = NULL;
static GtkListStore	*packages_store = NULL;
static GtkTreeStore	*groups_store = NULL;
static guint		 details_event_id = 0;
static guint		 status_id = 0;
static PkBitfield	 filters_current = 0;
static PkBitfield	 groups = 0;
static PkBitfield	 roles = 0;
static PkControl	*control = NULL;
static PkDesktop	*desktop = NULL;
static PkPackageSack	*package_sack = NULL;
static PkStatusEnum	 status_last = PK_STATUS_ENUM_UNKNOWN;
static PkTask		*task = NULL;

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

enum {
	DETAIL_COLUMN_TITLE,
	DETAIL_COLUMN_TEXT,
	DETAIL_COLUMN_URI,
	DETAIL_COLUMN_LAST
};

static void gpk_application_perform_search (gpointer user_data);

/**
 * gpk_application_state_get_icon:
 **/
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

/**
 * gpk_application_state_get_checkbox:
 **/
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

/**
 * gpk_application_set_text_buffer:
 **/
static void
gpk_application_set_text_buffer (GtkWidget *widget, const gchar *text)
{
	GtkTextBuffer *buffer;
	buffer = gtk_text_buffer_new (NULL);
	/* ITS4: ignore, not used for allocation */
	if (egg_strzero (text) == FALSE) {
		gtk_text_buffer_set_text (buffer, text, -1);
	} else {
		/* no information */
		gtk_text_buffer_set_text (buffer, "", -1);
	}
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);
}

/**
 * gpk_application_allow_install:
 **/
static void
gpk_application_allow_install (gboolean allow)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_install"));
	gtk_widget_set_sensitive (widget, allow);
}

/**
 * gpk_application_allow_remove:
 **/
static void
gpk_application_allow_remove (gboolean allow)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_remove"));
	gtk_widget_set_sensitive (widget, allow);
}

/**
 * gpk_application_packages_checkbox_invert:
 **/
static void
gpk_application_packages_checkbox_invert (gpointer user_data)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	const gchar *icon = NULL;
	gboolean checkbox;
	PkBitfield state;
	gboolean ret;
	gchar *package_id = NULL;
	gchar **split;

	/* get the selection and add */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_packages"));
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

	/* use the application icon if not selected */
	if (!pk_bitfield_contain (state, GPK_STATE_IN_LIST)) {
		split = pk_package_id_split (package_id);
		icon = gpk_desktop_guess_icon_name (desktop, split[PK_PACKAGE_ID_NAME]);
		g_strfreev (split);
	}

	/* get the new icon */
	if (icon == NULL)
		icon = gpk_application_state_get_icon (state);
	checkbox = gpk_application_state_get_checkbox (state);

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    PACKAGES_COLUMN_STATE, state,
			    PACKAGES_COLUMN_CHECKBOX, checkbox,
			    PACKAGES_COLUMN_IMAGE, icon,
			    -1);
	g_free (package_id);
}

/**
 * gpk_application_get_checkbox_enable:
 **/
static gboolean
gpk_application_get_checkbox_enable (PkBitfield state)
{
	gboolean enable_installed = TRUE;
	gboolean enable_available = TRUE;

	if (action == GPK_ACTION_INSTALL)
		enable_installed = FALSE;
	else if (action == GPK_ACTION_REMOVE)
		enable_available = FALSE;

	if (pk_bitfield_contain (state, GPK_STATE_INSTALLED))
		return enable_installed;
	return enable_available;
}

/**
 * gpk_application_set_buttons_apply_clear:
 **/
static void
gpk_application_set_buttons_apply_clear (gpointer user_data)
{
	GtkWidget *widget;
	GtkTreeView *treeview;
	gboolean valid;
	GtkTreeIter iter;
	GtkTreeModel *model;
	PkBitfield state;
	gboolean enabled;
	gchar *package_id;
	gint len;
	GPtrArray *array;

	/* okay to apply? */
	array = pk_package_sack_get_array (package_sack);
	len = array->len;
	g_ptr_array_unref (array);

	if (len == 0) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_apply"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_clear"));
		gtk_widget_set_sensitive (widget, FALSE);
		action = GPK_ACTION_NONE;
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_apply"));
		gtk_widget_set_sensitive (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_clear"));
		gtk_widget_set_sensitive (widget, TRUE);
	}

	/* correct the enabled state */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_packages"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* for all current items, reset the state if in the array */
	while (valid) {
		gtk_tree_model_get (model, &iter,
				    PACKAGES_COLUMN_STATE, &state,
				    PACKAGES_COLUMN_ID, &package_id,
				    -1);

		/* we never show the checkbox for the search helper */
		if (package_id == NULL) {
			enabled = FALSE;
		} else {
			enabled = gpk_application_get_checkbox_enable (state);
		}
		g_free (package_id);

		/* set visible */
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, PACKAGES_COLUMN_CHECKBOX_VISIBLE, enabled, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * gpk_application_get_selected_package:
 **/
static gboolean
gpk_application_get_selected_package (gchar **package_id, gchar **summary)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	gboolean ret;

	/* get the selection and add */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_packages"));
	selection = gtk_tree_view_get_selection (treeview);
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret) {
		g_warning ("no selection");
		goto out;
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
out:
	return ret;
}

/**
 * gpk_application_install:
 **/
static gboolean
gpk_application_install (gpointer user_data)
{
	gboolean ret;
	gchar *package_id_selected = NULL;
	gchar *summary_selected = NULL;
	PkPackage *package;

	/* get selection */
	ret = gpk_application_get_selected_package (&package_id_selected, &summary_selected);
	if (!ret) {
		g_warning ("no package selected");
		goto out;
	}

	/* changed mind, or wrong mode */
	if (action == GPK_ACTION_REMOVE) {
		ret = pk_package_sack_remove_package_by_id (package_sack, package_id_selected);
		if (ret) {
			g_debug ("removed %s from package array", package_id_selected);

			/* correct buttons */
			gpk_application_allow_install (FALSE);
			gpk_application_allow_remove (TRUE);
			gpk_application_packages_checkbox_invert (NULL);
			gpk_application_set_buttons_apply_clear (NULL);
			return TRUE;
		}
		g_warning ("wrong mode and not in array");
		return FALSE;
	}

	/* already added */
	package = pk_package_sack_find_by_id (package_sack, package_id_selected);
	if (package != NULL) {
		g_warning ("already added");
		goto out;
	}

	/* set mode */
	action = GPK_ACTION_INSTALL;

	/* add to array */
	package = pk_package_new ();
	pk_package_set_id (package, package_id_selected, NULL);
	g_object_set (package,
		      "info", PK_INFO_ENUM_AVAILABLE,
		      "summary", summary_selected,
		      NULL);
	pk_package_sack_add_package (package_sack, package);
	g_object_unref (package);

	/* correct buttons */
	gpk_application_allow_install (FALSE);
	gpk_application_allow_remove (TRUE);
	gpk_application_packages_checkbox_invert (NULL);
	gpk_application_set_buttons_apply_clear (NULL);
out:
	g_free (package_id_selected);
	g_free (summary_selected);
	return ret;
}

/**
 * gpk_application_menu_homepage_cb:
 **/
static void
gpk_application_menu_homepage_cb (GtkAction *_action, gpointer user_data)
{
	gpk_gnome_open (homepage_url);
}

/**
 * gpk_application_strcmp_indirect:
 **/
static gint
gpk_application_strcmp_indirect (gchar **a, gchar **b)
{
	return strcmp (*a, *b);
}

/**
 * gpk_application_get_files_cb:
 **/
static void
gpk_application_get_files_cb (PkClient *client, GAsyncResult *res, gpointer user_data)
{
	gboolean ret;
	gchar **files = NULL;
	gchar *package_id_selected = NULL;
	gchar **split = NULL;
	gchar *title = NULL;
	GError *error = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_sort = NULL;
	GtkWidget *dialog;
	GtkWindow *window;
	PkError *error_code = NULL;
	PkFiles *item;
	PkResults *results;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get files: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get files: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		goto out;
	}

	/* get data */
	array = pk_results_get_files_array (results);
	if (array->len != 1)
		goto out;

	/* assume only one option */
	item = g_ptr_array_index (array, 0);

	/* get selection */
	ret = gpk_application_get_selected_package (&package_id_selected, NULL);
	if (!ret) {
		g_warning ("no package selected");
		goto out;
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
	title = g_strdup_printf (ngettext ("%i file installed by %s",
					   "%i files installed by %s",
					   array_sort->len), array_sort->len, split[PK_PACKAGE_ID_NAME]);

	window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
	dialog = gtk_message_dialog_new (window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
	gpk_dialog_embed_file_list_widget (GTK_DIALOG (dialog), array_sort);
	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 250);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
out:
	g_free (title);
	g_strfreev (files);
	g_strfreev (split);
	g_free (package_id_selected);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (array_sort != NULL)
		g_ptr_array_unref (array_sort);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_application_status_changed_timeout_cb:
 **/
static gboolean
gpk_application_status_changed_timeout_cb (gpointer user_data)
{
	const gchar *text;
	GtkWidget *widget;

	/* set the text and show */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_status"));
	text = gpk_status_enum_to_localised_text (status_last);
	gtk_label_set_label (GTK_LABEL (widget), text);

	/* set icon */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_status"));
	gtk_image_set_from_icon_name (GTK_IMAGE (widget),
				      gpk_status_enum_to_icon_name (status_last),
				      GTK_ICON_SIZE_BUTTON);

	/* show containing box */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_status"));
	gtk_widget_show (widget);

	/* never repeat */
	status_id = 0;
	return FALSE;
}

/**
 * gpk_application_progress_cb:
 **/
static void
gpk_application_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
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
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_packages"));
			gtk_widget_set_sensitive (widget, TRUE);

			/* make apply button sensitive */
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_apply"));
			gtk_widget_set_sensitive (widget, TRUE);

			/* we've not yet shown, so don't bother */
			if (status_id > 0) {
				g_source_remove (status_id);
				status_id = 0;
			}

			widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_status"));
			gtk_widget_hide (widget);
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar_progress"));
			gtk_widget_hide (widget);
			goto out;
		}

		/* already pending show */
		if (status_id > 0)
			goto out;

		/* only show after some time in the transaction */
		status_id =
			g_timeout_add (GPK_UI_STATUS_SHOW_DELAY,
				       (GSourceFunc) gpk_application_status_changed_timeout_cb,
				       NULL);
		g_source_set_name_by_id (status_id,
					 "[GpkApplication] status-changed");

		/* save for the callback */
		status_last = status;

	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar_progress"));
		if (percentage > 0) {
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0f);
			gtk_widget_show (widget);
		} else {
			gtk_widget_hide (widget);
		}

	} else if (type == PK_PROGRESS_TYPE_ALLOW_CANCEL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_cancel"));
		gtk_widget_set_sensitive (widget, allow_cancel);
	}
out:
	return;
}

/**
 * gpk_application_menu_files_cb:
 **/
static void
gpk_application_menu_files_cb (GtkAction *_action, gpointer user_data)
{
	gboolean ret;
	gchar **package_ids = NULL;
	gchar *package_id_selected = NULL;

	/* get selection */
	ret = gpk_application_get_selected_package (&package_id_selected, NULL);
	if (!ret) {
		g_warning ("no package selected");
		goto out;
	}

	/* ensure new action succeeds */
	g_cancellable_reset (cancellable);

	/* set correct view */
	package_ids = pk_package_ids_from_id (package_id_selected);
	pk_client_get_files_async (PK_CLIENT (task), package_ids, cancellable,
				   (PkProgressCallback) gpk_application_progress_cb, NULL,
				   (GAsyncReadyCallback) gpk_application_get_files_cb, NULL);
out:
	g_free (package_id_selected);
	g_strfreev (package_ids);
}

/**
 * gpk_application_remove:
 **/
static gboolean
gpk_application_remove (gpointer user_data)
{
	gboolean ret;
	gchar *package_id_selected = NULL;
	gchar *summary_selected = NULL;
	PkPackage *package;

	/* get selection */
	ret = gpk_application_get_selected_package (&package_id_selected, &summary_selected);
	if (!ret) {
		g_warning ("no package selected");
		goto out;
	}

	/* changed mind, or wrong mode */
	if (action == GPK_ACTION_INSTALL) {
		ret = pk_package_sack_remove_package_by_id (package_sack, package_id_selected);
		if (ret) {
			g_debug ("removed %s from package array", package_id_selected);

			/* correct buttons */
			gpk_application_allow_install (TRUE);
			gpk_application_allow_remove (FALSE);
			gpk_application_packages_checkbox_invert (NULL);
			gpk_application_set_buttons_apply_clear (NULL);
			return TRUE;
		}
		g_warning ("wrong mode and not in array");
		return FALSE;
	}

	/* already added */
	ret = (pk_package_sack_find_by_id (package_sack, package_id_selected) == NULL);
	if (!ret) {
		g_warning ("already added");
		goto out;
	}

	action = GPK_ACTION_REMOVE;
	package = pk_package_new ();
	pk_package_set_id (package, package_id_selected, NULL);
	g_object_set (package,
		      "info", PK_INFO_ENUM_INSTALLED,
		      "summary", summary_selected,
		      NULL);
	pk_package_sack_add_package (package_sack, package);
	g_object_unref (package);

	/* correct buttons */
	gpk_application_allow_install (TRUE);
	gpk_application_allow_remove (FALSE);
	gpk_application_packages_checkbox_invert (NULL);
	gpk_application_set_buttons_apply_clear (NULL);
out:
	g_free (package_id_selected);
	g_free (summary_selected);
	return TRUE;
}

/**
 * gpk_application_menu_install_cb:
 **/
static void
gpk_application_menu_install_cb (GtkAction *_action, gpointer user_data)
{
	gpk_application_install (NULL);
}

/**
 * gpk_application_menu_remove_cb:
 **/
static void
gpk_application_menu_remove_cb (GtkAction *_action, gpointer user_data)
{
	gpk_application_remove (NULL);
}

/**
 * gpk_application_menu_run_cb:
 **/
static void
gpk_application_menu_run_cb (GtkAction *_action, gpointer user_data)
{
	gchar **package_ids;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	PkBitfield state;
	gboolean ret;
	gchar *package_id = NULL;

	/* get selection */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_packages"));
	selection = gtk_tree_view_get_selection (treeview);
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret) {
		g_warning ("no selection");
		return;
	}

	/* get item */
	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_ID, &package_id,
			    PACKAGES_COLUMN_STATE, &state, -1);

	/* only if installed */
	if (pk_bitfield_contain (state, GPK_STATE_INSTALLED)) {
		/* run this single package id */
		package_ids = pk_package_ids_from_id (package_id);
		gpk_helper_run_show (helper_run, package_ids);
		g_strfreev (package_ids);
	}
	g_free (package_id);
}

/**
 * gpk_application_get_requires_cb:
 **/
static void
gpk_application_get_requires_cb (PkClient *client, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;
	GPtrArray *array = NULL;
	GtkWindow *window;
	gchar *name = NULL;
	gchar *title = NULL;
	gchar *message = NULL;
	gchar **package_ids = NULL;
	GtkWidget *dialog;
	gchar *package_id_selected = NULL;
	gboolean ret;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get requires: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get requires: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		goto out;
	}

	/* get selection */
	ret = gpk_application_get_selected_package (&package_id_selected, NULL);
	if (!ret) {
		g_warning ("no package selected");
		goto out;
	}

	/* get data */
	array = pk_results_get_package_array (results);

	/* empty array */
	window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
	if (array->len == 0) {
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: no packages returned */
					_("No packages"),
					/* TRANSLATORS: this package is not required by any others */
					_("No other packages require this package"), NULL);
		goto out;
	}

	package_ids = pk_package_ids_from_id (package_id_selected);
	name = gpk_dialog_package_id_name_join_locale (package_ids);
	/* TRANSLATORS: title: how many packages require this package */
	title = g_strdup_printf (ngettext ("%i package requires %s",
					   "%i packages require %s",
					   array->len), array->len, name);

	/* TRANSLATORS: show a array of packages for the package */
	message = g_strdup_printf (ngettext ("Packages listed below require %s to function correctly.",
					     "Packages listed below require %s to function correctly.",
					     array->len), name);

	dialog = gtk_message_dialog_new (window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gpk_dialog_embed_package_list_widget (GTK_DIALOG (dialog), array);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
out:
	g_free (package_id_selected);
	g_strfreev (package_ids);
	g_free (name);
	g_free (title);
	g_free (message);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_application_menu_requires_cb:
 **/
static void
gpk_application_menu_requires_cb (GtkAction *_action, gpointer user_data)
{
	gboolean ret;
	gchar **package_ids = NULL;
	gchar *package_id_selected = NULL;

	/* get selection */
	ret = gpk_application_get_selected_package (&package_id_selected, NULL);
	if (!ret) {
		g_warning ("no package selected");
		goto out;
	}

	/* ensure new action succeeds */
	g_cancellable_reset (cancellable);

	/* get the requires */
	package_ids = pk_package_ids_from_id (package_id_selected);
	pk_client_get_requires_async (PK_CLIENT (task),
				      pk_bitfield_value (PK_FILTER_ENUM_NONE),
				      package_ids, TRUE, cancellable,
				      (PkProgressCallback) gpk_application_progress_cb, NULL,
				      (GAsyncReadyCallback) gpk_application_get_requires_cb, NULL);
out:
	g_free (package_id_selected);
	g_strfreev (package_ids);
}

/**
 * gpk_application_get_depends_cb:
 **/
static void
gpk_application_get_depends_cb (PkClient *client, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;
	GPtrArray *array = NULL;
	GtkWindow *window;
	gchar *name = NULL;
	gchar *title = NULL;
	gchar *message = NULL;
	gchar **package_ids = NULL;
	GtkWidget *dialog;
	gchar *package_id_selected = NULL;
	gboolean ret;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get depends: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get depends: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		goto out;
	}

	/* get data */
	array = pk_results_get_package_array (results);

	/* get selection */
	ret = gpk_application_get_selected_package (&package_id_selected, NULL);
	if (!ret) {
		g_warning ("no package selected");
		goto out;
	}

	/* empty array */
	window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
	if (array->len == 0) {
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: no packages returned */
					_("No packages"),
					/* TRANSLATORS: this package does not depend on any others */
					_("This package does not depend on any others"), NULL);
		goto out;
	}

	package_ids = pk_package_ids_from_id (package_id_selected);
	name = gpk_dialog_package_id_name_join_locale (package_ids);
	/* TRANSLATORS: title: show the number of other packages we depend on */
	title = g_strdup_printf (ngettext ("%i additional package is required for %s",
					   "%i additional packages are required for %s",
					   array->len), array->len, name);

	/* TRANSLATORS: message: show the array of dependent packages for this package */
	message = g_strdup_printf (ngettext ("Packages listed below are required for %s to function correctly.",
					     "Packages listed below are required for %s to function correctly.",
					     array->len), name);

	dialog = gtk_message_dialog_new (window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gpk_dialog_embed_package_list_widget (GTK_DIALOG (dialog), array);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	g_free (package_id_selected);
	g_strfreev (package_ids);
	g_free (name);
	g_free (title);
	g_free (message);
}

/**
 * gpk_application_menu_depends_cb:
 **/
static void
gpk_application_menu_depends_cb (GtkAction *_action, gpointer user_data)
{
	gboolean ret;
	gchar **package_ids = NULL;
	gchar *package_id_selected = NULL;

	/* get selection */
	ret = gpk_application_get_selected_package (&package_id_selected, NULL);
	if (!ret) {
		g_warning ("no package selected");
		goto out;
	}

	/* ensure new action succeeds */
	g_cancellable_reset (cancellable);

	/* get the depends */
	package_ids = pk_package_ids_from_id (package_id_selected);
	pk_client_get_depends_async (PK_CLIENT (task),
				     pk_bitfield_value (PK_FILTER_ENUM_NONE),
				     package_ids, TRUE, cancellable,
				     (PkProgressCallback) gpk_application_progress_cb, NULL,
				     (GAsyncReadyCallback) gpk_application_get_depends_cb, NULL);
out:
	g_free (package_id_selected);
	g_strfreev (package_ids);
}

/**
 * gpk_application_get_full_repo_name:
 **/
static const gchar *
gpk_application_get_full_repo_name (const gchar *data)
{
	const gchar *repo_name;

	/* if no data, we can't look up in the hash table */
	if (egg_strzero (data)) {
		g_warning ("no ident data");
		/* TRANSLATORS: the repo name is invalid or not found, fall back to this */
		return _("Invalid");
	}

	/* try to find in cached repo array */
	repo_name = (const gchar *) g_hash_table_lookup (repos, data);
	if (repo_name == NULL) {
		g_warning ("no repo name, falling back to %s", data);
		return data;
	}
	return repo_name;
}

/**
 * gpk_application_add_detail_item:
 **/
static void
gpk_application_add_detail_item (const gchar *title, const gchar *text, const gchar *uri)
{
	gchar *markup;
	GtkTreeView *treeview;
	GtkTreeIter iter;
	GtkTreeSelection *selection;

	/* we don't need to clear anymore */
	if (details_event_id > 0) {
		g_source_remove (details_event_id);
		details_event_id = 0;
	}

	/* format */
	markup = g_strdup_printf ("<b>%s:</b>", title);

	g_debug ("%s %s %s", markup, text, uri);
	gtk_list_store_append (details_store, &iter);
	gtk_list_store_set (details_store, &iter,
			    DETAIL_COLUMN_TITLE, markup,
			    DETAIL_COLUMN_TEXT, text,
			    DETAIL_COLUMN_URI, uri,
			    -1);

	g_free (markup);

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_detail"));
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_NONE);
	gtk_tree_view_columns_autosize (treeview);
}

/**
 * gpk_application_clear_details_really:
 **/
static gboolean
gpk_application_clear_details_really (gpointer user_data)
{
	GtkWidget *widget;

	/* hide details */
	gtk_list_store_clear (details_store);

	/* clear the old text */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "textview_description"));
	gpk_application_set_text_buffer (widget, NULL);

	/* hide dead widgets */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "scrolledwindow_detail"));
	gtk_widget_hide (widget);

	/* never repeat */
	return FALSE;
}

/**
 * gpk_application_clear_details:
 **/
static void
gpk_application_clear_details (gpointer user_data)
{
	/* only clear the last data if it takes a little while, else we flicker the display */
	if (details_event_id > 0)
		g_source_remove (details_event_id);
	details_event_id =
		g_timeout_add (200, (GSourceFunc) gpk_application_clear_details_really, NULL);
	g_source_set_name_by_id (details_event_id,
				 "[GpkApplication] clear-details");
}

/**
 * gpk_application_clear_packages:
 **/
static void
gpk_application_clear_packages (gpointer user_data)
{
	/* clear existing array */
	gtk_list_store_clear (packages_store);
	has_package = FALSE;
}

/**
 * gpk_application_text_format_display:
 **/
static gchar *
gpk_application_text_format_display (const gchar *ascii)
{
	gchar *text;
	egg_markdown_set_output (markdown, EGG_MARKDOWN_OUTPUT_TEXT);
	text = egg_markdown_parse (markdown, ascii);
	return text;
}

/**
 * gpk_application_add_item_to_results:
 **/
static void
gpk_application_add_item_to_results (PkPackage *item)
{
	GtkTreeIter iter;
	gchar *summary_markup;
	const gchar *icon = NULL;
	gchar *text;
	gboolean in_queue;
	gboolean installed;
	gboolean checkbox;
	gboolean enabled;
	PkBitfield state = 0;
	static guint package_cnt = 0;
	gchar **split;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *summary = NULL;
	GtkWidget *widget;

	/* get data */
	g_object_get (item,
		      "info", &info,
		      "package-id", &package_id,
		      "summary", &summary,
		      NULL);

	/* format if required */
	egg_markdown_set_output (markdown, EGG_MARKDOWN_OUTPUT_PANGO);
	summary_markup = egg_markdown_parse (markdown, summary);

	/* mark as got so we don't warn */
	has_package = TRUE;

	/* are we in the package array? */
	in_queue = (pk_package_sack_find_by_id (package_sack, package_id) != NULL);
	installed = (info == PK_INFO_ENUM_INSTALLED) || (info == PK_INFO_ENUM_COLLECTION_INSTALLED);

	if (installed)
		pk_bitfield_add (state, GPK_STATE_INSTALLED);
	if (in_queue)
		pk_bitfield_add (state, GPK_STATE_IN_LIST);

	/* special icon */
	if (info == PK_INFO_ENUM_COLLECTION_INSTALLED || info == PK_INFO_ENUM_COLLECTION_AVAILABLE)
		pk_bitfield_add (state, GPK_STATE_COLLECTION);

	/* use the application icon if available */
	split = pk_package_id_split (package_id);
	icon = gpk_desktop_guess_icon_name (desktop, split[PK_PACKAGE_ID_NAME]);
	g_strfreev (split);
	if (icon == NULL)
		icon = gpk_application_state_get_icon (state);

	checkbox = gpk_application_state_get_checkbox (state);

	/* use two lines */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "window_manager"));
	text = gpk_package_id_format_twoline (gtk_widget_get_style_context (widget),
					      package_id,
					      summary_markup);

	/* can we modify this? */
	enabled = gpk_application_get_checkbox_enable (state);

	gtk_list_store_append (packages_store, &iter);
	gtk_list_store_set (packages_store, &iter,
			    PACKAGES_COLUMN_STATE, state,
			    PACKAGES_COLUMN_CHECKBOX, checkbox,
			    PACKAGES_COLUMN_CHECKBOX_VISIBLE, enabled,
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_SUMMARY, summary,
			    PACKAGES_COLUMN_ID, package_id,
			    PACKAGES_COLUMN_IMAGE, icon,
			    -1);

	/* only process every n events else we re-order too many times */
	if (package_cnt++ % 200 == 0) {
		while (gtk_events_pending ())
			gtk_main_iteration ();
	}

	g_free (package_id);
	g_free (summary_markup);
	g_free (summary);
	g_free (text);
}

/**
 * gpk_application_suggest_better_search:
 **/
static void
gpk_application_suggest_better_search (gpointer user_data)
{
	const gchar *message = NULL;
	/* TRANSLATORS: no results were found for this search */
	const gchar *title = _("No results were found.");
	GtkTreeIter iter;
	gchar *text;
	PkBitfield state = 0;

	if (search_mode == GPK_MODE_GROUP ||
	    search_mode == GPK_MODE_ALL_PACKAGES) {
		/* TRANSLATORS: be helpful, but this shouldn't happen */
		message = _("Try entering a package name in the search bar.");
	}  else if (search_mode == GPK_MODE_SELECTED) {
		/* TRANSLATORS: nothing in the package queue */
		message = _("There are no packages queued to be installed or removed.");
	} else {
		if (search_type == GPK_SEARCH_NAME ||
		    search_type == GPK_SEARCH_FILE)
			/* TRANSLATORS: tell the user to switch to details search mode */
			message = _("Try searching package descriptions by clicking the icon next to the search text.");
		else
			/* TRANSLATORS: tell the user to try harder */
			message = _("Try again with a different search term.");
	}

	text = g_strdup_printf ("%s\n%s", title, message);
	gtk_list_store_append (packages_store, &iter);
	gtk_list_store_set (packages_store, &iter,
			    PACKAGES_COLUMN_STATE, state,
			    PACKAGES_COLUMN_CHECKBOX, FALSE,
			    PACKAGES_COLUMN_CHECKBOX_VISIBLE, FALSE,
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_IMAGE, "system-search",
			    PACKAGES_COLUMN_ID, NULL,
			    -1);
	g_free (text);
}

/**
 * gpk_application_perform_search_idle_cb:
 **/
static gboolean
gpk_application_perform_search_idle_cb (gpointer user_data)
{
	gpk_application_perform_search (NULL);
	return FALSE;
}

/**
 * gpk_application_select_exact_match:
 *
 * NOTE: we have to do this in the finished_cb, as if we do this as we return
 * results we cancel the search and start getting the package details.
 **/
static void
gpk_application_select_exact_match (const gchar *text)
{
	GtkTreeView *treeview;
	gboolean valid;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeSelection *selection = NULL;
	gchar *package_id;
	gchar **split;

	/* get the first iter in the array */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_packages"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* for all items in treeview */
	while (valid) {
		gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_ID, &package_id, -1);
		if (package_id != NULL) {
			/* exact match, so select and scroll */
			split = pk_package_id_split (package_id);
			if (g_strcmp0 (split[PK_PACKAGE_ID_NAME], text) == 0) {
				selection = gtk_tree_view_get_selection (treeview);
				gtk_tree_selection_select_iter (selection, &iter);
				path = gtk_tree_model_get_path (model, &iter);
				gtk_tree_view_scroll_to_cell (treeview, path, NULL, FALSE, 0.5f, 0.5f);
				gtk_tree_path_free (path);
			}
			g_strfreev (split);

			/* no point continuing for a second match */
			if (selection != NULL)
				break;
		}
		g_free (package_id);
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * gpk_application_run_installed:
 **/
static void
gpk_application_run_installed (PkResults *results)
{
	guint i;
	GPtrArray *array;
	PkPackage *item;
	GPtrArray *package_ids_array;
	gchar **package_ids = NULL;
	PkInfoEnum info;
	gchar *package_id = NULL;

	/* get the package array and filter on INSTALLED */
	package_ids_array = g_ptr_array_new_with_free_func (g_free);
	array = pk_results_get_package_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id,
			      NULL);
		if (info == PK_INFO_ENUM_INSTALLING)
			g_ptr_array_add (package_ids_array, g_strdup (package_id));
		g_free (package_id);
	}

	/* nothing to show */
	if (package_ids_array->len == 0) {
		g_debug ("nothing to do");
		goto out;
	}

	/* this is async */
	package_ids = pk_ptr_array_to_strv (package_ids_array);
	gpk_helper_run_show (helper_run, package_ids);

out:
	g_strfreev (package_ids);
	g_ptr_array_unref (package_ids_array);
	g_ptr_array_unref (array);
}

#if 0
/**
 * gpk_application_finished_cb:
 **/
static void
gpk_application_finished_cb (PkClient *client, PkExitEnum exit_enum, guint runtime, gpointer user_data)
{

//	widget = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar_progress"));
//	gtk_widget_hide (widget);

}
#endif

/**
 * gpk_application_cancel_cb:
 **/
static void
gpk_application_cancel_cb (GtkWidget *button_widget, gpointer user_data)
{
	g_cancellable_cancel (cancellable);

	/* switch buttons around */
	search_mode = GPK_MODE_UNKNOWN;
}

/**
 * gpk_application_set_button_find_sensitivity:
 **/
static void
gpk_application_set_button_find_sensitivity (gpointer user_data)
{
	gboolean sensitive;
	GtkWidget *widget;
	const gchar *search;

	/* get the text in the search bar */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_text"));
	search = gtk_entry_get_text (GTK_ENTRY (widget));

	/* only sensitive if not in the middle of a search and has valid text */
	sensitive = !search_in_progress && !egg_strzero (search);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_find"));
	gtk_widget_set_sensitive (widget, sensitive);
}

/**
 * gpk_application_search_cb:
 **/
static void
gpk_application_search_cb (PkClient *client, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;
	GPtrArray *array = NULL;
	PkPackage *item;
	guint i;
	GtkWidget *widget;
	GtkWindow *window;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to search: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to search: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		goto out;
	}

	/* get data */
	array = pk_results_get_package_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		gpk_application_add_item_to_results (item);
	}

	/* were there no entries found? */
	if (!has_package)
		gpk_application_suggest_better_search (NULL);

	/* if there is an exact match, select it */
	gpk_application_select_exact_match (search_text);

	/* focus back to the text extry */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_text"));
	gtk_widget_grab_focus (widget);

	/* reset UI */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_groups"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "textview_description"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_detail"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_text"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_apply"));
	gtk_widget_set_sensitive (widget, TRUE);
	gpk_application_set_buttons_apply_clear (NULL);
out:
	/* mark find button sensitive */
	search_in_progress = FALSE;
	gpk_application_set_button_find_sensitivity (NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "scrolledwindow_groups"));
	gtk_widget_set_sensitive (widget, TRUE);

	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_application_perform_search_name_details_file:
 **/
static void
gpk_application_perform_search_name_details_file (gpointer user_data)
{
	GtkEntry *entry;
	GtkWindow *window;
	GError *error = NULL;
	gboolean ret;
	gchar **searches = NULL;

	entry = GTK_ENTRY (gtk_builder_get_object (builder, "entry_text"));
	g_free (search_text);
	search_text = g_strdup (gtk_entry_get_text (entry));

	/* have we got input? */
	if (egg_strzero (search_text)) {
		g_debug ("no input");
		goto out;
	}

	ret = !egg_strzero (search_text);
	if (!ret) {
		g_debug ("invalid input text, will fail");
		/* TODO - make the dialog turn red... */
		window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: title: invalid text in the search bar */
					_("Invalid search text"),
					/* TRANSLATORS: message: tell the user that's not allowed */
					_("The search text contains invalid characters"), NULL);
		goto out;
	}
	g_debug ("find %s", search_text);

	/* mark find button insensitive */
	search_in_progress = TRUE;
	gpk_application_set_button_find_sensitivity (NULL);

	/* ensure new action succeeds */
	g_cancellable_reset (cancellable);

	/* do the search */
	searches = g_strsplit (search_text, " ", -1);
	if (search_type == GPK_SEARCH_NAME) {
		pk_task_search_names_async (task,
					     filters_current,
					     searches, cancellable,
					     (PkProgressCallback) gpk_application_progress_cb, NULL,
					     (GAsyncReadyCallback) gpk_application_search_cb, NULL);
	} else if (search_type == GPK_SEARCH_DETAILS) {
		pk_task_search_details_async (task,
					     filters_current,
					     searches, cancellable,
					     (PkProgressCallback) gpk_application_progress_cb, NULL,
					     (GAsyncReadyCallback) gpk_application_search_cb, NULL);
	} else if (search_type == GPK_SEARCH_FILE) {
		pk_task_search_files_async (task,
					     filters_current,
					     searches, cancellable,
					     (PkProgressCallback) gpk_application_progress_cb, NULL,
					     (GAsyncReadyCallback) gpk_application_search_cb, NULL);
	} else {
		g_warning ("invalid search type");
		goto out;
	}

	if (!ret) {
		window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: title: we failed to execute the method */
					_("The search could not be completed"),
					/* TRANSLATORS: low level failure, details to follow */
					_("Running the transaction failed"), error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_strfreev (searches);
}

/**
 * gpk_application_perform_search_others:
 **/
static void
gpk_application_perform_search_others (gpointer user_data)
{
	gchar **search_groups;

	/* ensure new action succeeds */
	g_cancellable_reset (cancellable);

	search_in_progress = TRUE;

	if (search_mode == GPK_MODE_GROUP) {
		search_groups = g_strsplit (search_group, " ", -1);
		pk_client_search_groups_async (PK_CLIENT(task),
					       filters_current, search_groups, cancellable,
					       (PkProgressCallback) gpk_application_progress_cb, NULL,
					       (GAsyncReadyCallback) gpk_application_search_cb, NULL);
		g_strfreev (search_groups);
	} else {
		pk_client_get_packages_async (PK_CLIENT(task),
					      filters_current, cancellable,
					      (PkProgressCallback) gpk_application_progress_cb, NULL,
					      (GAsyncReadyCallback) gpk_application_search_cb, NULL);
	}
}

/**
 * gpk_application_populate_selected:
 **/
static gboolean
gpk_application_populate_selected (gpointer user_data)
{
	guint i;
	PkPackage *package;
	GPtrArray *array;

	/* get size */
	array = pk_package_sack_get_array (package_sack);

	/* nothing in queue */
	if (array->len == 0) {
		gpk_application_suggest_better_search (NULL);
		goto out;
	}

	/* dump queue to package window */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		gpk_application_add_item_to_results (package);
	}

out:
	g_ptr_array_unref (array);
	return TRUE;
}

/**
 * gpk_application_perform_search:
 **/
static void
gpk_application_perform_search (gpointer user_data)
{
	GtkWidget *widget;

	/*if we are in the middle of a search, just return*/
	if (search_in_progress == TRUE)
		return;

	/* just shown the welcome screen */
	if (search_mode == GPK_MODE_UNKNOWN)
		return;

	if (search_mode == GPK_MODE_NAME_DETAILS_FILE ||
	    search_mode == GPK_MODE_GROUP ||
	    search_mode == GPK_MODE_SELECTED) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder,
							     "scrolledwindow_groups"));
		gtk_widget_set_sensitive (widget, FALSE);
	}

	g_debug ("CLEAR search");
	gpk_application_clear_details (NULL);
	gpk_application_clear_packages (NULL);

	if (search_mode == GPK_MODE_NAME_DETAILS_FILE) {
		gpk_application_perform_search_name_details_file (NULL);
	} else if (search_mode == GPK_MODE_GROUP ||
		   search_mode == GPK_MODE_ALL_PACKAGES) {
		gpk_application_perform_search_others (NULL);
	} else if (search_mode == GPK_MODE_SELECTED) {
		gpk_application_populate_selected (NULL);
	} else {
		g_debug ("doing nothing");
	}
}

/**
 * gpk_application_find_cb:
 **/
static void
gpk_application_find_cb (GtkWidget *button_widget, gpointer user_data)
{
	search_mode = GPK_MODE_NAME_DETAILS_FILE;
	gpk_application_perform_search (NULL);
}

/**
 * gpk_application_quit:
 * @event: The event type, unused.
 **/
static gboolean
gpk_application_quit (GtkApplication *application)
{
	GPtrArray *array;
	gint len;
	GtkResponseType result;
	GtkWindow *window;
	GtkWidget *dialog;

	/* do we have any items queued for removal or installation? */
	array = pk_package_sack_get_array (package_sack);
	len = array->len;
	g_ptr_array_unref (array);

	if (len != 0) {
		window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
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
	g_cancellable_cancel (cancellable);
	g_application_release (G_APPLICATION (application));
	return TRUE;
}

/**
 * gpk_application_menu_quit_cb:
 **/
static void
gpk_application_menu_quit_cb (GtkAction *_action, GtkApplication *application)
{
	gpk_application_quit (application);
}

/**
 * gpk_application_text_changed_cb:
 **/
static gboolean
gpk_application_text_changed_cb (GtkEntry *entry, GdkEventKey *event, gpointer user_data)
{
	GtkTreeView *treeview;
	GtkTreeSelection *selection;

	/* clear group selection if we have the tab */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_groups"));
		selection = gtk_tree_view_get_selection (treeview);
		gtk_tree_selection_unselect_all (selection);
	}

	/* mark find button sensitive */
	gpk_application_set_button_find_sensitivity (NULL);
	return FALSE;
}

/**
 * gpk_application_packages_installed_clicked_cb:
 **/
static void
gpk_application_packages_installed_clicked_cb (GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeSelection *selection;
	PkBitfield state;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_packages"));
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
		gpk_application_remove (NULL);
	} else {
		gpk_application_install (NULL);
	}
	gtk_tree_path_free (path);
}

static void gpk_application_packages_treeview_clicked_cb (GtkTreeSelection *selection, gpointer user_data);

/**
 * gpk_application_button_help_cb:
 **/
static void
gpk_application_button_help_cb (GtkWidget *widget_button, gpointer user_data)
{
	gpk_gnome_help ("add-remove");
}

/**
 * gpk_application_button_clear_cb:
 **/
static void
gpk_application_button_clear_cb (GtkWidget *widget_button, gpointer user_data)
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
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_packages"));
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
	pk_package_sack_clear (package_sack);

	/* force a button refresh */
	selection = gtk_tree_view_get_selection (treeview);
	gpk_application_packages_treeview_clicked_cb (selection, NULL);

	gpk_application_set_buttons_apply_clear (NULL);
}

/**
 * gpk_application_install_packages_cb:
 **/
static void
gpk_application_install_packages_cb (PkTask *_task, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;
	GtkWindow *window;
	guint idle_id;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		g_warning ("failed to install packages: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to install packages: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		goto out;
	}

	/* idle add in the background */
	idle_id = g_idle_add ((GSourceFunc) gpk_application_perform_search_idle_cb, NULL);
	g_source_set_name_by_id (idle_id, "[GpkApplication] search");

	/* find applications that were installed, and offer to run them */
	gpk_application_run_installed (results);

	/* clear if success */
	pk_package_sack_clear (package_sack);
	action = GPK_ACTION_NONE;
	gpk_application_set_buttons_apply_clear (NULL);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_application_remove_packages_cb:
 **/
static void
gpk_application_remove_packages_cb (PkTask *_task, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;
	GtkWindow *window;
	guint idle_id;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		g_warning ("failed to remove packages: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to remove packages: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		goto out;
	}

	/* idle add in the background */
	idle_id = g_idle_add ((GSourceFunc) gpk_application_perform_search_idle_cb, NULL);
	g_source_set_name_by_id (idle_id, "[GpkApplication] search");

	/* clear if success */
	pk_package_sack_clear (package_sack);
	action = GPK_ACTION_NONE;
	gpk_application_set_buttons_apply_clear (NULL);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_application_button_apply_cb:
 **/
static void
gpk_application_button_apply_cb (GtkWidget *widget, gpointer user_data)
{
	gchar **package_ids = NULL;
	gboolean autoremove;

	/* ensure new action succeeds */
	g_cancellable_reset (cancellable);

	package_ids = pk_package_sack_get_ids (package_sack);
	if (action == GPK_ACTION_INSTALL) {
		/* install */
		pk_task_install_packages_async (task, package_ids, cancellable,
						(PkProgressCallback) gpk_application_progress_cb, NULL,
						(GAsyncReadyCallback) gpk_application_install_packages_cb, NULL);

		/* make package array insensitive */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_packages"));
		gtk_widget_set_sensitive (widget, FALSE);

		/* make apply button insensitive */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_apply"));
		gtk_widget_set_sensitive (widget, FALSE);

	} else if (action == GPK_ACTION_REMOVE) {
		autoremove = g_settings_get_boolean (settings, GPK_SETTINGS_ENABLE_AUTOREMOVE);

		/* remove */
		pk_task_remove_packages_async (task, package_ids, TRUE, autoremove, cancellable,
					       (PkProgressCallback) gpk_application_progress_cb, NULL,
					       (GAsyncReadyCallback) gpk_application_remove_packages_cb, NULL);

		/* make package array insensitive */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_packages"));
		gtk_widget_set_sensitive (widget, FALSE);

		/* make apply button insensitive */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_apply"));
		gtk_widget_set_sensitive (widget, FALSE);
	}
	g_strfreev (package_ids);
	return;
}

static void
gpk_application_packages_add_columns (gpointer user_data)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_packages"));

	/* column for installed toggles */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (gpk_application_packages_installed_clicked_cb), NULL);

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

/**
 * gpk_application_groups_treeview_changed_cb:
 **/
static void
gpk_application_groups_treeview_changed_cb (GtkTreeSelection *selection, gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkEntry *entry;
	GtkTreePath *path;
	gboolean active;

	/* hide details */
	g_debug ("CLEAR tv changed");
	gpk_application_clear_details (NULL);
	gpk_application_clear_packages (NULL);

	/* clear the search text if we clicked the group array */
	entry = GTK_ENTRY (gtk_builder_get_object (builder, "entry_text"));
	gtk_entry_set_text (entry, "");

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (search_group);
		gtk_tree_model_get (model, &iter,
				    GROUPS_COLUMN_ID, &search_group,
				    GROUPS_COLUMN_ACTIVE, &active, -1);
		g_debug ("selected row is: %s (%i)", search_group, active);

		/* don't search parent groups */
		if (!active) {
			path = gtk_tree_model_get_path (model, &iter);

			/* select the parent group */
			gtk_tree_selection_select_path (selection, path);
			gtk_tree_path_free (path);
			return;
		}

		/* GetPackages? */
		if (g_strcmp0 (search_group, "all-packages") == 0)
			search_mode = GPK_MODE_ALL_PACKAGES;
		else if (g_strcmp0 (search_group, "selected") == 0)
			search_mode = GPK_MODE_SELECTED;
		else
			search_mode = GPK_MODE_GROUP;

		/* actually do the search */
		gpk_application_perform_search (NULL);
	}
}

/**
 * gpk_application_get_details_cb:
 **/
static void
gpk_application_get_details_cb (PkClient *client, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;
	GPtrArray *array = NULL;
	PkDetails *item;
	GtkWidget *widget;
	gchar *text;
	gchar *value;
	const gchar *repo_name;
	const gchar *group_text;
	gboolean installed;
	gchar **split = NULL;
	GtkWindow *window;
	gchar *package_id = NULL;
	gchar *url = NULL;
	PkGroupEnum group;
	gchar *license = NULL;
	gchar *description = NULL;
	guint64 size;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get list of categories: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get details: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		goto out;
	}

	/* get data */
	array = pk_results_get_details_array (results);
	if (array->len != 1) {
		g_warning ("not one entry %i", array->len);
		goto out;
	}

	/* only choose the first item */
	item = g_ptr_array_index (array, 0);

	/* hide to start */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "scrolledwindow_detail"));
	gtk_widget_show (widget);

	gtk_list_store_clear (details_store);

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

	/* if a collection, mark as such */
	if (g_strcmp0 (split[PK_PACKAGE_ID_DATA], "meta") == 0)
		/* TRANSLATORS: the type of package is a collection (metagroup) */
		gpk_application_add_detail_item (_("Type"), _("Collection"), NULL);

	/* homepage */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_homepage"));
	if (egg_strzero (url) == FALSE) {
		gtk_widget_set_sensitive (widget, TRUE);

		/* TRANSLATORS: tooltip: go to the web address */
		text = g_strdup_printf (_("Visit %s"), url);
		gtk_widget_set_tooltip_text (widget, text);
		g_free (text);

		/* TRANSLATORS: add an entry to go to the project home page */
		gpk_application_add_detail_item (_("Project"), _("Homepage"), url);

		/* save the url for the button */
		g_free (homepage_url);
		homepage_url = g_strdup (url);

	} else {
		gtk_widget_set_sensitive (widget, FALSE);
	}

	/* group */
	if (group != PK_GROUP_ENUM_UNKNOWN) {
		group_text = gpk_group_enum_to_localised_text (group);
		/* TRANSLATORS: the group the package belongs in */
		gpk_application_add_detail_item (_("Group"), group_text, NULL);
	}

	/* group */
	if (!egg_strzero (license)) {
		/* TRANSLATORS: the licence string for the package */
		gpk_application_add_detail_item (_("License"), license, NULL);
	}

	/* set the description */
	text = gpk_application_text_format_display (description);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "textview_description"));
	gpk_application_set_text_buffer (widget, text);
	g_free (text);

	/* if non-zero, set the size */
	if (size > 0) {
		/* set the size */
		value = g_format_size (size);
		if (g_strcmp0 (split[PK_PACKAGE_ID_DATA], "meta") == 0)
			/* TRANSLATORS: the size of the meta package */
			gpk_application_add_detail_item (_("Size"), value, NULL);
		else if (installed)
			/* TRANSLATORS: the installed size in bytes of the package */
			gpk_application_add_detail_item (_("Installed size"), value, NULL);
		else
			/* TRANSLATORS: the download size of the package */
			gpk_application_add_detail_item (_("Download size"), value, NULL);
		g_free (value);
	}

	/* set the repo text, or hide if installed */
	if (!installed && g_strcmp0 (split[PK_PACKAGE_ID_DATA], "meta") != 0) {
		/* get the full name of the repo from the repo_id */
		repo_name = gpk_application_get_full_repo_name (split[PK_PACKAGE_ID_DATA]);
		/* TRANSLATORS: where the package came from, the software source name */
		gpk_application_add_detail_item (_("Source"), repo_name, NULL);
	}
out:
	g_free (package_id);
	g_free (url);
	g_free (license);
	g_free (description);
	g_strfreev (split);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_application_packages_treeview_clicked_cb:
 **/
static void
gpk_application_packages_treeview_clicked_cb (GtkTreeSelection *selection, gpointer user_data)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean ret;
	gboolean show_install = TRUE;
	gboolean show_remove = TRUE;
	PkBitfield state;
	gchar **package_ids = NULL;
	gchar *package_id = NULL;

	/* This will only work in single or browse selection mode! */
	if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_debug ("no row selected");

		/* we cannot now add it */
		gpk_application_allow_install (FALSE);
		gpk_application_allow_remove (FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_selection"));
		gtk_widget_hide (widget);

		/* hide details */
		gpk_application_clear_details (NULL);
		goto out;
	}

	/* check we aren't a help line */
	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_STATE, &state,
			    PACKAGES_COLUMN_ID, &package_id,
			    -1);
	if (package_id == NULL) {
		g_debug ("ignoring help click");
		goto out;
	}

	/* show the menu item */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_selection"));
	gtk_widget_show (widget);

	show_install = (state == 0 ||
			state == pk_bitfield_from_enums (GPK_STATE_INSTALLED, GPK_STATE_IN_LIST, -1));
	show_remove = (state == pk_bitfield_value (GPK_STATE_INSTALLED) ||
		       state == pk_bitfield_value (GPK_STATE_IN_LIST));

	if (action == GPK_ACTION_INSTALL && !pk_bitfield_contain (state, GPK_STATE_IN_LIST))
		show_remove = FALSE;
	if (action == GPK_ACTION_REMOVE && !pk_bitfield_contain (state, GPK_STATE_IN_LIST))
		show_install = FALSE;

	/* only show buttons if we are in the correct mode */
	gpk_application_allow_install (show_install);
	gpk_application_allow_remove (show_remove);

	/* hide details */
	gpk_application_clear_details (NULL);

	/* only show run menuitem for installed programs */
	ret = pk_bitfield_contain (state, GPK_STATE_INSTALLED);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_run"));
	gtk_widget_set_sensitive (widget, ret);

	/* ensure new action succeeds */
	g_cancellable_reset (cancellable);

	/* get the details */
	package_ids = pk_package_ids_from_id (package_id);
	pk_client_get_details_async (PK_CLIENT(task), package_ids, cancellable,
				     (PkProgressCallback) gpk_application_progress_cb, NULL,
				     (GAsyncReadyCallback) gpk_application_get_details_cb, NULL);
out:
	g_free (package_id);
	g_strfreev (package_ids);
}

/**
 * gpk_application_notify_network_state_cb:
 **/
static void
gpk_application_notify_network_state_cb (PkControl *_control, GParamSpec *pspec, gpointer user_data)
{
	PkNetworkEnum state;

	/* show icon? */
	g_object_get (control,
		      "network-state", &state,
		      NULL);
	g_debug ("state=%i", state);
}

/**
 * gpk_application_group_add_data:
 **/
static void
gpk_application_group_add_data (PkGroupEnum group)
{
	GtkTreeIter iter;
	const gchar *icon_name;
	const gchar *text;

	gtk_tree_store_append (groups_store, &iter, NULL);

	text = gpk_group_enum_to_localised_text (group);
	icon_name = gpk_group_enum_to_icon_name (group);
	gtk_tree_store_set (groups_store, &iter,
			    GROUPS_COLUMN_NAME, text,
			    GROUPS_COLUMN_SUMMARY, NULL,
			    GROUPS_COLUMN_ID, pk_group_enum_to_string (group),
			    GROUPS_COLUMN_ICON, icon_name,
			    GROUPS_COLUMN_ACTIVE, TRUE,
			    -1);
}

/**
 * gpk_application_group_add_selected:
 **/
static void
gpk_application_group_add_selected (gpointer user_data)
{
	GtkTreeIter iter;

	gtk_tree_store_append (groups_store, &iter, NULL);
	gtk_tree_store_set (groups_store, &iter,
			    /* TRANSLATORS: this is a menu group of packages in the queue */
			    GROUPS_COLUMN_NAME, _("Selected packages"),
			    GROUPS_COLUMN_SUMMARY, NULL,
			    GROUPS_COLUMN_ID, "selected",
			    GROUPS_COLUMN_ICON, "edit-find",
			    GROUPS_COLUMN_ACTIVE, TRUE,
			    -1);
}

/**
 * gpk_application_popup_position_menu:
 **/
static void
gpk_application_popup_position_menu (GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer user_data)
{
	GtkWidget *widget;
	GdkWindow *window;
	GtkRequisition requisition;
	gint menu_xpos = 0;
	gint menu_ypos = 0;

	widget = GTK_WIDGET (user_data);

	/* find the location */
	window = gtk_widget_get_window (widget);
	gdk_window_get_origin (window, &menu_xpos, &menu_ypos);
	gtk_widget_get_preferred_size (GTK_WIDGET (widget), &requisition, NULL);

	/* set the position */
	*x = menu_xpos;
	*y = menu_ypos + requisition.height - 1;
	*push_in = TRUE;
}

/**
 * gpk_application_menu_search_by_name:
 **/
static void
gpk_application_menu_search_by_name (GtkMenuItem *item, gpointer data)
{
	GtkWidget *widget;

	/* change type */
	search_type = GPK_SEARCH_NAME;
	g_debug ("set search type=%i", search_type);

	/* save default to GSettings */
	g_settings_set_enum (settings,
			     GPK_SETTINGS_SEARCH_MODE,
			     search_type);

	/* set the new icon */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_text"));
	/* TRANSLATORS: entry tooltip: basic search */
	gtk_widget_set_tooltip_text (widget, _("Searching by name"));
	gtk_entry_set_icon_from_stock (GTK_ENTRY (widget), GTK_ENTRY_ICON_PRIMARY, GTK_STOCK_FIND);
}

/**
 * gpk_application_menu_search_by_description:
 **/
static void
gpk_application_menu_search_by_description (GtkMenuItem *item, gpointer data)
{
	GtkWidget *widget;

	/* set type */
	search_type = GPK_SEARCH_DETAILS;
	g_debug ("set search type=%i", search_type);

	/* save default to GSettings */
	g_settings_set_enum (settings,
			     GPK_SETTINGS_SEARCH_MODE,
			     search_type);

	/* set the new icon */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_text"));
	/* TRANSLATORS: entry tooltip: detailed search */
	gtk_widget_set_tooltip_text (widget, _("Searching by description"));
	gtk_entry_set_icon_from_stock (GTK_ENTRY (widget), GTK_ENTRY_ICON_PRIMARY, GTK_STOCK_EDIT);
}

/**
 * gpk_application_menu_search_by_file:
 **/
static void
gpk_application_menu_search_by_file (GtkMenuItem *item, gpointer data)
{
	GtkWidget *widget;

	/* set type */
	search_type = GPK_SEARCH_FILE;
	g_debug ("set search type=%i", search_type);

	/* save default to GSettings */
	g_settings_set_enum (settings,
			     GPK_SETTINGS_SEARCH_MODE,
			     search_type);

	/* set the new icon */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_text"));
	/* TRANSLATORS: entry tooltip: file search */
	gtk_widget_set_tooltip_text (widget, _("Searching by file"));
	gtk_entry_set_icon_from_stock (GTK_ENTRY (widget), GTK_ENTRY_ICON_PRIMARY, GTK_STOCK_OPEN);
}

/**
 * gpk_application_entry_text_icon_press_cb:
 **/
static void
gpk_application_entry_text_icon_press_cb (GtkEntry *entry, GtkEntryIconPosition icon_pos, GdkEventButton *event, gpointer data)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;
	GtkWidget *image;

	/* only respond to left button */
	if (event->button != 1)
		return;

	g_debug ("icon_pos=%i", icon_pos);

	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_NAME)) {
		/* TRANSLATORS: context menu item for the search type icon */
		item = gtk_image_menu_item_new_with_mnemonic (_("Search by name"));
		image = gtk_image_new_from_stock (GTK_STOCK_FIND, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_name), NULL);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_DETAILS)) {
		/* TRANSLATORS: context menu item for the search type icon */
		item = gtk_image_menu_item_new_with_mnemonic (_("Search by description"));
		image = gtk_image_new_from_stock (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_description), NULL);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_FILE)) {
		/* TRANSLATORS: context menu item for the search type icon */
		item = gtk_image_menu_item_new_with_mnemonic (_("Search by file name"));
		image = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_file), NULL);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gpk_application_popup_position_menu, entry,
			event->button, event->time);
}

/**
 * gpk_application_menu_help_cb:
 **/
static void
gpk_application_menu_help_cb (GtkAction *_action, gpointer user_data)
{
	gpk_gnome_help ("add-remove");
}

/**
 * gpk_application_menu_about_cb:
 **/
static void
gpk_application_menu_about_cb (GtkAction *_action, gpointer user_data)
{
	GtkWidget *main_window;
	const char *authors[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *documenters[] = {
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
	char	    *license_trans;

	/* Translators comment: put your own name here to appear in the about dialog. */
	if (!strcmp (translators, "translator-credits")) {
		translators = NULL;
	}

	license_trans = g_strconcat (_(license[0]), "\n\n", _(license[1]), "\n\n",
				     _(license[2]), "\n\n", _(license[3]), "\n",  NULL);

	/* use parent */
	main_window = GTK_WIDGET (gtk_builder_get_object (builder, "window_manager"));

	gtk_window_set_default_icon_name (GPK_ICON_SOFTWARE_INSTALLER);
	gtk_show_about_dialog (GTK_WINDOW (main_window),
			       "version", PACKAGE_VERSION,
			       "copyright", "Copyright \xc2\xa9 2007-2009 Richard Hughes",
			       "license", license_trans,
			       "wrap-license", TRUE,
			       "website-label", _("PackageKit Website"),
			       "website", "http://www.packagekit.org",
				/* TRANSLATORS: description of NULL, gpk-application that is */
			       "comments", _("Package Manager for GNOME"),
			       "authors", authors,
			       "documenters", documenters,
			       "artists", artists,
			       "translator-credits", translators,
			       "logo-icon-name", GPK_ICON_SOFTWARE_INSTALLER,
			       NULL);
	g_free (license_trans);
}

/**
 * gpk_application_menu_sources_cb:
 **/
static void
gpk_application_menu_sources_cb (GtkAction *_action, gpointer user_data)
{
	gboolean ret;
	guint xid;
	gchar *command;
	GtkWidget *window;

	/* get xid */
	window = GTK_WIDGET (gtk_builder_get_object (builder, "window_manager"));
	xid = gdk_x11_window_get_xid (gtk_widget_get_window (window));

	command = g_strdup_printf ("%s/gpk-prefs --parent-window %u", BINDIR, xid);
	g_debug ("running: %s", command);
	ret = g_spawn_command_line_async (command, NULL);
	if (!ret) {
		g_warning ("spawn of %s failed", command);
	}
	g_free (command);
}

/**
 * gpk_application_menu_log_cb:
 **/
static void
gpk_application_menu_log_cb (GtkAction *_action, gpointer user_data)
{
	gboolean ret;
	guint xid;
	gchar *command;
	GtkWidget *window;

	/* get xid */
	window = GTK_WIDGET (gtk_builder_get_object (builder, "window_manager"));
	xid = gdk_x11_window_get_xid (gtk_widget_get_window (window));

	command = g_strdup_printf ("%s/gpk-log --parent-window %u", BINDIR, xid);
	g_debug ("running: %s", command);
	ret = g_spawn_command_line_async (command, NULL);
	if (!ret) {
		g_warning ("spawn of %s failed", command);
	}
	g_free (command);
}

/**
 * gpk_application_refresh_cache_cb:
 **/
static void
gpk_application_refresh_cache_cb (PkClient *client, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;
	GtkWindow *window;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to refresh: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to refresh: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		goto out;
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_application_menu_refresh_cb:
 **/
static void
gpk_application_menu_refresh_cb (GtkAction *_action, gpointer user_data)
{
	/* ensure new action succeeds */
	g_cancellable_reset (cancellable);

	pk_task_refresh_cache_async (task, TRUE, cancellable,
				     (PkProgressCallback) gpk_application_progress_cb, NULL,
				     (GAsyncReadyCallback) gpk_application_refresh_cache_cb, NULL);
}

/**
 * gpk_application_menu_filter_installed_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_installed_cb (GtkWidget *widget, gpointer user_data)
{
	const gchar *name;

	name = gtk_buildable_get_name (GTK_BUILDABLE (widget));

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
		return;

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_INSTALLED);
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_NOT_INSTALLED);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_INSTALLED);
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_NOT_INSTALLED);
	} else {
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_INSTALLED);
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_NOT_INSTALLED);
	}

	/* refresh the search results */
	gpk_application_perform_search (NULL);
}

/**
 * gpk_application_menu_filter_devel_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_devel_cb (GtkWidget *widget, gpointer user_data)
{
	const gchar *name;

	name = gtk_buildable_get_name (GTK_BUILDABLE (widget));

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
		return;

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_DEVELOPMENT);
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_DEVELOPMENT);
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	} else {
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_DEVELOPMENT);
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	}

	/* refresh the search results */
	g_debug ("search devel clicked");
	gpk_application_perform_search (NULL);
}

/**
 * gpk_application_menu_filter_gui_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_gui_cb (GtkWidget *widget, gpointer user_data)
{
	const gchar *name;

	name = gtk_buildable_get_name (GTK_BUILDABLE (widget));

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
		return;

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_GUI);
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_NOT_GUI);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_GUI);
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_NOT_GUI);
	} else {
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_GUI);
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_NOT_GUI);
	}

	/* refresh the search results */
	gpk_application_perform_search (NULL);
}

/**
 * gpk_application_menu_filter_free_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_free_cb (GtkWidget *widget, gpointer user_data)
{
	const gchar *name;

	name = gtk_buildable_get_name (GTK_BUILDABLE (widget));

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
		return;

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_FREE);
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_NOT_FREE);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_FREE);
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_NOT_FREE);
	} else {
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_FREE);
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_NOT_FREE);
	}

	/* refresh the search results */
	gpk_application_perform_search (NULL);
}

/**
 * gpk_application_menu_filter_source_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_source_cb (GtkWidget *widget, gpointer user_data)
{
	const gchar *name;

	name = gtk_buildable_get_name (GTK_BUILDABLE (widget));

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
		return;

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_SOURCE);
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_NOT_SOURCE);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_SOURCE);
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_NOT_SOURCE);
	} else {
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_SOURCE);
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_NOT_SOURCE);
	}

	/* refresh the sesource results */
	gpk_application_perform_search (NULL);
}

/**
 * gpk_application_menu_filter_basename_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_basename_cb (GtkWidget *widget, gpointer user_data)
{
	gboolean enabled;

	/* save users preference to GSettings */
	enabled = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	g_settings_set_boolean (settings,
			       GPK_SETTINGS_FILTER_BASENAME, enabled);

	/* change the filter */
	if (enabled)
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_BASENAME);
	else
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_BASENAME);

	/* refresh the search results */
	gpk_application_perform_search (NULL);
}

/**
 * gpk_application_menu_filter_newest_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_newest_cb (GtkWidget *widget, gpointer user_data)
{
	gboolean enabled;

	/* save users preference to GSettings */
	enabled = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	g_settings_set_boolean (settings,
			       GPK_SETTINGS_FILTER_NEWEST, enabled);

	/* change the filter */
	if (enabled)
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_NEWEST);
	else
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_NEWEST);

	/* refresh the search results */
	gpk_application_perform_search (NULL);
}

/**
 * gpk_application_menu_filter_supported_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_supported_cb (GtkWidget *widget, gpointer user_data)
{
	gboolean enabled;

	/* save users preference to GSettings */
	enabled = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	g_settings_set_boolean (settings,
			       GPK_SETTINGS_FILTER_SUPPORTED, enabled);

	/* change the filter */
	if (enabled)
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_SUPPORTED);
	else
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_SUPPORTED);

	/* refresh the search results */
	gpk_application_perform_search (NULL);
}

/**
 * gpk_application_menu_filter_arch_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_arch_cb (GtkWidget *widget, gpointer user_data)
{
	gboolean enabled;

	/* save users preference to GSettings */
	enabled = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	g_settings_set_boolean (settings,
			       GPK_SETTINGS_FILTER_ARCH, enabled);

	/* change the filter */
	if (enabled)
		pk_bitfield_add (filters_current, PK_FILTER_ENUM_ARCH);
	else
		pk_bitfield_remove (filters_current, PK_FILTER_ENUM_ARCH);

	/* refresh the search results */
	gpk_application_perform_search (NULL);
}

/**
 * gpk_application_package_row_activated_cb:
 **/
static void
gpk_application_package_row_activated_cb (GtkTreeView *treeview, GtkTreePath *path,
					  GtkTreeViewColumn *col, gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean ret;
	PkBitfield state;
	gchar *package_id = NULL;

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
		goto out;
	}

	if (gpk_application_state_get_checkbox (state))
		gpk_application_remove (NULL);
	else
		gpk_application_install (NULL);
out:
	g_free (package_id);
}

/**
 * gpk_application_group_row_separator_func:
 **/
static gboolean
gpk_application_group_row_separator_func (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	gchar *name = NULL;
	gboolean ret;
	gtk_tree_model_get (model, iter, GROUPS_COLUMN_ID, &name, -1);
	ret = g_strcmp0 (name, "separator") == 0;
	g_free (name);
	return ret;
}

/**
 * gpk_application_treeview_renderer_clicked:
 **/
static void
gpk_application_treeview_renderer_clicked (GtkCellRendererToggle *cell, gchar *uri, gpointer user_data)
{
	g_debug ("clicked %s", uri);
	gpk_gnome_open (uri);
}

/**
 * gpk_application_treeview_add_columns_description:
 **/
static void
gpk_application_treeview_add_columns_description (gpointer user_data)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_detail"));

	/* title */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", DETAIL_COLUMN_TITLE);
	gtk_tree_view_append_column (treeview, column);

	/* column for uris */
	renderer = gpk_cell_renderer_uri_new ();
	g_signal_connect (renderer, "clicked", G_CALLBACK (gpk_application_treeview_renderer_clicked), NULL);
	/* TRANSLATORS: single column for the package details, not visible at the moment */
	column = gtk_tree_view_column_new_with_attributes (_("Text"), renderer,
							   "text", DETAIL_COLUMN_TEXT,
							   "uri", DETAIL_COLUMN_URI, NULL);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_columns_autosize (treeview);
}

/**
 * gpk_application_add_welcome:
 **/
static void
gpk_application_add_welcome (gpointer user_data)
{
	GtkTreeIter iter;
	const gchar *welcome;
	PkBitfield state = 0;

	g_debug ("CLEAR welcome");
	gpk_application_clear_packages (NULL);
	gtk_list_store_append (packages_store, &iter);

	/* enter something nice */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		/* TRANSLATORS: welcome text if we can click the group array */
		welcome = _("Enter a search word and then click find, or click a group to get started.");
	} else {
		/* TRANSLATORS: welcome text if we have to search by name */
		welcome = _("Enter a search word and then click find to get started.");
	}
	gtk_list_store_set (packages_store, &iter,
			    PACKAGES_COLUMN_STATE, state,
			    PACKAGES_COLUMN_CHECKBOX, FALSE,
			    PACKAGES_COLUMN_CHECKBOX_VISIBLE, FALSE,
			    PACKAGES_COLUMN_TEXT, welcome,
			    PACKAGES_COLUMN_IMAGE, "system-search",
			    PACKAGES_COLUMN_SUMMARY, NULL,
			    PACKAGES_COLUMN_ID, NULL,
			    -1);
}

/**
 * gpk_application_create_group_array_enum:
 **/
static void
gpk_application_create_group_array_enum (gpointer user_data)
{
	GtkWidget *widget;
	guint i;

	/* set to no indent */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_groups"));
	gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (widget), FALSE);
	gtk_tree_view_set_level_indentation  (GTK_TREE_VIEW (widget), 0);

	/* create group tree view if we can search by group */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		/* add all the groups supported (except collections, which we handled above */
		for (i=0; i<PK_GROUP_ENUM_LAST; i++) {
			if (pk_bitfield_contain (groups, i) &&
			    i != PK_GROUP_ENUM_COLLECTIONS && i != PK_GROUP_ENUM_NEWEST)
				gpk_application_group_add_data (i);
		}
	}
}

/**
 * gpk_application_get_categories_cb:
 **/
static void
gpk_application_get_categories_cb (PkClient *client, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;
	GPtrArray *array = NULL;
	GtkTreeIter iter;
	GtkTreeIter iter2;
	guint i, j;
	GtkTreeView *treeview;
	PkCategory *item;
	PkCategory *item2;
	GtkWindow *window;
	gchar *package_id = NULL;
	gchar *name = NULL;
	gchar *summary = NULL;
	gchar *cat_id = NULL;
	gchar *icon = NULL;
	gchar *parent_id_tmp = NULL;
	gchar *name_tmp = NULL;
	gchar *summary_tmp = NULL;
	gchar *cat_id_tmp = NULL;
	gchar *icon_tmp = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get list of categories: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get cats: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		goto out;
	}

	/* set to expanders with indent */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_groups"));
	gtk_tree_view_set_show_expanders (treeview, TRUE);
	gtk_tree_view_set_level_indentation  (treeview, 3);

	/* add repos with descriptions */
	array = pk_results_get_category_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "name", &name,
			      "summary", &summary,
			      "cat-id", &cat_id,
			      "icon", &icon,
			      NULL);

		gtk_tree_store_append (groups_store, &iter, NULL);
		gtk_tree_store_set (groups_store, &iter,
				    GROUPS_COLUMN_NAME, name,
				    GROUPS_COLUMN_SUMMARY, summary,
				    GROUPS_COLUMN_ID, cat_id,
				    GROUPS_COLUMN_ICON, icon,
				    GROUPS_COLUMN_ACTIVE, FALSE,
				    -1);
		j = 0;
		do {
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
				gtk_tree_store_append (groups_store, &iter2, &iter);
				gtk_tree_store_set (groups_store, &iter2,
						    GROUPS_COLUMN_NAME, name_tmp,
						    GROUPS_COLUMN_SUMMARY, summary_tmp,
						    GROUPS_COLUMN_ID, cat_id_tmp,
						    GROUPS_COLUMN_ICON, icon_tmp,
						    GROUPS_COLUMN_ACTIVE, TRUE,
						    -1);
				g_ptr_array_remove (array, item2);
			} else
				j++;
			g_free (parent_id_tmp);
			g_free (name_tmp);
			g_free (summary_tmp);
			g_free (cat_id_tmp);
			g_free (icon_tmp);
		} while (j < array->len);

		g_free (package_id);
		g_free (name);
		g_free (summary);
		g_free (cat_id);
		g_free (icon);
	}

	/* open all expanders */
	gtk_tree_view_collapse_all (treeview);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_application_create_group_array_categories:
 **/
static void
gpk_application_create_group_array_categories (gpointer user_data)
{
	/* ensure new action succeeds */
	g_cancellable_reset (cancellable);

	/* get categories supported */
	pk_client_get_categories_async (PK_CLIENT(task), cancellable,
				        (PkProgressCallback) gpk_application_progress_cb, NULL,
				        (GAsyncReadyCallback) gpk_application_get_categories_cb, NULL);
}

/**
 * gpk_application_key_changed_cb:
 *
 * We might have to do things when the keys change; do them here.
 **/
static void
gpk_application_key_changed_cb (GSettings *_settings, const gchar *key, gpointer user_data)
{
	GtkEntryCompletion *completion;
	gboolean ret;
	GtkEntry *entry;

	if (g_strcmp0 (key, GPK_SETTINGS_CATEGORY_GROUPS) == 0) {
		ret = g_settings_get_boolean (settings, key);
		gtk_tree_store_clear (groups_store);
		if (ret)
			gpk_application_create_group_array_categories (NULL);
		else
			gpk_application_create_group_array_enum (NULL);
	} else if (g_strcmp0 (key, GPK_SETTINGS_AUTOCOMPLETE) == 0) {
		ret = g_settings_get_boolean (settings, key);
		entry = GTK_ENTRY (gtk_builder_get_object (builder, "entry_text"));
		if (ret) {
			completion = gpk_package_entry_completion_new ();
			gtk_entry_set_completion (entry, completion);
			g_object_unref (completion);
		} else {
			gtk_entry_set_completion (entry, NULL);
		}
	}
}

/**
 * pk_backend_status_get_properties_cb:
 **/
static void
pk_backend_status_get_properties_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	GtkWidget *widget;
	GError *error = NULL;
//	PkControl *control = PK_CONTROL(object);
	gboolean ret;
	PkBitfield filters;
	gboolean enabled;
	GtkTreeIter iter;
	const gchar *icon_name;

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	if (!ret) {
		/* TRANSLATORS: daemon is broken */
		g_print ("%s: %s\n", _("Exiting as properties could not be retrieved"), error->message);
		g_error_free (error);
		goto out;
	}

	/* get values */
	g_object_get (control,
		      "roles", &roles,
		      "filters", &filters,
		      "groups", &groups,
		      NULL);

	/* Remove description/file array if needed. */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DETAILS) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "scrolledwindow2"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_FILES) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_files"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DEPENDS) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_depends"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_REQUIRES) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_requires"));
		gtk_widget_hide (widget);
	}

	/* hide the group selector if we don't support search-groups */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_GROUP) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "scrolledwindow_groups"));
		gtk_widget_hide (widget);
	}

	/* hide the refresh cache button if we can't do it */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REFRESH_CACHE) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_refresh"));
		gtk_widget_hide (widget);
	}

	/* hide the software-sources button if we can't do it */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_REPO_LIST) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_sources"));
		gtk_widget_hide (widget);
	}

	/* hide the filters we can't support */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_installed"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_devel"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_gui"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_FREE) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_free"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_ARCH) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_arch"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SOURCE) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_source"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SUPPORTED) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_supported"));
		gtk_widget_hide (widget);
	}

	/* BASENAME, use by default, or hide */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_basename"));
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_BASENAME)) {
		enabled = g_settings_get_boolean (settings,
						 GPK_SETTINGS_FILTER_BASENAME);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), enabled);
		/* work round a gtk2+ bug: toggled should be fired when doing gtk_check_menu_item_set_active */
		gpk_application_menu_filter_basename_cb (widget, NULL);
	} else {
		gtk_widget_hide (widget);
	}

	/* NEWEST, use by default, or hide */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_newest"));
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NEWEST)) {
		/* set from remembered state */
		enabled = g_settings_get_boolean (settings,
						  GPK_SETTINGS_FILTER_NEWEST);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), enabled);
		/* work round a gtk2+ bug: toggled should be fired when doing gtk_check_menu_item_set_active */
		gpk_application_menu_filter_newest_cb (widget, NULL);
	} else {
		gtk_widget_hide (widget);
	}

	/* ARCH, use by default, or hide */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_arch"));
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_ARCH)) {
		/* set from remembered state */
		enabled = g_settings_get_boolean (settings,
						  GPK_SETTINGS_FILTER_ARCH);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), enabled);
		/* work round a gtk2+ bug: toggled should be fired when doing gtk_check_menu_item_set_active */
		gpk_application_menu_filter_arch_cb (widget, NULL);
	} else {
		gtk_widget_hide (widget);
	}

	/* SUPPORTED, use by default, or hide */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_supported"));
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SUPPORTED)) {
		/* set from remembered state */
		enabled = g_settings_get_boolean (settings,
						  GPK_SETTINGS_FILTER_SUPPORTED);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), enabled);
		/* work round a gtk2+ bug: toggled should be fired when doing gtk_check_menu_item_set_active */
		gpk_application_menu_filter_supported_cb (widget, NULL);
	} else {
		gtk_widget_hide (widget);
	}

	/* add an "all" entry if we can GetPackages */
	ret = g_settings_get_boolean (settings, GPK_SETTINGS_SHOW_ALL_PACKAGES);
	if (ret && pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_PACKAGES)) {
		gtk_tree_store_append (groups_store, &iter, NULL);
		icon_name = gpk_role_enum_to_icon_name (PK_ROLE_ENUM_GET_PACKAGES);
		gtk_tree_store_set (groups_store, &iter,
				    /* TRANSLATORS: title: all of the packages on the system and availble in sources */
				    GROUPS_COLUMN_NAME, _("All packages"),
				    /* TRANSLATORS: tooltip: all packages */
				    GROUPS_COLUMN_SUMMARY, _("Show all packages"),
				    GROUPS_COLUMN_ID, "all-packages",
				    GROUPS_COLUMN_ACTIVE, TRUE,
				    GROUPS_COLUMN_ICON, icon_name, -1);
	}

	/* add these at the top of the array */
	if (pk_bitfield_contain (groups, PK_GROUP_ENUM_COLLECTIONS))
		gpk_application_group_add_data (PK_GROUP_ENUM_COLLECTIONS);
	if (pk_bitfield_contain (groups, PK_GROUP_ENUM_NEWEST))
		gpk_application_group_add_data (PK_GROUP_ENUM_NEWEST);

	/* add group item for selected items */
	gpk_application_group_add_selected (NULL);

	/* add a separator */
	gtk_tree_store_append (groups_store, &iter, NULL);
	gtk_tree_store_set (groups_store, &iter,
			    GROUPS_COLUMN_ID, "separator", -1);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_groups"));
	gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (widget),
					      gpk_application_group_row_separator_func, NULL, NULL);

	/* simple array or category tree? */
	ret = g_settings_get_boolean (settings, GPK_SETTINGS_CATEGORY_GROUPS);
	if (ret && pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_CATEGORIES))
		gpk_application_create_group_array_categories (NULL);
	else
		gpk_application_create_group_array_enum (NULL);

	/* set the search mode */
	search_type = g_settings_get_enum (settings, GPK_SETTINGS_SEARCH_MODE);

	/* search by name */
	if (search_type == GPK_SEARCH_NAME) {
		gpk_application_menu_search_by_name (NULL, NULL);

	/* set to details if we can we do the action? */
	} else if (search_type == GPK_SEARCH_DETAILS) {
		if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_DETAILS)) {
			gpk_application_menu_search_by_description (NULL, NULL);
		} else {
			g_warning ("cannot use mode %i as not capable, using name", search_type);
			gpk_application_menu_search_by_name (NULL, NULL);
		}

	/* set to file if we can we do the action? */
	} else if (search_type == GPK_SEARCH_FILE) {
		gpk_application_menu_search_by_file (NULL, NULL);

		if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_FILE)) {
			gpk_application_menu_search_by_file (NULL, NULL);
		} else {
			g_warning ("cannot use mode %i as not capable, using name", search_type);
			gpk_application_menu_search_by_name (NULL, NULL);
		}

	/* mode not recognized */
	} else {
		g_warning ("cannot recognize mode %i, using name", search_type);
		gpk_application_menu_search_by_name (NULL, NULL);
	}
out:
	return;
}

/**
 * gpk_application_get_repo_list_cb:
 **/
static void
gpk_application_get_repo_list_cb (PkClient *client, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;
	GPtrArray *array = NULL;
	PkRepoDetail *item;
	guint i;
	GtkWindow *window;
	gchar *repo_id = NULL;
	gchar *description = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get list of repos: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to repo list: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* if obvious message, don't tell the user */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
			window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
			gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
						gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		goto out;
	}

	/* add repos wih descriptions */
	array = pk_results_get_repo_detail_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "repo-id", &repo_id,
			      "description", &description,
			      NULL);

		g_debug ("repo = %s:%s", repo_id, description);
		/* no problem, just no point adding as we will fallback to the repo_id */
		if (description != NULL)
			g_hash_table_insert (repos, g_strdup (repo_id), g_strdup (description));
		g_free (repo_id);
		g_free (description);
	}

out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_application_activate_cb:
 **/
static void
gpk_application_activate_cb (GtkApplication *_application, gpointer user_data)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (builder, "window_manager"));
	gtk_window_present (window);
}

/**
 * gpk_application_startup_cb:
 **/
static void
gpk_application_startup_cb (GtkApplication *application, gpointer user_data)
{
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkWidget *image;
	GtkEntryCompletion *completion;
	GtkTreeSelection *selection;
	gboolean ret;
	GError *error = NULL;
	GSList *array;
	guint retval;

	package_sack = pk_package_sack_new ();
	settings = g_settings_new (GPK_SETTINGS_SCHEMA);
	cancellable = g_cancellable_new ();
	repos = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	markdown = egg_markdown_new ();
	egg_markdown_set_max_lines (markdown, 50);

	/* watch gnome-packagekit keys */
	g_signal_connect (settings, "changed", G_CALLBACK (gpk_application_key_changed_cb), NULL);

	/* create array stores */
	packages_store = gtk_list_store_new (PACKAGES_COLUMN_LAST,
					      G_TYPE_STRING,
					      G_TYPE_UINT64,
					      G_TYPE_BOOLEAN,
					      G_TYPE_BOOLEAN,
					      G_TYPE_STRING,
					      G_TYPE_STRING,
					      G_TYPE_STRING);
	groups_store = gtk_tree_store_new (GROUPS_COLUMN_LAST,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_BOOLEAN);
	details_store = gtk_list_store_new (DETAIL_COLUMN_LAST,
					    G_TYPE_STRING,
					    G_TYPE_STRING,
					    G_TYPE_STRING);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   "/usr/share/PackageKit/icons");

	control = pk_control_new ();

	/* this is what we use mainly */
	task = PK_TASK (gpk_task_new ());
	g_object_set (task,
		      "background", FALSE,
		      NULL);

	/* get properties */
	pk_control_get_properties_async (control, NULL, (GAsyncReadyCallback) pk_backend_status_get_properties_cb, NULL);
	g_signal_connect (control, "notify::network-state",
			  G_CALLBACK (gpk_application_notify_network_state_cb), NULL);

	/* get localized data from sqlite database */
	desktop = pk_desktop_new ();
	ret = pk_desktop_open_database (desktop, NULL);
	if (!ret)
		g_warning ("Failure opening database");

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-application.ui", &error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (builder, "window_manager"));
	gtk_application_add_window (application, GTK_WINDOW (main_window));

	/* helpers */
	helper_run = gpk_helper_run_new ();
	gpk_helper_run_set_parent (helper_run, GTK_WINDOW (main_window));

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SOFTWARE_INSTALLER);
	gtk_window_set_default_icon_name (GPK_ICON_SOFTWARE_INSTALLER);

	/* clear */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_clear"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_button_clear_cb), NULL);
	/* TRANSLATORS: tooltip on the clear button */
	gtk_widget_set_tooltip_text (widget, _("Clear current selection"));

	/* help */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_button_help_cb), NULL);

	/* set F1 = contents */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menu_about"));
	array = gtk_accel_groups_from_object (G_OBJECT (main_window));
	if (array != NULL)
		gtk_menu_set_accel_group (GTK_MENU (widget), GTK_ACCEL_GROUP (array->data));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_help"));
	gtk_menu_item_set_accel_path (GTK_MENU_ITEM (widget),
			              "<gpk-application>/menuitem_help");
	gtk_accel_map_add_entry ("<gpk-application>/menuitem_help", GDK_KEY_F1, 0);
	image = gtk_image_new_from_stock ("gtk-help", GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);

	/* install */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_apply"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_button_apply_cb), NULL);
	/* TRANSLATORS: tooltip on the apply button */
	gtk_widget_set_tooltip_text (widget, _("Changes are not applied instantly, this button applies all changes"));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_about"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_about_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_help"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_help_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_sources"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_sources_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_refresh"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_refresh_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_log"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_log_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_homepage"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_homepage_cb), NULL);
	/* TRANSLATORS: tooltip on the homepage button */
	gtk_widget_set_tooltip_text (widget, _("Visit home page for selected package"));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_files"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_files_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_install"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_install_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_remove"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_remove_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_depends"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_depends_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_requires"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_requires_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_run"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_run_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_quit"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_quit_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_selection"));
	gtk_widget_hide (widget);

	/* installed filter */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_installed_yes"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_installed_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_installed_no"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_installed_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_installed_both"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_installed_cb), NULL);

	/* devel filter */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_devel_yes"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_devel_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_devel_no"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_devel_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_devel_both"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_devel_cb), NULL);

	/* gui filter */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_gui_yes"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_gui_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_gui_no"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_gui_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_gui_both"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_gui_cb), NULL);

	/* free filter */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_free_yes"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_free_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_free_no"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_free_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_free_both"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_free_cb), NULL);

	/* source filter */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_source_yes"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_source_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_source_no"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_source_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_source_both"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_source_cb), NULL);

	/* basename filter */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_basename"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_basename_cb), NULL);

	/* newest filter */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_newest"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_newest_cb), NULL);

	/* arch filter */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_arch"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_arch_cb), NULL);

	/* supported filter */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "menuitem_supported"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_supported_cb), NULL);

	/* simple find button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_find"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_find_cb), NULL);
	/* TRANSLATORS: tooltip on the find button */
	gtk_widget_set_tooltip_text (widget, _("Find packages"));

	/* search cancel button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_cancel"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_cancel_cb), NULL);
	gtk_widget_set_sensitive (widget, FALSE);
	/* TRANSLATORS: tooltip on the cancel button */
	gtk_widget_set_tooltip_text (widget, _("Cancel search"));

	/* the fancy text entry widget */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_text"));

	/* autocompletion can be turned off as it's slow */
	ret = g_settings_get_boolean (settings, GPK_SETTINGS_AUTOCOMPLETE);
	if (ret) {
		/* create the completion object */
		completion = gpk_package_entry_completion_new ();
		gtk_entry_set_completion (GTK_ENTRY (widget), completion);
		g_object_unref (completion);
	}

	/* set focus on entry text */
	gtk_widget_grab_focus (widget);
	gtk_widget_show (widget);
	gtk_entry_set_icon_sensitive (GTK_ENTRY (widget), GTK_ENTRY_ICON_PRIMARY, TRUE);

	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_find_cb), NULL);
	g_signal_connect (widget, "paste-clipboard",
			  G_CALLBACK (gpk_application_find_cb), NULL);
	g_signal_connect (widget, "icon-press",
			  G_CALLBACK (gpk_application_entry_text_icon_press_cb), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_text"));
	g_signal_connect (GTK_EDITABLE (widget), "changed",
			  G_CALLBACK (gpk_application_text_changed_cb), NULL);

	/* mark find button insensitive */
	gpk_application_set_button_find_sensitivity (NULL);

	/* set a size, as much as the screen allows */
	ret = gpk_window_set_size_request (GTK_WINDOW (main_window), 1000, 1200);

	/* we are small form factor */
	if (!ret) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_packages"));
		gtk_box_set_homogeneous (GTK_BOX (widget), FALSE);
	}
	gtk_widget_show (GTK_WIDGET(main_window));

	/* set details box decent size */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_packages"));
	gtk_widget_set_size_request (widget, -1, 120);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_packages"));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));
	g_signal_connect (GTK_TREE_VIEW (widget), "row-activated",
			  G_CALLBACK (gpk_application_package_row_activated_cb), NULL);

	/* use a array store for the extra data */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_detail"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget), GTK_TREE_MODEL (details_store));

	/* add columns to the tree view */
	gpk_application_treeview_add_columns_description (NULL);

	/* sorted */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (packages_store),
					      PACKAGES_COLUMN_ID, GTK_SORT_ASCENDING);

	/* create package tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_packages"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (packages_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_application_packages_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	gpk_application_packages_add_columns (NULL);

	/* set up the groups checkbox */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_groups"));

	/* add columns to the tree view */
	gpk_application_groups_add_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (widget), GROUPS_COLUMN_SUMMARY);
	gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (widget), FALSE);
	gtk_tree_view_set_level_indentation  (GTK_TREE_VIEW (widget), 9);
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (groups_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_application_groups_treeview_changed_cb), NULL);

	/* get repos, so we can show the full name in the software source box */
	pk_client_get_repo_list_async (PK_CLIENT (task),
				       pk_bitfield_value (PK_FILTER_ENUM_NONE),
				       cancellable,
				       (PkProgressCallback) gpk_application_progress_cb, NULL,
				       (GAsyncReadyCallback) gpk_application_get_repo_list_cb, NULL);

	/* set current action */
	action = GPK_ACTION_NONE;
	gpk_application_set_buttons_apply_clear (NULL);

	/* hide details */
	gpk_application_clear_details (NULL);
out:
	/* welcome */
	gpk_application_add_welcome (NULL);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean program_version = FALSE;
	GOptionContext *context;
	GtkApplication *application;
	gboolean ret;
	gint status = 0;

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

	if (! g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();
	g_type_init ();
	gtk_init (&argc, &argv);

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("Add/Remove Software"));
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

	/* are we already activated? */
	application = gtk_application_new ("org.freedesktop.PackageKit.Application", 0);
	g_signal_connect (application, "startup",
			  G_CALLBACK (gpk_application_startup_cb), NULL);
	g_signal_connect (application, "activate",
			  G_CALLBACK (gpk_application_activate_cb), NULL);

	/* run */
	status = g_application_run (G_APPLICATION (application), argc, argv);
	g_object_unref (application);

	if (details_event_id > 0)
		g_source_remove (details_event_id);

	if (packages_store != NULL)
		g_object_unref (packages_store);
	if (details_store != NULL)
		g_object_unref (details_store);
	if (control != NULL)
		g_object_unref (control);
	if (task != NULL)
		g_object_unref (task);
	if (desktop != NULL)
		g_object_unref (desktop);
	if (settings != NULL)
		g_object_unref (settings);
	if (markdown != NULL)
		g_object_unref (markdown);
	if (builder != NULL)
		g_object_unref (builder);
	if (helper_run != NULL)
		g_object_unref (helper_run);
	if (cancellable != NULL)
		g_object_unref (cancellable);
	if (package_sack != NULL)
		g_object_unref (package_sack);
	if (repos != NULL)
		g_hash_table_destroy (repos);
	if (status_id > 0)
		g_source_remove (status_id);
	g_free (homepage_url);
	g_free (search_group);
	g_free (search_text);

	return status;
}

