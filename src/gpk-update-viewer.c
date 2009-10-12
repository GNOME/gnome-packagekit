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
#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gconf/gconf-client.h>
#include <packagekit-glib2/packagekit.h>
#include <canberra-gtk.h>
#include <unique/unique.h>

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
#include "gpk-session.h"
#include "gpk-update-viewer.h"

#define GPK_UPDATE_VIEWER_AUTO_QUIT_TIMEOUT	10 /* seconds */
#define GPK_UPDATE_VIEWER_AUTO_RESTART_TIMEOUT	60 /* seconds */
#define GPK_UPDATE_VIEWER_MOBILE_SMALL_SIZE	512*1024 /* bytes */

#define GPK_UPDATE_VIEWER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_UPDATE_VIEWER, GpkUpdateViewerPrivate))

struct GpkUpdateViewerPrivate
{
	gboolean		 ignore_updates_changed;
	gchar			*package_id_last;
	guint			 auto_shutdown_id;
	guint			 size_total;
	guint			 number_total;
	EggConsoleKit		*console;
	EggMarkdown		*markdown;
	GCancellable		*cancellable;
	GConfClient		*gconf_client;
	GPtrArray		*update_array;
	GtkBuilder		*builder;
	GtkListStore		*array_store_updates;
	GtkTextBuffer		*text_buffer;
	PkControl		*control;
	PkRestartEnum		 restart_update;
	PkTask			*task;
	GtkWidget		*info_bar;
	GtkWidget		*info_bar_label;
};

enum {
	SIGNAL_CLOSE,
	SIGNAL_LAST
};

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

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (GpkUpdateViewer, gpk_update_viewer, G_TYPE_OBJECT)

static gboolean gpk_update_viewer_get_new_update_array (GpkUpdateViewer *update_viewer);

/**
 * gpk_update_viewer_show:
 **/
void
gpk_update_viewer_show (GpkUpdateViewer *update_viewer)
{
	GtkWindow *window;
	window = GTK_WINDOW(gtk_builder_get_object (update_viewer->priv->builder, "dialog_updates"));
	gtk_window_present (window);
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
gpk_update_viewer_quit (GpkUpdateViewer *update_viewer)
{
	/* are we in a transaction */
	g_cancellable_cancel (update_viewer->priv->cancellable);

	egg_debug ("emitting action-close");
	g_signal_emit (update_viewer, signals [SIGNAL_CLOSE], 0);
}

/**
 * gpk_update_viewer_button_quit_cb:
 **/
static void
gpk_update_viewer_button_quit_cb (GtkWidget *widget, GpkUpdateViewer *update_viewer)
{
	gpk_update_viewer_quit (update_viewer);
}

/**
 * gpk_update_viewer_undisable_packages:
 **/
static void
gpk_update_viewer_undisable_packages (GpkUpdateViewer *update_viewer)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean valid;
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

	treeview = GTK_TREE_VIEW(gtk_builder_get_object (priv->builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	/* set all the checkboxes sensitive */
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_list_store_set (priv->array_store_updates, &iter,
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
	GpkUpdateViewer *update_viewer;
	update_viewer = GPK_UPDATE_VIEWER(g_object_get_data (G_OBJECT(dialog), "instance"));
	gtk_dialog_response (dialog, GTK_RESPONSE_CANCEL);
	update_viewer->priv->auto_shutdown_id = 0;
	return FALSE;
}

/**
 * gpk_update_viewer_check_restart:
 **/
static gboolean
gpk_update_viewer_check_restart (GpkUpdateViewer *update_viewer)
{
	GtkWindow *window;
	GtkWidget *dialog;
	gboolean ret = FALSE;
	const gchar *title;
	const gchar *message;
	const gchar *button;
	GtkResponseType response;
	gboolean show_button = TRUE;
	GError *error = NULL;
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

	/* get the text */
	title = gpk_restart_enum_to_localised_text (priv->restart_update);
	if (priv->restart_update == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: the message text for the restart */
		message = _("Some of the updates that were installed require the computer to be restarted before the changes will be applied.");
		/* TRANSLATORS: the button text for the restart */
		button = _("Restart Computer");

	} else if (priv->restart_update == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		/* TRANSLATORS: the message text for the restart */
		message = _("Some of the updates that were installed require the computer to be restarted to remain secure.");
		/* TRANSLATORS: the button text for the restart */
		button = _("Restart Computer");

	} else if (priv->restart_update == PK_RESTART_ENUM_SESSION) {
		/* TRANSLATORS: the message text for the logout */
		message = _("Some of the updates that were installed require you to log out and back in before the changes will be applied.");
		/* TRANSLATORS: the button text for the logout */
		button = _("Log Out");

	} else if (priv->restart_update == PK_RESTART_ENUM_SECURITY_SESSION) {
		/* TRANSLATORS: the message text for the logout */
		message = _("Some of the updates that were installed require you to log out and back in to remain secure.");
		/* TRANSLATORS: the button text for the logout */
		button = _("Log Out");

	} else {
		egg_warning ("unknown restart enum");
		goto out;
	}

	/* show modal dialog */
	window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_updates"));
	dialog = gtk_message_dialog_new (window, GTK_DIALOG_MODAL,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
					 "%s", title);

	/* check to see if restart is possible */
	if (priv->restart_update == PK_RESTART_ENUM_SYSTEM ||
	    priv->restart_update == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		egg_console_kit_can_restart (priv->console, &show_button, NULL);
	}

	/* only show the button if we can do the action */
	if (show_button)
		gtk_dialog_add_button (GTK_DIALOG (dialog), button, GTK_RESPONSE_OK);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog), "%s", message);
	gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_INSTALLER);

	/* setup a callback so we autoclose */
	g_object_set_data_full (G_OBJECT(dialog), "instance", g_object_ref (update_viewer), (GDestroyNotify) g_object_unref);
	priv->auto_shutdown_id = g_timeout_add_seconds (GPK_UPDATE_VIEWER_AUTO_RESTART_TIMEOUT, (GSourceFunc) gpk_update_viewer_auto_shutdown, dialog);

	response = gtk_dialog_run (GTK_DIALOG(dialog));
	gtk_widget_destroy (dialog);

	/* cancel */
	if (response != GTK_RESPONSE_OK)
		goto out;

	/* doing the action, return success */
	ret = TRUE;

	/* do the action */
	if (priv->restart_update == PK_RESTART_ENUM_SYSTEM)
		/* use consolekit to restart */
		ret = egg_console_kit_restart (priv->console, &error);
		if (!ret) {
			egg_warning ("cannot restart: %s", error->message);
			g_error_free (error);
		}
	else if (priv->restart_update == PK_RESTART_ENUM_SESSION) {
		GpkSession *session;
		session = gpk_session_new ();
		/* use gnome-session to log out */
		gpk_session_logout (session);
		g_object_unref (session);
	}
out:
	return ret;
}

/**
 * gpk_update_viewer_check_blocked_packages:
 **/
static void
gpk_update_viewer_check_blocked_packages (GpkUpdateViewer *update_viewer, GPtrArray *array)
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
	window = GTK_WINDOW(gtk_builder_get_object (update_viewer->priv->builder, "dialog_updates"));
	/* TRANSLATORS: we failed to install all the updates we requested */
	gpk_error_dialog_modal (window, _("Some updates were not installed"), text, NULL);
out:
	g_free (text);
}

/**
 * gpk_update_viewer_are_all_updates_selected:
 **/
