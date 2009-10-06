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

#include <locale.h>
#include <glib.h>
#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <dbus/dbus-glib.h>

#include <gconf/gconf-client.h>
#include <packagekit-glib2/packagekit.h>
#include <libnotify/notify.h>
#include <unique/unique.h>
#include <canberra-gtk.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-markdown.h"
#include "egg-console-kit.h"

#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-dialog.h"
#include "gpk-error.h"
#include "gpk-cell-renderer-size.h"
#include "gpk-cell-renderer-info.h"
#include "gpk-cell-renderer-restart.h"
#include "gpk-cell-renderer-spinner.h"
#include "gpk-enum.h"
#include "gpk-task.h"

#define GPK_UPDATE_VIEWER_AUTO_QUIT_TIMEOUT	10 /* seconds */
#define GPK_UPDATE_VIEWER_AUTO_RESTART_TIMEOUT	60 /* seconds */
#define GPK_UPDATE_VIEWER_MOBILE_SMALL_SIZE	512*1024 /* bytes */
#define GNOME_SESSION_MANAGER_SERVICE		"org.gnome.SessionManager"
#define GNOME_SESSION_MANAGER_PATH		"/org/gnome/SessionManager"
#define GNOME_SESSION_MANAGER_INTERFACE		"org.gnome.SessionManager"

static guint auto_shutdown_id = 0;
static GMainLoop *loop = NULL;
static GtkBuilder *builder = NULL;
static GtkListStore *array_store_updates = NULL;
static GtkTextBuffer *text_buffer = NULL;
static PkTask *task = NULL;
static PkControl *control = NULL;
static GPtrArray *update_array = NULL;
static EggMarkdown *markdown = NULL;
static gchar *package_id_last = NULL;
static PkRestartEnum restart_update = PK_RESTART_ENUM_NONE;
static guint size_total = 0;
static GConfClient *gconf_client = NULL;
static gchar **install_package_ids = NULL;
static EggConsoleKit *console = NULL;
static GCancellable *cancellable = NULL;

enum {
	GPK_UPDATES_COLUMN_TEXT,
	GPK_UPDATES_COLUMN_ID,
	GPK_UPDATES_COLUMN_INFO,
	GPK_UPDATES_COLUMN_SELECT,
	GPK_UPDATES_COLUMN_SENSITIVE,
	GPK_UPDATES_COLUMN_CLICKABLE,
	GPK_UPDATES_COLUMN_RESTART,
	GPK_UPDATES_COLUMN_SIZE,
	GPK_UPDATES_COLUMN_SIZE_DISPLAY,
	GPK_UPDATES_COLUMN_PERCENTAGE,
	GPK_UPDATES_COLUMN_STATUS,
	GPK_UPDATES_COLUMN_DETAILS_OBJ,
	GPK_UPDATES_COLUMN_UPDATE_DETAIL_OBJ,
	GPK_UPDATES_COLUMN_PULSE,
	GPK_UPDATES_COLUMN_LAST
};

static gboolean gpk_update_viewer_get_new_update_array (void);

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
	GError *error = NULL;
	gboolean ret;

	/* use consolekit to restart */
	ret = egg_console_kit_restart (console, &error);
	if (!ret) {
		egg_warning ("cannot restart: %s", error->message);
		g_error_free (error);
	}
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
 * gpk_update_viewer_quit:
 **/
static void
gpk_update_viewer_quit (void)
{
	/* are we in a transaction */
	g_cancellable_cancel (cancellable);
	g_main_loop_quit (loop);
}

/**
 * gpk_update_viewer_button_quit_cb:
 **/
