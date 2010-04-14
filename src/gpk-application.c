/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
#include <glib.h>
#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <gconf/gconf-client.h>
#include <math.h>
#include <string.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-markdown.h"

#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-error.h"
#include "gpk-enum.h"
#include "gpk-application.h"
#include "gpk-animated-icon.h"
#include "gpk-dialog.h"
#include "gpk-cell-renderer-uri.h"
#include "gpk-desktop.h"
#include "gpk-helper-repo-signature.h"
#include "gpk-helper-eula.h"
#include "gpk-helper-run.h"
#include "gpk-helper-deps-remove.h"
#include "gpk-helper-deps-install.h"
#include "gpk-helper-media-change.h"

static void     gpk_application_finalize   (GObject	    *object);

#define GPK_APPLICATION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_APPLICATION, GpkApplicationPrivate))

typedef enum {
	PK_SEARCH_NAME,
	PK_SEARCH_DETAILS,
	PK_SEARCH_FILE,
	PK_SEARCH_UNKNOWN
} PkSearchType;

typedef enum {
	PK_MODE_NAME_DETAILS_FILE,
	PK_MODE_GROUP,
	PK_MODE_ALL_PACKAGES,
	PK_MODE_SELECTED,
	PK_MODE_UNKNOWN
} PkSearchMode;

typedef enum {
	PK_ACTION_NONE,
	PK_ACTION_INSTALL,
	PK_ACTION_REMOVE,
	PK_ACTION_UNKNOWN
} PkActionMode;

struct GpkApplicationPrivate
{
	GtkBuilder		*builder;
	GConfClient		*gconf_client;
	GtkListStore		*packages_store;
	GtkTreeStore		*groups_store;
	GtkListStore		*details_store;
	EggMarkdown		*markdown;
	PkControl		*control;
	PkClient		*client_primary;
	PkClient		*client_secondary;
	PkConnection		*pconnection;
	PkDesktop		*desktop;
	gchar			*group;
	gchar			*url;
	gchar			*search_text;
	guint			 details_event_id;
	GHashTable		*repos;
	PkBitfield		 roles;
	PkBitfield		 filters;
	PkBitfield		 groups;
	PkBitfield		 filters_current;
	gboolean		 has_package; /* if we got a package in the search */
	PkSearchType		 search_type;
	PkSearchMode		 search_mode;
	PkActionMode		 action;
	PkPackageList		*package_list;
	GtkWidget		*image_status;
	GpkHelperRepoSignature	*helper_repo_signature;
	GpkHelperEula		*helper_eula;
	GpkHelperRun		*helper_run;
	GpkHelperDepsRemove	*helper_deps_remove;
	GpkHelperDepsInstall	*helper_deps_install;
	GpkHelperMediaChange	*helper_media_change;
#if !PK_CHECK_VERSION(0,5,2)
	gboolean		 dep_check_info_only; /* bodge to tell apart the differing uses of GetDepends */
#endif
	guint			 status_id;
	PkStatusEnum		 status_last;
};

enum {
	GPK_STATE_INSTALLED,
	GPK_STATE_IN_LIST,
	GPK_STATE_COLLECTION,
	GPK_STATE_UNKNOWN
};

enum {
	ACTION_CLOSE,
	LAST_SIGNAL
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

static guint	     signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GpkApplication, gpk_application, G_TYPE_OBJECT)

static void gpk_application_categories_finished (GpkApplication *application);
static gboolean gpk_application_perform_search (GpkApplication *application);

/**
 * gpk_application_class_init:
 * @klass: This graph class instance
 **/
static void
gpk_application_class_init (GpkApplicationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_application_finalize;
	g_type_class_add_private (klass, sizeof (GpkApplicationPrivate));

	signals [ACTION_CLOSE] =
		g_signal_new ("action-close",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkApplicationClass, action_close),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * gpk_application_show:
 **/
void
gpk_application_show (GpkApplication *application)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (application->priv->builder, "window_manager"));
	gtk_window_present (window);
}

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

	/* installed or in list */
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
gpk_application_allow_install (GpkApplication *application, gboolean allow)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_install"));
	gtk_widget_set_sensitive (widget, allow);
}

/**
 * gpk_application_allow_remove:
 **/
static void
gpk_application_allow_remove (GpkApplication *application, gboolean allow)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_remove"));
	gtk_widget_set_sensitive (widget, allow);
}

/**
 * gpk_application_packages_checkbox_invert:
 **/
