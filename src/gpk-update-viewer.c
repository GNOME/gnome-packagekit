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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <dbus/dbus-glib.h>

#include <gconf/gconf-client.h>
#include <packagekit-glib/packagekit.h>
#include <libnotify/notify.h>
#include <unique/unique.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-markdown.h"
#include "egg-console-kit.h"

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
#include "gpk-helper-repo-signature.h"
#include "gpk-helper-eula.h"

#define GPK_UPDATE_VIEWER_AUTO_CLOSE_TIMEOUT	10 /* seconds */
#define GPK_UPDATE_VIEWER_AUTO_RESTART_TIMEOUT	60 /* seconds */
#define GPK_UPDATE_VIEWER_MOBILE_SMALL_SIZE	512*1024 /* bytes */
#define GNOME_SESSION_MANAGER_SERVICE		"org.gnome.SessionManager"
#define GNOME_SESSION_MANAGER_PATH		"/org/gnome/SessionManager"
#define GNOME_SESSION_MANAGER_INTERFACE		"org.gnome.SessionManager"

static guint auto_shutdown_id = 0;
static GMainLoop *loop = NULL;
static GtkBuilder *builder = NULL;
static GtkListStore *list_store_updates = NULL;
static GtkTextBuffer *text_buffer = NULL;
static PkClient *client_primary = NULL;
static PkClient *client_secondary = NULL;
static PkControl *control = NULL;
static PkPackageList *update_list = NULL;
static GpkHelperRepoSignature *helper_repo_signature = NULL;
static GpkHelperEula *helper_eula = NULL;
static EggMarkdown *markdown = NULL;
static PkPackageId *package_id_last = NULL;
static PkRestartEnum restart_update = PK_RESTART_ENUM_NONE;
static gboolean running_hidden = FALSE;
static guint size_total = 0;

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
 * gpk_update_viewer_logout:
 **/
static void
gpk_update_viewer_logout (void)
{
	DBusGConnection *connection;
	DBusGProxy *proxy;
	GError *error = NULL;
	gboolean ret;

	/* get org.gnome.Session interface */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);
	proxy = dbus_g_proxy_new_for_name_owner (connection, GNOME_SESSION_MANAGER_SERVICE,
						 GNOME_SESSION_MANAGER_PATH,
						 GNOME_SESSION_MANAGER_INTERFACE, &error);
	if (proxy == NULL) {
		egg_warning ("cannot connect to proxy %s: %s", GNOME_SESSION_MANAGER_SERVICE, error->message);
		g_error_free (error);
		goto out;
	}

	/* log out of the session */
	ret = dbus_g_proxy_call (proxy, "Shutdown", &error, G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("cannot shutdown session: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_object_unref (proxy);
}

/**
 * gpk_update_viewer_shutdown:
 **/
static void
gpk_update_viewer_shutdown (void)
{
	EggConsoleKit *console;
	GError *error = NULL;
	gboolean ret;

	/* use consolekit to restart */
	console = egg_console_kit_new ();
	ret = egg_console_kit_restart (console, &error);
	if (!ret) {
		egg_warning ("cannot restart: %s", error->message);
		g_error_free (error);
	}
	g_object_unref (console);
}

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
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean valid;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	/* set all the checkboxes sensitive */
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_list_store_set (list_store_updates, &iter,
				    GPK_UPDATES_COLUMN_SENSITIVE, TRUE,
				    GPK_UPDATES_COLUMN_CLICKABLE, TRUE,
				    -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * gpk_update_viewer_button_check_connection:
 **/
static gboolean
gpk_update_viewer_button_check_connection (guint size)
{
	GtkWindow *window;
	GtkWidget *dialog;
	gboolean ret = TRUE;
	gchar *text_size = NULL;
	gchar *message = NULL;
	GtkResponseType response;
	GError *error = NULL;
	PkNetworkEnum state;

	/* get network state */
	state = pk_control_get_network_state (control, &error);
	if (error != NULL) {
		egg_warning ("failed to get network state: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* not on wireless mobile */
	if (state != PK_NETWORK_ENUM_MOBILE)
		goto out;

	/* not when small */
	if (size < GPK_UPDATE_VIEWER_MOBILE_SMALL_SIZE)
		goto out;

	/* show modal dialog */
	window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_updates"));
	dialog = gtk_message_dialog_new (window, GTK_DIALOG_MODAL,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_CANCEL,
					 "%s", _("Detected wireless broadband connection"));

	/* TRANSLATORS: this is the button text when we check if it's okay to download */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Update anyway"), GTK_RESPONSE_OK);
	text_size = g_format_size_for_display (size_total);

	/* TRANSLATORS, the %s is a size, e.g. 13.3Mb */
	message = g_strdup_printf (_("Connectivity is being provided by wireless broadband, and it may be expensive to download %s."), text_size);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog), "%s", message);

	gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_INSTALLER);
	response = gtk_dialog_run (GTK_DIALOG(dialog));
	gtk_widget_destroy (dialog);

	if (response != GTK_RESPONSE_OK)
		ret = FALSE;
out:
	g_free (text_size);
	g_free (message);
	return ret;
}


/**
 * gpk_update_viewer_button_install_cb:
 **/
static void
gpk_update_viewer_button_install_cb (GtkWidget *widget, gpointer data)
{
	GtkWindow *window;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	gboolean ret;
	gboolean valid;
	gboolean update;
	gboolean selected_all = TRUE;
	gboolean selected_any = FALSE;
	gchar *package_id;
	GError *error = NULL;
	GPtrArray *array = NULL;
	gchar **package_ids = NULL;

	/* check connection */
	ret = gpk_update_viewer_button_check_connection (size_total);
	if (!ret)
		goto out;

	/* hide the upgrade viewbox from now on */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "viewport_upgrade"));
	gtk_widget_hide (widget);

	egg_debug ("Doing the package updates");
	array = g_ptr_array_new ();

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	/* get the first iter in the list */
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* find out how many we should update */
	while (valid) {
		gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_SELECT, &update,
				    GPK_UPDATES_COLUMN_ID, &package_id, -1);

		/* set all the checkboxes insensitive */
		gtk_list_store_set (list_store_updates, &iter,
				    GPK_UPDATES_COLUMN_CLICKABLE, FALSE,
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
		window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_updates"));
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: we clicked apply, but had no packages selected */
					_("No updates selected"),
					_("No updates are selected"), NULL);
		return;
	}

	/* clear the selection */
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_unselect_all (selection);

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
	if (array != NULL) {
		g_ptr_array_foreach (array, (GFunc) g_free, NULL);
		g_ptr_array_free (array, TRUE);
	}
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
	gboolean ret;
	GError *error = NULL;
	PkRoleEnum role;
	PkStatusEnum status;

	/* if we are in a transaction, don't quit, just hide, as we want to return
	 * to this state if the dialog is run again */
	ret = pk_client_get_role (client_primary, &role, NULL, &error);
	if (!ret) {
		egg_warning ("failed to get role: %s", error->message);
		g_error_free (error);
		goto out;
	}
	if (role == PK_ROLE_ENUM_UNKNOWN) {
		egg_debug ("no role, so quitting");
		goto out;
	}
	ret = pk_client_get_status (client_primary, &status, &error);
	if (!ret) {
		egg_warning ("failed to get status: %s", error->message);
		g_error_free (error);
		goto out;
	}
	if (status == PK_STATUS_ENUM_FINISHED) {
		egg_debug ("status is finished, so quitting");
		goto out;
	}

	/* hide window */
	egg_debug ("hiding to preserve state");
	running_hidden = TRUE;
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_updates"));
	gtk_widget_hide (widget);
	return TRUE;
