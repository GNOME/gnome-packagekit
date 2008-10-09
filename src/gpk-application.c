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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <libsexy/sexy-icon-entry.h>
#include <math.h>
#include <string.h>
#include <polkit-gnome/polkit-gnome.h>

#include <pk-enum.h>
#include <pk-client.h>
#include <pk-control.h>
#include <pk-common.h>
#include <pk-connection.h>
#include <pk-package-id.h>
#include <pk-package-ids.h>
#include <pk-package-list.h>
#include <pk-extra.h>
#include <pk-details-obj.h>
#include <pk-category-obj.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-obj-list.h"

#include "gpk-client.h"
#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-error.h"
#include "gpk-enum.h"
#include "gpk-application.h"
#include "gpk-animated-icon.h"
#include "gpk-dialog.h"
#include "gpk-client-run.h"
#include "gpk-client-chooser.h"
#include "gpk-cell-renderer-uri.h"

static void     gpk_application_class_init (GpkApplicationClass *klass);
static void     gpk_application_init       (GpkApplication      *application);
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
	GladeXML		*glade_xml;
	GConfClient		*gconf_client;
	GtkListStore		*packages_store;
	GtkTreeStore		*groups_store;
	GtkListStore		*details_store;
	PkControl		*control;
	PkClient		*client_search;
	PkClient		*client_action;
	PkClient		*client_details;
	PkClient		*client_files;
	GpkClient		*gclient;
	PkConnection		*pconnection;
	PkExtra			*extra;
	gchar			*package;
	gchar			*group;
	gchar			*url;
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
	PACKAGES_COLUMN_CHECKBOX_ENABLE, /* sensitive */
	PACKAGES_COLUMN_TEXT,
	PACKAGES_COLUMN_ID,
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

static gboolean gpk_application_refresh_search_results (GpkApplication *application);

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
	GtkWidget *widget;
	widget = glade_xml_get_widget (application->priv->glade_xml, "window_manager");
	gtk_window_present (GTK_WINDOW (widget));
}

/**
 * gpk_application_state_get_icon:
 **/
const gchar *
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
gboolean
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
 * gpk_application_set_find_cancel_buttons:
 **/
static void
gpk_application_set_find_cancel_buttons (GpkApplication *application, gboolean find)
{
	GtkWidget *widget;

	/* if we can't do it, then just make the button insensitive */
	if (!pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_CANCEL)) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_cancel");
		gtk_widget_set_sensitive (widget, FALSE);
	}

	/* which tab to enable? */
	widget = glade_xml_get_widget (application->priv->glade_xml, "notebook_search_cancel");
	if (find) {
		gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), 0);
	} else {
		gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), 1);
	}
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
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_install");
	gtk_widget_set_sensitive (widget, allow);
}

/**
 * gpk_application_allow_remove:
 **/
static void
gpk_application_allow_remove (GpkApplication *application, gboolean allow)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_remove");
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
	GtkWidget *widget;
	const gchar *icon;
	gboolean checkbox;
	PkBitfield state;
	gboolean ret;

	/* get the selection and add */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	treeview = GTK_TREE_VIEW (widget);
	selection = gtk_tree_view_get_selection (treeview);
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret) {
		egg_warning ("no selection");
		return;
	}

	gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_STATE, &state, -1);

	/* do something with the value */
	pk_bitfield_invert (state, GPK_STATE_IN_LIST);

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
	guint length;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* okay to apply? */
	length = pk_package_list_get_size (application->priv->package_list);
	if (length == 0) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_apply");
		gtk_widget_set_sensitive (widget, FALSE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_clear");
		gtk_widget_set_sensitive (widget, FALSE);
		application->priv->action = PK_ACTION_NONE;
	} else {
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_apply");
		gtk_widget_set_sensitive (widget, TRUE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_clear");
		gtk_widget_set_sensitive (widget, TRUE);
	}

	/* correct the enabled state */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	treeview = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* for all current items, reset the state if in the list */
	while (valid) {
		gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_STATE, &state, -1);
		enabled = gpk_application_get_checkbox_enable (application, state);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, PACKAGES_COLUMN_CHECKBOX_ENABLE, enabled, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * gpk_application_install:
 **/
static gboolean
gpk_application_install (GpkApplication *application)
{
	gboolean ret;
	PkPackageId *id;

	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	/* shouldn't be possible */
	if (application->priv->package == NULL) {
		egg_warning ("no package");
		return FALSE;
	}

	/* changed mind, or wrong mode */
	if (application->priv->action == PK_ACTION_REMOVE) {
		ret = pk_package_list_remove (application->priv->package_list, application->priv->package);
		if (ret) {
			egg_debug ("removed %s from package list", application->priv->package);

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
	ret = pk_package_list_contains (application->priv->package_list, application->priv->package);
	if (ret) {
		egg_warning ("already added");
		return FALSE;
	}

	application->priv->action = PK_ACTION_INSTALL;
	id = pk_package_id_new_from_string (application->priv->package);
	pk_package_list_add (application->priv->package_list, 0, id, NULL);
	pk_package_id_free (id);

	/* correct buttons */
	gpk_application_allow_install (application, FALSE);
	gpk_application_allow_remove (application, TRUE);
	gpk_application_packages_checkbox_invert (application);
	gpk_application_set_buttons_apply_clear (application);
	return TRUE;
}

/**
 * gpk_application_menu_homepage_cb:
 **/
static void
gpk_application_menu_homepage_cb (GtkAction *action, GpkApplication *application)
{
	g_return_if_fail (PK_IS_APPLICATION (application));
	gpk_gnome_open (application->priv->url);
}

/**
 * gpk_application_strcmp_indirect:
 **/
gint
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
	GPtrArray *array;
	GError *error = NULL;
	gchar **files;
	gchar *title;
	GtkWidget *widget;
	GtkWidget *dialog;
	PkPackageId *id;

	g_return_if_fail (PK_IS_APPLICATION (application));

	files = gpk_client_get_file_list (application->priv->gclient, application->priv->package, &error);
	if (files == NULL) {
		egg_warning ("could not get file list: %s", error->message);
		g_error_free (error);
		return;
	}

	/* convert to pointer array */
	array = pk_strv_to_ptr_array (files);
	g_ptr_array_sort (array, (GCompareFunc) gpk_application_strcmp_indirect);

	/* title */
	id = pk_package_id_new_from_string (application->priv->package);
	title = g_strdup_printf (ngettext ("%i file installed by %s",
					   "%i files installed by %s",
					   array->len), array->len, id->name);

	widget = glade_xml_get_widget (application->priv->glade_xml, "window_manager");
	dialog = gtk_message_dialog_new (GTK_WINDOW (widget), GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
	gpk_dialog_embed_file_list_widget (GTK_DIALOG (dialog), array);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));

	g_free (title);
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);
	g_strfreev (files);
	pk_package_id_free (id);
}

/**
 * gpk_application_remove:
 **/