static gboolean
gpk_update_viewer_are_all_updates_selected (GtkTreeModel *model)
{
	gboolean selected = TRUE;
	gboolean valid;
	GtkTreeIter iter;

	/* if there are no entries selected, deselect the button */
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_SELECT, &selected,
				    -1);
		if (!selected)
			goto out;
		valid = gtk_tree_model_iter_next (model, &iter);
	}
out:
	return selected;
}

/**
 * gpk_update_viewer_update_packages_cb:
 **/
static void
gpk_update_viewer_update_packages_cb (PkTask *task, GAsyncResult *res, GpkUpdateViewer *update_viewer)
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
	gboolean ret;
	const gchar *message;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		egg_warning ("failed to update packages: %s", error->message);
		g_error_free (error);

		/* re-enable the package list */
		gpk_update_viewer_undisable_packages (update_viewer);

		/* allow clicking again */
		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_install"));
		gtk_widget_set_sensitive (widget, TRUE);

		goto out;
	}

	/* check error code */
	error_item = pk_results_get_error_code (results);
	if (error_item != NULL) {
		egg_warning ("failed to update packages: %s, %s", pk_error_enum_to_text (error_item->code), error_item->details);

		/* failed sound, using sounds from the naming spec */
		ca_context_play (ca_gtk_context_get (), 0,
				 CA_PROP_EVENT_ID, "dialog-warning",
				 /* TRANSLATORS: this is the application name for libcanberra */
				 CA_PROP_APPLICATION_NAME, _("GNOME PackageKit Update Viewer"),
				 /* TRANSLATORS: this is the sound description */
				 CA_PROP_EVENT_DESCRIPTION, _("Failed to update"), NULL);

		window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (error_item->code),
					gpk_error_enum_to_localised_message (error_item->code), error_item->details);

		/* re-enable the package list */
		gpk_update_viewer_undisable_packages (update_viewer);

		/* allow clicking again */
		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_install"));
		gtk_widget_set_sensitive (widget, TRUE);

		goto out;
	}

	gpk_update_viewer_undisable_packages (update_viewer);

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
	if (restart > priv->restart_update)
		priv->restart_update = restart;

	/* check blocked */
	array = pk_results_get_package_array (results);
	gpk_update_viewer_check_blocked_packages (update_viewer, array);
	g_ptr_array_unref (array);

	/* check restart */
	if (priv->restart_update == PK_RESTART_ENUM_SYSTEM ||
	    priv->restart_update == PK_RESTART_ENUM_SESSION ||
	    priv->restart_update == PK_RESTART_ENUM_SECURITY_SESSION ||
	    priv->restart_update == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		gpk_update_viewer_check_restart (update_viewer);
		gpk_update_viewer_quit (update_viewer);
		goto out;
	}

	/* hide close button */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_quit"));
	gtk_widget_hide (widget);

	/* show a new title */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "label_header_title"));
	/* TRANSLATORS: completed all updates */
	text = g_strdup_printf ("<big><b>%s</b></big>", _("Updates installed"));
	gtk_label_set_label (GTK_LABEL(widget), text);
	g_free (text);

	/* do different text depending on if we deselected any */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (priv->builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	ret = gpk_update_viewer_are_all_updates_selected (model);
	if (ret) {
		/* TRANSLATORS: title: all updates for the machine installed okay */
		message = _("All updates were installed successfully.");
	} else {
		/* TRANSLATORS: title: all the selected updates installed okay */
		message = _("The selected updates were installed successfully.");
	}

	/* show modal dialog */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "dialog_updates"));
	dialog = gtk_message_dialog_new (GTK_WINDOW(widget), GTK_DIALOG_MODAL,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
					 /* TRANSLATORS: title: all updates installed okay */
					 "%s", _("Updates installed"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog),
						  "%s", message);
	gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_INSTALLER);

	/* setup a callback so we autoclose */
	priv->auto_shutdown_id = g_timeout_add_seconds (GPK_UPDATE_VIEWER_AUTO_RESTART_TIMEOUT, (GSourceFunc) gpk_update_viewer_auto_shutdown, dialog);

	gtk_dialog_run (GTK_DIALOG(dialog));
	gtk_widget_destroy (dialog);

	/* quit after we successfully updated */
	gpk_update_viewer_quit (update_viewer);
out:
	/* no longer updating */
	priv->ignore_updates_changed = FALSE;

	if (error_item != NULL)
		pk_item_error_code_unref (error_item);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}


static GSList *active_rows = NULL;
static guint active_row_timeout_id = 0;

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
			gtk_list_store_set (GTK_LIST_STORE(model), &iter, GPK_UPDATES_COLUMN_PULSE, val + 1, -1);
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
	GSList *link;

	/* check if already active */
	ref = gtk_tree_row_reference_new (model, path);
	link = g_slist_find_custom (active_rows, (gconstpointer)ref, (GCompareFunc)gpk_update_viewer_compare_refs);
	if (link != NULL) {
		egg_debug ("already active");
		gtk_tree_row_reference_free (ref);
		goto out;
	}

	/* add poll */
	if (active_row_timeout_id == 0)
		active_row_timeout_id = g_timeout_add (60, (GSourceFunc)gpk_update_viewer_pulse_active_rows, NULL);

	active_rows = g_slist_prepend (active_rows, ref);