out:
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

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
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
				    GPK_UPDATES_COLUMN_STATUS, GPK_INFO_ENUM_DOWNLOADED, -1);
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
	PkInfoEnum info;
	gchar *text = NULL;
	gchar *package_id;
	GtkTreeView *treeview;
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
		treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
		model = gtk_tree_view_get_model (treeview);

		/* update icon */
		path = gpk_update_viewer_model_get_path (model, obj->id);
		if (path == NULL) {
			egg_debug ("not found ID for package");
			goto out;
		}

		gtk_tree_model_get_iter (model, &iter, path);

		/* if the info is finished, change the status to past tense */
		if (obj->info == PK_INFO_ENUM_FINISHED) {
			gtk_tree_model_get (model, &iter,
					    GPK_UPDATES_COLUMN_STATUS, &info, -1);
			/* promote to past tense if present tense */
			if (info < PK_INFO_ENUM_UNKNOWN)
				info += PK_INFO_ENUM_UNKNOWN;
		} else {
			info = obj->info;
		}
		gtk_list_store_set (list_store_updates, &iter,
				    GPK_UPDATES_COLUMN_STATUS, info, -1);
		gtk_tree_path_free (path);

		/* set package description */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_package"));
		//gtk_label_set_label (GTK_LABEL (widget), obj->summary);

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

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
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
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_install"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_cancel"));
		gtk_widget_hide (widget);
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_install"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_cancel"));
		gtk_widget_show (widget);
	}
}

/**
 * gpk_update_viewer_auto_shutdown:
 **/
static gboolean
gpk_update_viewer_auto_shutdown (GtkDialog *dialog)
{
	gtk_dialog_response (dialog, GTK_RESPONSE_CANCEL);
	auto_shutdown_id = 0;
	return FALSE;
}

/**
 * gpk_update_viewer_reconsider_info:
 **/