static void
gpk_update_viewer_button_quit_cb (GtkWidget *widget, gpointer data)
{
	gpk_update_viewer_quit ();
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
		gtk_list_store_set (array_store_updates, &iter,
				    GPK_UPDATES_COLUMN_SENSITIVE, TRUE,
				    GPK_UPDATES_COLUMN_CLICKABLE, TRUE,
				    -1);
		valid = gtk_tree_model_iter_next (model, &iter);
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
	gboolean show_button = TRUE;

	/* get the text */
	title = gpk_restart_enum_to_localised_text (restart);
	if (restart == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: the message text for the restart */
		message = _("Some of the updates that were installed require the computer to be restarted before the changes will be applied.");
		/* TRANSLATORS: the button text for the restart */
		button = _("Restart Computer");

	} else if (restart == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		/* TRANSLATORS: the message text for the restart */
		message = _("Some of the updates that were installed require the computer to be restarted to remain secure.");
		/* TRANSLATORS: the button text for the restart */
		button = _("Restart Computer");

	} else if (restart == PK_RESTART_ENUM_SESSION) {
		/* TRANSLATORS: the message text for the logout */
		message = _("Some of the updates that were installed require you to log out and back in before the changes will be applied.");
		/* TRANSLATORS: the button text for the logout */
		button = _("Log Out");

	} else if (restart == PK_RESTART_ENUM_SECURITY_SESSION) {
		/* TRANSLATORS: the message text for the logout */
		message = _("Some of the updates that were installed require you to log out and back in to remain secure.");
		/* TRANSLATORS: the button text for the logout */
		button = _("Log Out");

	} else {
		egg_warning ("unknown restart enum");
		goto out;
	}

	/* show modal dialog */
	window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_updates"));
	dialog = gtk_message_dialog_new (window, GTK_DIALOG_MODAL,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
					 "%s", title);

	/* check to see if restart is possible */
	if (restart == PK_RESTART_ENUM_SYSTEM ||
	    restart == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		egg_console_kit_can_restart (console, &show_button, NULL);
	}

	/* only show the button if we can do the action */
	if (show_button)
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
 * gpk_update_viewer_check_blocked_packages:
 **/
static void
gpk_update_viewer_check_blocked_packages (GPtrArray *array)
{
	guint i;
	const PkItemPackage *item;
	GString *string;
	gboolean exists = FALSE;
	gchar *text;
	GtkWindow *window;

	string = g_string_new ("");

	/* find any that are blocked */
	for (i=0;i<array->len;i++) {
		item = g_ptr_array_index (array, i);
		if (item->info == PK_INFO_ENUM_BLOCKED) {
			text = gpk_package_id_format_oneline (item->package_id, item->summary);
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
 * gpk_update_viewer_update_packages_cb:
 **/
static void
gpk_update_viewer_update_packages_cb (PkTask *_task, GAsyncResult *res, GMainLoop *_loop)
{
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array = NULL;
	PkItemPackage *item;
	guint i;
	GtkWidget *dialog;
	GtkWidget *widget;
	PkRestartEnum restart;
	gchar *text;
	PkItemErrorCode *error_item = NULL;
	GtkWindow *window;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		egg_warning ("failed to update packages: %s", error->message);
		g_error_free (error);

		/* re-enable the package list */
		gpk_update_viewer_undisable_packages ();

		/* allow clicking again */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_install"));
		gtk_widget_set_sensitive (widget, TRUE);

		goto out;
	}

	/* check error code */
	error_item = pk_results_get_error_code (results);
	if (error_item != NULL) {
		egg_warning ("failed to update packages: %s, %s", pk_error_enum_to_text (error_item->code), error_item->details);

		window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (error_item->code),
					gpk_error_enum_to_localised_message (error_item->code), error_item->details);

		/* re-enable the package list */
		gpk_update_viewer_undisable_packages ();

		/* allow clicking again */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_install"));
		gtk_widget_set_sensitive (widget, TRUE);

		goto out;
	}

	/* TODO: failed sound */
	/* play the sound, using sounds from the naming spec */
	ca_context_play (ca_gtk_context_get (), 0,
			 CA_PROP_EVENT_ID, "dialog-warning",
			 /* TRANSLATORS: this is the application name for libcanberra */
			 CA_PROP_APPLICATION_NAME, _("GNOME PackageKit Update Viewer"),
			 /* TRANSLATORS: this is the sound description */
			 CA_PROP_EVENT_DESCRIPTION, _("Failed to update"), NULL);

	gpk_update_viewer_undisable_packages ();

	/* get blocked data */
	array = pk_results_get_package_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		egg_debug ("updated %s:%s", pk_info_enum_to_text (item->info), item->package_id);
	}

	/* TODO: use ca_gtk_context_get_for_screen to allow use of GDK_MULTIHEAD_SAFE */

	/* play the sound, using sounds from the naming spec */
	ca_context_play (ca_gtk_context_get (), 0,
			 /* TODO: add a new sound to the spec */
			 CA_PROP_EVENT_ID, "complete-download",
			 /* TRANSLATORS: this is the application name for libcanberra */
			 CA_PROP_APPLICATION_NAME, _("GNOME PackageKit Update Viewer"),
			 /* TRANSLATORS: this is the sound description */
			 CA_PROP_EVENT_DESCRIPTION, _("Updated successfully"), NULL);

	/* get the worst restart case */
	restart = pk_results_get_require_restart_worst (results);
	if (restart > restart_update)
		restart_update = restart;

	/* check blocked */
	array = pk_results_get_package_array (results);
	gpk_update_viewer_check_blocked_packages (array);
	g_ptr_array_unref (array);

	/* check restart */
	if (restart_update == PK_RESTART_ENUM_SYSTEM ||
	    restart_update == PK_RESTART_ENUM_SESSION ||
	    restart_update == PK_RESTART_ENUM_SECURITY_SESSION ||
	    restart_update == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		gpk_update_viewer_check_restart (restart_update);
		g_main_loop_quit (loop);
	}

	/* hide close button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_quit"));
	gtk_widget_hide (widget);

	/* show a new title */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_header_title"));
	/* TRANSLATORS: completed all updates */
	text = g_strdup_printf ("<big><b>%s</b></big>", _("All selected updates installed..."));
	gtk_label_set_label (GTK_LABEL (widget), text);
	g_free (text);

	/* show modal dialog */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_updates"));
	dialog = gtk_message_dialog_new (GTK_WINDOW (widget), GTK_DIALOG_MODAL,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
					 /* TRANSLATORS: title: all updates installed okay */
					 "%s", _("All selected updates installed"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog),
						  "%s",
						  /* TRANSLATORS: software updates installed okay */
						  _("All selected updates were successfully installed."));
	gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_INSTALLER);

	/* setup a callback so we autoclose */
	auto_shutdown_id = g_timeout_add_seconds (GPK_UPDATE_VIEWER_AUTO_RESTART_TIMEOUT, (GSourceFunc) gpk_update_viewer_auto_shutdown, dialog);

	gtk_dialog_run (GTK_DIALOG(dialog));
	gtk_widget_destroy (dialog);

	/* quit after we successfully updated */
	g_main_loop_quit (loop);
out:
	if (error_item != NULL)
		pk_item_error_code_unref (error_item);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}


static GSList *active_rows = NULL;
static guint active_row_timeout = 0;

/**
 * gpk_update_viewer_compare_refs:
 **/
static gint
gpk_update_viewer_compare_refs (GtkTreeRowReference *a, GtkTreeRowReference *b)
{
	GtkTreeModel *am, *bm;
	GtkTreePath *ap, *bp;
	gint res;

	am = gtk_tree_row_reference_get_model (a);
	bm = gtk_tree_row_reference_get_model (b);

	res = 1;
	if (am == bm) {
		ap = gtk_tree_row_reference_get_path (a);
		bp = gtk_tree_row_reference_get_path (b);

		res = gtk_tree_path_compare (ap, bp);

		gtk_tree_path_free (ap);
		gtk_tree_path_free (bp);
	}

	return res;
}

/**
 * gpk_update_viewer_pulse_active_rows:
 **/
static gboolean
gpk_update_viewer_pulse_active_rows (void)
{
	GSList *l;
	GtkTreeRowReference *ref;
	GtkTreeModel *model;
	GtkTreePath *path;
	GtkTreeIter iter;
	gint val;

	for (l = active_rows; l; l = l->next) {
		ref = l->data;
		model = gtk_tree_row_reference_get_model (ref);
		path = gtk_tree_row_reference_get_path (ref);
		if (path) {
			gtk_tree_model_get_iter (model, &iter, path);
			gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_PULSE, &val, -1);
			gtk_list_store_set (GTK_LIST_STORE (model), &iter, GPK_UPDATES_COLUMN_PULSE, val + 1, -1);
			gtk_tree_path_free (path);
		}
	}

	return TRUE;
}

/**
 * gpk_update_viewer_add_active_row:
 **/