static gboolean
gpk_application_remove (GpkApplication *application)
{
	gboolean ret;
	PkPackageId *id;

	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	/* shouldn't be possible */
	if (application->priv->package == NULL) {
		egg_warning ("no package");
		return FALSE;
	}

	/* changed mind, or wrong mode */
	if (application->priv->action == PK_ACTION_INSTALL) {
		ret = pk_package_list_remove (application->priv->package_list, application->priv->package);
		if (ret) {
			egg_debug ("removed %s from package list", application->priv->package);

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
	ret = pk_package_list_contains (application->priv->package_list, application->priv->package);
	if (ret) {
		egg_warning ("already added");
		return FALSE;
	}

	application->priv->action = PK_ACTION_REMOVE;
	id = pk_package_id_new_from_string (application->priv->package);
	pk_package_list_add (application->priv->package_list, 0, id, NULL);
	pk_package_id_free (id);

	/* correct buttons */
	gpk_application_allow_install (application, TRUE);
	gpk_application_allow_remove (application, FALSE);
	gpk_application_packages_checkbox_invert (application);
	gpk_application_set_buttons_apply_clear (application);
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
	gchar *exec;
	GError *error = NULL;
	gchar **package_ids;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	GtkWidget *widget;
	PkBitfield state;
	gboolean ret;
	gchar *package_id = NULL;

	/* get selection */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	treeview = GTK_TREE_VIEW (widget);
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
		exec = gpk_client_run_show (package_ids);
		if (exec != NULL) {
			ret = g_spawn_command_line_async (exec, &error);
			if (!ret) {
				egg_warning ("failed to run: %s", error->message);
				g_error_free (error);
			}
		}
		g_free (exec);
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
	PkPackageList *list;
	GtkWidget *widget;
	gchar **package_ids;

	/* cancel any previous request */
	ret = pk_client_reset (application->priv->client_files, &error);
	if (!ret) {
		egg_warning ("failed to cancel, and adding to queue: %s", error->message);
		g_error_free (error);
		return;
	}

	/* get the requires */
	pk_client_set_synchronous (application->priv->client_files, TRUE, NULL);
	package_ids = pk_package_ids_from_id (application->priv->package);
	ret = pk_client_get_requires (application->priv->client_files, PK_FILTER_ENUM_NONE,
				      package_ids, TRUE, &error);
	pk_client_set_synchronous (application->priv->client_files, FALSE, NULL);

	if (!ret) {
		egg_warning ("failed to get requires: %s", error->message);
		g_error_free (error);
		return;
	}

	list = pk_client_get_package_list (application->priv->client_files);
	widget = glade_xml_get_widget (application->priv->glade_xml, "window_manager");
	if (pk_package_list_get_size (list) == 0) {
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("No packages"),
					_("No other packages require this package"), NULL);
	} else {
		gchar *name;
		gchar *title;
		gchar *message;
		guint length;
		GtkWidget *dialog;

		length = pk_package_list_get_size (list);
		name = gpk_dialog_package_id_name_join_locale (package_ids);
		title = g_strdup_printf (ngettext ("%i additional package require %s",
						   "%i additional packages require %s",
						   length), length, name);

		message = g_strdup_printf (ngettext ("Packages listed below require %s to function correctly.",
						     "Packages listed below require %s to function correctly.",
						     length), name);

		dialog = gtk_message_dialog_new (GTK_WINDOW (widget), GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
		gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
		gpk_dialog_embed_package_list_widget (GTK_DIALOG (dialog), list);

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));

		g_free (name);
		g_free (title);
		g_free (message);
	}

	g_strfreev (package_ids);
	g_object_unref (list);
}

/**
 * gpk_application_menu_depends_cb:
 **/
static void
gpk_application_menu_depends_cb (GtkAction *action, GpkApplication *application)
{
	GError *error = NULL;
	gboolean ret;
	PkPackageList *list;
	GtkWidget *widget;
	gchar **package_ids;

	/* cancel any previous request */
	ret = pk_client_reset (application->priv->client_files, &error);
	if (!ret) {
		egg_warning ("failed to cancel, and adding to queue: %s", error->message);
		g_error_free (error);
		return;
	}

	/* get the depends */
	pk_client_set_synchronous (application->priv->client_files, TRUE, NULL);
	package_ids = pk_package_ids_from_id (application->priv->package);
	ret = pk_client_get_depends (application->priv->client_files, PK_FILTER_ENUM_NONE,
				     package_ids, TRUE, &error);
	pk_client_set_synchronous (application->priv->client_files, FALSE, NULL);

	if (!ret) {
		egg_warning ("failed to get depends: %s", error->message);
		g_error_free (error);
		return;
	}

	list = pk_client_get_package_list (application->priv->client_files);
	widget = glade_xml_get_widget (application->priv->glade_xml, "window_manager");
	if (pk_package_list_get_size (list) == 0) {
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("No packages"),
					_("This package does not depends on any others"), NULL);
	} else {
		gchar *name;
		gchar *title;
		gchar *message;
		guint length;
		GtkWidget *dialog;

		length = pk_package_list_get_size (list);
		name = gpk_dialog_package_id_name_join_locale (package_ids);
		title = g_strdup_printf (ngettext ("%i additional package is required for %s",
						   "%i additional packages are required for %s",
						   length), length, name);

		message = g_strdup_printf (ngettext ("Packages listed below are required for %s to function correctly.",
						     "Packages listed below are required for %s to function correctly.",
						     length), name);

		dialog = gtk_message_dialog_new (GTK_WINDOW (widget), GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", title);
		gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
		gpk_dialog_embed_package_list_widget (GTK_DIALOG (dialog), list);

		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));

		g_free (name);
		g_free (title);
		g_free (message);
	}

	g_strfreev (package_ids);
	g_object_unref (list);
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
	GtkWidget *tree_view;
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

	tree_view = glade_xml_get_widget (application->priv->glade_xml, "treeview_detail");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_NONE);
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (tree_view));
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
	widget = glade_xml_get_widget (application->priv->glade_xml, "textview_description");
	gpk_application_set_text_buffer (widget, NULL);

	/* hide dead widgets */
	widget = glade_xml_get_widget (application->priv->glade_xml, "scrolledwindow_detail");
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
	application->priv->details_event_id = g_timeout_add (100, (GSourceFunc) gpk_application_clear_details_really, application);
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
 * gpk_application_details_cb:
 **/
static void
gpk_application_details_cb (PkClient *client, PkDetailsObj *details, GpkApplication *application)
{
	GtkWidget *widget;
	gchar *text;
	gchar *value;
	const gchar *repo_name;
	const gchar *icon;
	const gchar *group;
	gboolean valid;
	gboolean installed;

	g_return_if_fail (PK_IS_APPLICATION (application));

	installed = egg_strequal (details->id->data, "installed");

	/* get the icon */
	icon = pk_extra_get_icon_name (application->priv->extra, details->id->name);

	/* check icon actually exists and is valid in this theme */
	valid = gpk_check_icon_valid (icon);

	/* hide to start */
	widget = glade_xml_get_widget (application->priv->glade_xml, "scrolledwindow_detail");
	gtk_widget_show (widget);

	gtk_list_store_clear (application->priv->details_store);

	/* if a collection, mark as such */
	if (egg_strequal (details->id->data, "meta"))
		gpk_application_add_detail_item (application, _("Type"), _("Collection"), NULL);

	/* homepage */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_homepage");
	if (egg_strzero (details->url) == FALSE) {
		gtk_widget_set_sensitive (widget, TRUE);

		/* set the tooltip to where we are going */
		text = g_strdup_printf (_("Visit %s"), details->url);
		gtk_widget_set_tooltip_text (widget, text);
		g_free (text);

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
		gpk_application_add_detail_item (application, _("Group"), group, NULL);
	}

	/* group */
	if (!egg_strzero (details->license)) {
		/* This should be a licence enum value - bad API, bad.
		 * license = pk_license_enum_to_text (license_enum); */
		gpk_application_add_detail_item (application, _("License"), details->license, NULL);
	}

	/* set the description */
	text = g_markup_escape_text (details->description, -1);
	widget = glade_xml_get_widget (application->priv->glade_xml, "textview_description");
	gpk_application_set_text_buffer (widget, text);
	g_free (text);

	/* if non-zero, set the size */
	if (details->size > 0) {
		/* set the size */
		value = g_format_size_for_display (details->size);
		if (egg_strequal (details->id->data, "meta"))
			gpk_application_add_detail_item (application, _("Size"), value, NULL);
		else if (installed)
			gpk_application_add_detail_item (application, _("Installed size"), value, NULL);
		else
			gpk_application_add_detail_item (application, _("Download size"), value, NULL);
		g_free (value);
	}

	/* set the repo text, or hide if installed */
	if (!installed && !egg_strequal (details->id->data, "meta")) {
		/* get the full name of the repo from the repo_id */
		repo_name = gpk_application_get_full_repo_name (application, details->id->data);
		gpk_application_add_detail_item (application, _("Source"), repo_name, NULL);
	}
}

/**
 * gpk_application_package_cb:
 **/