static void
gpk_application_packages_checkbox_invert (GpkApplication *application)
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
	PkPackageId *id;

	/* get the selection and add */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
	selection = gtk_tree_view_get_selection (treeview);
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret) {
		egg_warning ("no selection");
		return;
	}

	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_ID, &package_id,
			    PACKAGES_COLUMN_STATE, &state,
			    -1);

	/* do something with the value */
	pk_bitfield_invert (state, GPK_STATE_IN_LIST);

	/* use the application icon if not selected */
	if (!pk_bitfield_contain (state, GPK_STATE_IN_LIST)) {
		id = pk_package_id_new_from_string (package_id);
		icon = gpk_desktop_guess_icon_name (application->priv->desktop, id->name);
		pk_package_id_free (id);
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
gpk_application_get_checkbox_enable (GpkApplication *application, PkBitfield state)
{
	gboolean enable_installed = TRUE;
	gboolean enable_available = TRUE;

	if (application->priv->action == PK_ACTION_INSTALL)
		enable_installed = FALSE;
	else if (application->priv->action == PK_ACTION_REMOVE)
		enable_available = FALSE;

	if (pk_bitfield_contain (state, GPK_STATE_INSTALLED))
		return enable_installed;
	return enable_available;
}

/**
 * gpk_application_set_buttons_apply_clear:
 **/
static void
gpk_application_set_buttons_apply_clear (GpkApplication *application)
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

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* okay to apply? */
	len = PK_OBJ_LIST (application->priv->package_list)->len;
	if (len == 0) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_apply"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_clear"));
		gtk_widget_set_sensitive (widget, FALSE);
		application->priv->action = PK_ACTION_NONE;
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_apply"));
		gtk_widget_set_sensitive (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_clear"));
		gtk_widget_set_sensitive (widget, TRUE);
	}

	/* correct the enabled state */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* for all current items, reset the state if in the list */
	while (valid) {
		gtk_tree_model_get (model, &iter,
				    PACKAGES_COLUMN_STATE, &state,
				    PACKAGES_COLUMN_ID, &package_id,
				    -1);

		/* we never show the checkbox for the search helper */
		if (package_id == NULL) {
			enabled = FALSE;
		} else {
			enabled = gpk_application_get_checkbox_enable (application, state);
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
gpk_application_get_selected_package (GpkApplication *application, gchar **package_id, gchar **summary)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	gboolean ret;

	/* get the selection and add */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
	selection = gtk_tree_view_get_selection (treeview);
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret) {
		egg_warning ("no selection");
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
gpk_application_install (GpkApplication *application)
{
	gboolean ret;
	PkPackageId *id;
	gchar *package_id_selected = NULL;
	gchar *summary_selected = NULL;

	g_return_val_if_fail (GPK_IS_APPLICATION (application), FALSE);

	/* get selection */
	ret = gpk_application_get_selected_package (application, &package_id_selected, &summary_selected);
	if (!ret) {
		egg_warning ("no package selected");
		goto out;
	}

	/* changed mind, or wrong mode */
	if (application->priv->action == PK_ACTION_REMOVE) {
		ret = pk_package_list_contains (application->priv->package_list, package_id_selected);
		if (ret) {
			egg_debug ("removed %s from package list", package_id_selected);
			pk_package_list_remove (application->priv->package_list, package_id_selected);

			/* correct buttons */
			gpk_application_allow_install (application, FALSE);
			gpk_application_allow_remove (application, TRUE);
			gpk_application_packages_checkbox_invert (application);
			gpk_application_set_buttons_apply_clear (application);
			return TRUE;
		}
		egg_warning ("wrong mode and not in list");
		return FALSE;
	}

	/* already added */
	ret = !pk_package_list_contains (application->priv->package_list, package_id_selected);
	if (!ret) {
		egg_warning ("already added");
		goto out;
	}

	/* set mode */
	application->priv->action = PK_ACTION_INSTALL;

	/* add to list */
	id = pk_package_id_new_from_string (package_id_selected);
	pk_package_list_add (application->priv->package_list, PK_INFO_ENUM_AVAILABLE, id, summary_selected);
	pk_package_id_free (id);

	/* correct buttons */
	gpk_application_allow_install (application, FALSE);
	gpk_application_allow_remove (application, TRUE);
	gpk_application_packages_checkbox_invert (application);
	gpk_application_set_buttons_apply_clear (application);
out:
	g_free (package_id_selected);
	g_free (summary_selected);
	return ret;
}

/**
 * gpk_application_menu_homepage_cb:
 **/
static void
gpk_application_menu_homepage_cb (GtkAction *action, GpkApplication *application)
{
	g_return_if_fail (GPK_IS_APPLICATION (application));
	gpk_gnome_open (application->priv->url);
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
 * gpk_application_menu_files_cb:
 **/
static void
gpk_application_menu_files_cb (GtkAction *action, GpkApplication *application)
{
	gboolean ret;
	GError *error = NULL;
	gchar **package_ids = NULL;
	gchar *package_id_selected = NULL;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* get selection */
	ret = gpk_application_get_selected_package (application, &package_id_selected, NULL);
	if (!ret) {
		egg_warning ("no package selected");
		goto out;
	}

	/* reset client */
	ret = pk_client_reset (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set correct view */
	package_ids = pk_package_ids_from_id (package_id_selected);
	ret = pk_client_get_files (application->priv->client_primary, package_ids, &error);
	if (!ret) {
		egg_warning ("cannot get file lists for %s: %s", package_id_selected, error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (package_id_selected);
	g_strfreev (package_ids);
}

/**
 * gpk_application_remove:
 **/
static gboolean
gpk_application_remove (GpkApplication *application)
{
	gboolean ret;
	PkPackageId *id;
	gchar *package_id_selected = NULL;
	gchar *summary_selected = NULL;

	g_return_val_if_fail (GPK_IS_APPLICATION (application), FALSE);

	/* get selection */
	ret = gpk_application_get_selected_package (application, &package_id_selected, &summary_selected);
	if (!ret) {
		egg_warning ("no package selected");
		goto out;
	}

	/* changed mind, or wrong mode */
	if (application->priv->action == PK_ACTION_INSTALL) {
		ret = pk_package_list_contains (application->priv->package_list, package_id_selected);
		if (ret) {
			egg_debug ("removed %s from package list", package_id_selected);
			pk_package_list_remove (application->priv->package_list, package_id_selected);

			/* correct buttons */
			gpk_application_allow_install (application, TRUE);
			gpk_application_allow_remove (application, FALSE);
			gpk_application_packages_checkbox_invert (application);
			gpk_application_set_buttons_apply_clear (application);
			return TRUE;
		}
		egg_warning ("wrong mode and not in list");
		return FALSE;
	}

	/* already added */
	ret = !pk_package_list_contains (application->priv->package_list, package_id_selected);
	if (!ret) {
		egg_warning ("already added");
		goto out;
	}

	application->priv->action = PK_ACTION_REMOVE;
	id = pk_package_id_new_from_string (package_id_selected);
	pk_package_list_add (application->priv->package_list, PK_INFO_ENUM_AVAILABLE, id, summary_selected);
	pk_package_id_free (id);

	/* correct buttons */
	gpk_application_allow_install (application, TRUE);
	gpk_application_allow_remove (application, FALSE);
	gpk_application_packages_checkbox_invert (application);
	gpk_application_set_buttons_apply_clear (application);
out:
	g_free (package_id_selected);
	g_free (summary_selected);
	return TRUE;
}

/**
 * gpk_application_menu_install_cb:
 **/
static void
gpk_application_menu_install_cb (GtkAction *action, GpkApplication *application)
{
	gpk_application_install (application);
}

/**
 * gpk_application_menu_remove_cb:
 **/
static void
gpk_application_menu_remove_cb (GtkAction *action, GpkApplication *application)
{
	gpk_application_remove (application);
}

/**
 * gpk_application_menu_run_cb:
 **/
static void
gpk_application_menu_run_cb (GtkAction *action, GpkApplication *application)
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
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
	selection = gtk_tree_view_get_selection (treeview);
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret) {
		egg_warning ("no selection");
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
		gpk_helper_run_show (application->priv->helper_run, package_ids);
		g_strfreev (package_ids);
	}
	g_free (package_id);
}

/**
 * gpk_application_menu_requires_cb:
 **/
static void
gpk_application_menu_requires_cb (GtkAction *action, GpkApplication *application)
{
	GError *error = NULL;
	gboolean ret;
	gchar **package_ids = NULL;
	gchar *package_id_selected = NULL;

	/* get selection */
	ret = gpk_application_get_selected_package (application, &package_id_selected, NULL);
	if (!ret) {
		egg_warning ("no package selected");
		goto out;
	}

	/* cancel any previous request */
	ret = pk_client_reset (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("failed to cancel, and adding to queue: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get the requires */
	package_ids = pk_package_ids_from_id (package_id_selected);
#if !PK_CHECK_VERSION(0,5,2)
	application->priv->dep_check_info_only = TRUE;
#endif
	ret = pk_client_get_requires (application->priv->client_primary, PK_FILTER_ENUM_NONE,
				      package_ids, TRUE, &error);
	if (!ret) {
		egg_warning ("failed to get requires: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (package_id_selected);
	g_strfreev (package_ids);
}

/**
 * gpk_application_menu_depends_cb:
 **/
static void
gpk_application_menu_depends_cb (GtkAction *action, GpkApplication *application)
{
	GError *error = NULL;
	gboolean ret;
	gchar **package_ids = NULL;
	gchar *package_id_selected = NULL;

	/* get selection */
	ret = gpk_application_get_selected_package (application, &package_id_selected, NULL);
	if (!ret) {
		egg_warning ("no package selected");
		goto out;
	}

	/* cancel any previous request */
	ret = pk_client_reset (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("failed to cancel, and adding to queue: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get the depends */
	package_ids = pk_package_ids_from_id (package_id_selected);
#if !PK_CHECK_VERSION(0,5,2)
	application->priv->dep_check_info_only = TRUE;
#endif
	ret = pk_client_get_depends (application->priv->client_primary, PK_FILTER_ENUM_NONE,
				     package_ids, TRUE, &error);
	if (!ret) {
		egg_warning ("failed to get depends: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (package_id_selected);
	g_strfreev (package_ids);
}

/**
 * gpk_application_get_full_repo_name:
 **/
static const gchar *
gpk_application_get_full_repo_name (GpkApplication *application, const gchar *data)
{
	const gchar *repo_name;

	/* if no data, we can't look up in the hash table */
	if (egg_strzero (data)) {
		egg_warning ("no ident data");
		/* TRANSLATORS: the repo name is invalid or not found, fall back to this */
		return _("Invalid");
	}

	/* try to find in cached repo list */
	repo_name = (const gchar *) g_hash_table_lookup (application->priv->repos, data);
	if (repo_name == NULL) {
		egg_warning ("no repo name, falling back to %s", data);
		return data;
	}
	return repo_name;
}

/**
 * gpk_application_add_detail_item:
 **/
static void
gpk_application_add_detail_item (GpkApplication *application, const gchar *title, const gchar *text, const gchar *uri)
{
	gchar *markup;
	GtkTreeView *treeview;
	GtkTreeIter iter;
	GtkTreeSelection *selection;

	/* we don't need to clear anymore */
	if (application->priv->details_event_id > 0) {
		g_source_remove (application->priv->details_event_id);
		application->priv->details_event_id = 0;
	}

	/* format */
	markup = g_strdup_printf ("<b>%s:</b>", title);

	egg_debug ("%s %s %s", markup, text, uri);
	gtk_list_store_append (application->priv->details_store, &iter);
	gtk_list_store_set (application->priv->details_store, &iter,
			    DETAIL_COLUMN_TITLE, markup,
			    DETAIL_COLUMN_TEXT, text,
			    DETAIL_COLUMN_URI, uri,
			    -1);

	g_free (markup);

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (application->priv->builder, "treeview_detail"));
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_NONE);
	gtk_tree_view_columns_autosize (treeview);
}

/**
 * gpk_application_clear_details_really:
 **/
static gboolean
gpk_application_clear_details_really (GpkApplication *application)
{
	GtkWidget *widget;

	/* hide details */
	gtk_list_store_clear (application->priv->details_store);

	/* clear the old text */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "textview_description"));
	gpk_application_set_text_buffer (widget, NULL);

	/* hide dead widgets */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "scrolledwindow_detail"));
	gtk_widget_hide (widget);

	/* never repeat */
	return FALSE;
}

/**
 * gpk_application_clear_details:
 **/
static void
gpk_application_clear_details (GpkApplication *application)
{
	/* only clear the last data if it takes a little while, else we flicker the display */
	if (application->priv->details_event_id > 0)
		g_source_remove (application->priv->details_event_id);
	application->priv->details_event_id = g_timeout_add (200, (GSourceFunc) gpk_application_clear_details_really, application);
}

/**
 * gpk_application_clear_packages:
 **/
static void
gpk_application_clear_packages (GpkApplication *application)
{
	/* clear existing list */
	gtk_list_store_clear (application->priv->packages_store);
	application->priv->has_package = FALSE;
}

/**
 * gpk_application_text_format_display:
 **/
static gchar *
gpk_application_text_format_display (GpkApplication *application, const gchar *ascii)
{
	gchar *text;
	egg_markdown_set_output (application->priv->markdown, EGG_MARKDOWN_OUTPUT_TEXT);
	text = egg_markdown_parse (application->priv->markdown, ascii);
	return text;
}

/**
 * gpk_application_details_cb:
 **/
static void
gpk_application_details_cb (PkClient *client, PkDetailsObj *details, GpkApplication *application)
{
	GtkWidget *widget;
	gchar *text;
	gchar *value;
	const gchar *repo_name;
	const gchar *group;
	gboolean installed;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	installed = g_strcmp0 (details->id->data, "installed") == 0;

	/* hide to start */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "scrolledwindow_detail"));
	gtk_widget_show (widget);

	gtk_list_store_clear (application->priv->details_store);

	/* if a collection, mark as such */
	if (g_strcmp0 (details->id->data, "meta") == 0)
		/* TRANSLATORS: the type of package is a collection (metagroup) */
		gpk_application_add_detail_item (application, _("Type"), _("Collection"), NULL);

	/* homepage */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_homepage"));
	if (egg_strzero (details->url) == FALSE) {
		gtk_widget_set_sensitive (widget, TRUE);

		/* TRANSLATORS: tooltip: go to the web address */
		text = g_strdup_printf (_("Visit %s"), details->url);
		gtk_widget_set_tooltip_text (widget, text);
		g_free (text);

		/* TRANSLATORS: add an entry to go to the project home page */
		gpk_application_add_detail_item (application, _("Project"), _("Homepage"), details->url);

		/* save the url for the button */
		g_free (application->priv->url);
		application->priv->url = g_strdup (details->url);

	} else {
		gtk_widget_set_sensitive (widget, FALSE);
	}

	/* group */
	if (details->group != PK_GROUP_ENUM_UNKNOWN) {
		group = gpk_group_enum_to_localised_text (details->group);
		/* TRANSLATORS: the group the package belongs in */
		gpk_application_add_detail_item (application, _("Group"), group, NULL);
	}

	/* group */
	if (!egg_strzero (details->license)) {
		/* TRANSLATORS: the licence string for the package */
		gpk_application_add_detail_item (application, _("License"), details->license, NULL);
	}

	/* menu path */
	value = gpk_desktop_guess_best_file (application->priv->desktop, details->id->name);
	if (value != NULL) {
		text = gpk_desktop_get_menu_path (value);
		if (text != NULL) {
			/* TRANSLATORS: the path in the menu, e.g. Applications -> Games */
			gpk_application_add_detail_item (application, _("Menu"), text, NULL);
		}
		g_free (text);
	}
	g_free (value);

	/* set the description */
	text = gpk_application_text_format_display (application, details->description);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "textview_description"));
	gpk_application_set_text_buffer (widget, text);
	g_free (text);

	/* if non-zero, set the size */
	if (details->size > 0) {
		/* set the size */
		value = g_format_size_for_display (details->size);
		if (g_strcmp0 (details->id->data, "meta") == 0)
			/* TRANSLATORS: the size of the meta package */
			gpk_application_add_detail_item (application, _("Size"), value, NULL);
		else if (installed)
			/* TRANSLATORS: the installed size in bytes of the package */
			gpk_application_add_detail_item (application, _("Installed size"), value, NULL);
		else
			/* TRANSLATORS: the download size of the package */
			gpk_application_add_detail_item (application, _("Download size"), value, NULL);
		g_free (value);
	}

	/* set the repo text, or hide if installed */
	if (!installed && g_strcmp0 (details->id->data, "meta") != 0) {
		/* get the full name of the repo from the repo_id */
		repo_name = gpk_application_get_full_repo_name (application, details->id->data);
		/* TRANSLATORS: where the package came from, the software source name */
		gpk_application_add_detail_item (application, _("Source"), repo_name, NULL);
	}
}

/**
 * gpk_application_add_obj_to_results:
 **/
static void
gpk_application_add_obj_to_results (GpkApplication *application, const PkPackageObj *obj)
{
	GtkTreeIter iter;
	gchar *summary;
	const gchar *icon = NULL;
	gchar *text;
	gchar *package_id;
	gboolean in_queue;
	gboolean installed;
	gboolean checkbox;
	gboolean enabled;
	PkBitfield state = 0;
	static guint package_cnt = 0;

	/* format if required */
	egg_markdown_set_output (application->priv->markdown, EGG_MARKDOWN_OUTPUT_PANGO);
	summary = egg_markdown_parse (application->priv->markdown, obj->summary);
	package_id = pk_package_id_to_string (obj->id);

	/* mark as got so we don't warn */
	application->priv->has_package = TRUE;

	/* are we in the package list? */
	in_queue = pk_package_list_contains (application->priv->package_list, package_id);
	installed = (obj->info == PK_INFO_ENUM_INSTALLED) || (obj->info == PK_INFO_ENUM_COLLECTION_INSTALLED);

	if (installed)
		pk_bitfield_add (state, GPK_STATE_INSTALLED);
	if (in_queue)
		pk_bitfield_add (state, GPK_STATE_IN_LIST);

	/* special icon */
	if (obj->info == PK_INFO_ENUM_COLLECTION_INSTALLED || obj->info == PK_INFO_ENUM_COLLECTION_AVAILABLE)
		pk_bitfield_add (state, GPK_STATE_COLLECTION);

	/* use the application icon if available */
	icon = gpk_desktop_guess_icon_name (application->priv->desktop, obj->id->name);
	if (icon == NULL)
		icon = gpk_application_state_get_icon (state);

	checkbox = gpk_application_state_get_checkbox (state);

	/* use two lines */
	text = gpk_package_id_format_twoline (obj->id, summary);

	/* can we modify this? */
	enabled = gpk_application_get_checkbox_enable (application, state);

	gtk_list_store_append (application->priv->packages_store, &iter);
	gtk_list_store_set (application->priv->packages_store, &iter,
			    PACKAGES_COLUMN_STATE, state,
			    PACKAGES_COLUMN_CHECKBOX, installed ^ in_queue,
			    PACKAGES_COLUMN_CHECKBOX_VISIBLE, enabled,
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_SUMMARY, obj->summary,
			    PACKAGES_COLUMN_ID, package_id,
			    PACKAGES_COLUMN_IMAGE, icon,
			    -1);

	/* only process every n events else we re-order too many times */
	if (package_cnt++ % 200 == 0) {
		while (gtk_events_pending ())
			gtk_main_iteration ();
	}

	g_free (package_id);
	g_free (summary);
	g_free (text);
}

/**
 * gpk_application_package_cb:
 **/
static void
gpk_application_package_cb (PkClient *client, const PkPackageObj *obj, GpkApplication *application)
{
	PkRoleEnum role;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	egg_debug ("package = %s:%s:%s", pk_info_enum_to_text (obj->info), obj->id->name, obj->summary);

	/* ignore not search data */
#if PK_CHECK_VERSION(0,5,1)
	g_object_get (client,
		      "role", &role,
		      NULL);
#else
	pk_client_get_role (client, &role, NULL, NULL);
#endif
	if (role == PK_ROLE_ENUM_GET_DEPENDS ||
	    role == PK_ROLE_ENUM_GET_REQUIRES ||
	    role == PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES)
		return;

	/* ignore progress */
	if (obj->info != PK_INFO_ENUM_INSTALLED && obj->info != PK_INFO_ENUM_AVAILABLE &&
	    obj->info != PK_INFO_ENUM_COLLECTION_INSTALLED && obj->info != PK_INFO_ENUM_COLLECTION_AVAILABLE)
		return;

	/* add to list */
	gpk_application_add_obj_to_results (application, obj);
}

/**
 * gpk_application_error_code_cb:
 **/
static void
gpk_application_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, GpkApplication *application)
{
	GtkWindow *window;
	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* obvious message, don't tell the user */
	if (code == PK_ERROR_ENUM_TRANSACTION_CANCELLED)
		return;

	/* ignore the ones we can handle */
	if (code == PK_ERROR_ENUM_NO_LICENSE_AGREEMENT ||
	    code == PK_ERROR_ENUM_MEDIA_CHANGE_REQUIRED ||
	    pk_error_code_is_need_untrusted (code)) {
		egg_debug ("error ignored as we're handling %s\n%s", pk_error_enum_to_text (code), details);
		return;
	}

	window = GTK_WINDOW (gtk_builder_get_object (application->priv->builder, "window_manager"));
	gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (code),
				gpk_error_enum_to_localised_message (code), details);
}

/**
 * gpk_application_suggest_better_search:
 **/