out:
	g_slist_free (link);
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
	gtk_list_store_set (GTK_LIST_STORE(model), &iter, GPK_UPDATES_COLUMN_PULSE, -1, -1);

	ref = gtk_tree_row_reference_new (model, path);
	link = g_slist_find_custom (active_rows, (gconstpointer)ref, (GCompareFunc)gpk_update_viewer_compare_refs);
	gtk_tree_row_reference_free (ref);
	if (link == NULL) {
		egg_warning ("row not already added");
		return;
	}

	active_rows = g_slist_remove_link (active_rows, link);
	gtk_tree_row_reference_free (link->data);
	g_slist_free (link);

	if (active_rows == NULL) {
		g_source_remove (active_row_timeout_id);
		active_row_timeout_id = 0;
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
gpk_update_viewer_progress_cb (PkProgress *progress, PkProgressType type, GpkUpdateViewer *update_viewer)
{
	gboolean allow_cancel;
	PkPackage *package;
	gchar *text;
	gint percentage;
	gint subpercentage;
	GtkWidget *widget;
	PkInfoEnum info;
	PkRoleEnum role;
	PkStatusEnum status;
	gchar *package_id = NULL;
	gchar *summary = NULL;
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

	g_object_get (progress,
		      "role", &role,
		      "status", &status,
		      "percentage", &percentage,
		      "subpercentage", &subpercentage,
		      "package", &package,
		      "allow-cancel", &allow_cancel,
		      NULL);

	if (type == PK_PROGRESS_TYPE_PACKAGE) {

		GtkTreeView *treeview;
		GtkTreeIter iter;
		GtkTreeModel *model;
		GtkTreeViewColumn *column;
		GtkTreePath *path;
		gboolean scroll;

		/* add the results, not the progress */
		if (role == PK_ROLE_ENUM_GET_UPDATES)
			return;

		g_object_get (package,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);

		/* find model */
		treeview = GTK_TREE_VIEW(gtk_builder_get_object (priv->builder, "treeview_updates"));
		model = gtk_tree_view_get_model (treeview);

		/* enable or disable the correct spinners */
		if (role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
			path = gpk_update_viewer_model_get_path (model, package_id);
			if (path != NULL) {
				if (info == PK_INFO_ENUM_FINISHED)
					gpk_update_viewer_remove_active_row (model, path);
				else
					gpk_update_viewer_add_active_row (model, path);
			}
			gtk_tree_path_free (path);
		}

		/* used for progress */
		if (g_strcmp0 (priv->package_id_last, package_id) != 0) {
			g_free (priv->package_id_last);
			priv->package_id_last = g_strdup (package_id);
		}

		/* update icon */
		path = gpk_update_viewer_model_get_path (model, package_id);
		if (path == NULL) {
			text = gpk_package_id_format_twoline (package_id, summary);
			egg_debug ("adding: id=%s, text=%s", package_id, text);
			gtk_list_store_append (priv->array_store_updates, &iter);
			gtk_list_store_set (priv->array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_TEXT, text,
					    GPK_UPDATES_COLUMN_ID, package_id,
					    GPK_UPDATES_COLUMN_INFO, info,
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
			gtk_list_store_set (priv->array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_SELECT, TRUE,
					    -1);
		}

		/* scroll to the active cell */
		scroll = gconf_client_get_bool (priv->gconf_client, GPK_CONF_UPDATE_VIEWER_SCROLL_ACTIVE, NULL);
		if (scroll) {
			column = gtk_tree_view_get_column (treeview, 3);
			gtk_tree_view_scroll_to_cell (treeview, path, column, FALSE, 0.0f, 0.0f);
		}

		/* only change the status when we're doing the actual update */
		if (role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
			/* if the info is finished, change the status to past tense */
			if (info == PK_INFO_ENUM_FINISHED) {
				gtk_tree_model_get (model, &iter,
						    GPK_UPDATES_COLUMN_STATUS, &info, -1);
				/* promote to past tense if present tense */
				if (info < PK_INFO_ENUM_LAST)
					info += PK_INFO_ENUM_LAST;
			}
			gtk_list_store_set (priv->array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_STATUS, info, -1);
		}

		gtk_tree_path_free (path);

	} else if (type == PK_PROGRESS_TYPE_STATUS) {

		GdkWindow *window;
		const gchar *title;
		GdkDisplay *display;
		GdkCursor *cursor;

		egg_debug ("status %s", pk_status_enum_to_text (status));

		/* use correct status pane */
		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "hbox_status"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "hbox_info"));
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
		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "label_status"));
		if (status == PK_STATUS_ENUM_FINISHED) {
			gtk_label_set_label (GTK_LABEL(widget), "");
			widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "image_progress"));
			gtk_widget_hide (widget);

			widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "progressbar_progress"));
			gtk_widget_hide (widget);

			widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_quit"));
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
			gtk_label_set_label (GTK_LABEL(widget), title);
			gtk_widget_show (widget);

			/* set icon */
			widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "image_progress"));
			gtk_image_set_from_icon_name (GTK_IMAGE(widget), gpk_status_enum_to_icon_name (status), GTK_ICON_SIZE_BUTTON);
			gtk_widget_show (widget);
		}

	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {

		GtkTreeView *treeview;
		GtkTreeModel *model;
		GtkTreeIter iter;
		GtkTreePath *path;
		guint size;
		guint size_display;

		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "progressbar_progress"));
		gtk_widget_show (widget);
		if (percentage != -1)
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);

		treeview = GTK_TREE_VIEW(gtk_builder_get_object (priv->builder, "treeview_updates"));
		model = gtk_tree_view_get_model (treeview);

		if (priv->package_id_last == NULL) {
			egg_debug ("no last package");
			return;
		}

		path = gpk_update_viewer_model_get_path (model, priv->package_id_last);
		if (path == NULL) {
			egg_debug ("not found ID for package");
			return;
		}

		gtk_tree_model_get_iter (model, &iter, path);
		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_SIZE, &size,
				    -1);
		if (subpercentage > 0) {
			size_display = size - ((size * subpercentage) / 100);
			gtk_list_store_set (priv->array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_PERCENTAGE, subpercentage,
					    GPK_UPDATES_COLUMN_SIZE_DISPLAY, size_display,
					    -1);
		}
		gtk_tree_path_free (path);

	} else if (type == PK_PROGRESS_TYPE_ALLOW_CANCEL) {
		gboolean idle;
		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_quit"));

		/* we have to also check for idle as we might be getting the AllowCancel(false)
		 * signal _after_ the PkClient has been marked as idle */
		g_object_get (priv->task, "idle", &idle, NULL);
		gtk_widget_set_sensitive (widget, (allow_cancel || idle));
	}
out:
	g_free (summary);
	g_free (package_id);
	if (package != NULL)
		g_object_unref (package);
}

/**
 * gpk_update_viewer_client_notify_idle_cb:
 **/
static void
gpk_update_viewer_client_notify_idle_cb (PkClient *client, GParamSpec *pspec, GpkUpdateViewer *update_viewer)
{
	gboolean idle;
	GtkWidget *widget;

	g_object_get (client,
		      "idle", &idle,
		      NULL);
	/* ensure button is sensitive */
	if (idle) {
		widget = GTK_WIDGET(gtk_builder_get_object (update_viewer->priv->builder, "button_quit"));
		gtk_widget_set_sensitive (widget, TRUE);
	}
}

/**
 * gpk_update_viewer_button_install_cb:
 **/
static void
gpk_update_viewer_button_install_cb (GtkWidget *widget, GpkUpdateViewer *update_viewer)
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
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

	/* hide the upgrade viewbox from now on */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "viewport_upgrade"));
	gtk_widget_hide (widget);

	egg_debug ("Doing the package updates");
	array = g_ptr_array_new ();

	treeview = GTK_TREE_VIEW(gtk_builder_get_object (priv->builder, "treeview_updates"));
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
		gtk_list_store_set (priv->array_store_updates, &iter,
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
		window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_updates"));
		gpk_error_dialog_modal (window,
					/* TRANSLATORS: we clicked apply, but had no packages selected */
					_("No updates selected"),
					_("No updates are selected"), NULL);
		return;
	}

	/* disable button */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_install"));
	gtk_widget_set_sensitive (widget, FALSE);

	/* clear the selection */
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_unselect_all (selection);

	/* save for finished */
	package_ids = pk_ptr_array_to_strv (array);

	/* get packages that also have to be updated */
	pk_task_update_packages_async (priv->task, package_ids, priv->cancellable,
				       (PkProgressCallback) gpk_update_viewer_progress_cb, update_viewer,
				       (GAsyncReadyCallback) gpk_update_viewer_update_packages_cb, update_viewer);
	g_strfreev (package_ids);

	/* from now on ignore updates-changed signals */
	priv->ignore_updates_changed = TRUE;

	/* get rid of the array, and free the contents */
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * gpk_update_viewer_button_upgrade_cb:
 **/
static void
gpk_update_viewer_button_upgrade_cb (GtkWidget *widget, GpkUpdateViewer *update_viewer)
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
gpk_update_viewer_button_delete_event_cb (GtkWidget *widget, GdkEvent *event, GpkUpdateViewer *update_viewer)
{
	gpk_update_viewer_quit (update_viewer);
	return TRUE;
}


/**
 * gpk_update_viewer_check_mobile_broadband:
 **/