static void
gpk_application_package_cb (PkClient *client, const PkPackageObj *obj, GpkApplication *application)
{
	GtkTreeIter iter;
	const gchar *summary_new;
	const gchar *icon = NULL;
	gchar *text;
	gchar *package_id;
	gboolean in_queue;
	gboolean installed;
	gboolean checkbox;
	gboolean enabled;
	PkBitfield state = 0;
	static guint package_cnt = 0;

	g_return_if_fail (PK_IS_APPLICATION (application));

	egg_debug ("package = %s:%s:%s", pk_info_enum_to_text (obj->info), obj->id->name, obj->summary);

	/* ignore progress */
	if (obj->info != PK_INFO_ENUM_INSTALLED && obj->info != PK_INFO_ENUM_AVAILABLE &&
	    obj->info != PK_INFO_ENUM_COLLECTION_INSTALLED && obj->info != PK_INFO_ENUM_COLLECTION_AVAILABLE)
		return;

	/* use the localised summary if available */
	summary_new = pk_extra_get_summary (application->priv->extra, obj->id->name);
	if (TRUE || summary_new == NULL)
		summary_new = obj->summary;

	/* mark as got so we don't warn */
	application->priv->has_package = TRUE;

	/* are we in the package list? */
	in_queue = pk_package_list_contains_obj (application->priv->package_list, obj);
	installed = (obj->info == PK_INFO_ENUM_INSTALLED) || (obj->info == PK_INFO_ENUM_COLLECTION_INSTALLED);

	if (installed)
		pk_bitfield_add (state, GPK_STATE_INSTALLED);
	if (in_queue)
		pk_bitfield_add (state, GPK_STATE_IN_LIST);

	/* special icon */
	if (obj->info == PK_INFO_ENUM_COLLECTION_INSTALLED || obj->info == PK_INFO_ENUM_COLLECTION_AVAILABLE)
		pk_bitfield_add (state, GPK_STATE_COLLECTION);

	/* use the application icon if available */
	icon = pk_extra_get_icon_name (application->priv->extra, obj->id->name);
	if (icon == NULL)
		icon = gpk_application_state_get_icon (state);

	checkbox = gpk_application_state_get_checkbox (state);

	/* use two lines */
	text = gpk_package_id_format_twoline (obj->id, summary_new);

	/* can we modify this? */
	enabled = gpk_application_get_checkbox_enable (application, state);

	package_id = pk_package_id_to_string (obj->id);
	gtk_list_store_append (application->priv->packages_store, &iter);
	gtk_list_store_set (application->priv->packages_store, &iter,
			    PACKAGES_COLUMN_STATE, state,
			    PACKAGES_COLUMN_CHECKBOX, installed ^ in_queue,
			    PACKAGES_COLUMN_CHECKBOX_ENABLE, enabled,
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_ID, package_id,
			    PACKAGES_COLUMN_IMAGE, icon,
			    -1);

	g_free (package_id);
	g_free (text);

	/* only process every n events else we re-order too many times */
	if (package_cnt++ % 200 == 0) {
		while (gtk_events_pending ())
			gtk_main_iteration ();
	}
}

/**
 * gpk_application_error_code_cb:
 **/
static void
gpk_application_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, GpkApplication *application)
{
	GtkWidget *widget;
	g_return_if_fail (PK_IS_APPLICATION (application));

	/* obvious message, don't tell the user */
	if (code == PK_ERROR_ENUM_TRANSACTION_CANCELLED)
		return;

	widget = glade_xml_get_widget (application->priv->glade_xml, "window_manager");
	gpk_error_dialog_modal (GTK_WINDOW (widget), gpk_error_enum_to_localised_text (code),
				gpk_error_enum_to_localised_message (code), details);
}

/**
 * gpk_application_refresh_search_results:
 **/
static gboolean
gpk_application_refresh_search_results (GpkApplication *application)
{
	gboolean ret;
	GError *error = NULL;
	PkRoleEnum role;

	/* get role -- do we actually need to do anything */
	pk_client_get_role (application->priv->client_search, &role, NULL, NULL);
	if (role == PK_ROLE_ENUM_UNKNOWN) {
		egg_debug ("no defined role, no not requeuing");
		return FALSE;
	}

	/* hide details */
	gpk_application_clear_details (application);
	gpk_application_clear_packages (application);

	ret = pk_client_requeue (application->priv->client_search, &error);
	if (!ret) {
		egg_warning ("failed to requeue the search: %s", error->message);
		g_error_free (error);
		return FALSE;
	}
	return TRUE;
}

/**
 * gpk_application_suggest_better_search:
 **/
static void
gpk_application_suggest_better_search (GpkApplication *application)
{
	const gchar *message = NULL;
	const gchar *title = _("No results were found.");
	GtkTreeIter iter;
	gchar *text;
	PkBitfield state = 0;

	if (application->priv->search_mode == PK_MODE_GROUP ||
	    application->priv->search_mode == PK_MODE_ALL_PACKAGES) {
		/* this shouldn't happen */
		message = _("Try entering a package name in the search bar.");
	} else {
		if (application->priv->search_type == PK_SEARCH_NAME ||
		    application->priv->search_type == PK_SEARCH_FILE)
			message = _("Try searching package descriptions by clicking the icon next to the search text.");
		else
			message = _("Try again with a different search term.");
	}

	text = g_strdup_printf ("%s\n%s", title, message);
	gtk_list_store_append (application->priv->packages_store, &iter);
	gtk_list_store_set (application->priv->packages_store, &iter,
			    PACKAGES_COLUMN_STATE, state,
			    PACKAGES_COLUMN_CHECKBOX, FALSE,
			    PACKAGES_COLUMN_CHECKBOX_ENABLE, FALSE,
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_IMAGE, "system-search",
			    -1);
	g_free (text);
}

/**
 * gpk_application_finished_cb:
 **/
static void
gpk_application_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, GpkApplication *application)
{
	GtkWidget *widget;
	PkRoleEnum role;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* get role */
	pk_client_get_role (client, &role, NULL, NULL);

	if (role == PK_ROLE_ENUM_SEARCH_NAME ||
	    role == PK_ROLE_ENUM_SEARCH_DETAILS ||
	    role == PK_ROLE_ENUM_SEARCH_GROUP ||
	    role == PK_ROLE_ENUM_GET_PACKAGES) {

		/* switch round buttons */
		gpk_application_set_find_cancel_buttons (application, TRUE);

		/* were there no entries found? */
		if (exit == PK_EXIT_ENUM_SUCCESS && !application->priv->has_package) {

			/* try to be helpful... */
			gpk_application_suggest_better_search (application);
		}

		/* focus back to the text extry */
		widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
		gtk_widget_grab_focus (widget);
	}

	/* do we need to update the search? */
	if (role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		/* refresh the search as the items may have changed and the filter has not changed */
		gpk_application_refresh_search_results (application);
	}
}

/**
 * gpk_application_cancel_cb:
 **/
static void
gpk_application_cancel_cb (GtkWidget *button_widget, GpkApplication *application)
{
	gboolean ret;

	g_return_if_fail (PK_IS_APPLICATION (application));

	ret = pk_client_cancel (application->priv->client_search, NULL);
	egg_debug ("canceled? %i", ret);

	/* switch buttons around */
	if (ret) {
		gpk_application_set_find_cancel_buttons (application, TRUE);
		application->priv->search_mode = PK_MODE_UNKNOWN;
	}
}

/**
 * gpk_application_perform_search_name_details_file:
 **/