static void
gpk_application_suggest_better_search (GpkApplication *application)
{
	const gchar *message = NULL;
	/* TRANSLATORS: no results were found for this search */
	const gchar *title = _("No results were found.");
	GtkTreeIter iter;
	gchar *text;
	PkBitfield state = 0;

	if (application->priv->search_mode == PK_MODE_GROUP ||
	    application->priv->search_mode == PK_MODE_ALL_PACKAGES) {
		/* TRANSLATORS: be helpful, but this shouldn't happen */
		message = _("Try entering a package name in the search bar.");
	}  else if (application->priv->search_mode == PK_MODE_SELECTED) {
		/* TRANSLATORS: nothing in the package queue */
		message = _("There are no packages queued to be installed or removed.");
	} else {
		if (application->priv->search_type == PK_SEARCH_NAME ||
		    application->priv->search_type == PK_SEARCH_FILE)
			/* TRANSLATORS: tell the user to switch to details search mode */
			message = _("Try searching package descriptions by clicking the icon next to the search text.");
		else
			/* TRANSLATORS: tell the user to try harder */
			message = _("Try again with a different search term.");
	}

	text = g_strdup_printf ("%s\n%s", title, message);
	gtk_list_store_append (application->priv->packages_store, &iter);
	gtk_list_store_set (application->priv->packages_store, &iter,
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
 * gpk_application_finished_get_depends:
 **/
static void
gpk_application_finished_get_depends (GpkApplication *application, PkPackageList *list)
{
	GtkWindow *window;
	gchar *name = NULL;
	gchar *title = NULL;
	gchar *message = NULL;
	gchar **package_ids = NULL;
	guint length;
	GtkWidget *dialog;
	gchar *package_id_selected = NULL;
	gboolean ret;

	/* get selection */
	ret = gpk_application_get_selected_package (application, &package_id_selected, NULL);
	if (!ret) {
		egg_warning ("no package selected");
		goto out;
	}

	/* empty list */
	window = GTK_WINDOW (gtk_builder_get_object (application->priv->builder, "window_manager"));
	if (pk_package_list_get_size (list) == 0) {
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: no packages returned */
					_("No packages"),
					/* TRANSLATORS: this package does not depend on any others */
					_("This package does not depends on any others"), NULL);
		goto out;
	}

	length = pk_package_list_get_size (list);
	package_ids = pk_package_ids_from_id (package_id_selected);
	name = gpk_dialog_package_id_name_join_locale (package_ids);
	/* TRANSLATORS: title: show the number of other packages we depend on */
	title = g_strdup_printf (ngettext ("%i additional package is required for %s",
					   "%i additional packages are required for %s",
					   length), length, name);

	/* TRANSLATORS: message: show the list of dependant packages for this package */
	message = g_strdup_printf (ngettext ("Packages listed below are required for %s to function correctly.",
					     "Packages listed below are required for %s to function correctly.",
					     length), name);

	dialog = gtk_message_dialog_new (window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gpk_dialog_embed_package_list_widget (GTK_DIALOG (dialog), list);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
out:
	g_free (package_id_selected);
	g_strfreev (package_ids);
	g_free (name);
	g_free (title);
	g_free (message);
}

/**
 * gpk_application_finished_get_requires:
 **/
static void
gpk_application_finished_get_requires (GpkApplication *application, PkPackageList *list)
{
	GtkWindow *window;
	gchar *name = NULL;
	gchar *title = NULL;
	gchar *message = NULL;
	gchar **package_ids = NULL;
	guint length;
	GtkWidget *dialog;
	gchar *package_id_selected = NULL;
	gboolean ret;

	/* get selection */
	ret = gpk_application_get_selected_package (application, &package_id_selected, NULL);
	if (!ret) {
		egg_warning ("no package selected");
		goto out;
	}

	/* empty list */
	window = GTK_WINDOW (gtk_builder_get_object (application->priv->builder, "window_manager"));
	if (pk_package_list_get_size (list) == 0) {
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: no packages returned */
					_("No packages"),
					/* TRANSLATORS: this package is not required by any others */
					_("No other packages require this package"), NULL);
		goto out;
	}

	length = pk_package_list_get_size (list);
	package_ids = pk_package_ids_from_id (package_id_selected);
	name = gpk_dialog_package_id_name_join_locale (package_ids);
	/* TRANSLATORS: title: how many packages require this package */
	title = g_strdup_printf (ngettext ("%i package requires %s",
					   "%i packages require %s",
					   length), length, name);

	/* TRANSLATORS: show a list of packages for the package */
	message = g_strdup_printf (ngettext ("Packages listed below require %s to function correctly.",
					     "Packages listed below require %s to function correctly.",
					     length), name);

	dialog = gtk_message_dialog_new (window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gpk_dialog_embed_package_list_widget (GTK_DIALOG (dialog), list);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
out:
	g_free (package_id_selected);
	g_strfreev (package_ids);
	g_free (name);
	g_free (title);
	g_free (message);
}

/**
 * gpk_application_perform_search_idle_cb:
 **/
static gboolean
gpk_application_perform_search_idle_cb (GpkApplication *application)
{
	gpk_application_perform_search (application);
	return FALSE;
}

/**
 * gpk_application_primary_requeue:
 **/
static gboolean
gpk_application_primary_requeue (GpkApplication *application)
{
	gboolean ret;
	GError *error = NULL;

	/* retry new action */
	ret = pk_client_requeue (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("Failed to requeue: %s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * gpk_application_select_exact_match:
 *
 * NOTE: we have to do this in the finished_cb, as if we do this as we return
 * results we cancel the search and start getting the package details.
 **/
static void
gpk_application_select_exact_match (GpkApplication *application, const gchar *text)
{
	GtkTreeView *treeview;
	gboolean valid;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeSelection *selection = NULL;
	gchar *package_id;
	PkPackageId *id;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* get the first iter in the list */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* for all items in treeview */
	while (valid) {
		gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_ID, &package_id, -1);
		if (package_id != NULL) {

			/* exact match, so select and scroll */
			id = pk_package_id_new_from_string (package_id);
			if (g_strcmp0 (id->name, text) == 0) {
				selection = gtk_tree_view_get_selection (treeview);
				gtk_tree_selection_select_iter (selection, &iter);
				path = gtk_tree_model_get_path (model, &iter);
				gtk_tree_view_scroll_to_cell (treeview, path, NULL, FALSE, 0.5f, 0.5f);
				gtk_tree_path_free (path);
			}
			pk_package_id_free (id);

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
gpk_application_run_installed (GpkApplication *application)
{
	guint i;
	guint len;
	PkPackageList *list;
	const PkPackageObj *obj;
	GPtrArray *array;
	gchar **package_ids = NULL;

	/* get the package list and filter on INSTALLED */
	array = g_ptr_array_new ();
	list = pk_client_get_package_list (application->priv->client_primary);
	len = PK_OBJ_LIST (list)->len;
	for (i=0; i<len; i++) {
		obj = pk_package_list_get_obj (list, i);
		if (obj->info == PK_INFO_ENUM_INSTALLING)
			g_ptr_array_add (array, pk_package_id_to_string (obj->id));
	}

	/* nothing to show */
	if (array->len == 0) {
		egg_debug ("nothing to do");
		goto out;
	}

	/* this is async */
	package_ids = pk_package_ids_from_array (array);
	gpk_helper_run_show (application->priv->helper_run, package_ids);

out:
	g_strfreev (package_ids);
	g_object_unref (list);
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);
}

/**
 * gpk_application_finished_cb:
 **/
static void
gpk_application_finished_cb (PkClient *client, PkExitEnum exit_enum, guint runtime, GpkApplication *application)
{
	GtkWidget *widget;
	PkRoleEnum role;
	PkPackageList *list;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* get role */
#if PK_CHECK_VERSION(0,5,1)
	g_object_get (client,
		      "role", &role,
		      NULL);
#else
	pk_client_get_role (client, &role, NULL, NULL);
#endif
	egg_debug ("role: %s, exit: %s", pk_role_enum_to_text (role), pk_exit_enum_to_text (exit_enum));

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "progressbar_progress"));
	gtk_widget_hide (widget);

	/* reset UI */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_groups"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "textview_description"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_detail"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "entry_text"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_apply"));
	gtk_widget_set_sensitive (widget, TRUE);
	gpk_application_set_buttons_apply_clear (application);

	/* need to handle retry with only_trusted=FALSE */
	if (client == application->priv->client_primary &&
	    exit_enum == PK_EXIT_ENUM_NEED_UNTRUSTED) {
		egg_debug ("need to handle untrusted");
		pk_client_set_only_trusted (client, FALSE);
		gpk_application_primary_requeue (application);
		return;
	}

	/* if secondary, ignore */
	if (client == application->priv->client_primary &&
	    (exit_enum == PK_EXIT_ENUM_KEY_REQUIRED ||
	     exit_enum == PK_EXIT_ENUM_EULA_REQUIRED)) {
		egg_debug ("ignoring primary sig-required or eula");
		return;
	}

	if (role == PK_ROLE_ENUM_GET_CATEGORIES) {
		/* get complex group list */
		gpk_application_categories_finished (application);
	}

#if PK_CHECK_VERSION(0,5,2)
	/* simulating */
	if (role == PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES &&
	    exit_enum == PK_EXIT_ENUM_SUCCESS) {
		list = pk_client_get_package_list (application->priv->client_primary);
		gpk_helper_deps_install_show (application->priv->helper_deps_install, application->priv->package_list, list);
		g_object_unref (list);
	}

	/* get reqs */
	if (role == PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES &&
	    exit_enum == PK_EXIT_ENUM_SUCCESS) {
		list = pk_client_get_package_list (application->priv->client_primary);
		gpk_helper_deps_remove_show (application->priv->helper_deps_remove, application->priv->package_list, list);
		g_object_unref (list);
	}

	/* get deps */
	if (role == PK_ROLE_ENUM_GET_DEPENDS &&
	    exit_enum == PK_EXIT_ENUM_SUCCESS) {
		list = pk_client_get_package_list (application->priv->client_primary);
		gpk_application_finished_get_depends (application, list);
		g_object_unref (list);
	}

	/* get reqs */
	if (role == PK_ROLE_ENUM_GET_REQUIRES &&
	    exit_enum == PK_EXIT_ENUM_SUCCESS) {
		list = pk_client_get_package_list (application->priv->client_primary);
		gpk_application_finished_get_requires (application, list);
		g_object_unref (list);
	}
#else
	/* get deps */
	if (role == PK_ROLE_ENUM_GET_DEPENDS &&
	    exit_enum == PK_EXIT_ENUM_SUCCESS) {
		list = pk_client_get_package_list (application->priv->client_primary);
		if (application->priv->dep_check_info_only)
			gpk_application_finished_get_depends (application, list);
		else
			gpk_helper_deps_install_show (application->priv->helper_deps_install, application->priv->package_list, list);
		g_object_unref (list);
	}

	/* get reqs */
	if (role == PK_ROLE_ENUM_GET_REQUIRES &&
	    exit_enum == PK_EXIT_ENUM_SUCCESS) {
		list = pk_client_get_package_list (application->priv->client_primary);
		if (application->priv->dep_check_info_only)
			gpk_application_finished_get_requires (application, list);
		else
			gpk_helper_deps_remove_show (application->priv->helper_deps_remove, application->priv->package_list, list);
		g_object_unref (list);
	}
#endif

	/* we've just agreed to auth or a EULA */
	if (role == PK_ROLE_ENUM_INSTALL_SIGNATURE ||
	    role == PK_ROLE_ENUM_ACCEPT_EULA) {
		if (exit_enum == PK_EXIT_ENUM_SUCCESS)
			gpk_application_primary_requeue (application);
	}

	/* do we need to update the search? */
	if (role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		/* refresh the search as the items may have changed and the filter has not changed */
		if (exit_enum == PK_EXIT_ENUM_SUCCESS) {
			/* idle add in the background */
			g_idle_add ((GSourceFunc) gpk_application_perform_search_idle_cb, application);

			/* find applications that were installed, and offer to run them */
			gpk_application_run_installed (application);

			/* clear if success */
			pk_obj_list_clear (PK_OBJ_LIST (application->priv->package_list));
			application->priv->action = PK_ACTION_NONE;
			gpk_application_set_buttons_apply_clear (application);
		}
	}

	/* we've just agreed to auth or a EULA */
	if (role == PK_ROLE_ENUM_INSTALL_SIGNATURE ||
	    role == PK_ROLE_ENUM_ACCEPT_EULA) {
		if (exit_enum == PK_EXIT_ENUM_SUCCESS)
			gpk_application_primary_requeue (application);
	}

	if (role == PK_ROLE_ENUM_SEARCH_NAME ||
	    role == PK_ROLE_ENUM_SEARCH_DETAILS ||
	    role == PK_ROLE_ENUM_SEARCH_GROUP ||
	    role == PK_ROLE_ENUM_GET_PACKAGES) {

		/* were there no entries found? */
		if (exit_enum == PK_EXIT_ENUM_SUCCESS && !application->priv->has_package) {
			gpk_application_suggest_better_search (application);
		}

		/* if there is an exact match, select it */
		gpk_application_select_exact_match (application, application->priv->search_text);

		/* focus back to the text extry */
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "entry_text"));
		gtk_widget_grab_focus (widget);
	}
}

/**
 * gpk_application_cancel_cb:
 **/
static void
gpk_application_cancel_cb (GtkWidget *button_widget, GpkApplication *application)
{
	gboolean ret;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	ret = pk_client_cancel (application->priv->client_primary, NULL);
	egg_debug ("canceled? %i", ret);

	/* switch buttons around */
	if (ret) {
		application->priv->search_mode = PK_MODE_UNKNOWN;
	}
}

/**
 * gpk_application_perform_search_name_details_file:
 **/
static gboolean
gpk_application_perform_search_name_details_file (GpkApplication *application)
{
	GtkEntry *entry;
	GtkWindow *window;
	GError *error = NULL;
	gboolean ret;

	entry = GTK_ENTRY (gtk_builder_get_object (application->priv->builder, "entry_text"));
	g_free (application->priv->search_text);
	application->priv->search_text = g_strdup (gtk_entry_get_text (entry));

	/* have we got input? */
	if (egg_strzero (application->priv->search_text)) {
		egg_debug ("no input");
		return FALSE;
	}

	ret = pk_strvalidate (application->priv->search_text);
	if (!ret) {
		egg_debug ("invalid input text, will fail");
		/* TODO - make the dialog turn red... */
		window = GTK_WINDOW (gtk_builder_get_object (application->priv->builder, "window_manager"));
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: title: invlid text in the search bar */
					_("Invalid search text"),
					/* TRANSLATORS: message: tell the user that's not allowed */
					_("The search text contains invalid characters"), NULL);
		return FALSE;
	}
	egg_debug ("find %s", application->priv->search_text);

	/* reset */
	ret = pk_client_reset (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* do the search */
	if (application->priv->search_type == PK_SEARCH_NAME) {
		ret = pk_client_search_name (application->priv->client_primary,
					     application->priv->filters_current,
					     application->priv->search_text, &error);
	} else if (application->priv->search_type == PK_SEARCH_DETAILS) {
		ret = pk_client_search_details (application->priv->client_primary,
					     application->priv->filters_current,
					     application->priv->search_text, &error);
	} else if (application->priv->search_type == PK_SEARCH_FILE) {
		ret = pk_client_search_file (application->priv->client_primary,
					     application->priv->filters_current,
					     application->priv->search_text, &error);
	} else {
		egg_warning ("invalid search type");
		return FALSE;
	}

	if (!ret) {
		window = GTK_WINDOW (gtk_builder_get_object (application->priv->builder, "window_manager"));
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: title: we failed to execute the mthod */
					_("The search could not be completed"),
					/* TRANSLATORS: low level failure, details to follow */
					_("Running the transaction failed"), error->message);
		g_error_free (error);
		return FALSE;
	}

	return TRUE;
}

/**
 * gpk_application_perform_search_others:
 **/
static gboolean
gpk_application_perform_search_others (GpkApplication *application)
{
	gboolean ret;
	GtkWindow *window;
	GError *error = NULL;

	g_return_val_if_fail (GPK_IS_APPLICATION (application), FALSE);
	g_return_val_if_fail (application->priv->group != NULL, FALSE);

	/* cancel this, we don't care about old results that are pending */
	ret = pk_client_reset (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	if (application->priv->search_mode == PK_MODE_GROUP) {
		ret = pk_client_search_group (application->priv->client_primary,
					      application->priv->filters_current,
					      application->priv->group, &error);
	} else {
		ret = pk_client_get_packages (application->priv->client_primary,
					      application->priv->filters_current, &error);
	}

	if (!ret) {
		window = GTK_WINDOW (gtk_builder_get_object (application->priv->builder, "window_manager"));
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: title: could not get group data */
					_("The group could not be queried"),
					/* TRANSLATORS: low level failure */
					_("Running the transaction failed"), error->message);
		g_error_free (error);
		return FALSE;
	}

	return TRUE;
}

/**
 * gpk_application_populate_selected:
 **/
static gboolean
gpk_application_populate_selected (GpkApplication *application)
{
	guint i;
	guint len;
	PkPackageList *list;
	const PkPackageObj *obj;

	list = application->priv->package_list;
	len = PK_OBJ_LIST (list)->len;

	/* nothing in queue */
	if (len == 0) {
		gpk_application_suggest_better_search (application);
		goto out;
	}

	/* dump queue to package window */
	for (i=0; i<len; i++) {
		obj = pk_package_list_get_obj (list, i);
		gpk_application_add_obj_to_results (application, obj);
	}
out:
	return TRUE;
}

/**
 * gpk_application_perform_search:
 **/
static gboolean
gpk_application_perform_search (GpkApplication *application)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (GPK_IS_APPLICATION (application), FALSE);

	gpk_application_clear_details (application);
	gpk_application_clear_packages (application);

	if (application->priv->search_mode == PK_MODE_NAME_DETAILS_FILE) {
		ret = gpk_application_perform_search_name_details_file (application);
	} else if (application->priv->search_mode == PK_MODE_GROUP ||
		   application->priv->search_mode == PK_MODE_ALL_PACKAGES) {
		ret = gpk_application_perform_search_others (application);
	} else if (application->priv->search_mode == PK_MODE_SELECTED) {
		ret = gpk_application_populate_selected (application);
	} else {
		egg_debug ("doing nothing");
	}
	return ret;
}