static void
gpk_update_viewer_add_active_row (GtkTreeModel *model, GtkTreePath *path)
{
	GtkTreeRowReference *ref;

	if (!active_row_timeout) {
		active_row_timeout = g_timeout_add (60, (GSourceFunc)gpk_update_viewer_pulse_active_rows, NULL);
	}

	ref = gtk_tree_row_reference_new (model, path);
	active_rows = g_slist_prepend (active_rows, ref);
}

/**
 * gpk_update_viewer_remove_active_row:
 **/
static void
gpk_update_viewer_remove_active_row (GtkTreeModel *model, GtkTreePath *path)
{
	GSList *link;
	GtkTreeRowReference *ref;
	GtkTreeIter iter;

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, GPK_UPDATES_COLUMN_PULSE, -1, -1);

	ref = gtk_tree_row_reference_new (model, path);
	link = g_slist_find_custom (active_rows, (gconstpointer)ref, (GCompareFunc)gpk_update_viewer_compare_refs);
	gtk_tree_row_reference_free (ref);
	g_assert (link);

	active_rows = g_slist_remove_link (active_rows, link);
	gtk_tree_row_reference_free (link->data);
	g_slist_free (link);

	if (active_rows == NULL) {
		g_source_remove (active_row_timeout);
		active_row_timeout = 0;
	}
}

/**
 * gpk_update_viewer_find_iter_model_cb:
 **/
static gboolean
gpk_update_viewer_find_iter_model_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, const gchar *package_id)
{
	gchar *package_id_tmp = NULL;
	GtkTreePath **_path = NULL;
	gboolean ret = FALSE;
	gchar **split;
	gchar **split_tmp;

	_path = (GtkTreePath **) g_object_get_data (G_OBJECT(model), "_path");
	gtk_tree_model_get (model, iter,
			    GPK_UPDATES_COLUMN_ID, &package_id_tmp,
			    -1);

	/* only match on the name */
	split = pk_package_id_split (package_id);
	split_tmp = pk_package_id_split (package_id_tmp);
	if (g_strcmp0 (split[PK_PACKAGE_ID_NAME], split_tmp[PK_PACKAGE_ID_NAME]) == 0) {
		*_path = gtk_tree_path_copy (path);
		ret = TRUE;
	}
	g_free (package_id_tmp);
	g_strfreev (split);
	g_strfreev (split_tmp);
	return ret;
}

/**
 * gpk_update_viewer_model_get_path:
 **/
static GtkTreePath *
gpk_update_viewer_model_get_path (GtkTreeModel *model, const gchar *package_id)
{
	GtkTreePath *path = NULL;
	g_return_val_if_fail (package_id != NULL, NULL);
	g_object_set_data (G_OBJECT(model), "_path", (gpointer) &path);
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_update_viewer_find_iter_model_cb, (gpointer) package_id);
	g_object_steal_data (G_OBJECT(model), "_path");
	return path;
}

/**
 * gpk_update_viewer_progress_cb:
 **/