static gboolean
gpk_application_perform_search_name_details_file (GpkApplication *application)
{
	GtkWidget *widget;
	const gchar *package;
	GError *error = NULL;
	gboolean ret;

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	package = gtk_entry_get_text (GTK_ENTRY (widget));

	/* have we got input? */
	if (egg_strzero (package)) {
		egg_debug ("no input");
		return FALSE;
	}

	ret = pk_strvalidate (package);
	if (!ret) {
		egg_debug ("invalid input text, will fail");
		/* TODO - make the dialog turn red... */
		widget = glade_xml_get_widget (application->priv->glade_xml, "window_manager");
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Invalid search text"),
					_("The search text contains invalid characters"), NULL);
		return FALSE;
	}
	egg_debug ("find %s", package);

	/* reset */
	ret = pk_client_reset (application->priv->client_search, &error);
	if (!ret) {
		egg_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* do the search */
	if (application->priv->search_type == PK_SEARCH_NAME) {
		ret = pk_client_search_name (application->priv->client_search, application->priv->filters_current, package, &error);
	} else if (application->priv->search_type == PK_SEARCH_DETAILS) {
		ret = pk_client_search_details (application->priv->client_search, application->priv->filters_current, package, &error);
	} else if (application->priv->search_type == PK_SEARCH_FILE) {
		ret = pk_client_search_file (application->priv->client_search, application->priv->filters_current, package, &error);
	} else {
		egg_warning ("invalid search type");
		return FALSE;
	}

	if (!ret) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "window_manager");
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("The search could not be completed"),
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
	GtkWidget *widget;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);
	g_return_val_if_fail (application->priv->group != NULL, FALSE);

	/* cancel this, we don't care about old results that are pending */
	ret = pk_client_reset (application->priv->client_search, &error);
	if (!ret) {
		egg_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	if (application->priv->search_mode == PK_MODE_GROUP) {
		ret = pk_client_search_group (application->priv->client_search,
					      application->priv->filters_current,
					      application->priv->group, &error);
	} else {
		ret = pk_client_get_packages (application->priv->client_search,
					      application->priv->filters_current, &error);
	}

	if (!ret) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "window_manager");
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("The group could not be queried"),
					_("Running the transaction failed"), error->message);
		g_error_free (error);
		return FALSE;
	}

	return TRUE;
}

/**
 * gpk_application_perform_search:
 **/
static gboolean
gpk_application_perform_search (GpkApplication *application)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	gpk_application_clear_details (application);
	gpk_application_clear_packages (application);

	if (application->priv->search_mode == PK_MODE_NAME_DETAILS_FILE) {
		ret = gpk_application_perform_search_name_details_file (application);
	} else if (application->priv->search_mode == PK_MODE_GROUP ||
		   application->priv->search_mode == PK_MODE_ALL_PACKAGES) {
		ret = gpk_application_perform_search_others (application);
	} else {
		egg_debug ("doing nothing");
	}
	if (!ret)
		return ret;

	/* switch around buttons */
	gpk_application_set_find_cancel_buttons (application, FALSE);

	return ret;
}

/**
 * gpk_application_find_cb:
 **/
static void
gpk_application_find_cb (GtkWidget *button_widget, GpkApplication *application)
{
	g_return_if_fail (PK_IS_APPLICATION (application));

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
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	/* we might have visual stuff running, close them down */
	ret = pk_client_cancel (application->priv->client_search, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
		error = NULL;
	}
	ret = pk_client_cancel (application->priv->client_details, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
		error = NULL;
	}
	ret = pk_client_cancel (application->priv->client_files, &error);
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
	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	gpk_application_quit (application);
	return FALSE;
}

/**
 * gpk_application_text_changed_cb:
 **/
static gboolean
gpk_application_text_changed_cb (GtkEntry *entry, GdkEventKey *event, GpkApplication *application)
{
	gboolean valid;
	GtkWidget *widget;
	const gchar *package;
	GtkTreeSelection *selection;

	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	package = gtk_entry_get_text (GTK_ENTRY (widget));

	/* clear group selection if we have the tab */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_groups");
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
		gtk_tree_selection_unselect_all (selection);
	}

	/* check for invalid chars */
	valid = pk_strvalidate (package);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
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
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeSelection *selection;
	PkBitfield state;

	g_return_if_fail (PK_IS_APPLICATION (application));

	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	treeview = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (treeview);
	path = gtk_tree_path_new_from_string (path_str);

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	g_free (application->priv->package);
	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_STATE, &state,
			    PACKAGES_COLUMN_ID, &application->priv->package, -1);

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
	GtkWidget *widget;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	const gchar *icon;
	PkBitfield state;
	gboolean ret;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* get the first iter in the list */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	treeview = GTK_TREE_VIEW (widget);
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

	pk_package_list_clear (application->priv->package_list);

	/* force a button refresh */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
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
	gchar *exec;

	g_return_if_fail (PK_IS_APPLICATION (application));

	package_ids = pk_package_list_to_strv (application->priv->package_list);
	if (application->priv->action == PK_ACTION_INSTALL) {
		gpk_client_set_interaction (application->priv->gclient, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
		ret = gpk_client_install_package_ids (application->priv->gclient, package_ids, NULL);
		/* can we show the user the new application? */
		if (ret) {
			exec = gpk_client_run_show (package_ids);
			if (exec != NULL) {
				ret = g_spawn_command_line_async (exec, &error);
				if (!ret) {
					egg_warning ("failed to run: %s", error->message);
					g_error_free (error);
				}
			}
			g_free (exec);
		}
	}
	if (application->priv->action == PK_ACTION_REMOVE) {
		gpk_client_set_interaction (application->priv->gclient, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
		ret = gpk_client_remove_package_ids (application->priv->gclient, package_ids, NULL);
	}
	g_strfreev (package_ids);

	/* refresh the search as the items may have changed and the filter has not changed */
	if (ret) {
		/* clear if success */
		pk_package_list_clear (application->priv->package_list);
		application->priv->action = PK_ACTION_NONE;
		gpk_application_set_buttons_apply_clear (application);
		gpk_application_refresh_search_results (application);
	}
}

static void
gpk_application_packages_add_columns (GpkApplication *application)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkWidget *widget;

	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	treeview = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (treeview);

	/* column for installed toggles */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (gpk_application_packages_installed_clicked_cb), application);
	column = gtk_tree_view_column_new_with_attributes (_("Installed"), renderer,
							   "active", PACKAGES_COLUMN_CHECKBOX,
							   "visible", PACKAGES_COLUMN_CHECKBOX_ENABLE, NULL);
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
	column = gtk_tree_view_column_new_with_attributes (_("Name"), renderer,
							   "text", GROUPS_COLUMN_NAME,
							   "text", GROUPS_COLUMN_SUMMARY, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GROUPS_COLUMN_NAME);
	gtk_tree_view_append_column (treeview, column);

}

/**
 * gpk_application_groups_treeview_clicked_cb:
 **/