/**
 * gpk_application_find_cb:
 **/
static void
gpk_application_find_cb (GtkWidget *button_widget, GpkApplication *application)
{
	g_return_if_fail (GPK_IS_APPLICATION (application));

	application->priv->search_mode = PK_MODE_NAME_DETAILS_FILE;
	gpk_application_perform_search (application);
}

/**
 * gpk_application_quit:
 * @event: The event type, unused.
 **/
static gboolean
gpk_application_quit (GpkApplication *application)
{
	gint len;
	gboolean ret;
	GtkResponseType result;
	GError *error = NULL;
	GtkWindow *window;
	GtkWidget *dialog;

	g_return_val_if_fail (GPK_IS_APPLICATION (application), FALSE);

	/* do we have any items queued for removal or installation? */
	len = PK_OBJ_LIST (application->priv->package_list)->len;
	if (len != 0) {
		window = GTK_WINDOW (gtk_builder_get_object (application->priv->builder, "window_manager"));
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
	ret = pk_client_cancel (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
		error = NULL;
	}
	ret = pk_client_cancel (application->priv->client_secondary, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
		error = NULL;
	}

	egg_debug ("emitting action-close");
	g_signal_emit (application, signals [ACTION_CLOSE], 0);
	return TRUE;
}

/**
 * gpk_application_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
gpk_application_delete_event_cb (GtkWidget	*widget,
				GdkEvent	*event,
				GpkApplication	*application)
{
	g_return_val_if_fail (GPK_IS_APPLICATION (application), FALSE);

	return !gpk_application_quit (application);
}

/**
 * gpk_application_menu_quit_cb:
 **/
static void
gpk_application_menu_quit_cb (GtkAction *action, GpkApplication *application)
{
	gpk_application_quit (application);
}

/**
 * gpk_application_text_changed_cb:
 **/
static gboolean
gpk_application_text_changed_cb (GtkEntry *entry, GdkEventKey *event, GpkApplication *application)
{
	gboolean valid;
	GtkWidget *widget;
	GtkTreeView *treeview;
	const gchar *package;
	GtkTreeSelection *selection;

	g_return_val_if_fail (GPK_IS_APPLICATION (application), FALSE);

	package = gtk_entry_get_text (entry);

	/* clear group selection if we have the tab */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		treeview = GTK_TREE_VIEW (gtk_builder_get_object (application->priv->builder, "treeview_groups"));
		selection = gtk_tree_view_get_selection (treeview);
		gtk_tree_selection_unselect_all (selection);
	}

	/* check for invalid chars */
	valid = pk_strvalidate (package);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_find"));
	if (valid == FALSE || egg_strzero (package))
		gtk_widget_set_sensitive (widget, FALSE);
	else
		gtk_widget_set_sensitive (widget, TRUE);
	return FALSE;
}

/**
 * gpk_application_packages_installed_clicked_cb:
 **/
static void
gpk_application_packages_installed_clicked_cb (GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	GpkApplication *application = (GpkApplication *) data;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeSelection *selection;
	PkBitfield state;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
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
		gpk_application_remove (application);
	} else {
		gpk_application_install (application);
	}
	gtk_tree_path_free (path);
}

static void gpk_application_packages_treeview_clicked_cb (GtkTreeSelection *selection, GpkApplication *application);

/**
 * gpk_application_button_help_cb:
 **/
static void
gpk_application_button_help_cb (GtkWidget *widget_button, GpkApplication *application)
{
	gpk_gnome_help ("add-remove");
}

/**
 * gpk_application_button_clear_cb:
 **/
static void
gpk_application_button_clear_cb (GtkWidget *widget_button, GpkApplication *application)
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

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* get the first iter in the list */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* for all current items, reset the state if in the list */
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
	pk_obj_list_clear (PK_OBJ_LIST (application->priv->package_list));

	/* force a button refresh */
	selection = gtk_tree_view_get_selection (treeview);
	gpk_application_packages_treeview_clicked_cb (selection, application);

	gpk_application_set_buttons_apply_clear (application);
}

/**
 * gpk_application_button_apply_cb:
 **/