static void
gpk_update_viewer_check_mobile_broadband (GpkUpdateViewer *update_viewer)
{
	gboolean ret = TRUE;
	PkNetworkEnum state;
	const gchar *message;
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

	/* get network state */
	g_object_get (priv->control,
		      "network-state", &state,
		      NULL);

	/* hide by default */
	gtk_widget_hide (priv->info_bar);

	/* not on wireless mobile */
	if (state != PK_NETWORK_ENUM_MOBILE)
		goto out;

	/* not when small */
	if (priv->size_total < GPK_UPDATE_VIEWER_MOBILE_SMALL_SIZE)
		goto out;

	/* not when ignored */
	ret = gconf_client_get_bool (priv->gconf_client, GPK_CONF_UPDATE_VIEWER_MOBILE_BBAND, NULL);
	if (!ret)
		goto out;

	/* show a warning message */

	/* TRANSLATORS, are we going to cost the user lots of money? */
	message = ngettext ("Connectivity is being provided by wireless broadband, and it may be expensive to update this package.",
			    "Connectivity is being provided by wireless broadband, and it may be expensive to update these packages.",
			    priv->number_total);
	gtk_label_set_label (GTK_LABEL(priv->info_bar_label), message);

	gtk_info_bar_set_message_type (GTK_INFO_BAR(priv->info_bar), GTK_MESSAGE_WARNING);
	gtk_widget_show (priv->info_bar);
out:
	return;
}

/**
 * gpk_update_viewer_reconsider_info:
 **/
static void
gpk_update_viewer_reconsider_info (GpkUpdateViewer *update_viewer)
{
	GtkTreeIter iter;
	GtkWidget *widget;
	GtkWidget *dialog;
	gboolean valid;
	gboolean selected;
	gboolean any_selected = FALSE;
	guint len;
	guint size;
	PkRestartEnum restart;
	PkRestartEnum restart_worst = PK_RESTART_ENUM_NONE;
	const gchar *title;
	gchar *text;
	gchar *text_size;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

	treeview = GTK_TREE_VIEW(gtk_builder_get_object (priv->builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	/* reset to zero */
	priv->size_total = 0;
	priv->number_total = 0;

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
			priv->size_total += size;
			priv->number_total++;
			if (restart > restart_worst)
				restart_worst = restart;
		}
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* action button */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_install"));
	gtk_widget_set_sensitive (widget, any_selected);

	/* sensitive */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "scrolledwindow_updates"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "scrolledwindow_details"));
	gtk_widget_set_sensitive (widget, TRUE);

	/* set the pluralisation of the button */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_install"));
	/* TRANSLATORS: this is the button text when we have updates */
	title = ngettext ("_Install Update", "_Install Updates", priv->number_total);
	gtk_button_set_label (GTK_BUTTON (widget), title);

	/* no updates */
	len = priv->update_array->len;
	if (len == 0) {
		/* hide close button */
		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_quit"));
		gtk_widget_hide (widget);

		/* show a new title */
		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "label_header_title"));
		/* TRANSLATORS: there are no updates */
		text = g_strdup_printf ("<big><b>%s</b></big>", _("There are no updates available"));
		gtk_label_set_label (GTK_LABEL(widget), text);
		g_free (text);

		/* show modal dialog */
		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "dialog_updates"));
		dialog = gtk_message_dialog_new (GTK_WINDOW(widget), GTK_DIALOG_MODAL,
						 GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
						 /* TRANSLATORS: title: warn the user they are quitting with unapplied changes */
						 "%s", _("All software is up to date"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog),
							  "%s",
							  /* TRANSLATORS: tell the user the problem */
							  _("There are no software updates available for your computer at this time."));
		gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_INSTALLER);

		/* setup a callback so we autoclose */
		priv->auto_shutdown_id = g_timeout_add_seconds (GPK_UPDATE_VIEWER_AUTO_RESTART_TIMEOUT, (GSourceFunc) gpk_update_viewer_auto_shutdown, dialog);

		gtk_dialog_run (GTK_DIALOG(dialog));
		gtk_widget_destroy (dialog);

		/* exit the program */
		gpk_update_viewer_quit (update_viewer);
		goto out;
	}

	/* use correct status pane */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "hbox_status"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "hbox_info"));
	gtk_widget_show (widget);

	/* restart */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "label_info"));
	if (restart_worst == PK_RESTART_ENUM_NONE) {
		gtk_widget_hide (widget);
		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "image_info"));
		gtk_widget_hide (widget);
	} else {
		gtk_label_set_label (GTK_LABEL(widget), gpk_restart_enum_to_localised_text_future (restart_worst));
		gtk_widget_show (widget);
		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "image_info"));
		gtk_image_set_from_icon_name (GTK_IMAGE(widget), gpk_restart_enum_to_icon_name (restart_worst), GTK_ICON_SIZE_BUTTON);
		gtk_widget_show (widget);
	}

	/* header */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "label_header_title"));
	text = g_strdup_printf (ngettext ("There is %i update available",
					  "There are %i updates available", len), len);
	text_size = g_strdup_printf ("<big><b>%s</b></big>", text);
	gtk_label_set_label (GTK_LABEL(widget), text_size);
	g_free (text);
	g_free (text_size);
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "hbox_header"));
	gtk_widget_show (widget);

	/* total */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "label_summary"));
	if (priv->number_total == 0) {
		gtk_label_set_label (GTK_LABEL(widget), "");
	} else {
		if (priv->size_total == 0) {
			/* TRANSLATORS: how many updates are selected in the UI */
			text = g_strdup_printf (ngettext ("%i update selected",
							  "%i updates selected",
							  priv->number_total), priv->number_total);
			gtk_label_set_label (GTK_LABEL(widget), text);
			g_free (text);
		} else {
			text_size = g_format_size_for_display (priv->size_total);
			/* TRANSLATORS: how many updates are selected in the UI, and the size of packages to download */
			text = g_strdup_printf (ngettext ("%i update selected (%s)",
							  "%i updates selected (%s)",
							  priv->number_total), priv->number_total, text_size);
			gtk_label_set_label (GTK_LABEL(widget), text);
			g_free (text);
			g_free (text_size);
		}
	}

	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "label_summary"));
	gtk_widget_show (widget);
out:
	gpk_update_viewer_check_mobile_broadband (update_viewer);
}

/**
 * gpk_update_viewer_treeview_update_toggled:
 **/