static void
gpk_application_groups_treeview_clicked_cb (GtkTreeSelection *selection, GpkApplication *application)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkWidget *widget;
	GtkTreeView *treeview;
	GtkTreePath *path;
	gboolean active;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* hide details */
	gpk_application_clear_details (application);
	gpk_application_clear_packages (application);

	/* clear the search text if we clicked the group list */
	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	gtk_entry_set_text (GTK_ENTRY (widget), "");

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (application->priv->group);
		gtk_tree_model_get (model, &iter,
				     GROUPS_COLUMN_ID, &application->priv->group,
				     GROUPS_COLUMN_ACTIVE, &active, -1);
		egg_debug ("selected row is: %s (%i)", application->priv->group, active);

		/* don't search parent groups */
		if (!active) {
			treeview = GTK_TREE_VIEW (glade_xml_get_widget (application->priv->glade_xml, "treeview_detail"));
			path = gtk_tree_model_get_path (model, &iter);

			/* select the parent group */
			gtk_tree_selection_select_path (selection, path);
			gtk_tree_path_free (path);
			return;
		}

		/* GetPackages? */
		if (egg_strequal (application->priv->group, "all-packages"))
			application->priv->search_mode = PK_MODE_ALL_PACKAGES;
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
	gchar **package_ids;
	gchar *image;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* reset */
	g_free (application->priv->package);
	application->priv->package = NULL;

	/* This will only work in single or browse selection mode! */
	if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
		egg_debug ("no row selected");

		/* we cannot now add it */
		gpk_application_allow_install (application, FALSE);
		gpk_application_allow_remove (application, FALSE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_selection");
		gtk_widget_hide (widget);

		/* hide details */
		gpk_application_clear_details (application);
		return;
	}

	/* check we aren't a help line */
	gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_IMAGE, &image, -1);
	ret = egg_strequal (image, "system-search");
	g_free (image);
	if (ret) {
		egg_debug ("ignoring help click");
		return;
	}

	/* show the menu item */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_selection");
	gtk_widget_show (widget);

	/* get data */
	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_STATE, &state,
			    PACKAGES_COLUMN_ID, &application->priv->package, -1);

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
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_run");
	gtk_widget_set_sensitive (widget, ret);

	/* cancel any previous request */
	ret = pk_client_reset (application->priv->client_details, &error);
	if (!ret) {
		egg_warning ("failed to cancel, and adding to queue: %s", error->message);
		g_error_free (error);
		return;
	}

	/* get the details */
	package_ids = pk_package_ids_from_id (application->priv->package);
	ret = pk_client_get_details (application->priv->client_details, package_ids, &error);
	g_strfreev (package_ids);
	if (!ret) {
		egg_warning ("failed to get details: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_application_connection_changed_cb:
 **/
static void
gpk_application_connection_changed_cb (PkConnection *pconnection, gboolean connected, GpkApplication *application)
{
	g_return_if_fail (PK_IS_APPLICATION (application));

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
 * gpk_application_create_custom_widget:
 **/
static GtkWidget *
gpk_application_create_custom_widget (GladeXML *xml, gchar *func_name, gchar *name,
				      gchar *string1, gchar *string2,
				      gint int1, gint int2, gpointer user_data)
{
	if (egg_strequal (name, "entry_text")) {
		return sexy_icon_entry_new ();
	}
	if (egg_strequal (name, "image_status")) {
		return gpk_animated_icon_new ();
	}
	egg_warning ("name unknown='%s'", name);
	return NULL;
}

/**
 * gpk_application_popup_position_menu:
 **/
static void
gpk_application_popup_position_menu (GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer user_data)
{
	GtkWidget     *widget;
	GtkRequisition requisition;
	gint menu_xpos = 0;
	gint menu_ypos = 0;

	widget = GTK_WIDGET (user_data);

	/* find the location */
	gdk_window_get_origin (widget->window, &menu_xpos, &menu_ypos);
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
	GtkWidget *icon;
	GtkWidget *widget;
	GpkApplication *application = GPK_APPLICATION (data);

	/* change type */
	application->priv->search_type = PK_SEARCH_NAME;
	egg_debug ("set search type=%i", application->priv->search_type);

	/* set the new icon */
	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	gtk_widget_set_tooltip_text (widget, _("Searching by name"));
	icon = gtk_image_new_from_stock (GTK_STOCK_FIND, GTK_ICON_SIZE_MENU);
	sexy_icon_entry_set_icon (SEXY_ICON_ENTRY (widget), SEXY_ICON_ENTRY_PRIMARY, GTK_IMAGE (icon));
}

/**
 * gpk_application_menu_search_by_description:
 **/
static void
gpk_application_menu_search_by_description (GtkMenuItem *item, gpointer data)
{
	GtkWidget *icon;
	GtkWidget *widget;
	GpkApplication *application = GPK_APPLICATION (data);

	/* set type */
	application->priv->search_type = PK_SEARCH_DETAILS;
	egg_debug ("set search type=%i", application->priv->search_type);

	/* set the new icon */
	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	gtk_widget_set_tooltip_text (widget, _("Searching by description"));
	icon = gtk_image_new_from_stock (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
	sexy_icon_entry_set_icon (SEXY_ICON_ENTRY (widget), SEXY_ICON_ENTRY_PRIMARY, GTK_IMAGE (icon));
}

/**
 * gpk_application_menu_search_by_file:
 **/
static void
gpk_application_menu_search_by_file (GtkMenuItem *item, gpointer data)
{
	GtkWidget *icon;
	GtkWidget *widget;
	GpkApplication *application = GPK_APPLICATION (data);

	/* set type */
	application->priv->search_type = PK_SEARCH_FILE;
	egg_debug ("set search type=%i", application->priv->search_type);

	/* set the new icon */
	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	gtk_widget_set_tooltip_text (widget, _("Searching by file"));
	icon = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU);
	sexy_icon_entry_set_icon (SEXY_ICON_ENTRY (widget), SEXY_ICON_ENTRY_PRIMARY, GTK_IMAGE (icon));
}

/**
 * gpk_application_entry_text_icon_pressed_cb:
 **/
static void
gpk_application_entry_text_icon_pressed_cb (SexyIconEntry *entry, gint icon_pos, gint button, gpointer data)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;
	GtkWidget *image;
	GpkApplication *application = GPK_APPLICATION (data);

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* only respond to left button */
	if (button != 1) {
		return;
	}
	egg_debug ("icon_pos=%i", icon_pos);

	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_NAME)) {
		item = gtk_image_menu_item_new_with_mnemonic (_("Search by name"));
		image = gtk_image_new_from_stock (GTK_STOCK_FIND, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_name), application);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_DETAILS)) {
		item = gtk_image_menu_item_new_with_mnemonic (_("Search by description"));
		image = gtk_image_new_from_stock (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_description), application);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_FILE)) {
		item = gtk_image_menu_item_new_with_mnemonic (_("Search by file name"));
		image = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_file), application);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gpk_application_popup_position_menu, entry,
			1, gtk_get_current_event_time());
}

/**
 *  * gpk_application_about_dialog_url_cb:
 *   **/
static void
gpk_application_about_dialog_url_cb (GtkAboutDialog *about, const char *address, gpointer data)
{
	GError *error = NULL;
	gboolean ret;

	char *cmdline;
	GdkScreen *gscreen;
	GtkWidget *error_dialog;
	gchar *url;
	gchar *protocol = (gchar*) data;

	if (protocol != NULL)
		url = g_strconcat (protocol, address, NULL);
	else
		url = g_strdup (address);

	gscreen = gtk_window_get_screen (GTK_WINDOW (about));

	cmdline = g_strconcat ("xdg-open ", url, NULL);
	ret = gdk_spawn_command_line_on_screen (gscreen, cmdline, &error);
	g_free (cmdline);

	if (ret)
		goto out;

	g_error_free (error);
	error = NULL;
	cmdline = g_strconcat ("gnome-open ", url, NULL);
	ret = gdk_spawn_command_line_on_screen (gscreen, cmdline, &error);
	g_free (cmdline);

	if (!ret) {
		error_dialog = gtk_message_dialog_new (GTK_WINDOW (about),
						       GTK_DIALOG_MODAL,
						       GTK_MESSAGE_INFO,
						       GTK_BUTTONS_OK,
						       _("Failed to show url"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (error_dialog),
							  "%s", error->message);
		gtk_dialog_run (GTK_DIALOG (error_dialog));
		gtk_widget_destroy (error_dialog);
		g_error_free (error);
	}

out:
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
		N_("PackageKit is free software; you can redistribute it and/or\n"
		   "modify it under the terms of the GNU General Public License\n"
		   "as published by the Free Software Foundation; either version 2\n"
		   "of the License, or (at your option) any later version."),
		N_("PackageKit is distributed in the hope that it will be useful,\n"
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		   "GNU General Public License for more details."),
		N_("You should have received a copy of the GNU General Public License\n"
		   "along with this program; if not, write to the Free Software\n"
		   "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA\n"
		   "02110-1301, USA.")
	};
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
		gtk_about_dialog_set_email_hook (gpk_application_about_dialog_url_cb, "mailto:", NULL);
	}

	/* use parent */
	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");

	gtk_window_set_default_icon_name (GPK_ICON_SOFTWARE_INSTALLER);
	gtk_show_about_dialog (GTK_WINDOW (main_window),
			       "version", PACKAGE_VERSION,
			       "copyright", "Copyright \xc2\xa9 2007-2008 Richard Hughes",
			       "license", license_trans,
			       "website-label", _("PackageKit Website"),
			       "website", "http://www.packagekit.org",
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

	g_return_if_fail (PK_IS_APPLICATION (application));

	ret = g_spawn_command_line_async ("gpk-repo", NULL);
	if (!ret) {
		egg_warning ("spawn of pk-repo failed");
	}
}