static void
gpk_update_viewer_progress_cb (PkProgress *progress, PkProgressType type, GMainLoop *_loop)
{
	gboolean allow_cancel;
	gchar *package_id;
	gchar *text;
	gint percentage;
	gint subpercentage;
	GtkWidget *widget;
	PkInfoEnum info = PK_INFO_ENUM_UNKNOWN;
	PkRoleEnum role;
	PkStatusEnum status;

	g_object_get (progress,
		      "role", &role,
		      "status", &status,
		      "percentage", &percentage,
		      "subpercentage", &subpercentage,
		      "package-id", &package_id,
		      "allow-cancel", &allow_cancel,
		      NULL);

	if (type == PK_PROGRESS_TYPE_PACKAGE_ID) {

		GtkTreeView *treeview;
		GtkTreeIter iter;
		GtkTreeModel *model;
		GtkTreeViewColumn *column;
		GtkTreePath *path;
		gboolean scroll;

		/* add the results, not the progress */
		if (role == PK_ROLE_ENUM_GET_UPDATES)
			return;

		/* used for progress */
		g_free (package_id_last);
		package_id_last = g_strdup (package_id);

		/* find model */
		treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
		model = gtk_tree_view_get_model (treeview);

		/* update icon */
		path = gpk_update_viewer_model_get_path (model, package_id);
		if (path == NULL) {
			text = gpk_package_id_format_twoline (package_id, NULL); //TODO: summary
			egg_debug ("adding: id=%s, text=%s", package_id, text);
			gtk_list_store_append (array_store_updates, &iter);
			gtk_list_store_set (array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_TEXT, text,
					    GPK_UPDATES_COLUMN_ID, package_id,
					    GPK_UPDATES_COLUMN_INFO, PK_INFO_ENUM_NORMAL, //TODO info
					    GPK_UPDATES_COLUMN_SELECT, TRUE,
					    GPK_UPDATES_COLUMN_SENSITIVE, FALSE,
					    GPK_UPDATES_COLUMN_CLICKABLE, FALSE,
					    GPK_UPDATES_COLUMN_RESTART, PK_RESTART_ENUM_NONE,
					    GPK_UPDATES_COLUMN_STATUS, PK_INFO_ENUM_UNKNOWN,
					    GPK_UPDATES_COLUMN_SIZE, 0,
					    GPK_UPDATES_COLUMN_SIZE_DISPLAY, 0,
					    GPK_UPDATES_COLUMN_PERCENTAGE, 0,
					    GPK_UPDATES_COLUMN_PULSE, -1,
					    -1);
			g_free (text);
			path = gpk_update_viewer_model_get_path (model, package_id);
			if (path == NULL) {
				egg_warning ("found no package %s", package_id);
				goto out;
			}
		}

		gtk_tree_model_get_iter (model, &iter, path);

		/* if we are adding deps, then select the checkbox */
		if (role == PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES) {
			gtk_list_store_set (array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_SELECT, TRUE,
					    -1);
		}

		/* scroll to the active cell */
		scroll = gconf_client_get_bool (gconf_client, GPK_CONF_UPDATE_VIEWER_SCROLL_ACTIVE, NULL);
		if (scroll) {
			column = gtk_tree_view_get_column (treeview, 3);
			gtk_tree_view_scroll_to_cell (treeview, path, column, FALSE, 0.0f, 0.0f);
		}

		/* if the info is finished, change the status to past tense */
		if (info == PK_INFO_ENUM_FINISHED) {
			gtk_tree_model_get (model, &iter,
					    GPK_UPDATES_COLUMN_STATUS, &info, -1);
			/* promote to past tense if present tense */
			if (info < PK_INFO_ENUM_UNKNOWN)
				info += PK_INFO_ENUM_UNKNOWN;
		}
		gtk_list_store_set (array_store_updates, &iter,
				    GPK_UPDATES_COLUMN_STATUS, info, -1);

		gtk_tree_path_free (path);

	} else if (type == PK_PROGRESS_TYPE_STATUS) {

		GdkWindow *window;
		const gchar *title;
		GdkDisplay *display;
		GdkCursor *cursor;

		egg_debug ("status %s", pk_status_enum_to_text (status));

		/* use correct status pane */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_status"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "hbox_info"));
		gtk_widget_hide (widget);

		/* set cursor back to normal */
		window = gtk_widget_get_window (widget);
		if (status == PK_STATUS_ENUM_FINISHED) {
			gdk_window_set_cursor (window, NULL);
		} else {
			display = gdk_display_get_default ();
			cursor = gdk_cursor_new_for_display (display, GDK_WATCH);
			gdk_window_set_cursor (window, cursor);
			gdk_cursor_unref (cursor);
		}

		/* set status */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_status"));
		if (status == PK_STATUS_ENUM_FINISHED) {
			gtk_label_set_label (GTK_LABEL (widget), "");
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_progress"));
			gtk_widget_hide (widget);

			widget = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar_progress"));
			gtk_widget_hide (widget);

			widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_quit"));
			gtk_widget_set_sensitive (widget, TRUE);
		} else {
			if (status == PK_STATUS_ENUM_QUERY || status == PK_STATUS_ENUM_SETUP) {
				/* TRANSLATORS: querying update array */
				title = _("Getting the list of updates");
			} else if (status == PK_STATUS_ENUM_WAIT) {
				title = "";
			} else {
				title = gpk_status_enum_to_localised_text (status);
			}
			gtk_label_set_label (GTK_LABEL (widget), title);

			/* set icon */
			widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_progress"));
			gtk_image_set_from_icon_name (GTK_IMAGE (widget), gpk_status_enum_to_icon_name (status), GTK_ICON_SIZE_BUTTON);
			gtk_widget_show (widget);
		}

	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {

		GtkTreeView *treeview;
		GtkTreeModel *model;
		GtkTreeIter iter;
		GtkTreePath *path;
		guint oldval;
		guint size;
		guint size_display;

		widget = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar_progress"));
		gtk_widget_show (widget);
		if (percentage != -1)
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
		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_PERCENTAGE, &oldval,
				    GPK_UPDATES_COLUMN_SIZE, &size,
				    -1);
		if ((oldval > 0 && oldval < 100) != (subpercentage > 0 && subpercentage < 100)) {
			if (oldval > 0 && oldval < 100)
				gpk_update_viewer_remove_active_row (model, path);
			else
				gpk_update_viewer_add_active_row (model, path);
		}
		if (subpercentage > 0) {
			size_display = size - ((size * subpercentage) / 100);
			gtk_list_store_set (array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_PERCENTAGE, subpercentage,
					    GPK_UPDATES_COLUMN_SIZE_DISPLAY, size_display,
					    -1);
		}
		gtk_tree_path_free (path);

	} else if (type == PK_PROGRESS_TYPE_ALLOW_CANCEL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_quit"));
		gtk_widget_set_sensitive (widget, allow_cancel);
	}
out:
	g_free (package_id);
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
	gboolean valid;
	gboolean update;
	gboolean selected_any = FALSE;
	gchar *package_id;
	GPtrArray *array = NULL;
	gchar **package_ids = NULL;
	PkInfoEnum info;

	/* hide the upgrade viewbox from now on */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "viewport_upgrade"));
	gtk_widget_hide (widget);

	egg_debug ("Doing the package updates");
	array = g_ptr_array_new ();

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	/* get the first iter in the array */
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* find out how many we should update */
	while (valid) {
		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_INFO, &info,
				    GPK_UPDATES_COLUMN_SELECT, &update,
				    GPK_UPDATES_COLUMN_ID, &package_id, -1);

		/* set all the checkboxes insensitive */
		gtk_list_store_set (array_store_updates, &iter,
				    GPK_UPDATES_COLUMN_CLICKABLE, FALSE,
				    GPK_UPDATES_COLUMN_SENSITIVE, FALSE, -1);

		/* any selected? */
		if (update)
			selected_any = TRUE;

		/* if selected, and not added previously because of deps */
		if (update && info != PK_INFO_ENUM_AVAILABLE) {
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

	/* disable button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_install"));
	gtk_widget_set_sensitive (widget, FALSE);

	/* clear the selection */
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_unselect_all (selection);

	/* save for finished */
	package_ids = pk_ptr_array_to_strv (array);
	g_strfreev (install_package_ids);
	install_package_ids = g_strdupv (package_ids);

	/* get packages that also have to be updated */
	pk_task_update_packages_async (task, package_ids, cancellable,
				       (PkProgressCallback) gpk_update_viewer_progress_cb, NULL,
				       (GAsyncReadyCallback) gpk_update_viewer_update_packages_cb, loop);
	g_strfreev (package_ids);

	/* get rid of the array, and free the contents */
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * gpk_update_viewer_button_upgrade_cb:
 **/
static void
gpk_update_viewer_button_upgrade_cb (GtkWidget *widget, gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	ret = g_spawn_command_line_async ("/usr/share/PackageKit/pk-upgrade-distro.sh", &error);
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
	gpk_update_viewer_quit ();
	return TRUE;
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

	/* reset to zero */
	size_total = 0;

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
	len = update_array->len;
	if (len == 0) {
		/* hide close button */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_quit"));
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
						 GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
						 /* TRANSLATORS: title: warn the user they are quitting with unapplied changes */
						 "%s", _("All software is up to date"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog),
							  "%s",
							  /* TRANSLATORS: tell the user the problem */
							  _("There are no software updates available for your computer at this time."));
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
		if (size_total == 0) {
			/* TRANSLATORS: how many updates are selected in the UI */
			text = g_strdup_printf (ngettext ("%i update selected",
							  "%i updates selected",
							  number_total), number_total);
			gtk_label_set_label (GTK_LABEL (widget), text);
			g_free (text);
		} else {
			text_size = g_format_size_for_display (size_total);
			/* TRANSLATORS: how many updates are selected in the UI, and the size of packages to download */
			text = g_strdup_printf (ngettext ("%i update selected (%s)",
							  "%i updates selected (%s)",
							  number_total), number_total, text_size);
			gtk_label_set_label (GTK_LABEL (widget), text);
			g_free (text);
			g_free (text_size);
		}
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_summary"));
	gtk_widget_show (widget);
out:
	return;
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
	g_object_set (renderer,
		      "stock-size", GTK_ICON_SIZE_BUTTON,
		      "ignore-values", "unknown", NULL);
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
	gtk_tree_view_column_set_sort_column_id (column, GPK_UPDATES_COLUMN_ID);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_append_column (treeview, column);
	g_signal_connect (treeview, "size-allocate", G_CALLBACK (gpk_update_viewer_treeview_updates_size_allocate_cb), renderer);

	/* --- column for progress --- */
	column = gtk_tree_view_column_new ();
	/* TRANSLATORS: a column that has state of each package */
	gtk_tree_view_column_set_title (column, _("Status"));
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), FALSE);
	gtk_tree_view_column_set_sort_column_id (column, GPK_UPDATES_COLUMN_STATUS);

	/* status */
	renderer = gpk_cell_renderer_info_new ();
	g_object_set (renderer,
		      "stock-size", GTK_ICON_SIZE_BUTTON,
		      "ignore-values", "unknown", NULL);
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_add_attribute (column, renderer, "value", GPK_UPDATES_COLUMN_STATUS);

	/* column for progress */
	renderer = gpk_cell_renderer_spinner_new ();
	g_object_set (renderer, "size", GTK_ICON_SIZE_BUTTON, NULL);
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_add_attribute (column, renderer, "pulse", GPK_UPDATES_COLUMN_PULSE);
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), FALSE);

	gtk_tree_view_append_column (treeview, column);
	g_object_set_data (G_OBJECT (column), "tooltip-id", GINT_TO_POINTER (GPK_UPDATES_COLUMN_STATUS));

	/* tooltips */
	g_signal_connect (treeview, "query-tooltip", G_CALLBACK (gpk_update_viewer_treeview_query_tooltip_cb), NULL);
	g_object_set (treeview, "has-tooltip", TRUE, NULL);

	/* --- column for size --- */
	column = gtk_tree_view_column_new ();
	/* TRANSLATORS: a column that has size of each package */
	gtk_tree_view_column_set_title (column, _("Size"));
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), FALSE);
	gtk_tree_view_column_set_sort_column_id (column, GPK_UPDATES_COLUMN_SIZE_DISPLAY);

	/* size */
	renderer = gpk_cell_renderer_size_new ();
	g_object_set (renderer,
		      "alignment", PANGO_ALIGN_RIGHT,
		      "xalign", 1.0f,
		      NULL);
	g_object_set (renderer,
		      "value", GPK_UPDATES_COLUMN_SIZE_DISPLAY, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "value", GPK_UPDATES_COLUMN_SIZE_DISPLAY);

	gtk_tree_view_append_column (treeview, column);
	g_object_set_data (G_OBJECT (column), "tooltip-id", GINT_TO_POINTER (GPK_UPDATES_COLUMN_SIZE_DISPLAY));
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

	array = g_ptr_array_new_with_free_func (g_free);

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
gpk_update_viewer_populate_details (const PkItemUpdateDetail *item)
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
	if (item->issued != NULL && item->updated != NULL) {
		issued = pk_iso8601_from_date (item->issued);
		updated = pk_iso8601_from_date (item->updated);
		/* TRANSLATORS: this is when the notification was issued and then updated*/
		line = g_strdup_printf (_("This notification was issued on %s and last updated on %s."), issued, updated);
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, line, -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
		g_free (issued);
		g_free (updated);
		g_free (line);
	} else if (item->issued != NULL) {
		issued = pk_iso8601_from_date (item->issued);
		/* TRANSLATORS: this is when the update was issued */
		line = g_strdup_printf (_("This notification was issued on %s."), issued);
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, line, -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
		g_free (issued);
		g_free (line);
	}

	/* update text */
	if (!egg_strzero (item->update_text)) {
		/* convert the bullets */
		line = egg_markdown_parse (markdown, item->update_text);
		if (!egg_strzero (line)) {
			gtk_text_buffer_insert_markup (text_buffer, &iter, line);
			gtk_text_buffer_insert (text_buffer, &iter, "\n\n", -1);
			update_text = TRUE;
		}
		g_free (line);
	}

	/* add all the links */
	if (!egg_strzero (item->vendor_url)) {
		array = gpk_update_viewer_get_uris (item->vendor_url);
		/* TRANSLATORS: this is a array of vendor URLs */
		title = ngettext ("For more information about this update please visit this website:",
				  "For more information about this update please visit these websites:", array->len);
		gpk_update_viewer_add_description_link_item (text_buffer, &iter, title, array);
		g_ptr_array_unref (array);
	}
	if (!egg_strzero (item->bugzilla_url)) {
		array = gpk_update_viewer_get_uris (item->bugzilla_url);
		/* TRANSLATORS: this is a array of bugzilla URLs */
		title = ngettext ("For more information about bugs fixed by this update please visit this website:",
				  "For more information about bugs fixed by this update please visit these websites:", array->len);
		gpk_update_viewer_add_description_link_item (text_buffer, &iter, title, array);
		g_ptr_array_unref (array);
	}
	if (!egg_strzero (item->cve_url)) {
		array = gpk_update_viewer_get_uris (item->cve_url);
		/* TRANSLATORS: this is a array of CVE (security) URLs */
		title = ngettext ("For more information about this security update please visit this website:",
				  "For more information about this security update please visit these websites:", array->len);
		gpk_update_viewer_add_description_link_item (text_buffer, &iter, title, array);
		g_ptr_array_unref (array);
	}

	/* reboot */
	if (item->restart == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: reboot required */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("The computer will have to be restarted after the update for the changes to take effect."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	} else if (item->restart == PK_RESTART_ENUM_SESSION) {
		/* TRANSLATORS: log out required */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("You will need to log out and back in after the update for the changes to take effect."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	}

	/* state */
	if (item->state == PK_UPDATE_STATE_ENUM_UNSTABLE) {
		/* TRANSLATORS: this is the stability status of the update */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("The classifaction of this update is unstable which means it is not designed for production use."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	} else if (item->state == PK_UPDATE_STATE_ENUM_TESTING) {
		/* TRANSLATORS: this is the stability status of the update */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("This is a test update, and is not designed for normal use. Please report any problems or regressions you encounter."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	}

	/* only show changelog if we didn't have any update text */
	if (!update_text && !egg_strzero (item->changelog)) {
		line = egg_markdown_parse (markdown, item->changelog);
		if (!egg_strzero (line)) {
			/* TRANSLATORS: this is a ChangeLog */
			line2 = g_strdup_printf ("%s\n%s\n", _("The developer logs will be shown as no description is available for this update:"), line);
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
	PkItemUpdateDetail *item = NULL;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {

		/* set loading text */
		gtk_text_buffer_set_text (text_buffer, _("Loading..."), -1);

		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_UPDATE_DETAIL_OBJ, &item,
				    GPK_UPDATES_COLUMN_ID, &package_id, -1);
		egg_debug ("selected row is: %s, %p", package_id, item);
		g_free (package_id);
		if (item != NULL)
			gpk_update_viewer_populate_details (item);
	} else {
		egg_debug ("no row selected");
	}
}

/**
 * gpk_update_viewer_get_details_cb:
 **/
static void
gpk_update_viewer_get_details_cb (PkClient *client, GAsyncResult *res, GMainLoop *_loop)
{
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array = NULL;
	PkItemDetails *item;
	guint i;
	GtkWidget *widget;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeView *treeview;
	GtkTreeIter iter;
	PkItemErrorCode *error_item = NULL;
	GtkWindow *window;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to get details: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_item = pk_results_get_error_code (results);
	if (error_item != NULL) {
		egg_warning ("failed to get details: %s, %s", pk_error_enum_to_text (error_item->code), error_item->details);

		window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (error_item->code),
					gpk_error_enum_to_localised_message (error_item->code), error_item->details);
		goto out;
	}

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	/* get data */
	array = pk_results_get_details_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);

		path = gpk_update_viewer_model_get_path (model, item->package_id);
		if (path == NULL) {
			egg_debug ("not found ID for details");
			return;
		}

		gtk_tree_model_get_iter (model, &iter, path);
		gtk_tree_path_free (path);
		gtk_list_store_set (array_store_updates, &iter,
				    GPK_UPDATES_COLUMN_DETAILS_OBJ, (gpointer) pk_item_details_ref (item),
				    GPK_UPDATES_COLUMN_SIZE, (gint)item->size,
				    GPK_UPDATES_COLUMN_SIZE_DISPLAY, (gint)item->size,
				    -1);
		/* in cache */
		if (item->size == 0)
			gtk_list_store_set (array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_STATUS, GPK_INFO_ENUM_DOWNLOADED, -1);
	}

	/* select the first entry in the updates array now we've got data */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_updates"));
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	gtk_tree_selection_unselect_all (selection);
	path = gtk_tree_path_new_first ();
	gtk_tree_selection_select_path (selection, path);
	gtk_tree_path_free (path);

	/* set info */
	gpk_update_viewer_reconsider_info (model);
out:
	if (error_item != NULL)
		pk_item_error_code_unref (error_item);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_update_viewer_get_update_detail_cb:
 **/
static void
gpk_update_viewer_get_update_detail_cb (PkClient *client, GAsyncResult *res, GMainLoop *_loop)
{
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array = NULL;
	PkItemUpdateDetail *item;
	guint i;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	PkItemErrorCode *error_item = NULL;
	GtkWindow *window;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to get update details: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_item = pk_results_get_error_code (results);
	if (error_item != NULL) {
		egg_warning ("failed to get update details: %s, %s", pk_error_enum_to_text (error_item->code), error_item->details);

		window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (error_item->code),
					gpk_error_enum_to_localised_message (error_item->code), error_item->details);
		goto out;
	}

	/* get data */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	array = pk_results_get_update_detail_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		path = gpk_update_viewer_model_get_path (model, item->package_id);
		if (path == NULL) {
			egg_warning ("not found ID for update detail");
			continue;
		}

		gtk_tree_model_get_iter (model, &iter, path);
		gtk_tree_path_free (path);
		gtk_list_store_set (array_store_updates, &iter,
				    GPK_UPDATES_COLUMN_UPDATE_DETAIL_OBJ, (gpointer) pk_item_update_detail_ref (item),
				    GPK_UPDATES_COLUMN_RESTART, item->restart, -1);
	}
out:
	if (error_item != NULL)
		pk_item_error_code_unref (error_item);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_update_viewer_repo_array_changed_cb:
 **/
static void
gpk_update_viewer_repo_array_changed_cb (PkClient *client, gpointer data)
{
	gpk_update_viewer_get_new_update_array ();
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

	/* get the first iter in the array */
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

	/* get the first iter in the array */
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

	/* get the first iter in the array */
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
	gboolean clickable = FALSE;
	GtkTreeIter iter;
	GtkTreeModel *model;

	/* get the first iter in the array */
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
	}
}