static void
gpk_update_viewer_treeview_update_toggled (GtkCellRendererToggle *cell, gchar *path_str, GpkUpdateViewer *update_viewer)
{
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	gboolean update;
	gchar *package_id;
	GtkTreeView *treeview;
	GtkTreeModel *model;

	treeview = GTK_TREE_VIEW(gtk_builder_get_object (update_viewer->priv->builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_SELECT, &update,
			    GPK_UPDATES_COLUMN_ID, &package_id, -1);

	/* unstage */
	update ^= 1;

	egg_debug ("update %s[%i]", package_id, update);
	g_free (package_id);

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE(model), &iter, GPK_UPDATES_COLUMN_SELECT, update, -1);

	/* clean up */
	gtk_tree_path_free (path);

	/* if there are no entries selected, deselect the button */
	gpk_update_viewer_reconsider_info (update_viewer);
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
gpk_update_viewer_treeview_query_tooltip_cb (GtkWidget *widget, gint x, gint y, gboolean keyboard, GtkTooltip *tooltip, GpkUpdateViewer *update_viewer)
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
	model = gtk_tree_view_get_model (GTK_TREE_VIEW(widget));
	gtk_tree_view_convert_widget_to_bin_window_coords (GTK_TREE_VIEW(widget), x, y, &bin_x, &bin_y);
	ret = gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW(widget), bin_x, bin_y, &path, &column, &cell_x, &cell_y);

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
		gtk_tree_view_set_tooltip_cell (GTK_TREE_VIEW(widget), tooltip, path, column, NULL);
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
gpk_update_viewer_treeview_add_columns_update (GpkUpdateViewer *update_viewer, GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeModel *model;

	/* restart */
	renderer = gpk_cell_renderer_restart_new ();
	g_object_set (renderer,
		      "stock-size", GTK_ICON_SIZE_BUTTON,
		      NULL);
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
		      "ignore-values", "unknown",
		      NULL);
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_add_attribute (column, renderer, "value", GPK_UPDATES_COLUMN_INFO);

	/* select toggle */
	renderer = gtk_cell_renderer_toggle_new ();
	model = gtk_tree_view_get_model (treeview);
	g_signal_connect (renderer, "toggled", G_CALLBACK (gpk_update_viewer_treeview_update_toggled), update_viewer);
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
		      "ignore-values", "unknown",
		      NULL);
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
gpk_update_viewer_populate_details (GpkUpdateViewer *update_viewer, const PkItemUpdateDetail *item)
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
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

	/* get info  */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (priv->builder, "treeview_updates"));
	selection = gtk_tree_view_get_selection (treeview);
	if (gtk_tree_selection_get_selected (selection, &model, &treeiter))
		gtk_tree_model_get (model, &treeiter,
				    GPK_UPDATES_COLUMN_INFO, &info, -1);
	else
		info = PK_INFO_ENUM_NORMAL;

	/* blank */
	gtk_text_buffer_set_text (priv->text_buffer, "", -1);
	gtk_text_buffer_get_start_iter (priv->text_buffer, &iter);

	if (info == PK_INFO_ENUM_ENHANCEMENT) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gtk_text_buffer_insert_with_tags_by_name (priv->text_buffer, &iter, ("This update will add new features and expand functionality."), -1, "para", NULL);
		gtk_text_buffer_insert (priv->text_buffer, &iter, "\n", -1);
	} else if (info == PK_INFO_ENUM_BUGFIX) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gtk_text_buffer_insert_with_tags_by_name (priv->text_buffer, &iter, _("This update will fix bugs and other non-critical problems."), -1, "para", NULL);
		gtk_text_buffer_insert (priv->text_buffer, &iter, "\n", -1);
	} else if (info == PK_INFO_ENUM_IMPORTANT) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gtk_text_buffer_insert_with_tags_by_name (priv->text_buffer, &iter, _("This update is important as it may solve critical problems."), -1, "para", "important", NULL);
		gtk_text_buffer_insert (priv->text_buffer, &iter, "\n", -1);
	} else if (info == PK_INFO_ENUM_SECURITY) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gtk_text_buffer_insert_with_tags_by_name (priv->text_buffer, &iter, _("This update is needed to fix a security vulnerability with this package."), -1, "para", "important", NULL);
		gtk_text_buffer_insert (priv->text_buffer, &iter, "\n", -1);
	} else if (info == PK_INFO_ENUM_BLOCKED) {
		/* TRANSLATORS: this is the update type, e.g. security */
		gtk_text_buffer_insert_with_tags_by_name (priv->text_buffer, &iter, _("This update is blocked."), -1, "para", NULL);
		gtk_text_buffer_insert (priv->text_buffer, &iter, "\n", -1);
	}

	/* issued and updated */
	if (item->issued != NULL && item->updated != NULL) {
		issued = pk_iso8601_from_date (item->issued);
		updated = pk_iso8601_from_date (item->updated);
		/* TRANSLATORS: this is when the notification was issued and then updated*/
		line = g_strdup_printf (_("This notification was issued on %s and last updated on %s."), issued, updated);
		gtk_text_buffer_insert_with_tags_by_name (priv->text_buffer, &iter, line, -1, "para", NULL);
		gtk_text_buffer_insert (priv->text_buffer, &iter, "\n", -1);
		g_free (issued);
		g_free (updated);
		g_free (line);
	} else if (item->issued != NULL) {
		issued = pk_iso8601_from_date (item->issued);
		/* TRANSLATORS: this is when the update was issued */
		line = g_strdup_printf (_("This notification was issued on %s."), issued);
		gtk_text_buffer_insert_with_tags_by_name (priv->text_buffer, &iter, line, -1, "para", NULL);
		gtk_text_buffer_insert (priv->text_buffer, &iter, "\n", -1);
		g_free (issued);
		g_free (line);
	}

	/* update text */
	if (!egg_strzero (item->update_text)) {
		/* convert the bullets */
		line = egg_markdown_parse (priv->markdown, item->update_text);
		if (!egg_strzero (line)) {
			gtk_text_buffer_insert_markup (priv->text_buffer, &iter, line);
			gtk_text_buffer_insert (priv->text_buffer, &iter, "\n\n", -1);
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
		gpk_update_viewer_add_description_link_item (priv->text_buffer, &iter, title, array);
		g_ptr_array_unref (array);
	}
	if (!egg_strzero (item->bugzilla_url)) {
		array = gpk_update_viewer_get_uris (item->bugzilla_url);
		/* TRANSLATORS: this is a array of bugzilla URLs */
		title = ngettext ("For more information about bugs fixed by this update please visit this website:",
				  "For more information about bugs fixed by this update please visit these websites:", array->len);
		gpk_update_viewer_add_description_link_item (priv->text_buffer, &iter, title, array);
		g_ptr_array_unref (array);
	}
	if (!egg_strzero (item->cve_url)) {
		array = gpk_update_viewer_get_uris (item->cve_url);
		/* TRANSLATORS: this is a array of CVE (security) URLs */
		title = ngettext ("For more information about this security update please visit this website:",
				  "For more information about this security update please visit these websites:", array->len);
		gpk_update_viewer_add_description_link_item (priv->text_buffer, &iter, title, array);
		g_ptr_array_unref (array);
	}

	/* reboot */
	if (item->restart == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: reboot required */
		gtk_text_buffer_insert_with_tags_by_name (priv->text_buffer, &iter, _("The computer will have to be restarted after the update for the changes to take effect."), -1, "para", NULL);
		gtk_text_buffer_insert (priv->text_buffer, &iter, "\n", -1);
	} else if (item->restart == PK_RESTART_ENUM_SESSION) {
		/* TRANSLATORS: log out required */
		gtk_text_buffer_insert_with_tags_by_name (priv->text_buffer, &iter, _("You will need to log out and back in after the update for the changes to take effect."), -1, "para", NULL);
		gtk_text_buffer_insert (priv->text_buffer, &iter, "\n", -1);
	}

	/* state */
	if (item->state == PK_UPDATE_STATE_ENUM_UNSTABLE) {
		/* TRANSLATORS: this is the stability status of the update */
		gtk_text_buffer_insert_with_tags_by_name (priv->text_buffer, &iter, _("The classifaction of this update is unstable which means it is not designed for production use."), -1, "para", NULL);
		gtk_text_buffer_insert (priv->text_buffer, &iter, "\n", -1);
	} else if (item->state == PK_UPDATE_STATE_ENUM_TESTING) {
		/* TRANSLATORS: this is the stability status of the update */
		gtk_text_buffer_insert_with_tags_by_name (priv->text_buffer, &iter, _("This is a test update, and is not designed for normal use. Please report any problems or regressions you encounter."), -1, "para", NULL);
		gtk_text_buffer_insert (priv->text_buffer, &iter, "\n", -1);
	}

	/* only show changelog if we didn't have any update text */
	if (!update_text && !egg_strzero (item->changelog)) {
		line = egg_markdown_parse (priv->markdown, item->changelog);
		if (!egg_strzero (line)) {
			/* TRANSLATORS: this is a ChangeLog */
			line2 = g_strdup_printf ("%s\n%s\n", _("The developer logs will be shown as no description is available for this update:"), line);
			gtk_text_buffer_insert_markup (priv->text_buffer, &iter, line2);
			g_free (line2);
		}
		g_free (line);
	}
}

/**
 * gpk_packages_treeview_clicked_cb:
 **/