static void
gpk_update_viewer_reconsider_info (GtkTreeModel *model)
{
	GtkTreeIter iter;
	GtkWidget *widget;
	GtkWidget *dialog;
	gboolean valid;
	gboolean selected;
	gboolean any_selected = FALSE;
	guint len;
	guint size;
	guint number_total = 0;
	PkRestartEnum restart;
	PkRestartEnum restart_worst = PK_RESTART_ENUM_NONE;
	const gchar *title;
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
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_install"));
	gtk_widget_set_sensitive (widget, any_selected);

	/* sensitive */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "scrolledwindow_updates"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "scrolledwindow_details"));
	gtk_widget_set_sensitive (widget, TRUE);

	/* set the pluralisation of the button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_install"));
	/* TRANSLATORS: this is the button text when we have updates */
	title = ngettext ("_Install Update", "_Install Updates", number_total);
	gtk_button_set_label (GTK_BUTTON (widget), title);

	/* no updates */
	len = PK_OBJ_LIST(update_list)->len;
	if (len == 0) {
		/* hide close button */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
		gtk_widget_hide (widget);

		/* show a new title */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_header_title"));
		/* TRANSLATORS: there are no updates */
		text = g_strdup_printf ("<big><b>%s</b></big>", _("There are no updates available"));
		gtk_label_set_label (GTK_LABEL (widget), text);
		g_free (text);

		/* show modal dialog */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_updates"));
		dialog = gtk_message_dialog_new (GTK_WINDOW (widget), GTK_DIALOG_MODAL,
						 GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
						 /* TRANSLATORS: title: warn the user they are quitting with unapplied changes */
						 "%s", _("No updates available"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog),
							  "%s",
							  /* TRANSLATORS: tell the user the problem */
							  _("There are no updates available for your computer at this time."));
		gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_INSTALLER);

		/* setup a callback so we autoclose */
		auto_shutdown_id = g_timeout_add_seconds (GPK_UPDATE_VIEWER_AUTO_RESTART_TIMEOUT, (GSourceFunc) gpk_update_viewer_auto_shutdown, dialog);

		gtk_dialog_run (GTK_DIALOG(dialog));
		gtk_widget_destroy (dialog);

		/* exit the program */
		g_main_loop_quit (loop);
		goto out;
	}

	/* use correct status pane */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_status"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_info"));
	gtk_widget_show (widget);

	/* restart */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_info"));
	if (restart_worst == PK_RESTART_ENUM_NONE) {
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_info"));
		gtk_widget_hide (widget);
	} else {
		gtk_label_set_label (GTK_LABEL (widget), gpk_restart_enum_to_localised_text_future (restart_worst));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_info"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), gpk_restart_enum_to_icon_name (restart_worst), GTK_ICON_SIZE_BUTTON);
		gtk_widget_show (widget);
	}

	/* header */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_header_title"));
	text = g_strdup_printf (ngettext ("There is %i update available",
					  "There are %i updates available", len), len);
	text_size = g_strdup_printf ("<big><b>%s</b></big>", text);
	gtk_label_set_label (GTK_LABEL (widget), text_size);
	g_free (text);
	g_free (text_size);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_header"));
	gtk_widget_show (widget);

	/* total */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_summary"));
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

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_summary"));
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

	/* use correct status pane */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_status"));
	gtk_widget_show (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_info"));
	gtk_widget_hide (widget);

	/* set cursor back to normal */
	if (status == PK_STATUS_ENUM_FINISHED) {
		gdk_window_set_cursor (widget->window, NULL);
	}

	/* clear package */
	if (status == PK_STATUS_ENUM_WAIT) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_package"));
		gtk_label_set_label (GTK_LABEL (widget), "");
	}

	/* set status */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_status"));
	if (status == PK_STATUS_ENUM_FINISHED) {
		gtk_label_set_label (GTK_LABEL (widget), "");
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_progress"));
		gtk_widget_hide (widget);
		goto out;
	}
	if (status == PK_STATUS_ENUM_QUERY || status == PK_STATUS_ENUM_SETUP) {
		/* TRANSLATORS: querying update list */
		text = _("Getting the list of updates");
	} else {
		text = gpk_status_enum_to_localised_text (status);
	}

	/* set label */
	gtk_label_set_label (GTK_LABEL (widget), text);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_progress"));

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
 * gpk_update_viewer_treeview_query_tooltip_cb:
 */
static gboolean
gpk_update_viewer_treeview_query_tooltip_cb (GtkWidget *widget, gint x, gint y, gboolean keyboard, GtkTooltip *tooltip, gpointer user_data)
{
	gboolean ret;
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	GtkTreeModel *model;
	PkInfoEnum info;
	PkRestartEnum restart;
	gint bin_x, bin_y, cell_x, cell_y, col_id;
	const gchar *text = NULL;

	/* get path */
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));
	gtk_tree_view_convert_widget_to_bin_window_coords (GTK_TREE_VIEW (widget), x, y, &bin_x, &bin_y);
	ret = gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget), bin_x, bin_y, &path, &column, &cell_x, &cell_y);

	/* did not get path */
	if (!ret || column == NULL || path == NULL)
		goto out;

	/* get iter at path */
	gtk_tree_model_get_iter (model, &iter, path);

	/* Find out what column we are over */
	col_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (column), "tooltip-id"));
	switch (col_id) {
	case GPK_UPDATES_COLUMN_INFO:
		gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_INFO, &info, -1);
		text = gpk_info_enum_to_localised_text (info);
		break;
	case GPK_UPDATES_COLUMN_RESTART:
		gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_RESTART, &restart, -1);
		if (restart == PK_RESTART_ENUM_NONE) {
			ret = FALSE;
			break;
		}
		text = gpk_restart_enum_to_localised_text_future (restart);
		break;
	case GPK_UPDATES_COLUMN_STATUS:
		gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_STATUS, &info, -1);
		if (info == PK_INFO_ENUM_UNKNOWN) {
			ret = FALSE;
			break;
		}
		text = gpk_info_status_enum_to_text (info);
		break;
	default:
		/* ignore */
		ret = FALSE;
		break;
	}

	/* set tooltip */
	if (text != NULL) {
		gtk_tooltip_set_text (tooltip, text);
		gtk_tree_view_set_tooltip_cell (GTK_TREE_VIEW (widget), tooltip, path, column, NULL);
	}