static void
gpk_application_button_apply_cb (GtkWidget *widget, GpkApplication *application)
{
	gboolean ret = FALSE;
	GError *error = NULL;
	gchar **package_ids = NULL;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	package_ids = pk_package_list_to_strv (application->priv->package_list);
	if (application->priv->action == PK_ACTION_INSTALL) {

		/* reset client */
		ret = pk_client_reset (application->priv->client_primary, &error);
		if (!ret) {
			egg_warning ("failed to cancel: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* install */
#if PK_CHECK_VERSION(0,5,2)
		ret = pk_client_simulate_install_packages (application->priv->client_primary, package_ids, &error);
#else
		application->priv->dep_check_info_only = FALSE;
		ret = pk_client_get_depends (application->priv->client_primary, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), package_ids, TRUE, &error);
#endif
		if (!ret) {
			egg_warning ("failed to get depends: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* make package list insensitive */
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
		gtk_widget_set_sensitive (widget, FALSE);
	}
	if (application->priv->action == PK_ACTION_REMOVE) {
		/* reset client */
		ret = pk_client_reset (application->priv->client_primary, &error);
		if (!ret) {
			egg_warning ("failed to cancel: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* remove */
#if PK_CHECK_VERSION(0,5,2)
		ret = pk_client_simulate_remove_packages (application->priv->client_primary, package_ids, &error);
#else
		application->priv->dep_check_info_only = FALSE;
		ret = pk_client_get_requires (application->priv->client_primary, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_ids, TRUE, &error);
#endif
		if (!ret) {
			egg_warning ("failed to get requires: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* make package list insensitive */
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
		gtk_widget_set_sensitive (widget, FALSE);
	}
out:
	g_strfreev (package_ids);
	return;
}

static void
gpk_application_packages_add_columns (GpkApplication *application)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (application->priv->builder, "treeview_packages"));

	/* column for installed toggles */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (gpk_application_packages_installed_clicked_cb), application);

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
							   "text", GROUPS_COLUMN_SUMMARY, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GROUPS_COLUMN_NAME);
	gtk_tree_view_append_column (treeview, column);

}

/**
 * gpk_application_groups_treeview_changed_cb:
 **/
static void
gpk_application_groups_treeview_changed_cb (GtkTreeSelection *selection, GpkApplication *application)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkEntry *entry;
	GtkTreePath *path;
	gboolean active;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* hide details */
	gpk_application_clear_details (application);
	gpk_application_clear_packages (application);

	/* clear the search text if we clicked the group list */
	entry = GTK_ENTRY (gtk_builder_get_object (application->priv->builder, "entry_text"));
	gtk_entry_set_text (entry, "");

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (application->priv->group);
		gtk_tree_model_get (model, &iter,
				     GROUPS_COLUMN_ID, &application->priv->group,
				     GROUPS_COLUMN_ACTIVE, &active, -1);
		egg_debug ("selected row is: %s (%i)", application->priv->group, active);

		/* don't search parent groups */
		if (!active) {
			path = gtk_tree_model_get_path (model, &iter);

			/* select the parent group */
			gtk_tree_selection_select_path (selection, path);
			gtk_tree_path_free (path);
			return;
		}

		/* GetPackages? */
		if (g_strcmp0 (application->priv->group, "all-packages") == 0)
			application->priv->search_mode = PK_MODE_ALL_PACKAGES;
		else if (g_strcmp0 (application->priv->group, "selected") == 0)
			application->priv->search_mode = PK_MODE_SELECTED;
		else
			application->priv->search_mode = PK_MODE_GROUP;

		/* actually do the search */
		gpk_application_perform_search (application);
	}
}

/**
 * gpk_application_packages_treeview_clicked_cb:
 **/
static void
gpk_application_packages_treeview_clicked_cb (GtkTreeSelection *selection, GpkApplication *application)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean ret;
	GError *error = NULL;
	gboolean show_install = TRUE;
	gboolean show_remove = TRUE;
	PkBitfield state;
	gchar **package_ids = NULL;
	gchar *package_id = NULL;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* This will only work in single or browse selection mode! */
	if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
		egg_debug ("no row selected");

		/* we cannot now add it */
		gpk_application_allow_install (application, FALSE);
		gpk_application_allow_remove (application, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_selection"));
		gtk_widget_hide (widget);

		/* hide details */
		gpk_application_clear_details (application);
		goto out;
	}

	/* check we aren't a help line */
	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_STATE, &state,
			    PACKAGES_COLUMN_ID, &package_id,
			    -1);
	if (package_id == NULL) {
		egg_debug ("ignoring help click");
		goto out;
	}

	/* show the menu item */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_selection"));
	gtk_widget_show (widget);

	show_install = (state == 0 ||
			state == pk_bitfield_from_enums (GPK_STATE_INSTALLED, GPK_STATE_IN_LIST, -1));
	show_remove = (state == pk_bitfield_value (GPK_STATE_INSTALLED) ||
		       state == pk_bitfield_value (GPK_STATE_IN_LIST));

	if (application->priv->action == PK_ACTION_INSTALL && !pk_bitfield_contain (state, GPK_STATE_IN_LIST))
		show_remove = FALSE;
	if (application->priv->action == PK_ACTION_REMOVE && !pk_bitfield_contain (state, GPK_STATE_IN_LIST))
		show_install = FALSE;

	/* only show buttons if we are in the correct mode */
	gpk_application_allow_install (application, show_install);
	gpk_application_allow_remove (application, show_remove);

	/* hide details */
	gpk_application_clear_details (application);

	/* only show run menuitem for installed programs */
	ret = pk_bitfield_contain (state, GPK_STATE_INSTALLED);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_run"));
	gtk_widget_set_sensitive (widget, ret);

	/* cancel any previous request */
	ret = pk_client_reset (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("failed to cancel, and adding to queue: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get the details */
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_get_details (application->priv->client_primary, package_ids, &error);
	if (!ret) {
		egg_warning ("failed to get details: %s", error->message);
		g_error_free (error);
	}
out:
	g_free (package_id);
	g_strfreev (package_ids);
}

/**
 * gpk_application_connection_changed_cb:
 **/
static void
gpk_application_connection_changed_cb (PkConnection *pconnection, gboolean connected, GpkApplication *application)
{
	g_return_if_fail (GPK_IS_APPLICATION (application));

	egg_debug ("connected=%i", connected);
}

/**
 * gpk_application_group_add_data:
 **/
static void
gpk_application_group_add_data (GpkApplication *application, PkGroupEnum group)
{
	GtkTreeIter iter;
	const gchar *icon_name;
	const gchar *text;

	gtk_tree_store_append (application->priv->groups_store, &iter, NULL);

	text = gpk_group_enum_to_localised_text (group);
	icon_name = gpk_group_enum_to_icon_name (group);
	gtk_tree_store_set (application->priv->groups_store, &iter,
			    GROUPS_COLUMN_NAME, text,
			    GROUPS_COLUMN_SUMMARY, NULL,
			    GROUPS_COLUMN_ID, pk_group_enum_to_text (group),
			    GROUPS_COLUMN_ICON, icon_name,
			    GROUPS_COLUMN_ACTIVE, TRUE,
			    -1);
}

/**
 * gpk_application_group_add_selected:
 **/
static void
gpk_application_group_add_selected (GpkApplication *application)
{
	GtkTreeIter iter;

	gtk_tree_store_append (application->priv->groups_store, &iter, NULL);
	gtk_tree_store_set (application->priv->groups_store, &iter,
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
	gtk_widget_size_request (GTK_WIDGET (widget), &requisition);

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
	GpkApplication *application = GPK_APPLICATION (data);

	/* change type */
	application->priv->search_type = PK_SEARCH_NAME;
	egg_debug ("set search type=%i", application->priv->search_type);

	/* save default to GConf */
	gconf_client_set_string (application->priv->gconf_client, GPK_CONF_APPLICATION_SEARCH_MODE, "name", NULL);

	/* set the new icon */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "entry_text"));
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
	GpkApplication *application = GPK_APPLICATION (data);

	/* set type */
	application->priv->search_type = PK_SEARCH_DETAILS;
	egg_debug ("set search type=%i", application->priv->search_type);

	/* save default to GConf */
	gconf_client_set_string (application->priv->gconf_client, GPK_CONF_APPLICATION_SEARCH_MODE, "details", NULL);

	/* set the new icon */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "entry_text"));
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
	GpkApplication *application = GPK_APPLICATION (data);

	/* set type */
	application->priv->search_type = PK_SEARCH_FILE;
	egg_debug ("set search type=%i", application->priv->search_type);

	/* save default to GConf */
	gconf_client_set_string (application->priv->gconf_client, GPK_CONF_APPLICATION_SEARCH_MODE, "file", NULL);

	/* set the new icon */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "entry_text"));
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
	GpkApplication *application = GPK_APPLICATION (data);

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* only respond to left button */
	if (event->button != 1)
		return;

	egg_debug ("icon_pos=%i", icon_pos);

	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_NAME)) {
		/* TRANSLATORS: context menu item for the search type icon */
		item = gtk_image_menu_item_new_with_mnemonic (_("Search by name"));
		image = gtk_image_new_from_stock (GTK_STOCK_FIND, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_name), application);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_DETAILS)) {
		/* TRANSLATORS: context menu item for the search type icon */
		item = gtk_image_menu_item_new_with_mnemonic (_("Search by description"));
		image = gtk_image_new_from_stock (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_description), application);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_FILE)) {
		/* TRANSLATORS: context menu item for the search type icon */
		item = gtk_image_menu_item_new_with_mnemonic (_("Search by file name"));
		image = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_file), application);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gpk_application_popup_position_menu, entry,
			event->button, event->time);
}

/**
 *  * gpk_application_about_dialog_url_cb:
 *   **/
static void
gpk_application_about_dialog_url_cb (GtkAboutDialog *about, const char *address, gpointer data)
{
	GError *error = NULL;
	gboolean ret;

	GdkScreen *gscreen;
	GtkWidget *error_dialog;
	gchar *url;
	gchar *protocol = (gchar*) data;

	if (protocol != NULL)
		url = g_strconcat (protocol, address, NULL);
	else
		url = g_strdup (address);

	gscreen = gtk_window_get_screen (GTK_WINDOW (about));

	ret = gtk_show_uri (gscreen, url, gtk_get_current_event_time (), &error);

	if (!ret) {
		error_dialog = gtk_message_dialog_new (GTK_WINDOW (about),
						       GTK_DIALOG_MODAL,
						       GTK_MESSAGE_INFO,
						       GTK_BUTTONS_OK,
						       /* TRANSLATORS: packaging problem, failed to show link */
						       _("Failed to show url"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (error_dialog),
							  "%s", error->message);
		gtk_dialog_run (GTK_DIALOG (error_dialog));
		gtk_widget_destroy (error_dialog);
		g_error_free (error);
	}
	g_free (url);
}

/**
 * gpk_application_menu_help_cb:
 **/
static void
gpk_application_menu_help_cb (GtkAction *action, GpkApplication *application)
{
	gpk_gnome_help ("add-remove");
}

/**
 * gpk_application_menu_about_cb:
 **/
static void
gpk_application_menu_about_cb (GtkAction *action, GpkApplication *application)
{
	static gboolean been_here = FALSE;
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

	if (!been_here) {
		been_here = TRUE;
		gtk_about_dialog_set_url_hook (gpk_application_about_dialog_url_cb, NULL, NULL);
		gtk_about_dialog_set_email_hook (gpk_application_about_dialog_url_cb, (gpointer) "mailto:", NULL);
	}

	/* use parent */
	main_window = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "window_manager"));

	gtk_window_set_default_icon_name (GPK_ICON_SOFTWARE_INSTALLER);
	gtk_show_about_dialog (GTK_WINDOW (main_window),
			       "version", PACKAGE_VERSION,
			       "copyright", "Copyright \xc2\xa9 2007-2009 Richard Hughes",
			       "license", license_trans,
			       "wrap-license", TRUE,
			       "website-label", _("PackageKit Website"),
			       "website", "http://www.packagekit.org",
				/* TRANSLATORS: description of application, gpk-application that is */
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
gpk_application_menu_sources_cb (GtkAction *action, GpkApplication *application)
{
	gboolean ret;
	guint xid;
	gchar *command;
	GtkWidget *window;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* get xid */
	window = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "window_manager"));
	xid = gdk_x11_drawable_get_xid (gtk_widget_get_window (window));

	command = g_strdup_printf ("%s/gpk-repo --parent-window %u", BINDIR, xid);
	egg_debug ("running: %s", command);
	ret = g_spawn_command_line_async (command, NULL);
	if (!ret) {
		egg_warning ("spawn of %s failed", command);
	}
	g_free (command);
}

/**
 * gpk_application_menu_log_cb:
 **/
static void
gpk_application_menu_log_cb (GtkAction *action, GpkApplication *application)
{
	gboolean ret;
	guint xid;
	gchar *command;
	GtkWidget *window;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* get xid */
	window = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "window_manager"));
	xid = gdk_x11_drawable_get_xid (gtk_widget_get_window (window));

	command = g_strdup_printf ("%s/gpk-log --parent-window %u", BINDIR, xid);
	egg_debug ("running: %s", command);
	ret = g_spawn_command_line_async (command, NULL);
	if (!ret) {
		egg_warning ("spawn of %s failed", command);
	}
	g_free (command);
}

/**
 * gpk_application_menu_refresh_cb:
 **/
static void
gpk_application_menu_refresh_cb (GtkAction *action, GpkApplication *application)
{
	gboolean ret;
	GError *error = NULL;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* reset client */
	ret = pk_client_reset (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set correct view */
	ret = pk_client_refresh_cache (application->priv->client_primary, TRUE, &error);
	if (!ret) {
		egg_warning ("cannot get refresh cache: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return;
}

/**
 * gpk_application_menu_filter_installed_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_installed_cb (GtkWidget *widget, GpkApplication *application)
{
	const gchar *name;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
		return;

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_INSTALLED);
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_INSTALLED);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_INSTALLED);
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_NOT_INSTALLED);
	} else {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_INSTALLED);
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_INSTALLED);
	}

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_menu_filter_devel_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_devel_cb (GtkWidget *widget, GpkApplication *application)
{
	const gchar *name;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
		return;

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_DEVELOPMENT);
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_DEVELOPMENT);
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	} else {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_DEVELOPMENT);
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	}

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_menu_filter_gui_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_gui_cb (GtkWidget *widget, GpkApplication *application)
{
	const gchar *name;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
		return;

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_GUI);
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_GUI);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_GUI);
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_NOT_GUI);
	} else {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_GUI);
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_GUI);
	}

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_menu_filter_free_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_free_cb (GtkWidget *widget, GpkApplication *application)
{
	const gchar *name;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
		return;

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_FREE);
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_FREE);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_FREE);
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_NOT_FREE);
	} else {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_FREE);
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_FREE);
	}

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_menu_filter_source_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_source_cb (GtkWidget *widget, GpkApplication *application)
{
	const gchar *name;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
		return;

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_SOURCE);
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_SOURCE);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_SOURCE);
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_NOT_SOURCE);
	} else {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_SOURCE);
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_SOURCE);
	}

	/* refresh the sesource results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_menu_filter_basename_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_basename_cb (GtkWidget *widget, GpkApplication *application)
{
	gboolean enabled;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* save users preference to gconf */
	enabled = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	gconf_client_set_bool (application->priv->gconf_client,
			       GPK_CONF_APPLICATION_FILTER_BASENAME, enabled, NULL);

	/* change the filter */
	if (enabled)
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_BASENAME);
	else
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_BASENAME);

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_menu_filter_newest_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_newest_cb (GtkWidget *widget, GpkApplication *application)
{
	gboolean enabled;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* save users preference to gconf */
	enabled = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	gconf_client_set_bool (application->priv->gconf_client,
			       GPK_CONF_APPLICATION_FILTER_NEWEST, enabled, NULL);

	/* change the filter */
	if (enabled)
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_NEWEST);
	else
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NEWEST);

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_menu_filter_arch_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_arch_cb (GtkWidget *widget, GpkApplication *application)
{
	gboolean enabled;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* save users preference to gconf */
	enabled = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	gconf_client_set_bool (application->priv->gconf_client,
			       GPK_CONF_APPLICATION_FILTER_ARCH, enabled, NULL);

	/* change the filter */
	if (enabled)
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_ARCH);
	else
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_ARCH);

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_status_changed_timeout_cb:
 **/
static gboolean
gpk_application_status_changed_timeout_cb (GpkApplication *application)
{
	const gchar *text;
	GtkWidget *widget;

	/* set the text and show */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "label_status"));
	text = gpk_status_enum_to_localised_text (application->priv->status_last);
	gtk_label_set_label (GTK_LABEL (widget), text);

	/* set icon */
	gpk_set_animated_icon_from_status (GPK_ANIMATED_ICON (application->priv->image_status),
					   application->priv->status_last, GTK_ICON_SIZE_LARGE_TOOLBAR);

	/* show containing box */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "hbox_status"));
	gtk_widget_show (widget);

	/* never repeat */
	application->priv->status_id = 0;
	return FALSE;
}