/**
 * gpk_update_viewer_packages_to_ids:
 **/
static gchar **
gpk_update_viewer_packages_to_ids (GPtrArray *array)
{
	guint i;
	gchar **value;
	PkItemPackage *item;

	value = g_new0 (gchar *, array->len + 1);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		value[i] = g_strdup (item->package_id);
	}
	return value;
}

/**
 * gpk_update_viewer_get_updates_cb:
 **/
static void
gpk_update_viewer_get_updates_cb (PkClient *client, GAsyncResult *res, GMainLoop *_loop)
{
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array = NULL;
	PkItemPackage *item;
	gchar *text = NULL;
	gboolean selected;
	GtkTreeIter iter;
	guint i;
	gchar **package_ids;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkWidget *widget;
	PkItemErrorCode *error_item = NULL;
	GtkWindow *window;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to get list of updates: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_item = pk_results_get_error_code (results);
	if (error_item != NULL) {
		egg_warning ("failed to get updates: %s, %s", pk_error_enum_to_text (error_item->code), error_item->details);

		window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (error_item->code),
					gpk_error_enum_to_localised_message (error_item->code), error_item->details);
		goto out;
	}

	/* get data */
	array = pk_results_get_package_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* add to array store */
		text = gpk_package_id_format_twoline (item->package_id, item->summary);
		egg_debug ("adding: id=%s, text=%s", item->package_id, text);
		selected = (item->info != PK_INFO_ENUM_BLOCKED);
		gtk_list_store_append (array_store_updates, &iter);
		gtk_list_store_set (array_store_updates, &iter,
				    GPK_UPDATES_COLUMN_TEXT, text,
				    GPK_UPDATES_COLUMN_ID, item->package_id,
				    GPK_UPDATES_COLUMN_INFO, item->info,
				    GPK_UPDATES_COLUMN_SELECT, selected,
				    GPK_UPDATES_COLUMN_SENSITIVE, selected,
				    GPK_UPDATES_COLUMN_CLICKABLE, selected,
				    GPK_UPDATES_COLUMN_RESTART, PK_RESTART_ENUM_NONE,
				    GPK_UPDATES_COLUMN_STATUS, PK_INFO_ENUM_UNKNOWN,
				    GPK_UPDATES_COLUMN_SIZE, 0,
				    GPK_UPDATES_COLUMN_SIZE_DISPLAY, 0,
				    GPK_UPDATES_COLUMN_PERCENTAGE, 0,
				    GPK_UPDATES_COLUMN_PULSE, -1,
				    -1);
		g_free (text);
	}

	/* get the download sizes */
	if (update_array != NULL)
		g_ptr_array_unref (update_array);
	update_array = pk_results_get_package_array (results);

	/* sort by name */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model), GPK_UPDATES_COLUMN_ID, GTK_SORT_ASCENDING);

	/* get the download sizes */
	if (update_array->len > 0) {
		package_ids = gpk_update_viewer_packages_to_ids (array);

		/* get the details of all the packages */
		pk_client_get_update_detail_async (PK_CLIENT(task), package_ids, cancellable,
						   (PkProgressCallback) gpk_update_viewer_progress_cb, NULL,
						   (GAsyncReadyCallback) gpk_update_viewer_get_update_detail_cb, loop);

		/* get the details of all the packages */
		pk_client_get_details_async (PK_CLIENT(task), package_ids, cancellable,
					     (PkProgressCallback) gpk_update_viewer_progress_cb, NULL,
					     (GAsyncReadyCallback) gpk_update_viewer_get_details_cb, loop);

		g_strfreev (package_ids);
	}

	/* are now able to do action */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_install"));
	gtk_widget_set_sensitive (widget, TRUE);

	/* set info */
	gpk_update_viewer_reconsider_info (model);