out:
	if (path != NULL)
		gtk_tree_path_free(path);
	return ret;
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
	g_object_set_data (G_OBJECT (column), "tooltip-id", GINT_TO_POINTER (GPK_UPDATES_COLUMN_RESTART));

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
	g_object_set_data (G_OBJECT (column), "tooltip-id", GINT_TO_POINTER (GPK_UPDATES_COLUMN_INFO));

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer,
		      "wrap-mode", PANGO_WRAP_WORD,
		      "ellipsize", PANGO_ELLIPSIZE_END,
		      NULL);
	/* TRANSLATORS: a column that has name of the package that will be updated */
	column = gtk_tree_view_column_new_with_attributes (_("Software"), renderer,
							   "markup", GPK_UPDATES_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_UPDATES_COLUMN_TEXT);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_append_column (treeview, column);
	g_signal_connect (treeview, "size-allocate", G_CALLBACK (gpk_update_viewer_treeview_updates_size_allocate_cb), renderer);

	/* column for size */
	renderer = gpk_cell_renderer_size_new ();
	g_object_set (renderer,
		      "alignment", PANGO_ALIGN_RIGHT,
		      "xalign", 1.0f,
		      NULL);
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

	/* status */
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
	g_object_set_data (G_OBJECT (column), "tooltip-id", GINT_TO_POINTER (GPK_UPDATES_COLUMN_STATUS));

	/* tooltips */
	g_signal_connect (treeview, "query-tooltip", G_CALLBACK (gpk_update_viewer_treeview_query_tooltip_cb), NULL);
	g_object_set (treeview, "has-tooltip", TRUE, NULL);
}

/**
 * gpk_update_viewer_add_description_link_item:
 **/
static void
gpk_update_viewer_add_description_link_item (GtkTextBuffer *buffer, GtkTextIter *iter, const gchar *title, const GPtrArray *array)
{
	GtkTextTag *tag;
	const gchar *uri;
	gint i;

	/* insert at end */
	gtk_text_buffer_insert_with_tags_by_name (buffer, iter, title, -1, "para", NULL);

	for (i=0; i<array->len; i++) {
		uri = g_ptr_array_index (array, i);
		gtk_text_buffer_insert (buffer, iter, "\n", -1);
		gtk_text_buffer_insert (buffer, iter, "• ", -1);
		tag = gtk_text_buffer_create_tag (buffer, NULL,
						  "foreground", "blue",
						  "underline", PANGO_UNDERLINE_SINGLE,
						  NULL);
		g_object_set_data (G_OBJECT (tag), "href", g_strdup (uri));
		gtk_text_buffer_insert_with_tags (buffer, iter, uri, -1, tag, NULL);
		gtk_text_buffer_insert (buffer, iter, ".", -1);
	}
	gtk_text_buffer_insert (buffer, iter, "\n", -1);
}

/**
 * gpk_update_viewer_add_description_link_item:
 **/
static GPtrArray *
gpk_update_viewer_get_uris (const gchar *url_string)
{
	GPtrArray *array;
	gchar **urls;
	guint length;
	gint i;

	array = g_ptr_array_new ();

	urls = g_strsplit (url_string, ";", 0);
	length = g_strv_length (urls);

	/* could we have malformed descriptions with ';' in them? */
	if (length % 2 != 0) {
		egg_warning ("length not correct, correcting");
		length--;
	}

	/* copy into array */
	for (i=0; i<length; i+=2)
		g_ptr_array_add (array, g_strdup (urls[i]));

	return array;
}

/**
 * gpk_update_viewer_populate_details:
 **/