/**
 * gpk_application_status_changed_cb:
 **/
static void
gpk_application_status_changed_cb (PkClient *client, PkStatusEnum status, GpkApplication *application)
{
	GtkWidget *widget;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	if (application->priv->action == PK_ACTION_INSTALL ||
	    application->priv->action == PK_ACTION_REMOVE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_groups"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "textview_description"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_detail"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "entry_text"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_apply"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_clear"));
		gtk_widget_set_sensitive (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_find"));
		gtk_widget_set_sensitive (widget, FALSE);
	}

	if (status == PK_STATUS_ENUM_FINISHED) {

		/* re-enable UI */
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
		gtk_widget_set_sensitive (widget, TRUE);

		/* we've not yet shown, so don't bother */
		if (application->priv->status_id > 0) {
			g_source_remove (application->priv->status_id);
			application->priv->status_id = 0;
		}

		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "hbox_status"));
		gtk_widget_hide (widget);
		gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (application->priv->image_status), FALSE);
		goto out;
	}

	/* already pending show */
	if (application->priv->status_id > 0)
		goto out;

	/* only show after some time in the transaction */
	application->priv->status_id = g_timeout_add (GPK_UI_STATUS_SHOW_DELAY, (GSourceFunc) gpk_application_status_changed_timeout_cb, application);

out:
	/* save for the callback */
	application->priv->status_last = status;
}

/**
 * gpk_application_allow_cancel_cb:
 **/
static void
gpk_application_allow_cancel_cb (PkClient *client, gboolean allow_cancel, GpkApplication *application)
{
	GtkWidget *widget;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_cancel"));
	gtk_widget_set_sensitive (widget, allow_cancel);
}

/**
 * gpk_application_repo_signature_event_cb:
 **/