out:
	if (error_item != NULL)
		pk_item_error_code_unref (error_item);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_update_viewer_get_new_update_array
 **/
static gboolean
gpk_update_viewer_get_new_update_array (void)
{
	gboolean ret;
	GtkWidget *widget;
	gchar *text = NULL;
	PkBitfield filter = PK_FILTER_ENUM_NONE;

	/* clear all widgets */
	gtk_list_store_clear (array_store_updates);
	gtk_text_buffer_set_text (text_buffer, "", -1);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_header_title"));
	/* TRANSLATORS: this is the header */
	text = g_strdup_printf ("<big><b>%s</b></big>", _("Checking for updates..."));
	gtk_label_set_label (GTK_LABEL (widget), text);

	/* only show newest updates? */
	ret = gconf_client_get_bool (gconf_client, GPK_CONF_UPDATE_VIEWER_ONLY_NEWEST, NULL);
	if (ret) {
		egg_debug ("only showing newest updates");
		filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST, -1);
	}

	/* get new array */
	pk_client_get_updates_async (PK_CLIENT(task), filter, cancellable,
				     (PkProgressCallback) gpk_update_viewer_progress_cb, NULL,
				     (GAsyncReadyCallback) gpk_update_viewer_get_updates_cb, loop);
	g_free (text);
	return ret;
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
	GdkWindow *window;

	gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (text_view), GTK_TEXT_WINDOW_WIDGET, event->x, event->y, &x, &y);
	gpk_update_viewer_textview_set_cursor (GTK_TEXT_VIEW (text_view), x, y);
	window = gtk_widget_get_window (text_view);
	gdk_window_get_pointer (window, NULL, NULL, NULL);
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
	GdkWindow *window;

	window = gtk_widget_get_window (text_view);
	gdk_window_get_pointer (window, &wx, &wy, NULL);
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
	/* now try to get newest update array */
	egg_debug ("updates changed");
	gpk_update_viewer_get_new_update_array ();
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
 * gpk_update_viewer_search_equal_func:
 **/