/**
 * gpk_application_menu_refresh_cb:
 **/
static void
gpk_application_menu_refresh_cb (GtkAction *action, GpkApplication *application)
{
	gpk_client_refresh_cache (application->priv->gclient, NULL);
}

/**
 * gpk_application_menu_filter_installed_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_installed_cb (GtkWidget *widget, GpkApplication *application)
{
	const gchar *name;

	g_return_if_fail (PK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))) {
		return;
	}

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

	g_return_if_fail (PK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))) {
		return;
	}

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

	g_return_if_fail (PK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))) {
		return;
	}

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

	g_return_if_fail (PK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))) {
		return;
	}

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
 * gpk_application_menu_filter_arch_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_arch_cb (GtkWidget *widget, GpkApplication *application)
{
	const gchar *name;

	g_return_if_fail (PK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))) {
		return;
	}

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_ARCH);
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_ARCH);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_ARCH);
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_NOT_ARCH);
	} else {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_ARCH);
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_ARCH);
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

	g_return_if_fail (PK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))) {
		return;
	}

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

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* save users preference to gconf */
	enabled = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	gconf_client_set_bool (application->priv->gconf_client,
			       GPK_CONF_APPLICATION_FILTER_BASENAME, enabled, NULL);

	/* change the filter */
	if (enabled) {
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_BASENAME);
	} else {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_BASENAME);
	}

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

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* save users preference to gconf */
	enabled = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	gconf_client_set_bool (application->priv->gconf_client,
			       GPK_CONF_APPLICATION_FILTER_NEWEST, enabled, NULL);

	/* change the filter */
	if (enabled) {
		pk_bitfield_add (application->priv->filters_current, PK_FILTER_ENUM_NEWEST);
	} else {
		pk_bitfield_remove (application->priv->filters_current, PK_FILTER_ENUM_NEWEST);
	}

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_status_changed_cb:
 **/
static void
gpk_application_status_changed_cb (PkClient *client, PkStatusEnum status, GpkApplication *application)
{
	const gchar *text;
	GtkWidget *widget;

	g_return_if_fail (PK_IS_APPLICATION (application));

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_status");
	if (status == PK_STATUS_ENUM_FINISHED) {
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (application->priv->glade_xml, "image_status");
		gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (widget), FALSE);
		return;
	}

	/* set the text and show */
	gtk_widget_show (widget);
	widget = glade_xml_get_widget (application->priv->glade_xml, "label_status");
	text = gpk_status_enum_to_localised_text (status);
	gtk_label_set_label (GTK_LABEL (widget), text);

	/* set icon */
	widget = glade_xml_get_widget (application->priv->glade_xml, "image_status");
	gpk_set_animated_icon_from_status (GPK_ANIMATED_ICON (widget), status, GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_widget_show (widget);
}

/**
 * gpk_application_allow_cancel_cb:
 **/
static void
gpk_application_allow_cancel_cb (PkClient *client, gboolean allow_cancel, GpkApplication *application)
{
	GtkWidget *widget;

	g_return_if_fail (PK_IS_APPLICATION (application));

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_cancel");
	gtk_widget_set_sensitive (widget, allow_cancel);
}

/**
 * gpk_application_package_row_activated_cb:
 **/
void
gpk_application_package_row_activated_cb (GtkTreeView *treeview, GtkTreePath *path,
					 GtkTreeViewColumn *col, GpkApplication *application)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean ret;
	PkBitfield state;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* get selection */
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter (model, &iter, path);
	if (!ret) {
		egg_warning ("failed to get selection");
		return;
	}

	g_free (application->priv->package);
	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_STATE, &state,
			    PACKAGES_COLUMN_ID, &application->priv->package, -1);
	if (gpk_application_state_get_checkbox (state))
		gpk_application_remove (application);
	else
		gpk_application_install (application);
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
	ret = egg_strequal (name, "separator");
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
	g_return_if_fail (PK_IS_APPLICATION (application));

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

	treeview = GTK_TREE_VIEW (glade_xml_get_widget (application->priv->glade_xml, "treeview_detail"));

	/* title */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", DETAIL_COLUMN_TITLE);
	gtk_tree_view_append_column (treeview, column);

	/* column for uris */
	renderer = gpk_cell_renderer_uri_new ();
	g_signal_connect (renderer, "clicked", G_CALLBACK (gpk_application_treeview_renderer_clicked), application);
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
		welcome = _("Enter a package name and then click find, or click a group to get started.");
	} else {
		welcome = _("Enter a package name and then click find to get started.");
	}
	gtk_list_store_set (application->priv->packages_store, &iter,
			    PACKAGES_COLUMN_STATE, state,
			    PACKAGES_COLUMN_CHECKBOX, FALSE,
			    PACKAGES_COLUMN_CHECKBOX_ENABLE, FALSE,
			    PACKAGES_COLUMN_TEXT, welcome,
			    PACKAGES_COLUMN_IMAGE, "system-search", -1);
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
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_groups");
	gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (widget), FALSE);
	gtk_tree_view_set_level_indentation  (GTK_TREE_VIEW (widget), 0);

	/* add an "all" entry if we can GetPackages */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_PACKAGES)) {
		gtk_tree_store_append (application->priv->groups_store, &iter, NULL);
		icon_name = gpk_role_enum_to_icon_name (PK_ROLE_ENUM_GET_PACKAGES);
		gtk_tree_store_set (application->priv->groups_store, &iter,
				    GROUPS_COLUMN_NAME, _("All packages"),
				    GROUPS_COLUMN_SUMMARY, _("Show all packages"),
				    GROUPS_COLUMN_ID, "all-packages",
				    GROUPS_COLUMN_ACTIVE, TRUE,
				    GROUPS_COLUMN_ICON, icon_name, -1);
	}

	/* no group information */
	if (application->priv->groups == 0)
		return FALSE;

	/* add this at the top of the list */
	if (pk_bitfield_contain (application->priv->groups, PK_GROUP_ENUM_COLLECTIONS))
		gpk_application_group_add_data (application, PK_GROUP_ENUM_COLLECTIONS);

	/* add a separator only if we can do both */
	if ((pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_PACKAGES) ||
	     pk_bitfield_contain (application->priv->groups, PK_GROUP_ENUM_COLLECTIONS)) &&
	     pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		gtk_tree_store_append (application->priv->groups_store, &iter, NULL);
		gtk_tree_store_set (application->priv->groups_store, &iter,
				    GROUPS_COLUMN_ID, "separator", -1);
		widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_groups");
		gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (widget),
						      gpk_application_group_row_separator_func, NULL, NULL);
	}

	/* create group tree view if we can search by group */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		/* add all the groups supported (except collections, which we handled above */
		for (i=0; i<PK_GROUP_ENUM_UNKNOWN; i++) {
			if (pk_bitfield_contain (application->priv->groups, i) &&
			    i != PK_GROUP_ENUM_COLLECTIONS)
				gpk_application_group_add_data (application, i);
		}
	}

	/* we populated the menu  */
	return TRUE;
}

/**
 * gpk_application_categories_finished_cb:
 **/