static void
gpk_packages_treeview_clicked_cb (GtkTreeSelection *selection, GpkUpdateViewer *update_viewer)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *package_id;
	PkItemUpdateDetail *item = NULL;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {

		/* set loading text */
		gtk_text_buffer_set_text (update_viewer->priv->text_buffer, _("Loading..."), -1);

		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_UPDATE_DETAIL_OBJ, &item,
				    GPK_UPDATES_COLUMN_ID, &package_id, -1);
		egg_debug ("selected row is: %s, %p", package_id, item);
		g_free (package_id);
		if (item != NULL)
			gpk_update_viewer_populate_details (update_viewer, item);
	} else {
		egg_debug ("no row selected");
	}
}

/**
 * gpk_update_viewer_get_details_cb:
 **/
static void
gpk_update_viewer_get_details_cb (PkClient *client, GAsyncResult *res, GpkUpdateViewer *update_viewer)
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
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

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

		window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (error_item->code),
					gpk_error_enum_to_localised_message (error_item->code), error_item->details);
		goto out;
	}

	treeview = GTK_TREE_VIEW(gtk_builder_get_object (priv->builder, "treeview_updates"));
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
		gtk_list_store_set (priv->array_store_updates, &iter,
				    GPK_UPDATES_COLUMN_DETAILS_OBJ, (gpointer) pk_item_details_ref (item),
				    GPK_UPDATES_COLUMN_SIZE, (gint)item->size,
				    GPK_UPDATES_COLUMN_SIZE_DISPLAY, (gint)item->size,
				    -1);
		/* in cache */
		if (item->size == 0)
			gtk_list_store_set (priv->array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_STATUS, GPK_INFO_ENUM_DOWNLOADED, -1);
	}

	/* select the first entry in the updates array now we've got data */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "treeview_updates"));
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(widget));
	gtk_tree_selection_unselect_all (selection);
	path = gtk_tree_path_new_first ();
	gtk_tree_selection_select_path (selection, path);
	gtk_tree_path_free (path);

	/* set info */
	gpk_update_viewer_reconsider_info (update_viewer);
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
gpk_update_viewer_get_update_detail_cb (PkClient *client, GAsyncResult *res, GpkUpdateViewer *update_viewer)
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
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

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

		window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (error_item->code),
					gpk_error_enum_to_localised_message (error_item->code), error_item->details);
		goto out;
	}

	/* get data */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (priv->builder, "treeview_updates"));
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
		gtk_list_store_set (priv->array_store_updates, &iter,
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
gpk_update_viewer_repo_array_changed_cb (PkClient *client, GpkUpdateViewer *update_viewer)
{
	gpk_update_viewer_get_new_update_array (update_viewer);
}

/**
 * gpk_update_viewer_detail_popup_menu_select_all:
 **/
static void
gpk_update_viewer_detail_popup_menu_select_all (GtkWidget *menuitem, GpkUpdateViewer *update_viewer)
{
	GtkTreeView *treeview;
	gboolean valid;
	GtkTreeIter iter;
	GtkTreeModel *model;
	PkInfoEnum info;

	/* get the first iter in the array */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (update_viewer->priv->builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_INFO, &info, -1);
		if (info != PK_INFO_ENUM_BLOCKED)
			gtk_list_store_set (GTK_LIST_STORE(model), &iter,
					    GPK_UPDATES_COLUMN_SELECT, TRUE, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* if there are no entries selected, deselect the button */
	gpk_update_viewer_reconsider_info (update_viewer);
}

/**
 * gpk_update_viewer_detail_popup_menu_select_security:
 **/
static void
gpk_update_viewer_detail_popup_menu_select_security (GtkWidget *menuitem, GpkUpdateViewer *update_viewer)
{
	GtkTreeView *treeview;
	gboolean valid;
	gboolean ret;
	GtkTreeIter iter;
	GtkTreeModel *model;
	PkInfoEnum info;

	/* get the first iter in the array */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (update_viewer->priv->builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_INFO, &info, -1);
		ret = (info == PK_INFO_ENUM_SECURITY);
		gtk_list_store_set (GTK_LIST_STORE(model), &iter,
				    GPK_UPDATES_COLUMN_SELECT, ret, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* if there are no entries selected, deselect the button */
	gpk_update_viewer_reconsider_info (update_viewer);
}

/**
 * gpk_update_viewer_detail_popup_menu_select_none:
 **/
static void
gpk_update_viewer_detail_popup_menu_select_none (GtkWidget *menuitem, GpkUpdateViewer *update_viewer)
{
	GtkTreeView *treeview;
	gboolean valid;
	GtkTreeIter iter;
	GtkTreeModel *model;

	/* get the first iter in the array */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (update_viewer->priv->builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter, -1);
		gtk_list_store_set (GTK_LIST_STORE(model), &iter,
				    GPK_UPDATES_COLUMN_SELECT, FALSE, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* if there are no entries selected, deselect the button */
	gpk_update_viewer_reconsider_info (update_viewer);
}

/**
 * gpk_update_viewer_get_checked_status:
 **/
static gboolean
gpk_update_viewer_get_checked_status (GpkUpdateViewer *update_viewer, gboolean *all_checked, gboolean *none_checked)
{
	GtkTreeView *treeview;
	gboolean valid;
	gboolean update;
	gboolean clickable = FALSE;
	GtkTreeIter iter;
	GtkTreeModel *model;

	/* get the first iter in the array */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (update_viewer->priv->builder, "treeview_updates"));
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
gpk_update_viewer_detail_popup_menu_create (GtkWidget *treeview, GdkEventButton *event, GpkUpdateViewer *update_viewer)
{
	GtkWidget *menu;
	GtkWidget *menuitem;
	gboolean all_checked;
	gboolean none_checked;
	gboolean ret;

	menu = gtk_menu_new();

	/* we don't want to show 'Select all' if they are all checked */
	ret = gpk_update_viewer_get_checked_status (update_viewer, &all_checked, &none_checked);
	if (!ret) {
		egg_debug ("ignoring as we are locked down");
		return;
	}

	if (!all_checked) {
		/* TRANSLATORS: right click menu, select all the updates */
		menuitem = gtk_menu_item_new_with_label (_("Select all"));
		g_signal_connect (menuitem, "activate",
				  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_all), update_viewer);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	}

	if (!none_checked) {
		/* TRANSLATORS: right click menu, unselect all the updates */
		menuitem = gtk_menu_item_new_with_label (_("Unselect all"));
		g_signal_connect (menuitem, "activate",
				  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_none), update_viewer);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	}

	/* TRANSLATORS: right click menu, select only security updates */
	menuitem = gtk_menu_item_new_with_label (_("Select security updates"));
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_security), update_viewer);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);

	/* TRANSLATORS: right click option, ignore this update name, not currently used */
	menuitem = gtk_menu_item_new_with_label (_("Ignore this update"));
	gtk_widget_set_sensitive (GTK_WIDGET(menuitem), FALSE);
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_all), update_viewer);
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
gpk_update_viewer_detail_button_pressed (GtkWidget *treeview, GdkEventButton *event, GpkUpdateViewer *update_viewer)
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
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(treeview));
	if (gtk_tree_selection_count_selected_rows (selection) <= 1) {
		/* Get tree path for row that was clicked */
		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW(treeview),
						   (gint) event->x, (gint) event->y, &path,
						   NULL, NULL, NULL)) {
			gtk_tree_selection_unselect_all (selection);
			gtk_tree_selection_select_path (selection, path);
			gtk_tree_path_free (path);
		}
	}

	/* create */
	gpk_update_viewer_detail_popup_menu_create (treeview, event, update_viewer);
	return TRUE;
}