static void
gpk_application_repo_signature_event_cb (GpkHelperRepoSignature *helper_repo_signature, GtkResponseType type, const gchar *key_id, const gchar *package_id, GpkApplication *application)
{
	gboolean ret;
	GError *error = NULL;

	if (type != GTK_RESPONSE_YES) {
		goto out;
	}

	/* reset client */
	ret = pk_client_reset (application->priv->client_secondary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* install signature */
	ret = pk_client_install_signature (application->priv->client_secondary, PK_SIGTYPE_ENUM_GPG, key_id, package_id, &error);
	if (!ret) {
		egg_warning ("cannot install signature: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return;
}

/**
 * gpk_application_eula_event_cb:
 **/
static void
gpk_application_eula_event_cb (GpkHelperEula *helper_eula, GtkResponseType type, const gchar *eula_id, GpkApplication *application)
{
	gboolean ret;
	GError *error = NULL;

	if (type != GTK_RESPONSE_YES) {
		goto out;
	}

	/* reset client */
	ret = pk_client_reset (application->priv->client_secondary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* install signature */
	ret = pk_client_accept_eula (application->priv->client_secondary, eula_id, &error);
	if (!ret) {
		egg_warning ("cannot accept eula: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return;
}

/**
 * gpk_application_deps_remove_event_cb:
 **/
static void
gpk_application_deps_remove_event_cb (GpkHelperDepsRemove *helper_deps_remove, GtkResponseType type, GpkApplication *application)
{
	gboolean ret;
	GError *error = NULL;
	gchar **package_ids = NULL;
	GtkWidget *widget;

	if (type != GTK_RESPONSE_YES) {
		goto out;
	}

	/* reset client */
	ret = pk_client_reset (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* actually remove packages this time */
	package_ids = pk_package_list_to_strv (application->priv->package_list);
	ret = pk_client_remove_packages (application->priv->client_primary, package_ids, TRUE, FALSE, &error);
	if (!ret) {
		egg_warning ("cannot remove packages: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* make package list insensitive */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
	gtk_widget_set_sensitive (widget, FALSE);
out:
	g_strfreev (package_ids);
}

/**
 * gpk_application_deps_install_event_cb:
 **/
static void
gpk_application_deps_install_event_cb (GpkHelperDepsInstall *helper_deps_install, GtkResponseType type, GpkApplication *application)
{
	gboolean ret;
	GError *error = NULL;
	gchar **package_ids = NULL;
	GtkWidget *widget;

	if (type != GTK_RESPONSE_YES) {
		goto out;
	}

	/* reset client */
	ret = pk_client_reset (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* actually remove packages this time */
	package_ids = pk_package_list_to_strv (application->priv->package_list);
#if PK_CHECK_VERSION(0,5,0)
	ret = pk_client_install_packages (application->priv->client_primary, TRUE, package_ids, &error);
#else
	ret = pk_client_install_packages (application->priv->client_primary, package_ids, &error);
#endif
	if (!ret) {
		egg_warning ("cannot install packages: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* make package list insensitive */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
	gtk_widget_set_sensitive (widget, FALSE);

out:
	g_strfreev (package_ids);
}

/**
 * gpk_application_media_change_event_cb:
 **/
static void
gpk_application_media_change_event_cb (GpkHelperMediaChange *helper_media_change, GtkResponseType type, GpkApplication *application)
{
	if (type != GTK_RESPONSE_YES)
		goto out;

	/* requeue */
	gpk_application_primary_requeue (application);
out:
	return;
}

/**
 * gpk_application_eula_required_cb:
 **/
static void
gpk_application_eula_required_cb (PkClient *client, const gchar *eula_id, const gchar *package_id,
				    const gchar *vendor_name, const gchar *license_agreement, GpkApplication *application)
{
	/* use the helper */
	gpk_helper_eula_show (application->priv->helper_eula, eula_id, package_id, vendor_name, license_agreement);
}

#if PK_CHECK_VERSION(0,4,7)
/**
 * gpk_application_media_change_required_cb:
 **/
static void
gpk_application_media_change_required_cb (PkClient *client, PkMediaTypeEnum type, const gchar *media_id, const gchar *media_text, GpkApplication *application)
{
	/* use the helper */
	gpk_helper_media_change_show (application->priv->helper_media_change, type, media_id, media_text);
}
#endif

/**
 * gpk_application_repo_signature_required_cb:
 **/
static void
gpk_application_repo_signature_required_cb (PkClient *client, const gchar *package_id, const gchar *repository_name,
					      const gchar *key_url, const gchar *key_userid, const gchar *key_id,
					      const gchar *key_fingerprint, const gchar *key_timestamp,
					      PkSigTypeEnum type, GpkApplication *application)
{
	/* use the helper */
	gpk_helper_repo_signature_show (application->priv->helper_repo_signature, package_id, repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp);
}

/**
 * gpk_application_package_row_activated_cb:
 **/
static void
gpk_application_package_row_activated_cb (GtkTreeView *treeview, GtkTreePath *path,
					  GtkTreeViewColumn *col, GpkApplication *application)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean ret;
	PkBitfield state;
	gchar *package_id = NULL;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* get selection */
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter (model, &iter, path);
	if (!ret) {
		egg_warning ("failed to get selection");
		return;
	}

	/* get data */
	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_STATE, &state,
			    PACKAGES_COLUMN_ID, &package_id,
			    -1);

	/* check we aren't a help line */
	if (package_id == NULL) {
		egg_debug ("ignoring help click");
		goto out;
	}

	if (gpk_application_state_get_checkbox (state))
		gpk_application_remove (application);
	else
		gpk_application_install (application);
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
 * pk_application_repo_detail_cb:
 **/
static void
pk_application_repo_detail_cb (PkClient *client, const gchar *repo_id,
			       const gchar *description, gboolean enabled,
			       GpkApplication *application)
{
	g_return_if_fail (GPK_IS_APPLICATION (application));

	egg_debug ("repo = %s:%s", repo_id, description);
	/* no problem, just no point adding as we will fallback to the repo_id */
	if (description == NULL)
		return;
	g_hash_table_insert (application->priv->repos, g_strdup (repo_id), g_strdup (description));
}

/**
 * gpk_application_treeview_renderer_clicked:
 **/
static void
gpk_application_treeview_renderer_clicked (GtkCellRendererToggle *cell, gchar *uri, GpkApplication *application)
{
	egg_debug ("clicked %s", uri);
	gpk_gnome_open (uri);
}

/**
 * gpk_application_treeview_add_columns_description:
 **/
static void
gpk_application_treeview_add_columns_description (GpkApplication *application)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeView *treeview;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (application->priv->builder, "treeview_detail"));

	/* title */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", DETAIL_COLUMN_TITLE);
	gtk_tree_view_append_column (treeview, column);

	/* column for uris */
	renderer = gpk_cell_renderer_uri_new ();
	g_signal_connect (renderer, "clicked", G_CALLBACK (gpk_application_treeview_renderer_clicked), application);
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
gpk_application_add_welcome (GpkApplication *application)
{
	GtkTreeIter iter;
	const gchar *welcome;
	PkBitfield state = 0;

	gpk_application_clear_packages (application);
	gtk_list_store_append (application->priv->packages_store, &iter);

	/* enter something nice */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		/* TRANSLATORS: welcome text if we can click the group list */
		welcome = _("Enter a package name and then click find, or click a group to get started.");
	} else {
		/* TRANSLATORS: welcome text if we have to search by name */
		welcome = _("Enter a package name and then click find to get started.");
	}
	gtk_list_store_set (application->priv->packages_store, &iter,
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
 * gpk_application_create_group_list_enum:
 **/
static gboolean
gpk_application_create_group_list_enum (GpkApplication *application)
{
	GtkWidget *widget;
	guint i;
	GtkTreeIter iter;
	const gchar *icon_name;

	/* set to no indent */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_groups"));
	gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (widget), FALSE);
	gtk_tree_view_set_level_indentation  (GTK_TREE_VIEW (widget), 0);

	/* add an "all" entry if we can GetPackages */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_PACKAGES)) {
		gtk_tree_store_append (application->priv->groups_store, &iter, NULL);
		icon_name = gpk_role_enum_to_icon_name (PK_ROLE_ENUM_GET_PACKAGES);
		gtk_tree_store_set (application->priv->groups_store, &iter,
				    /* TRANSLATORS: title: all of the packages on the system and availble in sources */
				    GROUPS_COLUMN_NAME, _("All packages"),
				    /* TRANSLATORS: tooltip: all packages */
				    GROUPS_COLUMN_SUMMARY, _("Show all packages"),
				    GROUPS_COLUMN_ID, "all-packages",
				    GROUPS_COLUMN_ACTIVE, TRUE,
				    GROUPS_COLUMN_ICON, icon_name, -1);
	}

	/* no group information */
	if (application->priv->groups == 0)
		return FALSE;

	/* add these at the top of the list */
	if (pk_bitfield_contain (application->priv->groups, PK_GROUP_ENUM_COLLECTIONS))
		gpk_application_group_add_data (application, PK_GROUP_ENUM_COLLECTIONS);
	if (pk_bitfield_contain (application->priv->groups, PK_GROUP_ENUM_NEWEST))
		gpk_application_group_add_data (application, PK_GROUP_ENUM_NEWEST);

	/* add group item for selected items */
	gpk_application_group_add_selected (application);

	/* add a separator only if we can do both */
	gtk_tree_store_append (application->priv->groups_store, &iter, NULL);
	gtk_tree_store_set (application->priv->groups_store, &iter,
			    GROUPS_COLUMN_ID, "separator", -1);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_groups"));
	gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (widget),
					      gpk_application_group_row_separator_func, NULL, NULL);

	/* create group tree view if we can search by group */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		/* add all the groups supported (except collections, which we handled above */
		for (i=0; i<PK_GROUP_ENUM_UNKNOWN; i++) {
			if (pk_bitfield_contain (application->priv->groups, i) &&
			    i != PK_GROUP_ENUM_COLLECTIONS && i != PK_GROUP_ENUM_NEWEST)
				gpk_application_group_add_data (application, i);
		}
	}

	/* we populated the menu  */
	return TRUE;
}

/**
 * gpk_application_categories_finished:
 **/
static void
gpk_application_categories_finished (GpkApplication *application)
{
	PkObjList *list;
	const PkCategoryObj *obj;
	const PkCategoryObj *obj2;
	GtkTreeIter iter;
	GtkTreeIter iter2;
	guint i, j;
	GtkTreeView *treeview;
	const gchar *icon_name;

	/* set to expanders with indent */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (application->priv->builder, "treeview_groups"));
	gtk_tree_view_set_show_expanders (treeview, TRUE);
	gtk_tree_view_set_level_indentation  (treeview, 3);

	/* add an "all" entry if we can GetPackages */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_PACKAGES)) {
		gtk_tree_store_append (application->priv->groups_store, &iter, NULL);
		icon_name = gpk_role_enum_to_icon_name (PK_ROLE_ENUM_GET_PACKAGES);
		gtk_tree_store_set (application->priv->groups_store, &iter,
				    /* TRANSLATORS: title: all of the packages on the system and availble in sources */
				    GROUPS_COLUMN_NAME, _("All packages"),
				    /* TRANSLATORS: tooltip: all packages */
				    GROUPS_COLUMN_SUMMARY, _("Show all packages"),
				    GROUPS_COLUMN_ID, "all-packages",
				    GROUPS_COLUMN_ACTIVE, TRUE,
				    GROUPS_COLUMN_ICON, icon_name, -1);
	}

	/* add these at the top of the list */
	if (pk_bitfield_contain (application->priv->groups, PK_GROUP_ENUM_COLLECTIONS))
		gpk_application_group_add_data (application, PK_GROUP_ENUM_COLLECTIONS);
	if (pk_bitfield_contain (application->priv->groups, PK_GROUP_ENUM_NEWEST))
		gpk_application_group_add_data (application, PK_GROUP_ENUM_NEWEST);

	/* add group item for selected items */
	gpk_application_group_add_selected (application);

	/* add a separator */
	gtk_tree_store_append (application->priv->groups_store, &iter, NULL);
	gtk_tree_store_set (application->priv->groups_store, &iter,
			    GROUPS_COLUMN_ID, "separator", -1);
	gtk_tree_view_set_row_separator_func (treeview,
					      gpk_application_group_row_separator_func, NULL, NULL);

	/* get return values */
#if PK_CHECK_VERSION(0,5,0)
	list = pk_client_get_category_list (application->priv->client_primary);
#else
	list = pk_client_get_cached_objects (application->priv->client_primary); /* removed in 0.5.x */
#endif
	if (list->len == 0) {
		egg_warning ("no results from GetCategories");
		goto out;
	}

	for (i=0; i < list->len; i++) {
		obj = pk_obj_list_index (list, i);

		gtk_tree_store_append (application->priv->groups_store, &iter, NULL);
		gtk_tree_store_set (application->priv->groups_store, &iter,
				    GROUPS_COLUMN_NAME, obj->name,
				    GROUPS_COLUMN_SUMMARY, obj->summary,
				    GROUPS_COLUMN_ID, obj->cat_id,
				    GROUPS_COLUMN_ICON, obj->icon,
				    GROUPS_COLUMN_ACTIVE, FALSE,
				    -1);
		j = 0;
		do {
			/* only allows groups two layers deep */
			obj2 = pk_obj_list_index (list, j);
			if (g_strcmp0 (obj2->parent_id, obj->cat_id) == 0) {
				gtk_tree_store_append (application->priv->groups_store, &iter2, &iter);
				gtk_tree_store_set (application->priv->groups_store, &iter2,
						    GROUPS_COLUMN_NAME, obj2->name,
						    GROUPS_COLUMN_SUMMARY, obj2->summary,
						    GROUPS_COLUMN_ID, obj2->cat_id,
						    GROUPS_COLUMN_ICON, obj2->icon,
						    GROUPS_COLUMN_ACTIVE, TRUE,
						    -1);
				pk_obj_list_remove (list, obj2);
			} else
				j++;
		} while (j < list->len);
	}

	/* open all expanders */
	gtk_tree_view_collapse_all (treeview);
	g_object_unref (list);
out:
	return;
}

/**
 * gpk_application_create_group_list_categories:
 **/
static gboolean
gpk_application_create_group_list_categories (GpkApplication *application)
{
	GError *error = NULL;
	gboolean ret = FALSE;

	/* check we can do this */
	if (!pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_CATEGORIES)) {
		egg_warning ("backend does not support complex groups");
		goto out;
	}

	/* reset client */
	ret = pk_client_reset (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get categories supported */
	pk_client_set_synchronous (application->priv->client_primary, TRUE, NULL);
	ret = pk_client_get_categories (application->priv->client_primary, &error);
	pk_client_set_synchronous (application->priv->client_primary, FALSE, NULL);
	if (!ret) {
		egg_warning ("failed to get categories: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return ret;
}

/**
 * gpk_application_gconf_key_changed_cb:
 *
 * We might have to do things when the gconf keys change; do them here.
 **/
static void
gpk_application_gconf_key_changed_cb (GConfClient *client, guint cnxn_id, GConfEntry *gconf_entry, GpkApplication *application)
{
	GtkEntryCompletion *completion;
	GConfValue *value;
	gboolean ret;
	GtkEntry *entry;

	value = gconf_entry_get_value (gconf_entry);
	if (value == NULL)
		return;

	if (g_strcmp0 (gconf_entry->key, GPK_CONF_APPLICATION_CATEGORY_GROUPS) == 0) {
		ret = gconf_value_get_bool (value);
		gtk_tree_store_clear (application->priv->groups_store);
		if (ret)
			gpk_application_create_group_list_categories (application);
		else
			gpk_application_create_group_list_enum (application);
	} else if (g_strcmp0 (gconf_entry->key, GPK_CONF_AUTOCOMPLETE) == 0) {
		ret = gconf_value_get_bool (value);
		entry = GTK_ENTRY (gtk_builder_get_object (application->priv->builder, "entry_text"));
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
 * gpk_application_files_cb:
 **/
static void
gpk_application_files_cb (PkClient *client, const gchar *package_id,
			  const gchar *filelist, GpkApplication *application)
{
	GPtrArray *array;
	gchar **files;
	gchar *title;
	GtkWindow *window;
	GtkWidget *dialog;
	PkPackageId *id;
	gchar *package_id_selected = NULL;
	gboolean ret;

	g_return_if_fail (GPK_IS_APPLICATION (application));

	/* get selection */
	ret = gpk_application_get_selected_package (application, &package_id_selected, NULL);
	if (!ret) {
		egg_warning ("no package selected");
		goto out;
	}

	files = g_strsplit (filelist, ";", -1);

	/* convert to pointer array */
	array = pk_strv_to_ptr_array (files);
	g_ptr_array_sort (array, (GCompareFunc) gpk_application_strcmp_indirect);

	/* title */
	id = pk_package_id_new_from_string (package_id_selected);
	/* TRANSLATORS: title: how many files are installed by the application */
	title = g_strdup_printf (ngettext ("%i file installed by %s",
					   "%i files installed by %s",
					   array->len), array->len, id->name);

	window = GTK_WINDOW (gtk_builder_get_object (application->priv->builder, "window_manager"));
	dialog = gtk_message_dialog_new (window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
	gpk_dialog_embed_file_list_widget (GTK_DIALOG (dialog), array);
	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_default_size (GTK_WINDOW (dialog), 600, 250);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));

	g_free (title);
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);
	g_strfreev (files);
	pk_package_id_free (id);
out:
	g_free (package_id_selected);
}

/**
 * gpk_application_init:
 **/
static void
gpk_application_init (GpkApplication *application)
{
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkWidget *image;
	GtkEntryCompletion *completion;
	GtkTreeSelection *selection;
	gboolean enabled;
	gboolean ret;
	gchar *mode;
	GError *error = NULL;
	GSList *list;
	guint retval;
	GtkBox *box;

	application->priv = GPK_APPLICATION_GET_PRIVATE (application);
	application->priv->group = NULL;
	application->priv->url = NULL;
	application->priv->search_text = NULL;
	application->priv->has_package = FALSE;
#if !PK_CHECK_VERSION(0,5,2)
	application->priv->dep_check_info_only = FALSE;
#endif
	application->priv->details_event_id = 0;
	application->priv->status_id = 0;
	application->priv->status_last = PK_STATUS_ENUM_UNKNOWN;
	application->priv->package_list = pk_package_list_new ();

	application->priv->gconf_client = gconf_client_get_default ();
	application->priv->repos = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	application->priv->search_type = PK_SEARCH_UNKNOWN;
	application->priv->search_mode = PK_MODE_UNKNOWN;
	application->priv->filters_current = PK_FILTER_ENUM_NONE;

	application->priv->markdown = egg_markdown_new ();
	egg_markdown_set_max_lines (application->priv->markdown, 50);

	/* watch gnome-packagekit keys */
	gconf_client_add_dir (application->priv->gconf_client, GPK_CONF_DIR,
			      GCONF_CLIENT_PRELOAD_NONE, NULL);
	gconf_client_notify_add (application->priv->gconf_client, GPK_CONF_DIR,
				 (GConfClientNotifyFunc) gpk_application_gconf_key_changed_cb,
				 application, NULL, NULL);

	/* create list stores */
	application->priv->packages_store = gtk_list_store_new (PACKAGES_COLUMN_LAST,
							        G_TYPE_STRING,
								G_TYPE_UINT64,
							        G_TYPE_BOOLEAN,
							        G_TYPE_BOOLEAN,
							        G_TYPE_STRING,
							        G_TYPE_STRING,
							        G_TYPE_STRING);
	application->priv->groups_store = gtk_tree_store_new (GROUPS_COLUMN_LAST,
							      G_TYPE_STRING,
							      G_TYPE_STRING,
							      G_TYPE_STRING,
							      G_TYPE_STRING,
							      G_TYPE_BOOLEAN);
	application->priv->details_store = gtk_list_store_new (DETAIL_COLUMN_LAST,
							       G_TYPE_STRING,
							       G_TYPE_STRING,
							       G_TYPE_STRING);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   "/usr/share/PackageKit/icons");

	application->priv->control = pk_control_new ();

	/* this is what we use mainly */
	application->priv->client_primary = pk_client_new ();
	pk_client_set_use_buffer (application->priv->client_primary, TRUE, NULL);
	g_signal_connect (application->priv->client_primary, "files",
			  G_CALLBACK (gpk_application_files_cb), application);
	g_signal_connect (application->priv->client_primary, "package",
			  G_CALLBACK (gpk_application_package_cb), application);
	g_signal_connect (application->priv->client_primary, "details",
			  G_CALLBACK (gpk_application_details_cb), application);
	g_signal_connect (application->priv->client_primary, "error-code",
			  G_CALLBACK (gpk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_primary, "finished",
			  G_CALLBACK (gpk_application_finished_cb), application);
	g_signal_connect (application->priv->client_primary, "status-changed",
			  G_CALLBACK (gpk_application_status_changed_cb), application);
	g_signal_connect (application->priv->client_primary, "allow-cancel",
			  G_CALLBACK (gpk_application_allow_cancel_cb), application);
	g_signal_connect (application->priv->client_primary, "repo-detail",
			  G_CALLBACK (pk_application_repo_detail_cb), application);
	g_signal_connect (application->priv->client_primary, "repo-signature-required",
			  G_CALLBACK (gpk_application_repo_signature_required_cb), application);
	g_signal_connect (application->priv->client_primary, "eula-required",
			  G_CALLBACK (gpk_application_eula_required_cb), application);
#if PK_CHECK_VERSION(0,4,7)
	g_signal_connect (application->priv->client_primary, "media-change-required",
			  G_CALLBACK (gpk_application_media_change_required_cb), application);
#endif

	/* this is for auth and eula callbacks */
	application->priv->client_secondary = pk_client_new ();
	pk_client_set_use_buffer (application->priv->client_secondary, TRUE, NULL);
	g_signal_connect (application->priv->client_secondary, "error-code",
			  G_CALLBACK (gpk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_secondary, "finished",
			  G_CALLBACK (gpk_application_finished_cb), application);

	/* get bitfield */
	application->priv->roles = pk_control_get_actions (application->priv->control, NULL);
	application->priv->filters = pk_control_get_filters (application->priv->control, NULL);
	application->priv->groups = pk_control_get_groups (application->priv->control, NULL);

	application->priv->pconnection = pk_connection_new ();
	g_signal_connect (application->priv->pconnection, "connection-changed",
			  G_CALLBACK (gpk_application_connection_changed_cb), application);

	/* get localised data from sqlite database */
	application->priv->desktop = pk_desktop_new ();
	ret = pk_desktop_open_database (application->priv->desktop, NULL);
	if (!ret)
		egg_warning ("Failure opening database");

	/* get UI */
	application->priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (application->priv->builder, GPK_DATA "/gpk-application.ui", &error);
	if (error != NULL) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out_build;
	}

	/* add animated widget */
	application->priv->image_status = gpk_animated_icon_new ();
	box = GTK_BOX (gtk_builder_get_object (application->priv->builder, "hbox_status"));
	gtk_box_pack_start (box, application->priv->image_status, FALSE, FALSE, 0);
	gtk_box_reorder_child (box, application->priv->image_status, 0);
	gtk_widget_show (application->priv->image_status);

	main_window = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "window_manager"));

	/* helpers */
	application->priv->helper_repo_signature = gpk_helper_repo_signature_new ();
	g_signal_connect (application->priv->helper_repo_signature, "event", G_CALLBACK (gpk_application_repo_signature_event_cb), application);
	gpk_helper_repo_signature_set_parent (application->priv->helper_repo_signature, GTK_WINDOW (main_window));

	application->priv->helper_eula = gpk_helper_eula_new ();
	g_signal_connect (application->priv->helper_eula, "event", G_CALLBACK (gpk_application_eula_event_cb), application);
	gpk_helper_eula_set_parent (application->priv->helper_eula, GTK_WINDOW (main_window));

	application->priv->helper_run = gpk_helper_run_new ();
	gpk_helper_run_set_parent (application->priv->helper_run, GTK_WINDOW (main_window));

	application->priv->helper_deps_remove = gpk_helper_deps_remove_new ();
	g_signal_connect (application->priv->helper_deps_remove, "event", G_CALLBACK (gpk_application_deps_remove_event_cb), application);
	gpk_helper_deps_remove_set_parent (application->priv->helper_deps_remove, GTK_WINDOW (main_window));

	application->priv->helper_deps_install = gpk_helper_deps_install_new ();
	g_signal_connect (application->priv->helper_deps_install, "event", G_CALLBACK (gpk_application_deps_install_event_cb), application);
	gpk_helper_deps_install_set_parent (application->priv->helper_deps_install, GTK_WINDOW (main_window));

	application->priv->helper_media_change = gpk_helper_media_change_new ();
	g_signal_connect (application->priv->helper_media_change, "event", G_CALLBACK (gpk_application_media_change_event_cb), application);
	gpk_helper_media_change_set_parent (application->priv->helper_media_change, GTK_WINDOW (main_window));

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SOFTWARE_INSTALLER);
	gtk_window_set_default_icon_name (GPK_ICON_SOFTWARE_INSTALLER);

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (gpk_application_delete_event_cb), application);

	/* clear */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_clear"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_button_clear_cb), application);
	/* TRANSLATORS: tooltip on the clear button */
	gtk_widget_set_tooltip_text (widget, _("Clear current selection"));

	/* help */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_button_help_cb), application);

	/* set F1 = contents */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menu_about"));
	list = gtk_accel_groups_from_object (G_OBJECT (main_window));
	if (list != NULL)
		gtk_menu_set_accel_group (GTK_MENU (widget), GTK_ACCEL_GROUP (list->data));

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_help"));
	gtk_menu_item_set_accel_path (GTK_MENU_ITEM (widget),
			              "<gpk-application>/menuitem_help");
	gtk_accel_map_add_entry ("<gpk-application>/menuitem_help", GDK_F1, 0);
	image = gtk_image_new_from_stock ("gtk-help", GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);

	/* install */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_apply"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_button_apply_cb), application);
	/* TRANSLATORS: tooltip on the apply button */
	gtk_widget_set_tooltip_text (widget, _("Changes are not applied instantly, this button applies all changes"));

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_about"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_about_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_help"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_help_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_sources"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_sources_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_refresh"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_refresh_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_log"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_log_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_homepage"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_homepage_cb), application);
	/* TRANSLATORS: tooltip on the homepage button */
	gtk_widget_set_tooltip_text (widget, _("Visit home page for selected package"));

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_files"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_files_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_install"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_install_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_remove"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_remove_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_depends"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_depends_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_requires"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_requires_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_run"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_run_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_quit"));
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_quit_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_selection"));
	gtk_widget_hide (widget);

	/* installed filter */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_installed_yes"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_installed_cb), application);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_installed_no"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_installed_cb), application);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_installed_both"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_installed_cb), application);

	/* devel filter */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_devel_yes"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_devel_cb), application);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_devel_no"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_devel_cb), application);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_devel_both"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_devel_cb), application);

	/* gui filter */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_gui_yes"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_gui_cb), application);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_gui_no"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_gui_cb), application);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_gui_both"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_gui_cb), application);

	/* free filter */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_free_yes"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_free_cb), application);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_free_no"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_free_cb), application);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_free_both"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_free_cb), application);

	/* source filter */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_source_yes"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_source_cb), application);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_source_no"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_source_cb), application);
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_source_both"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_source_cb), application);

	/* basename filter */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_basename"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_basename_cb), application);

	/* newest filter */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_newest"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_newest_cb), application);

	/* newest filter */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_arch"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_arch_cb), application);

	/* Remove description/file list if needed. */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_DETAILS) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "scrolledwindow2"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_FILES) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_files"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_DEPENDS) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_depends"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_REQUIRES) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_requires"));
		gtk_widget_hide (widget);
	}

	/* hide the group selector if we don't support search-groups */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_GROUP) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "scrolledwindow_groups"));
		gtk_widget_hide (widget);
	}

	/* hide the refresh cache button if we can't do it */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_REFRESH_CACHE) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_refresh"));
		gtk_widget_hide (widget);
	}

	/* hide the software-sources button if we can't do it */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_REPO_LIST) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_sources"));
		gtk_widget_hide (widget);
	}

	/* simple find button */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_find"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_find_cb), application);
	/* TRANSLATORS: tooltip on the find button */
	gtk_widget_set_tooltip_text (widget, _("Find packages"));

	/* search cancel button */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_cancel"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_cancel_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);
	/* TRANSLATORS: tooltip on the cancel button */
	gtk_widget_set_tooltip_text (widget, _("Cancel search"));

	/* the fancy text entry widget */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "entry_text"));

	/* autocompletion can be turned off as it's slow */
	ret = gconf_client_get_bool (application->priv->gconf_client, GPK_CONF_AUTOCOMPLETE, NULL);
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
			  G_CALLBACK (gpk_application_find_cb), application);
	g_signal_connect (widget, "paste-clipboard",
			  G_CALLBACK (gpk_application_find_cb), application);
	g_signal_connect (widget, "icon-press",
			  G_CALLBACK (gpk_application_entry_text_icon_press_cb), application);

	/* hide the filters we can't support */
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_INSTALLED) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_installed"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_DEVELOPMENT) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_devel"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_GUI) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_gui"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_FREE) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_free"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_ARCH) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_arch"));
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_SOURCE) == FALSE) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_source"));
		gtk_widget_hide (widget);
	}

	/* BASENAME, use by default, or hide */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_basename"));
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_BASENAME)) {
		enabled = gconf_client_get_bool (application->priv->gconf_client,
						 GPK_CONF_APPLICATION_FILTER_BASENAME, NULL);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), enabled);
		/* work round a gtk2+ bug: toggled should be fired when doing gtk_check_menu_item_set_active */
		gpk_application_menu_filter_basename_cb (widget, application);
	} else {
		gtk_widget_hide (widget);
	}

	/* NEWEST, use by default, or hide */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_newest"));
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_NEWEST)) {
		/* set from remembered state */
		enabled = gconf_client_get_bool (application->priv->gconf_client,
						 GPK_CONF_APPLICATION_FILTER_NEWEST, NULL);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), enabled);
		/* work round a gtk2+ bug: toggled should be fired when doing gtk_check_menu_item_set_active */
		gpk_application_menu_filter_newest_cb (widget, application);
	} else {
		gtk_widget_hide (widget);
	}

	/* ARCH, use by default, or hide */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "menuitem_arch"));
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_ARCH)) {
		/* set from remembered state */
		enabled = gconf_client_get_bool (application->priv->gconf_client,
						 GPK_CONF_APPLICATION_FILTER_ARCH, NULL);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), enabled);
		/* work round a gtk2+ bug: toggled should be fired when doing gtk_check_menu_item_set_active */
		gpk_application_menu_filter_arch_cb (widget, application);
	} else {
		gtk_widget_hide (widget);
	}

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "entry_text"));
	g_signal_connect (widget, "key-press-event",
			  G_CALLBACK (gpk_application_text_changed_cb), application);
	g_signal_connect (widget, "key-release-event",
			  G_CALLBACK (gpk_application_text_changed_cb), application);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "button_find"));
	gtk_widget_set_sensitive (widget, FALSE);

	/* set a size, if the screen allows */
	ret = gpk_window_set_size_request (GTK_WINDOW (main_window), 1000, 500);

	/* we are small form factor */
	if (!ret) {
		widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "hbox_packages"));
		gtk_box_set_homogeneous (GTK_BOX (widget), FALSE);
	}
	gtk_widget_show (GTK_WIDGET(main_window));

	/* set details box decent size */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "hbox_packages"));
	gtk_widget_set_size_request (widget, -1, 120);

	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));
	g_signal_connect (GTK_TREE_VIEW (widget), "row-activated",
			  G_CALLBACK (gpk_application_package_row_activated_cb), application);

	/* use a list store for the extra data */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_detail"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget), GTK_TREE_MODEL (application->priv->details_store));

	/* add columns to the tree view */
	gpk_application_treeview_add_columns_description (application);

	/* sorted */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (application->priv->packages_store),
					      PACKAGES_COLUMN_ID, GTK_SORT_ASCENDING);

	/* create package tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_packages"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (application->priv->packages_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_application_packages_treeview_clicked_cb), application);

	/* add columns to the tree view */
	gpk_application_packages_add_columns (application);

	/* set up the groups checkbox */
	widget = GTK_WIDGET (gtk_builder_get_object (application->priv->builder, "treeview_groups"));

	/* add columns to the tree view */
	gpk_application_groups_add_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (widget), GROUPS_COLUMN_SUMMARY);
	gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (widget), FALSE);
	gtk_tree_view_set_level_indentation  (GTK_TREE_VIEW (widget), 9);
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (application->priv->groups_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_application_groups_treeview_changed_cb), application);

	/* simple list or category tree? */
	ret = gconf_client_get_bool (application->priv->gconf_client, GPK_CONF_APPLICATION_CATEGORY_GROUPS, NULL);
	if (ret)
		ret = gpk_application_create_group_list_categories (application);

	/* fallback to creating a simple list if we can't do category list */
	if (!ret)
		gpk_application_create_group_list_enum (application);

	/* reset client */
	ret = pk_client_reset (application->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out_build;
	}

	/* get repos, so we can show the full name in the software source box */
	ret = pk_client_get_repo_list (application->priv->client_primary, PK_FILTER_ENUM_NONE, &error);
	if (!ret) {
		egg_warning ("failed to get repo list: %s", error->message);
		g_error_free (error);
	}

	/* set current action */
	application->priv->action = PK_ACTION_NONE;
	gpk_application_set_buttons_apply_clear (application);

	/* hide details */
	gpk_application_clear_details (application);

	/* set the search mode */
	mode = gconf_client_get_string (application->priv->gconf_client, GPK_CONF_APPLICATION_SEARCH_MODE, NULL);
	if (mode == NULL) {
		egg_warning ("search mode not set, using name");
		mode = g_strdup ("name");
	}

	/* search by name */
	if (g_strcmp0 (mode, "name") == 0) {
		gpk_application_menu_search_by_name (NULL, application);

	/* set to details if we can we do the action? */
	} else if (g_strcmp0 (mode, "details") == 0) {
		if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_DETAILS)) {
			gpk_application_menu_search_by_description (NULL, application);
		} else {
			egg_warning ("cannont use mode %s as not capable, using name", mode);
			gpk_application_menu_search_by_name (NULL, application);
		}

	/* set to file if we can we do the action? */
	} else if (g_strcmp0 (mode, "file") == 0) {
		gpk_application_menu_search_by_file (NULL, application);

		if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_FILE)) {
			gpk_application_menu_search_by_file (NULL, application);
		} else {
			egg_warning ("cannont use mode %s as not capable, using name", mode);
			gpk_application_menu_search_by_name (NULL, application);
		}

	/* mode not recognised */
	} else {
		egg_warning ("cannot recognise mode %s, using name", mode);
		gpk_application_menu_search_by_name (NULL, application);
	}
	g_free (mode);