static gboolean
gpk_update_viewer_search_equal_func (GtkTreeModel *model, gint column, const gchar *key, GtkTreeIter *iter, gpointer search_data)
{
	char *text;
	char *cn_key;
	char *cn_text;
	gboolean result;

	gtk_tree_model_get (model, iter, column, &text, -1);

	cn_key = g_utf8_casefold (key, -1);
	cn_text = g_utf8_casefold (text, -1);

	if (strstr (cn_text, cn_key))
		result = FALSE;
	else
		result = TRUE;

	g_free (text);
	g_free (cn_key);
	g_free (cn_text);

	return result;
}

/**
 * gpk_update_viewer_get_distro_upgrades_best:
 **/
static PkItemDistroUpgrade *
gpk_update_viewer_get_distro_upgrades_best (GPtrArray *array)
{
	PkItemDistroUpgrade *item;
	guint i;

	/* find a stable update */
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (item->state == PK_UPDATE_STATE_ENUM_STABLE)
			goto out;
	}
	item = NULL;
out:
	return item;
}

/**
 * gpk_update_viewer_get_distro_upgrades_cb:
 **/
static void
gpk_update_viewer_get_distro_upgrades_cb (PkClient *client, GAsyncResult *res, GMainLoop *_loop)
{
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array = NULL;
	PkItemDistroUpgrade *item;
	gchar *text = NULL;
	gchar *text_format = NULL;
	GtkWidget *widget;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	PkItemErrorCode *error_item = NULL;
	GtkWindow *window;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to get list of distro upgrades: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_item = pk_results_get_error_code (results);
	if (error_item != NULL) {
		egg_warning ("failed to get list of distro upgrades: %s, %s", pk_error_enum_to_text (error_item->code), error_item->details);

		window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (error_item->code),
					gpk_error_enum_to_localised_message (error_item->code), error_item->details);
		goto out;
	}

	/* get data */
	array = pk_results_get_distro_upgrade_array (results);
	item = gpk_update_viewer_get_distro_upgrades_best (array);
	if (item == NULL)
		goto out;

	/* only display last (newest) distro */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_upgrade"));
	/* TRANSLATORS: new distro available, e.g. F9 to F10 */
	text = g_strdup_printf (_("New distribution upgrade release '%s' is available"), item->summary);
	text_format = g_strdup_printf ("<b>%s</b>", text);
	gtk_label_set_label (GTK_LABEL (widget), text_format);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "viewport_upgrade"));
	gtk_widget_show (widget);

	/* get model */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	gpk_update_viewer_reconsider_info (model);