static void
gpk_application_categories_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, GpkApplication *application)
{
	const GPtrArray	*categories;
	EggObjList *list;
	const PkCategoryObj *obj;
	const PkCategoryObj *obj2;
	GtkTreeIter iter;
	GtkTreeIter iter2;
	guint i, j;
	GtkWidget *widget;
	const gchar *icon_name;

	/* set to expanders with indent */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_groups");
	gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (widget), TRUE);
	gtk_tree_view_set_level_indentation  (GTK_TREE_VIEW (widget), 3);

	/* add an "all" entry if we can GetPackages */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_PACKAGES)) {
		gtk_tree_store_append (application->priv->groups_store, &iter, NULL);
		icon_name = gpk_role_enum_to_icon_name (PK_ROLE_ENUM_GET_PACKAGES);
		gtk_tree_store_set (application->priv->groups_store, &iter,
				    GROUPS_COLUMN_NAME, _("All packages"),
				    GROUPS_COLUMN_SUMMARY, _("Show all packages"),
				    GROUPS_COLUMN_ID, "all-packages",
				    GROUPS_COLUMN_ACTIVE, TRUE,
				    GROUPS_COLUMN_ICON, icon_name, -1);
	}

	/* add this at the top of the list */
	if (pk_bitfield_contain (application->priv->groups, PK_GROUP_ENUM_COLLECTIONS))
		gpk_application_group_add_data (application, PK_GROUP_ENUM_COLLECTIONS);

	/* add a separator only if we can do both */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_PACKAGES) ||
	    pk_bitfield_contain (application->priv->groups, PK_GROUP_ENUM_COLLECTIONS)) {
		gtk_tree_store_append (application->priv->groups_store, &iter, NULL);
		gtk_tree_store_set (application->priv->groups_store, &iter,
				    GROUPS_COLUMN_ID, "separator", -1);
		widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_groups");
		gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (widget),
						      gpk_application_group_row_separator_func, NULL, NULL);
	}

	/* get return values */
	categories = pk_client_get_cached_objects (client);
	if (categories->len == 0) {
		egg_warning ("no results from GetCategories");
		goto out;
	}

	/* copy the categories into a list so we can remove then */
	list = egg_obj_list_new ();
	egg_obj_list_set_copy (list, (EggObjListCopyFunc) pk_category_obj_copy);
	egg_obj_list_set_free (list, (EggObjListFreeFunc) pk_category_obj_free);
	egg_obj_list_add_array (list, categories);

	for (i=0; i < list->len; i++) {
		obj = egg_obj_list_index (list, i);

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
			obj2 = egg_obj_list_index (list, j);
			if (egg_strequal (obj2->parent_id, obj->cat_id)) {
				gtk_tree_store_append (application->priv->groups_store, &iter2, &iter);
				gtk_tree_store_set (application->priv->groups_store, &iter2,
						    GROUPS_COLUMN_NAME, obj2->name,
						    GROUPS_COLUMN_SUMMARY, obj2->summary,
						    GROUPS_COLUMN_ID, obj2->cat_id,
						    GROUPS_COLUMN_ICON, obj2->icon,
						    GROUPS_COLUMN_ACTIVE, TRUE,
						    -1);
				egg_obj_list_remove (list, obj2);
			} else
				j++;
		} while (j < list->len);
	}

	/* open all expanders */
	gtk_tree_view_collapse_all (GTK_TREE_VIEW (widget));

	g_object_unref (list);
out:
	g_object_unref (client);
}

/**
 * gpk_application_create_group_list_categories:
 **/
static gboolean
gpk_application_create_group_list_categories (GpkApplication *application)
{
	GError *error = NULL;
	PkClient *client;
	gboolean ret;

	/* check we can do this */
	if (!pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_CATEGORIES)) {
		egg_warning ("backend does not support complex groups");
		return FALSE;
	}

	/* async */
	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	g_signal_connect (client, "finished",
			  G_CALLBACK (gpk_application_categories_finished_cb), application);
	g_signal_connect (client, "finished",
			  G_CALLBACK (gpk_application_finished_cb), application);
	g_signal_connect (client, "status-changed",
			  G_CALLBACK (gpk_application_status_changed_cb), application);

	/* get categories supported */
	ret = pk_client_get_categories (client, &error);
	if (!ret) {
		egg_warning ("failed to get categories: %s", error->message);
		g_error_free (error);
		g_object_unref (client);
	}

	/* client will be unreff'd in finished handler */
	return ret;
}

/**
 * gpk_application_gconf_key_changed_cb:
 *
 * We might have to do things when the gconf keys change; do them here.
 **/
static void
gpk_application_gconf_key_changed_cb (GConfClient *client, guint cnxn_id, GConfEntry *entry, GpkApplication *application)
{
	GConfValue *value;
	gboolean ret;
	value = gconf_entry_get_value (entry);
	if (value == NULL)
		return;

	if (egg_strequal (entry->key, GPK_CONF_APPLICATION_CATEGORY_GROUPS)) {
		ret = gconf_value_get_bool (value);
		gtk_tree_store_clear (application->priv->groups_store);
		if (ret)
			gpk_application_create_group_list_categories (application);
		else
			gpk_application_create_group_list_enum (application);
	}
}

/**
 * gpk_application_init:
 **/