out_build:
	/* welcome */
	gpk_application_add_welcome (application);
}

/**
 * gpk_application_finalize:
 * @object: This graph class instance
 **/
static void
gpk_application_finalize (GObject *object)
{
	GpkApplication *application;
	g_return_if_fail (GPK_IS_APPLICATION (object));

	application = GPK_APPLICATION (object);
	application->priv = GPK_APPLICATION_GET_PRIVATE (application);

	if (application->priv->details_event_id > 0)
		g_source_remove (application->priv->details_event_id);

	g_object_unref (application->priv->packages_store);
	g_object_unref (application->priv->details_store);
	g_object_unref (application->priv->control);
	g_object_unref (application->priv->client_primary);
	g_object_unref (application->priv->client_secondary);
	g_object_unref (application->priv->pconnection);
	g_object_unref (application->priv->desktop);
	g_object_unref (application->priv->gconf_client);
	g_object_unref (application->priv->markdown);
	g_object_unref (application->priv->builder);
	g_object_unref (application->priv->helper_eula);
	g_object_unref (application->priv->helper_run);
	g_object_unref (application->priv->helper_deps_remove);
	g_object_unref (application->priv->helper_deps_install);
	g_object_unref (application->priv->helper_media_change);
	g_object_unref (application->priv->helper_repo_signature);

	if (application->priv->status_id > 0)
		g_source_remove (application->priv->status_id);

	g_free (application->priv->url);
	g_free (application->priv->group);
	g_free (application->priv->search_text);
	g_hash_table_destroy (application->priv->repos);

	G_OBJECT_CLASS (gpk_application_parent_class)->finalize (object);
}

/**
 * gpk_application_new:
 * Return value: new GpkApplication instance.
 **/
GpkApplication *
gpk_application_new (void)
{
	GpkApplication *application;
	application = g_object_new (GPK_TYPE_APPLICATION, NULL);
	return GPK_APPLICATION (application);
}