static void
gpk_update_viewer_populate_details (const PkUpdateDetailObj *obj)
{
	GtkTreeView *treeview;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter treeiter;
	GPtrArray *array;
	PkInfoEnum info;
	gchar *line;
	gchar *line2;
	const gchar *title;
	gchar *issued;
	gchar *updated;
	GtkTextIter iter;
	gboolean update_text = FALSE;

	/* get info  */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	selection = gtk_tree_view_get_selection (treeview);
	if (gtk_tree_selection_get_selected (selection, &model, &treeiter))
		gtk_tree_model_get (model, &treeiter,
				    GPK_UPDATES_COLUMN_INFO, &info, -1);
	else
		info = PK_INFO_ENUM_NORMAL;

	/* blank */
	gtk_text_buffer_set_text (text_buffer, "", -1);
	gtk_text_buffer_get_start_iter (text_buffer, &iter);

	if (info == PK_INFO_ENUM_ENHANCEMENT) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, ("This update will add new features and expand functionality."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	} else if (info == PK_INFO_ENUM_BUGFIX) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("This update will fix bugs and other non-critical problems."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	} else if (info == PK_INFO_ENUM_IMPORTANT) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("This update is important as it may solve critical problems."), -1, "para", "important", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	} else if (info == PK_INFO_ENUM_SECURITY) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("This update is needed to fix a security vulnerability with this package."), -1, "para", "important", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	} else if (info == PK_INFO_ENUM_BLOCKED) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("This update is blocked."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	}

	/* issued and updated */
	if (obj->issued != NULL && obj->updated != NULL) {
		issued = pk_iso8601_from_date (obj->issued);
		updated = pk_iso8601_from_date (obj->updated);
		/* TRANSLATORS: this is when the notification was issued and then updated*/
		line = g_strdup_printf (_("This notification was issued on %s and last updated on %s."), issued, updated);
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, line, -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
		g_free (issued);
		g_free (updated);
		g_free (line);
	} else if (obj->issued != NULL) {
		issued = pk_iso8601_from_date (obj->issued);
		/* TRANSLATORS: this is when the update was issued */
		line = g_strdup_printf (_("This notification was issued on %s."), issued);
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, line, -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
		g_free (issued);
		g_free (line);
	}

	/* update text */
	if (!egg_strzero (obj->update_text)) {
		/* convert the bullets */
		line = egg_markdown_parse (markdown, obj->update_text);
		if (!egg_strzero (line)) {
			gtk_text_buffer_insert_markup (text_buffer, &iter, line);
			gtk_text_buffer_insert (text_buffer, &iter, "\n\n", -1);
			update_text = TRUE;
		}
		g_free (line);
	}

	/* add all the links */
	if (!egg_strzero (obj->vendor_url)) {
		array = gpk_update_viewer_get_uris (obj->vendor_url);
		/* TRANSLATORS: this is a list of vendor URLs */
		title = ngettext ("For more information about this update please visit this website:",
				  "For more information about this update please visit these websites:", array->len);
		gpk_update_viewer_add_description_link_item (text_buffer, &iter, title, array);
		g_ptr_array_foreach (array, (GFunc) g_free, NULL);
		g_ptr_array_free (array, TRUE);
	}
	if (!egg_strzero (obj->bugzilla_url)) {
		array = gpk_update_viewer_get_uris (obj->bugzilla_url);
		/* TRANSLATORS: this is a list of bugzilla URLs */
		title = ngettext ("For more information about bugs fixed by this update please visit this website:",
				  "For more information about bugs fixed by this update please visit these websites:", array->len);
		gpk_update_viewer_add_description_link_item (text_buffer, &iter, title, array);
		g_ptr_array_foreach (array, (GFunc) g_free, NULL);
		g_ptr_array_free (array, TRUE);
	}
	if (!egg_strzero (obj->cve_url)) {
		array = gpk_update_viewer_get_uris (obj->cve_url);
		/* TRANSLATORS: this is a list of CVE (security) URLs */
		title = ngettext ("For more information about this security update please visit this website:",
				  "For more information about this security update please visit these websites:", array->len);
		gpk_update_viewer_add_description_link_item (text_buffer, &iter, title, array);
		g_ptr_array_foreach (array, (GFunc) g_free, NULL);
		g_ptr_array_free (array, TRUE);
	}

	/* reboot */
	if (obj->restart == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: reboot required */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("The computer will have to be restarted after the update for the changes to take effect."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	} else if (obj->restart == PK_RESTART_ENUM_SESSION) {
		/* TRANSLATORS: log out required */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("You will need to log out and back in after the update for the changes to take effect."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	}

	/* state */
	if (obj->state == PK_UPDATE_STATE_ENUM_UNSTABLE) {
		/* TRANSLATORS: this is the stability status of the update */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("The classifaction of this update is unstable which means it is not designed for production use."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	} else if (obj->state == PK_UPDATE_STATE_ENUM_TESTING) {
		/* TRANSLATORS: this is the stability status of the update */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("This is a test update, and is not designed for normal use. Please report any problems or regressions you encounter."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	}

	/* only show changelog if we didn't have any update text */
	if (!update_text && !egg_strzero (obj->changelog)) {
		line = egg_markdown_parse (markdown, obj->changelog);
		if (!egg_strzero (line)) {
			/* TRANSLATORS: this is a ChangeLog */
			line2 = g_strdup_printf ("%s\n%s\n", _("The developer logs will be shown as no information is available for this update:"), line);
			gtk_text_buffer_insert_markup (text_buffer, &iter, line2);
			g_free (line2);
		}
		g_free (line);
	}
}

/**
 * gpk_packages_treeview_clicked_cb:
 **/
static void
gpk_packages_treeview_clicked_cb (GtkTreeSelection *selection, gpointer data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *package_id;
	PkUpdateDetailObj *obj = NULL;

	/* set loading text */
	gtk_text_buffer_set_text (text_buffer, _("Loading..."), -1);

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
	GtkWindow *window;

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
	window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_updates"));
	/* TRANSLATORS: we failed to install all the updates we requested */
	gpk_error_dialog_modal (window, _("Some updates were not installed"), text, NULL);
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
 * gpk_update_viewer_primary_requeue:
 **/
static gboolean
gpk_update_viewer_primary_requeue (gpointer data)
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
 * gpk_update_viewer_check_restart:
 **/
static gboolean
gpk_update_viewer_check_restart (PkRestartEnum restart)
{
	GtkWindow *window;
	GtkWidget *dialog;
	gboolean ret = FALSE;
	const gchar *title;
	const gchar *message;
	const gchar *button;
	GtkResponseType response;

	if (restart != PK_RESTART_ENUM_SYSTEM &&
	    restart != PK_RESTART_ENUM_SESSION)
		goto out;

	/* get the text */
	title = gpk_restart_enum_to_localised_text (restart);
	if (restart == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: the message text for the restart */
		message = _("Some of the updates that were installed require the computer to be restarted before the changes will be applied.");
		/* TRANSLATORS: the button text for the restart */
		button = _("Restart Computer");
	} else {
		/* TRANSLATORS: the message text for the logout */
		message = _("Some of the updates that were installed require you to log out and back in before the changes will be applied.");
		/* TRANSLATORS: the button text for the logout */
		button = _("Log Out");
	}

	/* show modal dialog */
	window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_updates"));
	dialog = gtk_message_dialog_new (window, GTK_DIALOG_MODAL,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
					 "%s", title);
	gtk_dialog_add_button (GTK_DIALOG (dialog), button, GTK_RESPONSE_OK);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog), "%s", message);
	gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_INSTALLER);

	/* setup a callback so we autoclose */
	auto_shutdown_id = g_timeout_add_seconds (GPK_UPDATE_VIEWER_AUTO_RESTART_TIMEOUT, (GSourceFunc) gpk_update_viewer_auto_shutdown, dialog);

	response = gtk_dialog_run (GTK_DIALOG(dialog));
	gtk_widget_destroy (dialog);

	/* cancel */
	if (response != GTK_RESPONSE_OK)
		goto out;

	/* doing the action, return success */
	ret = TRUE;

	/* do the action */
	if (restart == PK_RESTART_ENUM_SYSTEM)
		gpk_update_viewer_shutdown ();
	else if (restart == PK_RESTART_ENUM_SESSION)
		gpk_update_viewer_logout ();
out:
	return ret;
}

/**
 * gpk_update_viewer_finished_cb:
 **/
static void
gpk_update_viewer_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, gpointer data)
{
	GtkWidget *widget;
	GtkTreeView *treeview;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	PkBitfield roles;
	PkRoleEnum role;
	PkPackageList *list;
	PkRestartEnum restart;

	pk_client_get_role (client, &role, NULL, NULL);
	egg_debug ("role: %s, exit: %s", pk_role_enum_to_text (role), pk_exit_enum_to_text (exit));

	/* clear package */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_package"));
	gtk_label_set_label (GTK_LABEL (widget), "");

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar_progress"));
	gtk_widget_hide (widget);

	/* hidden window, so quit at this point */
	if (running_hidden) {
		egg_debug ("transaction finished whilst hidden, so exit");
		g_main_loop_quit (loop);
	}

	/* if secondary, ignore */
	if (client == client_primary &&
	    (exit == PK_EXIT_ENUM_KEY_REQUIRED ||
	     exit == PK_EXIT_ENUM_EULA_REQUIRED)) {
		egg_debug ("ignoring primary sig-required or eula");
		return;
	}

	/* get model */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

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
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_install"));
		gtk_widget_set_sensitive (widget, TRUE);

		/* set info */
		gpk_update_viewer_reconsider_info (model);
	}

	if (role == PK_ROLE_ENUM_GET_DETAILS) {

		/* get the distro-upgrades if we support it */
		roles = pk_control_get_actions (control, NULL);
		if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES))
			g_idle_add ((GSourceFunc) gpk_update_viewer_finished_get_distro_upgrades_cb, NULL);

		/* select the first entry in the updates list now we've got data */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_updates"));
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
		gtk_tree_selection_unselect_all (selection);
		path = gtk_tree_path_new_first ();
		gtk_tree_selection_select_path (selection, path);
		gtk_tree_path_free (path);

		/* set info */
		gpk_update_viewer_reconsider_info (model);
	}

	if (role == PK_ROLE_ENUM_GET_DISTRO_UPGRADES) {
		/* set info */
		gpk_update_viewer_reconsider_info (model);
	}

	/* we've just agreed to auth or a EULA */
	if (role == PK_ROLE_ENUM_INSTALL_SIGNATURE ||
	    role == PK_ROLE_ENUM_ACCEPT_EULA) {
		if (exit == PK_EXIT_ENUM_SUCCESS)
			gpk_update_viewer_primary_requeue (NULL);
		else
			gpk_update_viewer_undisable_packages ();
	}

	/* check if we need to display infomation about blocked packages */
	if (exit == PK_EXIT_ENUM_SUCCESS &&
	    (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	     role == PK_ROLE_ENUM_UPDATE_PACKAGES)) {

		/* get the worst restart case */
		restart = pk_client_get_require_restart (client_primary);
		if (restart > restart_update)
			restart_update = restart;

		/* check blocked */
		list = pk_client_get_package_list (client_primary);
		gpk_update_viewer_check_blocked_packages (list);
		g_object_unref (list);

		/* check restart */
		gpk_update_viewer_check_restart (restart_update);

		/* quit after we successfully updated */
		g_main_loop_quit (loop);
	}

	/* check if we need to refresh list */
	if (exit != PK_EXIT_ENUM_SUCCESS &&
	    (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	     role == PK_ROLE_ENUM_UPDATE_PACKAGES)) {
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
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar_progress"));
	gtk_widget_show (widget);
	if (percentage != PK_CLIENT_PERCENTAGE_INVALID)
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

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
	GtkWindow *window;

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

	window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_updates"));
	gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (code),
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
	PkInfoEnum info;

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
 * gpk_update_viewer_detail_popup_menu_select_security:
 **/
static void
gpk_update_viewer_detail_popup_menu_select_security (GtkWidget *menuitem, gpointer userdata)
{
	GtkTreeView *treeview = GTK_TREE_VIEW (userdata);
	gboolean valid;
	gboolean ret;
	GtkTreeIter iter;
	GtkTreeModel *model;
	PkInfoEnum info;

	/* get the first iter in the list */
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_INFO, &info, -1);
		ret = (info == PK_INFO_ENUM_SECURITY);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    GPK_UPDATES_COLUMN_SELECT, ret, -1);
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
static gboolean
gpk_update_viewer_get_checked_status (gboolean *all_checked, gboolean *none_checked)
{
	GtkTreeView *treeview;
	gboolean valid;
	gboolean update;
	gboolean clickable;
	GtkTreeIter iter;
	GtkTreeModel *model;

	/* get the first iter in the list */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	*all_checked = TRUE;
	*none_checked = TRUE;
	while (valid) {
		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_SELECT, &update,
				    GPK_UPDATES_COLUMN_CLICKABLE, &clickable, -1);
		if (update)
			*none_checked = FALSE;
		else
			*all_checked = FALSE;
		valid = gtk_tree_model_iter_next (model, &iter);
	}
	return clickable;
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
	gboolean ret;

	menu = gtk_menu_new();

	/* we don't want to show 'Select all' if they are all checked */
	ret = gpk_update_viewer_get_checked_status (&all_checked, &none_checked);
	if (!ret) {
		egg_debug ("ignoring as we are locked down");
		return;
	}

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

	/* TRANSLATORS: right click menu, select only security updates */
	menuitem = gtk_menu_item_new_with_label (_("Select security updates"));
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_security), treeview);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);

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
 * gpk_update_viewer_message_received_cb
 **/