/**
 * gpk_update_viewer_detail_popup_menu:
 **/
static gboolean
gpk_update_viewer_detail_popup_menu (GtkWidget *treeview, GpkUpdateViewer *update_viewer)
{
	gpk_update_viewer_detail_popup_menu_create (treeview, NULL, update_viewer);
	return TRUE;
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
gpk_update_viewer_get_updates_cb (PkClient *client, GAsyncResult *res, GpkUpdateViewer *update_viewer)
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
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

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

		window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_updates"));
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
		gtk_list_store_append (priv->array_store_updates, &iter);
		gtk_list_store_set (priv->array_store_updates, &iter,
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
	if (priv->update_array != NULL)
		g_ptr_array_unref (priv->update_array);
	priv->update_array = pk_results_get_package_array (results);

	/* sort by name */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (priv->builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model), GPK_UPDATES_COLUMN_ID, GTK_SORT_ASCENDING);

	/* get the download sizes */
	if (priv->update_array->len > 0) {
		package_ids = gpk_update_viewer_packages_to_ids (array);

		/* get the details of all the packages */
		pk_client_get_update_detail_async (PK_CLIENT(priv->task), package_ids, priv->cancellable,
						   (PkProgressCallback) gpk_update_viewer_progress_cb, update_viewer,
						   (GAsyncReadyCallback) gpk_update_viewer_get_update_detail_cb, update_viewer);

		/* get the details of all the packages */
		pk_client_get_details_async (PK_CLIENT(priv->task), package_ids, priv->cancellable,
					     (PkProgressCallback) gpk_update_viewer_progress_cb, update_viewer,
					     (GAsyncReadyCallback) gpk_update_viewer_get_details_cb, update_viewer);

		g_strfreev (package_ids);
	}

	/* are now able to do action */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_install"));
	gtk_widget_set_sensitive (widget, TRUE);

	/* set info */
	gpk_update_viewer_reconsider_info (update_viewer);

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
gpk_update_viewer_get_new_update_array (GpkUpdateViewer *update_viewer)
{
	gboolean ret;
	GtkWidget *widget;
	gchar *text = NULL;
	PkBitfield filter = PK_FILTER_ENUM_NONE;
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

	/* clear all widgets */
	gtk_list_store_clear (priv->array_store_updates);
	gtk_text_buffer_set_text (priv->text_buffer, "", -1);

	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "label_header_title"));
	/* TRANSLATORS: this is the header */
	text = g_strdup_printf ("<big><b>%s</b></big>", _("Checking for updates..."));
	gtk_label_set_label (GTK_LABEL(widget), text);

	/* only show newest updates? */
	ret = gconf_client_get_bool (priv->gconf_client, GPK_CONF_UPDATE_VIEWER_ONLY_NEWEST, NULL);
	if (ret) {
		egg_debug ("only showing newest updates");
		filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST, -1);
	}

	/* get new array */
	pk_client_get_updates_async (PK_CLIENT(priv->task), filter, priv->cancellable,
				     (PkProgressCallback) gpk_update_viewer_progress_cb, update_viewer,
				     (GAsyncReadyCallback) gpk_update_viewer_get_updates_cb, update_viewer);
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
gpk_update_viewer_updates_changed_cb (PkControl *control, GpkUpdateViewer *update_viewer)
{
	/* now try to get newest update array */
	egg_debug ("updates changed");
	if (update_viewer->priv->ignore_updates_changed) {
		egg_debug ("ignoring");
		return;
	}
	gpk_update_viewer_get_new_update_array (update_viewer);
}

/**
 * gpk_update_viewer_vpaned_realized_cb:
 **/
static void
gpk_update_viewer_vpaned_realized_cb (GtkWidget *widget, GpkUpdateViewer *update_viewer)
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
gpk_update_viewer_get_distro_upgrades_cb (PkClient *client, GAsyncResult *res, GpkUpdateViewer *update_viewer)
{
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array = NULL;
	PkItemDistroUpgrade *item;
	gchar *text = NULL;
	gchar *text_format = NULL;
	GtkWidget *widget;
	PkItemErrorCode *error_item = NULL;
	GtkWindow *window;
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

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

		window = GTK_WINDOW(gtk_builder_get_object (priv->builder, "dialog_updates"));
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
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "label_upgrade"));
	/* TRANSLATORS: new distro available, e.g. F9 to F10 */
	text = g_strdup_printf (_("New distribution upgrade release '%s' is available"), item->summary);
	text_format = g_strdup_printf ("<b>%s</b>", text);
	gtk_label_set_label (GTK_LABEL(widget), text_format);

	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "viewport_upgrade"));
	gtk_widget_show (widget);

	/* get model */
	gpk_update_viewer_reconsider_info (update_viewer);
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
 * gpk_update_viewer_get_properties_cb:
 **/
static void
gpk_update_viewer_get_properties_cb (PkControl *control, GAsyncResult *res, GpkUpdateViewer *update_viewer)
{
	GError *error = NULL;
	gboolean ret;
	PkBitfield roles;
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	if (!ret) {
		/* TRANSLATORS: backend is broken, and won't tell us what it supports */
		g_print ("%s: %s\n", _("Exiting as backend details could not be retrieved"), error->message);
		g_error_free (error);
		gpk_update_viewer_quit (update_viewer);
		goto out;
	}

	/* get values */
	g_object_get (control,
		      "roles", &roles,
		      NULL);

	/* get the distro-upgrades if we support it */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		pk_client_get_distro_upgrades_async (PK_CLIENT(priv->task), priv->cancellable,
						     (PkProgressCallback) gpk_update_viewer_progress_cb, update_viewer,
						     (GAsyncReadyCallback) gpk_update_viewer_get_distro_upgrades_cb, update_viewer);
	}
out:
	return;
}

/**
 * gpk_update_viewer_notify_network_state_cb:
 **/
static void
gpk_update_viewer_notify_network_state_cb (PkControl *control, GParamSpec *pspec, GpkUpdateViewer *update_viewer)
{
	gpk_update_viewer_check_mobile_broadband (update_viewer);
}

/**
 * gpk_update_viewer_init:
 **/