static void
gpk_application_init (GpkApplication *application)
{
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkEntryCompletion *completion;
	GtkTreeSelection *selection;
	gboolean enabled;
	gboolean ret;
	GError *error = NULL;

	application->priv = GPK_APPLICATION_GET_PRIVATE (application);
	application->priv->package = NULL;
	application->priv->group = NULL;
	application->priv->url = NULL;
	application->priv->has_package = FALSE;
	application->priv->details_event_id = 0;
	application->priv->package_list = pk_package_list_new ();

	application->priv->gconf_client = gconf_client_get_default ();
	application->priv->repos = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	application->priv->search_type = PK_SEARCH_UNKNOWN;
	application->priv->search_mode = PK_MODE_UNKNOWN;
	application->priv->filters_current = PK_FILTER_ENUM_NONE;

	/* watch gnome-power-manager keys */
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
	application->priv->gclient = gpk_client_new ();

	application->priv->client_search = pk_client_new ();
	g_signal_connect (application->priv->client_search, "package",
			  G_CALLBACK (gpk_application_package_cb), application);
	g_signal_connect (application->priv->client_search, "error-code",
			  G_CALLBACK (gpk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_search, "finished",
			  G_CALLBACK (gpk_application_finished_cb), application);
	g_signal_connect (application->priv->client_search, "status-changed",
			  G_CALLBACK (gpk_application_status_changed_cb), application);
	g_signal_connect (application->priv->client_search, "allow-cancel",
			  G_CALLBACK (gpk_application_allow_cancel_cb), application);

	application->priv->client_action = pk_client_new ();
	g_signal_connect (application->priv->client_action, "package",
			  G_CALLBACK (gpk_application_package_cb), application);
	g_signal_connect (application->priv->client_action, "error-code",
			  G_CALLBACK (gpk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_action, "finished",
			  G_CALLBACK (gpk_application_finished_cb), application);
	g_signal_connect (application->priv->client_action, "status-changed",
			  G_CALLBACK (gpk_application_status_changed_cb), application);
	g_signal_connect (application->priv->client_action, "allow-cancel",
			  G_CALLBACK (gpk_application_allow_cancel_cb), application);
	g_signal_connect (application->priv->client_action, "repo-detail",
			  G_CALLBACK (pk_application_repo_detail_cb), application);

	application->priv->client_details = pk_client_new ();
	g_signal_connect (application->priv->client_details, "details",
			  G_CALLBACK (gpk_application_details_cb), application);
	g_signal_connect (application->priv->client_details, "error-code",
			  G_CALLBACK (gpk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_details, "finished",
			  G_CALLBACK (gpk_application_finished_cb), application);
	g_signal_connect (application->priv->client_details, "status-changed",
			  G_CALLBACK (gpk_application_status_changed_cb), application);
	g_signal_connect (application->priv->client_details, "allow-cancel",
			  G_CALLBACK (gpk_application_allow_cancel_cb), application);

	application->priv->client_files = pk_client_new ();
	pk_client_set_use_buffer (application->priv->client_files, TRUE, NULL);
	g_signal_connect (application->priv->client_files, "error-code",
			  G_CALLBACK (gpk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_files, "finished",
			  G_CALLBACK (gpk_application_finished_cb), application);
	g_signal_connect (application->priv->client_files, "status-changed",
			  G_CALLBACK (gpk_application_status_changed_cb), application);
	g_signal_connect (application->priv->client_files, "allow-cancel",
			  G_CALLBACK (gpk_application_allow_cancel_cb), application);

	/* get bitfield */
	application->priv->roles = pk_control_get_actions (application->priv->control, NULL);
	application->priv->filters = pk_control_get_filters (application->priv->control, NULL);
	application->priv->groups = pk_control_get_groups (application->priv->control, NULL);

	application->priv->pconnection = pk_connection_new ();
	g_signal_connect (application->priv->pconnection, "connection-changed",
			  G_CALLBACK (gpk_application_connection_changed_cb), application);

	/* get localised data from sqlite database */
	application->priv->extra = pk_extra_new ();
	ret = pk_extra_set_database (application->priv->extra, NULL);
	if (!ret)
		egg_warning ("Failure setting database");

	/* set the locale to default */
	pk_extra_set_locale (application->priv->extra, NULL);

	/* use custom widgets */
	glade_set_custom_handler (gpk_application_create_custom_widget, application);

	application->priv->glade_xml = glade_xml_new (GPK_DATA "/gpk-application.glade", NULL, NULL);
	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");

	/* make GpkClient windows modal */
	gtk_widget_realize (main_window);
	gpk_client_set_parent (application->priv->gclient, GTK_WINDOW (main_window));

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SOFTWARE_INSTALLER);

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (gpk_application_delete_event_cb), application);

	/* clear */
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_clear");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_button_clear_cb), application);
	gtk_widget_set_tooltip_text (widget, _("Clear current selection"));

	/* help */
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_button_help_cb), application);

	/* install */
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_apply");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_button_apply_cb), application);
	gtk_widget_set_tooltip_text (widget, _("Changes are not applied instantly, this button applies all changes"));

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_about");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_about_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_help");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_help_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_sources");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_sources_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_refresh");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_refresh_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_homepage");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_homepage_cb), application);
	gtk_widget_set_tooltip_text (widget, _("Visit homepage for selected package"));

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_files");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_files_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_install");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_install_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_remove");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_remove_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_depends");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_depends_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_requires");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_requires_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_run");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_run_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_selection");
	gtk_widget_hide (widget);

	/* installed filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_installed_yes");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_installed_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_installed_no");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_installed_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_installed_both");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_installed_cb), application);

	/* devel filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_devel_yes");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_devel_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_devel_no");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_devel_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_devel_both");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_devel_cb), application);

	/* gui filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_gui_yes");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_gui_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_gui_no");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_gui_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_gui_both");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_gui_cb), application);

	/* free filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_free_yes");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_free_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_free_no");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_free_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_free_both");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_free_cb), application);

	/* arch filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_arch_yes");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_arch_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_arch_no");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_arch_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_arch_both");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_arch_cb), application);

	/* source filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_source_yes");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_source_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_source_no");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_source_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_source_both");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_source_cb), application);

	/* basename filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_basename");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_basename_cb), application);

	/* newest filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_newest");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_newest_cb), application);

	/* Remove description/file list if needed. */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_DETAILS) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "scrolledwindow2");
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_FILES) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_files");
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_DEPENDS) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_depends");
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_REQUIRES) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_requires");
		gtk_widget_hide (widget);
	}

	/* hide the group selector if we don't support search-groups */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_GROUP) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "scrolledwindow_groups");
		gtk_widget_hide (widget);
	}

	/* hide the refresh cache button if we can't do it */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_REFRESH_CACHE) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_refresh");
		gtk_widget_hide (widget);
	}

	/* hide the software-sources button if we can't do it */
	if (pk_bitfield_contain (application->priv->roles, PK_ROLE_ENUM_GET_REPO_LIST) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_sources");
		gtk_widget_hide (widget);
	}

	/* simple find button */
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_find_cb), application);
	gtk_widget_set_tooltip_text (widget, _("Find packages"));

	/* search cancel button */
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_cancel");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_cancel_cb), application);
	gtk_widget_set_tooltip_text (widget, _("Cancel search"));

	/* the fancy text entry widget */
	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");

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
	sexy_icon_entry_set_icon_highlight (SEXY_ICON_ENTRY (widget), SEXY_ICON_ENTRY_PRIMARY, TRUE);
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_find_cb), application);
	g_signal_connect (widget, "icon-pressed",
			  G_CALLBACK (gpk_application_entry_text_icon_pressed_cb), application);

	/* hide the filters we can't support */
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_INSTALLED) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_installed");
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_DEVELOPMENT) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_devel");
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_GUI) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_gui");
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_FREE) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_free");
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_ARCH) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_arch");
		gtk_widget_hide (widget);
	}
	if (pk_bitfield_contain (application->priv->filters, PK_FILTER_ENUM_SOURCE) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_source");
		gtk_widget_hide (widget);
	}

	/* BASENAME, use by default, or hide */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_basename");
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
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_newest");
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

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	g_signal_connect (widget, "key-press-event",
			  G_CALLBACK (gpk_application_text_changed_cb), application);
	g_signal_connect (widget, "key-release-event",
			  G_CALLBACK (gpk_application_text_changed_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	gtk_widget_set_sensitive (widget, FALSE);

	gtk_widget_set_size_request (main_window, 1000, 500);
	gtk_widget_show (main_window);

	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));
	g_signal_connect (GTK_TREE_VIEW (widget), "row-activated",
			  G_CALLBACK (gpk_application_package_row_activated_cb), application);

	/* use a list store for the extra data */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_detail");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget), GTK_TREE_MODEL (application->priv->details_store));

	/* add columns to the tree view */
	gpk_application_treeview_add_columns_description (application);

	/* sorted */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (application->priv->packages_store),
					      PACKAGES_COLUMN_ID, GTK_SORT_ASCENDING);

	/* create package tree view */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (application->priv->packages_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_application_packages_treeview_clicked_cb), application);

	/* add columns to the tree view */
	gpk_application_packages_add_columns (application);

	/* set up the groups checkbox */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_groups");

	/* add columns to the tree view */
	gpk_application_groups_add_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (widget), GROUPS_COLUMN_SUMMARY);
	gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (widget), FALSE);
	gtk_tree_view_set_level_indentation  (GTK_TREE_VIEW (widget), 9);
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (application->priv->groups_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_application_groups_treeview_clicked_cb), application);

	/* simple list or category tree? */
	ret = gconf_client_get_bool (application->priv->gconf_client, GPK_CONF_APPLICATION_CATEGORY_GROUPS, NULL);
	if (ret)
		ret = gpk_application_create_group_list_categories (application);

	/* fallback to creating a simple list if we can't do category list */
	if (!ret)
		gpk_application_create_group_list_enum (application);

	/* get repos, so we can show the full name in the software source box */
	ret = pk_client_get_repo_list (application->priv->client_action, PK_FILTER_ENUM_NONE, &error);
	if (!ret) {
		egg_warning ("failed to get repo list: %s", error->message);
		g_error_free (error);
	}

	/* set current action */
	application->priv->action = PK_ACTION_NONE;
	gpk_application_set_buttons_apply_clear (application);

	/* hide details */
	gpk_application_clear_details (application);

	/* coldplug icon to default to search by name*/
	gpk_application_menu_search_by_name (NULL, application);

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
	g_return_if_fail (PK_IS_APPLICATION (object));

	application = GPK_APPLICATION (object);
	application->priv = GPK_APPLICATION_GET_PRIVATE (application);

	if (application->priv->details_event_id > 0)
		g_source_remove (application->priv->details_event_id);

	g_object_unref (application->priv->glade_xml);
	g_object_unref (application->priv->packages_store);
	g_object_unref (application->priv->details_store);
	g_object_unref (application->priv->control);
	g_object_unref (application->priv->client_search);
	g_object_unref (application->priv->client_action);
	g_object_unref (application->priv->client_details);
	g_object_unref (application->priv->client_files);
	g_object_unref (application->priv->pconnection);
	g_object_unref (application->priv->extra);
	g_object_unref (application->priv->gconf_client);
	g_object_unref (application->priv->gclient);
	g_object_unref (application->priv->package_list);

	g_free (application->priv->url);
	g_free (application->priv->group);
	g_free (application->priv->package);
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