static void
gpk_update_viewer_message_received_cb (UniqueApp *app, UniqueCommand command, UniqueMessageData *message_data, guint time_ms, gpointer data)
{
	GtkWindow *window;
	if (command == UNIQUE_ACTIVATE) {
		window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_updates"));
		gtk_window_present (window);
		/* not hidden anymore */
		running_hidden = FALSE;
	}
}

/**
 * gpk_update_viewer_get_new_update_list
 **/
static gboolean
gpk_update_viewer_get_new_update_list (void)
{
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;
	gchar *text = NULL;

	/* clear all widgets */
	gtk_list_store_clear (list_store_updates);
	gtk_text_buffer_set_text (text_buffer, "", -1);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_header_title"));
	/* TRANSLATORS: this is the header */
	text = g_strdup_printf ("<big><b>%s</b></big>", _("Checking for updates..."));
	gtk_label_set_label (GTK_LABEL (widget), text);

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
	g_free (text);
	return ret;
}

/**
 * gpk_update_viewer_allow_cancel_cb:
 **/
static void
gpk_update_viewer_allow_cancel_cb (PkClient *client, gboolean allow_cancel, gpointer data)
{
	GdkDisplay *display;
	GdkCursor *cursor;
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_cancel"));
	gtk_widget_set_sensitive (widget, allow_cancel);

	/* set cursor */
	if (allow_cancel) {
		gdk_window_set_cursor (widget->window, NULL);
	} else {
		display = gdk_display_get_default ();
		cursor = gdk_cursor_new_for_display (display, GDK_WATCH);
		gdk_window_set_cursor (widget->window, cursor);
		gdk_cursor_unref (cursor);
	}
}