static void
gpk_update_viewer_init (GpkUpdateViewer *update_viewer)
{
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkTreeSelection *selection;
	gboolean ret;
	guint retval;
	GError *error = NULL;
	GpkUpdateViewerPrivate *priv;

	update_viewer->priv = GPK_UPDATE_VIEWER_GET_PRIVATE(update_viewer);
	priv = update_viewer->priv;

	priv->auto_shutdown_id = 0;
	priv->size_total = 0;
	priv->ignore_updates_changed = FALSE;
	priv->restart_update = PK_RESTART_ENUM_NONE;

	priv->gconf_client = gconf_client_get_default ();
	priv->console = egg_console_kit_new ();
	priv->cancellable = g_cancellable_new ();
	priv->markdown = egg_markdown_new ();
	egg_markdown_set_output (priv->markdown, EGG_MARKDOWN_OUTPUT_PANGO);
	egg_markdown_set_escape (priv->markdown, TRUE);
	egg_markdown_set_autocode (priv->markdown, TRUE);

	priv->control = pk_control_new ();
	g_signal_connect (priv->control, "repo-list-changed",
			  G_CALLBACK (gpk_update_viewer_repo_array_changed_cb), update_viewer);
	g_signal_connect (priv->control, "updates-changed",
			  G_CALLBACK (gpk_update_viewer_updates_changed_cb), update_viewer);
	g_signal_connect (priv->control, "notify::network-state",
			  G_CALLBACK (gpk_update_viewer_notify_network_state_cb), update_viewer);

	/* this is what we use mainly */
	priv->task = PK_TASK(gpk_task_new ());
	g_signal_connect (priv->task, "notify::idle",
			  G_CALLBACK (gpk_update_viewer_client_notify_idle_cb), update_viewer);
	g_object_set (priv->task,
		      "background", FALSE,
		      NULL);

	/* get properties */
	pk_control_get_properties_async (priv->control, NULL, (GAsyncReadyCallback) gpk_update_viewer_get_properties_cb, update_viewer);

	/* get UI */
	priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (priv->builder, GPK_DATA "/gpk-update-viewer.ui", &error);
	if (retval == 0) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out;
	}

	main_window = GTK_WIDGET(gtk_builder_get_object (priv->builder, "dialog_updates"));
	g_signal_connect (main_window, "delete_event", G_CALLBACK (gpk_update_viewer_button_delete_event_cb), update_viewer);
	gtk_window_set_icon_name (GTK_WINDOW(main_window), GPK_ICON_SOFTWARE_INSTALLER);

	/* create array stores */
	priv->array_store_updates = gtk_list_store_new (GPK_UPDATES_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT,
						 G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
						 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
						 G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_INT);
	priv->text_buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_create_tag (priv->text_buffer, "para",
				    "pixels_above_lines", 5,
				    "wrap-mode", GTK_WRAP_WORD,
				    NULL);
	gtk_text_buffer_create_tag (priv->text_buffer, "important",
				    "weight", PANGO_WEIGHT_BOLD,
				    NULL);

	/* no upgrades yet */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "viewport_upgrade"));
	gtk_widget_hide (widget);

	/* description */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "textview_details"));
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), priv->text_buffer);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (widget), FALSE);
	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (widget), 5);
	g_signal_connect (GTK_TEXT_VIEW (widget), "key-press-event", G_CALLBACK (gpk_update_viewer_textview_key_press_event), update_viewer);
	g_signal_connect (GTK_TEXT_VIEW (widget), "event-after", G_CALLBACK (gpk_update_viewer_textview_event_after), update_viewer);
	g_signal_connect (GTK_TEXT_VIEW (widget), "motion-notify-event", G_CALLBACK (gpk_update_viewer_textview_motion_notify_event), update_viewer);
	g_signal_connect (GTK_TEXT_VIEW (widget), "visibility-notify-event", G_CALLBACK (gpk_update_viewer_textview_visibility_notify_event), update_viewer);

	/* updates */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "treeview_updates"));
	gtk_tree_view_set_search_column (GTK_TREE_VIEW(widget), GPK_UPDATES_COLUMN_TEXT);
	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW(widget), gpk_update_viewer_search_equal_func, NULL, NULL);
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW(widget));
	gtk_tree_view_set_model (GTK_TREE_VIEW(widget),
				 GTK_TREE_MODEL (priv->array_store_updates));
	gpk_update_viewer_treeview_add_columns_update (update_viewer, GTK_TREE_VIEW(widget));
	g_signal_connect (widget, "popup-menu",
			  G_CALLBACK (gpk_update_viewer_detail_popup_menu), update_viewer);
	g_signal_connect (widget, "button-press-event",
			  G_CALLBACK (gpk_update_viewer_detail_button_pressed), update_viewer);

	/* selection */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(widget));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_packages_treeview_clicked_cb), update_viewer);

	/* bottom UI */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "progressbar_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "label_summary"));
	gtk_widget_hide (widget);

	/* help button */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_help_cb), (gpointer) "update-viewer");

	/* set install button insensitive */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_install"));
	gtk_widget_set_sensitive (widget, FALSE);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_install_cb), update_viewer);

	/* sensitive */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "scrolledwindow_updates"));
	gtk_widget_set_sensitive (widget, FALSE);
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "scrolledwindow_details"));
	gtk_widget_set_sensitive (widget, FALSE);

	/* close button */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_quit"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_quit_cb), update_viewer);
	gtk_window_set_focus (GTK_WINDOW(main_window), widget);

	/* upgrade button */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "button_upgrade"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_upgrade_cb), update_viewer);

	/* set a size, if the screen allows */
	ret = gpk_window_set_size_request (GTK_WINDOW(main_window), 700, 600);
	if (!ret) {
		egg_debug ("small form factor mode");
		/* hide the header in SFF mode */
		widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "hbox_header"));
		gtk_widget_hide (widget);
	}

	/* use correct status pane */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "label_status"));
	gtk_widget_set_size_request (widget, -1, 32);
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "label_info"));
	gtk_widget_set_size_request (widget, -1, 32);

	/* add info bar: TODO, fix glade to put this in the ui file */
	priv->info_bar = gtk_info_bar_new ();
	gtk_widget_set_no_show_all (priv->info_bar, TRUE);

	/* pack label into infobar */
	priv->info_bar_label = gtk_label_new ("");
	widget = gtk_info_bar_get_content_area (GTK_INFO_BAR(priv->info_bar));
	gtk_container_add (GTK_CONTAINER(widget), priv->info_bar_label);
	gtk_widget_show (priv->info_bar_label);

	/* pack infobar into main UI */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "vbox1"));
	gtk_box_pack_start (GTK_BOX(widget), priv->info_bar, FALSE, FALSE, 3);
	gtk_box_reorder_child (GTK_BOX(widget), priv->info_bar, 1);

	/* show window */
	gtk_widget_show (main_window);

	/* set the paned to be in the middle */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder, "vpaned_updates"));
	g_signal_connect (widget, "realize",
			  G_CALLBACK (gpk_update_viewer_vpaned_realized_cb), update_viewer);

	/* coldplug */
	gpk_update_viewer_get_new_update_array (update_viewer);
out:
	return;
}

/**
 * gpk_update_viewer_finalize:
 * @object: This graph class instance
 **/
static void
gpk_update_viewer_finalize (GObject *object)
{
	GpkUpdateViewer *update_viewer = GPK_UPDATE_VIEWER (object);
	GpkUpdateViewerPrivate *priv = update_viewer->priv;

	/* we might have visual stuff running, close it down */
	g_cancellable_cancel (priv->cancellable);

	if (priv->update_array != NULL)
		g_ptr_array_unref (priv->update_array);
	g_free (priv->package_id_last);
	g_object_unref (priv->array_store_updates);
	g_object_unref (priv->builder);
	g_object_unref (priv->cancellable);
	g_object_unref (priv->console);
	g_object_unref (priv->control);
	g_object_unref (priv->gconf_client);
	g_object_unref (priv->markdown);
	g_object_unref (priv->task);
	g_object_unref (priv->text_buffer);

	G_OBJECT_CLASS (gpk_update_viewer_parent_class)->finalize (object);
}

/**
 * gpk_update_viewer_class_init:
 * @klass: This graph class instance
 **/
static void
gpk_update_viewer_class_init (GpkUpdateViewerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_update_viewer_finalize;
	g_type_class_add_private (klass, sizeof (GpkUpdateViewerPrivate));

	signals [SIGNAL_CLOSE] =
		g_signal_new ("action-close",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkUpdateViewerClass, action_close),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * gpk_update_viewer_new:
 * Return value: new GpkUpdateViewer instance.
 **/
GpkUpdateViewer *
gpk_update_viewer_new (void)
{
	GpkUpdateViewer *update_viewer;
	update_viewer = g_object_new (GPK_TYPE_UPDATE_VIEWER, NULL);
	return GPK_UPDATE_VIEWER (update_viewer);
}