out:
	g_free (text);
	g_free (text_format);
	if (error_item != NULL)
		pk_item_error_code_unref (error_item);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_update_viewer_set_network_state:
 **/
static void
gpk_update_viewer_set_network_state (PkNetworkEnum state)
{
	GtkWindow *window;
	GtkWidget *dialog;
	gboolean ret = TRUE;
	gchar *text_size = NULL;
	gchar *message = NULL;
	guint size = 0; //TODO: FIXME

	/* not on wireless mobile */
	if (state != PK_NETWORK_ENUM_MOBILE)
		goto out;

	/* not when small */
	if (size < GPK_UPDATE_VIEWER_MOBILE_SMALL_SIZE)
		goto out;

	/* not when ignored */
	ret = gconf_client_get_bool (gconf_client, GPK_CONF_UPDATE_VIEWER_MOBILE_BBAND, NULL);
	if (!ret)
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
	gpk_dialog_embed_do_not_show_widget (GTK_DIALOG (dialog), GPK_CONF_UPDATE_VIEWER_MOBILE_BBAND);
	gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_INSTALLER);
	gtk_dialog_run (GTK_DIALOG(dialog));
	gtk_widget_destroy (dialog);
out:
	g_free (text_size);
	g_free (message);
}

/**
 * gpk_update_viewer_get_properties_cb:
 **/
static void
gpk_update_viewer_get_properties_cb (PkControl *_control, GAsyncResult *res, GMainLoop *_loop)
{
//	GtkWidget *widget;
	GError *error = NULL;
	gboolean ret;
	PkBitfield roles;
	PkNetworkEnum state;

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	if (!ret) {
		/* TRANSLATORS: backend is broken, and won't tell us what it supports */
		g_print ("%s: %s\n", _("Exiting as backend details could not be retrieved"), error->message);
		g_error_free (error);
		g_main_loop_quit (loop);
		goto out;
	}

	/* get values */
	g_object_get (control,
		      "roles", &roles,
		      "network-state", &state,
		      NULL);

	gpk_update_viewer_set_network_state (state);

	/* get the distro-upgrades if we support it */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		pk_client_get_distro_upgrades_async (PK_CLIENT(task), cancellable,
						     (PkProgressCallback) gpk_update_viewer_progress_cb, NULL,
						     (GAsyncReadyCallback) gpk_update_viewer_get_distro_upgrades_cb, loop);
	}
out:
	return;
}

/**
 * gpk_update_viewer_notify_network_state_cb:
 **/
static void
gpk_update_viewer_notify_network_state_cb (PkControl *control_, GParamSpec *pspec, gpointer user_data)
{
	PkNetworkEnum state;

	/* show icon? */
	g_object_get (control,
		      "network-state", &state,
		      NULL);
	gpk_update_viewer_set_network_state (state);
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
	unique_app = unique_app_new ("org.freedesktop.PackageKit.UpdateViewer", NULL);
	if (unique_app_is_running (unique_app)) {
		egg_debug ("You have another instance running. This program will now close");
		unique_app_send_message (unique_app, UNIQUE_ACTIVATE, NULL);
		goto unique_out;
	}

	/* get GConf instance */
	gconf_client = gconf_client_get_default ();
	console = egg_console_kit_new ();

	g_signal_connect (unique_app, "message-received", G_CALLBACK (gpk_update_viewer_message_received_cb), NULL);

	cancellable = g_cancellable_new ();
	markdown = egg_markdown_new ();
	egg_markdown_set_output (markdown, EGG_MARKDOWN_OUTPUT_PANGO);
	egg_markdown_set_escape (markdown, TRUE);
	egg_markdown_set_autocode (markdown, TRUE);

	control = pk_control_new ();
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (gpk_update_viewer_repo_array_changed_cb), NULL);
	g_signal_connect (control, "updates-changed",
			  G_CALLBACK (gpk_update_viewer_updates_changed_cb), NULL);
	g_signal_connect (control, "notify::network-state",
			  G_CALLBACK (gpk_update_viewer_notify_network_state_cb), NULL);

	/* this is what we use mainly */
	task = PK_TASK(gpk_task_new ());
	g_object_set (task,
		      "background", FALSE,
		      NULL);

	/* get properties */
	pk_control_get_properties_async (control, NULL, (GAsyncReadyCallback) gpk_update_viewer_get_properties_cb, loop);

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-update-viewer.ui", &error);
	if (retval == 0) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out_build;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_updates"));
	g_signal_connect (main_window, "delete_event", G_CALLBACK (gpk_update_viewer_button_delete_event_cb), NULL);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SOFTWARE_INSTALLER);

	/* create array stores */
	array_store_updates = gtk_list_store_new (GPK_UPDATES_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT,
						 G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
						 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
						 G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_INT);
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
	gtk_tree_view_set_search_column (GTK_TREE_VIEW (widget), GPK_UPDATES_COLUMN_TEXT);
	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (widget), gpk_update_viewer_search_equal_func, NULL, NULL);
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (array_store_updates));
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
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_quit"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_quit_cb), NULL);
	gtk_window_set_focus (GTK_WINDOW(main_window), widget);

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
	gpk_update_viewer_get_new_update_array ();

	/* wait */
	loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (loop);

	/* we might have visual stuff running, close it down */
	g_cancellable_cancel (cancellable);

	g_main_loop_unref (loop);

	if (update_array != NULL)
		g_ptr_array_unref (update_array);

	g_strfreev (install_package_ids);
	g_object_unref (array_store_updates);
	g_object_unref (text_buffer);
	g_free (package_id_last);
out_build:
	g_object_unref (gconf_client);
	g_object_unref (control);
	g_object_unref (markdown);
	g_object_unref (cancellable);
	g_object_unref (task);
	g_object_unref (console);
	g_object_unref (builder);
unique_out:
	g_object_unref (unique_app);

	return 0;
}