/**
 * gpk_update_viewer_eula_cb:
 **/
static void
gpk_update_viewer_eula_required_cb (PkClient *client, const gchar *eula_id, const gchar *package_id,
				    const gchar *vendor_name, const gchar *license_agreement, gpointer data)
{
	/* use the helper */
	gpk_helper_eula_show (helper_eula, eula_id, package_id, vendor_name, license_agreement);
}

/**
 * gpk_update_viewer_repo_signature_event_cb:
 **/
static void
gpk_update_viewer_repo_signature_event_cb (GpkHelperRepoSignature *_helper_repo_signature, GtkResponseType type, const gchar *key_id, const gchar *package_id, gpointer data)
{
	GtkTreeView *treeview;
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
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	gpk_update_viewer_reconsider_info (model);
}

/**
 * gpk_update_viewer_eula_event_cb:
 **/
static void
gpk_update_viewer_eula_event_cb (GpkHelperRepoSignature *_helper_eula, GtkResponseType type, const gchar *eula_id, gpointer data)
{
	GtkTreeView *treeview;
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
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
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
	gpk_helper_repo_signature_show (helper_repo_signature, package_id, repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp);
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
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_upgrade"));
	/* TRANSLATORS: new distro available, e.g. F9 to F10 */
	text = g_strdup_printf (_("New distribution upgrade release '%s' is available"), obj->summary);
	text_format = g_strdup_printf ("<b>%s</b>", text);
	gtk_label_set_label (GTK_LABEL (widget), text_format);
	g_free (text);
	g_free (text_format);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "viewport_upgrade"));
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
		if (href != NULL)
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
 * gpk_update_viewer_updates_changed_cb:
 **/
static void
gpk_update_viewer_updates_changed_cb (PkControl *_control, gpointer data)
{
	/* now try to get newest update list */
	egg_debug ("updates changed");
	gpk_update_viewer_get_new_update_list ();
}

/**
 * gpk_update_viewer_vpaned_realized_cb:
 **/
static void
gpk_update_viewer_vpaned_realized_cb (GtkWidget *widget, gpointer data)
{
	GtkRequisition req;
	gtk_widget_size_request (widget, &req);
	egg_debug ("req.height=%i", req.height);
	if (req.height != 0)
		gtk_paned_set_position (GTK_PANED (widget), 166);
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
	guint retval;
	GError *error = NULL;
	UniqueApp *unique_app;

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
	unique_app = unique_app_new ("org.freedesktop.PackageKit.UpdateViewer2", NULL);
	if (unique_app_is_running (unique_app)) {
		egg_debug ("You have another instance running. This program will now close");
		unique_app_send_message (unique_app, UNIQUE_ACTIVATE, NULL);
		goto unique_out;
	}

	g_signal_connect (unique_app, "message-received", G_CALLBACK (gpk_update_viewer_message_received_cb), NULL);

	markdown = egg_markdown_new ();
	egg_markdown_set_output (markdown, EGG_MARKDOWN_OUTPUT_PANGO);
	egg_markdown_set_escape (markdown, TRUE);
	egg_markdown_set_autocode (markdown, TRUE);

	control = pk_control_new ();
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (gpk_update_viewer_repo_list_changed_cb), NULL);
	g_signal_connect (control, "updates-changed",
			  G_CALLBACK (gpk_update_viewer_updates_changed_cb), NULL);

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

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-update-viewer.ui", &error);
	if (error != NULL) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out_build;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_updates"));
	g_signal_connect (main_window, "delete_event", G_CALLBACK (gpk_update_viewer_button_delete_event_cb), NULL);

	/* helpers */
	helper_repo_signature = gpk_helper_repo_signature_new ();
	g_signal_connect (helper_repo_signature, "event", G_CALLBACK (gpk_update_viewer_repo_signature_event_cb), NULL);
	gpk_helper_repo_signature_set_parent (helper_repo_signature, GTK_WINDOW (main_window));

	helper_eula = gpk_helper_eula_new ();
	g_signal_connect (helper_eula, "event", G_CALLBACK (gpk_update_viewer_eula_event_cb), NULL);
	gpk_helper_eula_set_parent (helper_eula, GTK_WINDOW (main_window));

	/* create list stores */
	list_store_updates = gtk_list_store_new (GPK_UPDATES_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT,
						 G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
						 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_POINTER);
	text_buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_create_tag (text_buffer, "para",
				    "pixels_above_lines", 5,
				    "wrap-mode", GTK_WRAP_WORD,
				    NULL);
	gtk_text_buffer_create_tag (text_buffer, "important",
				    "weight", PANGO_WEIGHT_BOLD,
				    NULL);

	/* no upgrades yet */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "viewport_upgrade"));
	gtk_widget_hide (widget);

	/* description */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "textview_details"));
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), text_buffer);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (widget), FALSE);
	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (widget), 5);
	g_signal_connect (GTK_TEXT_VIEW (widget), "key-press-event", G_CALLBACK (gpk_update_viewer_textview_key_press_event), NULL);
	g_signal_connect (GTK_TEXT_VIEW (widget), "event-after", G_CALLBACK (gpk_update_viewer_textview_event_after), NULL);
	g_signal_connect (GTK_TEXT_VIEW (widget), "motion-notify-event", G_CALLBACK (gpk_update_viewer_textview_motion_notify_event), NULL);
	g_signal_connect (GTK_TEXT_VIEW (widget), "visibility-notify-event", G_CALLBACK (gpk_update_viewer_textview_visibility_notify_event), NULL);

	/* updates */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_updates"));
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
			  G_CALLBACK (gpk_packages_treeview_clicked_cb), NULL);

	/* bottom UI */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_summary"));
	gtk_widget_hide (widget);

	/* help button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_help_cb), (gpointer) "update-viewer");

	/* set install button insensitive */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_install"));
	gtk_widget_set_sensitive (widget, FALSE);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_install_cb), NULL);

	/* sensitive */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "scrolledwindow_updates"));
	gtk_widget_set_sensitive (widget, FALSE);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "scrolledwindow_details"));
	gtk_widget_set_sensitive (widget, FALSE);

	/* close button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_close_cb), NULL);
	gtk_window_set_focus (GTK_WINDOW(main_window), widget);

	/* hide cancel button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_cancel"));
	gtk_widget_hide (widget);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_cancel_cb), NULL);

	/* upgrade button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_upgrade"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_upgrade_cb), NULL);

	/* set a size, if the screen allows */
	ret = gpk_window_set_size_request (GTK_WINDOW (main_window), 700, 600);
	if (!ret) {
		egg_debug ("small form factor mode");
		/* hide the header in SFF mode */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_header"));
		gtk_widget_hide (widget);
	}

	/* use correct status pane */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_status"));
	gtk_widget_set_size_request (widget, -1, 32);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_info"));
	gtk_widget_set_size_request (widget, -1, 32);

	/* show window */
	gtk_widget_show (main_window);

	/* set the paned to be in the middle */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "vpaned_updates"));
	g_signal_connect (widget, "realize",
			  G_CALLBACK (gpk_update_viewer_vpaned_realized_cb), NULL);

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

	g_object_unref (helper_eula);
	g_object_unref (helper_repo_signature);
	g_object_unref (builder);
	g_object_unref (list_store_updates);
	g_object_unref (text_buffer);
	pk_package_id_free (package_id_last);
out_build:
	g_object_unref (control);
	g_object_unref (markdown);
	g_object_unref (client_primary);
	g_object_unref (client_secondary);
	g_object_unref (builder);
unique_out:
	g_object_unref (unique_app);

	return 0;
}

