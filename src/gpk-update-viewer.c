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
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-string.h"
#include "egg-markdown.h"
#ifdef HAVE_SYSTEMD
#include "systemd-proxy.h"
#endif

#include "gpk-cell-renderer-info.h"
#include "gpk-cell-renderer-restart.h"
#include "gpk-cell-renderer-size.h"
#include "gpk-common.h"
#include "gpk-dialog.h"
#include "gpk-enum.h"
#include "gpk-error.h"
#include "gpk-gnome.h"
#include "gpk-session.h"
#include "gpk-task.h"
#include "gpk-debug.h"

#define GPK_UPDATE_VIEWER_AUTO_QUIT_TIMEOUT	10 /* seconds */
#define GPK_UPDATE_VIEWER_AUTO_RESTART_TIMEOUT	60 /* seconds */
#define GPK_UPDATE_VIEWER_MOBILE_SMALL_SIZE	512*1024 /* bytes */

static	gboolean		 ignore_updates_changed = FALSE;
static	gchar			*package_id_last = NULL;
static	guint			 auto_shutdown_id = 0;
static	guint			 size_total = 0;
static	guint			 number_total = 0;
static	PkRestartEnum		 restart_worst = 0;
#ifdef HAVE_SYSTEMD
static  SystemdProxy		*proxy = NULL;
#endif
static	EggMarkdown		*markdown = NULL;
static	GCancellable		*cancellable = NULL;
static	GSettings		*settings = NULL;
static	GPtrArray		*update_array = NULL;
static	GtkBuilder		*builder = NULL;
static	GtkTreeStore		*array_store_updates = NULL;
static	GtkTextBuffer		*text_buffer = NULL;
static	PkControl		*control = NULL;
static	PkRestartEnum		 restart_update = 0;
static	PkTask			*task = NULL;
static	GtkWidget		*info_updates = NULL;
static	GtkWidget		*info_mobile = NULL;
static	GtkWidget		*info_mobile_label = NULL;
static	GtkApplication		*application = NULL;
static	PkBitfield		 roles = 0;
static	gboolean		 have_available_distro_upgrades = FALSE;

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
	GPK_UPDATES_COLUMN_VISIBLE,
	GPK_UPDATES_COLUMN_LAST
};

static gboolean gpk_update_viewer_get_new_update_array (void);

/**
 * gpk_update_viewer_quit:
 **/
static void
gpk_update_viewer_quit (void)
{
	/* are we in a transaction */
	g_cancellable_cancel (cancellable);
	g_application_release (G_APPLICATION (application));
}

/**
 * gpk_update_viewer_button_quit_cb:
 **/
static void
gpk_update_viewer_button_quit_cb (GtkWidget *widget, gpointer user_data)
{
	gpk_update_viewer_quit ();
}

/**
 * gpk_update_viewer_packages_set_sensitive:
 **/
static void
gpk_update_viewer_packages_set_sensitive (gboolean sensitive)
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean valid;
	gboolean child_valid;
	GtkTreeIter child_iter;

	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	/* set all the checkboxes sensitive */
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_store_set (array_store_updates, &iter,
				    GPK_UPDATES_COLUMN_SENSITIVE, sensitive,
				    GPK_UPDATES_COLUMN_CLICKABLE, sensitive,
				    -1);

		/* do for children too */
		child_valid = gtk_tree_model_iter_children (model, &child_iter, &iter);
		while (child_valid) {
			gtk_tree_store_set (GTK_TREE_STORE(model), &child_iter,
					    GPK_UPDATES_COLUMN_SENSITIVE, sensitive,
					    GPK_UPDATES_COLUMN_CLICKABLE, sensitive,
					    -1);
			child_valid = gtk_tree_model_iter_next (model, &child_iter);
		}

		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * gpk_update_viewer_auto_shutdown_cb:
 **/
static gboolean
gpk_update_viewer_auto_shutdown_cb (GtkDialog *dialog)
{
	g_debug ("autoclosing dialog");
	gtk_dialog_response (dialog, GTK_RESPONSE_CANCEL);
	auto_shutdown_id = 0;
	return FALSE;
}

/**
 * gpk_update_viewer_error_dialog:
 **/
static void
gpk_update_viewer_error_dialog (const gchar *title, const gchar *message, const gchar *details)
{
	GtkWindow *window;

	/* fallback */
	if (message == NULL) {
		/* TRANSLATORS: we don't have a lot to go on here */
		message = _("Failed to process request.");
	}

	g_warning ("%s: %s", title, details);
	window = GTK_WINDOW(gtk_builder_get_object (builder, "dialog_updates"));
	gpk_error_dialog_modal (window, title, message, details);
}

/**
 * gpk_update_viewer_check_restart:
 **/
static gboolean
gpk_update_viewer_check_restart (void)
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

	/* get the text */
	title = gpk_restart_enum_to_localised_text (restart_update);
	if (restart_update == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: the message text for the restart */
		message = _("Some of the updates that were installed require the computer to be restarted before the changes will be applied.");
		/* TRANSLATORS: the button text for the restart */
		button = _("Restart Computer");

	} else if (restart_update == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		/* TRANSLATORS: the message text for the restart */
		message = _("Some of the updates that were installed require the computer to be restarted to remain secure.");
		/* TRANSLATORS: the button text for the restart */
		button = _("Restart Computer");

	} else if (restart_update == PK_RESTART_ENUM_SESSION) {
		/* TRANSLATORS: the message text for the log out */
		message = _("Some of the updates that were installed require you to log out and back in before the changes will be applied.");
		/* TRANSLATORS: the button text for the log out */
		button = _("Log Out");

	} else if (restart_update == PK_RESTART_ENUM_SECURITY_SESSION) {
		/* TRANSLATORS: the message text for the log out */
		message = _("Some of the updates that were installed require you to log out and back in to remain secure.");
		/* TRANSLATORS: the button text for the log out */
		button = _("Log Out");

	} else {
		g_warning ("unknown restart enum");
		goto out;
	}

	/* show modal dialog */
	window = GTK_WINDOW(gtk_builder_get_object (builder, "dialog_updates"));
	dialog = gtk_message_dialog_new (window, GTK_DIALOG_MODAL,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
					 "%s", title);

	/* check to see if restart is possible */
	if (restart_update == PK_RESTART_ENUM_SYSTEM ||
	    restart_update == PK_RESTART_ENUM_SECURITY_SYSTEM) {
#ifdef HAVE_SYSTEMD
		systemd_proxy_can_restart (proxy, &show_button, NULL);
#else
		show_button = FALSE;
#endif
	}

	/* only show the button if we can do the action */
	if (show_button)
		gtk_dialog_add_button (GTK_DIALOG (dialog), button, GTK_RESPONSE_OK);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog), "%s", message);
	gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_UPDATE);
	response = gtk_dialog_run (GTK_DIALOG(dialog));
	gtk_widget_destroy (dialog);

	/* cancel */
	if (response != GTK_RESPONSE_OK)
		goto out;

	/* doing the action, return success */
	ret = TRUE;

	/* do the action */
	if (restart_update == PK_RESTART_ENUM_SYSTEM) {
#ifdef HAVE_SYSTEMD
		ret = systemd_proxy_restart (proxy, &error);
		if (!ret) {
			/* TRANSLATORS: the PackageKit request did not complete, and it did not send an error */
			gpk_update_viewer_error_dialog (_("Could not restart"), NULL, error->message);
			g_error_free (error);
		}
#endif
	} else if (restart_update == PK_RESTART_ENUM_SESSION) {
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
gpk_update_viewer_check_blocked_packages (GPtrArray *array)
{
	guint i;
	PkPackage *item;
	GString *string;
	gboolean exists = FALSE;
	gchar *text;
	GtkWindow *window;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *summary = NULL;

	string = g_string_new ("");

	/* find any that are blocked */
	for (i=0;i<array->len;i++) {
		item = g_ptr_array_index (array, i);

		/* get data */
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);

		if (info == PK_INFO_ENUM_BLOCKED) {
			text = gpk_package_id_format_oneline (package_id, summary);
			g_string_append_printf (string, "%s\n", text);
			g_free (text);
			exists = TRUE;
		}

		g_free (package_id);
		g_free (summary);
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
	window = GTK_WINDOW(gtk_builder_get_object (builder, "dialog_updates"));
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
	gboolean child_valid;
	GtkTreeIter child_iter;
	GtkTreeIter iter;

	/* if there are no entries selected, deselect the button */
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_SELECT, &selected,
				    -1);
		if (!selected)
			goto out;

		/* do for children too */
		child_valid = gtk_tree_model_iter_children (model, &child_iter, &iter);
		while (child_valid) {
			gtk_tree_model_get (model, &child_iter,
					    GPK_UPDATES_COLUMN_SELECT, &selected,
					    -1);
			if (!selected)
				goto out;
			child_valid = gtk_tree_model_iter_next (model, &child_iter);
		}

		valid = gtk_tree_model_iter_next (model, &iter);
	}
out:
	return selected;
}

/**
 * gpk_update_viewer_update_packages_cb:
 **/
static void
gpk_update_viewer_update_packages_cb (PkTask *_task, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array = NULL;
	GtkWidget *dialog;
	GtkWidget *widget;
	PkRestartEnum restart;
	gchar *text;
	PkError *error_code = NULL;
	GtkWindow *window;
	gboolean ret;
	const gchar *message;
	GtkTreeView *treeview;
	GtkTreeModel *model;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		/* not a PK error */
		if (error->domain != PK_CLIENT_ERROR) {
			/* TRANSLATORS: the PackageKit request did not complete, and it did not send an error */
			gpk_update_viewer_error_dialog (_("Could not update packages"), NULL, error->message);
		} else if (error->code == PK_CLIENT_ERROR_DECLINED_SIMULATION) {
			g_debug ("ignoring the declined-simulation error");
		} else if (error->code > PK_CLIENT_ERROR_LAST) {
			gint code = error->code - PK_CLIENT_ERROR_LAST;
			/* we've passed the PackageKit error code in the GError->code */
			gpk_update_viewer_error_dialog (gpk_error_enum_to_localised_text (code),
							gpk_error_enum_to_localised_message (code),
							error->message);
		} else {
			/* TRANSLATORS: the PackageKit request did not complete, and it did not send an error */
			gpk_update_viewer_error_dialog (_("Could not update packages"), NULL, error->message);
		}
		g_error_free (error);

		/* re-enable the package list */
		gpk_update_viewer_packages_set_sensitive (TRUE);

		/* allow clicking again */
		widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_install"));
		gtk_widget_set_sensitive (widget, TRUE);

		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to update packages: %s, %s",
			   pk_error_enum_to_string (pk_error_get_code (error_code)),
			   pk_error_get_details (error_code));

		window = GTK_WINDOW(gtk_builder_get_object (builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
					gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* re-enable the package list */
		gpk_update_viewer_packages_set_sensitive (TRUE);

		/* allow clicking again */
		widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_install"));
		gtk_widget_set_sensitive (widget, TRUE);

		goto out;
	}

	gpk_update_viewer_packages_set_sensitive (TRUE);

	/* get the worst restart case */
	restart = pk_results_get_require_restart_worst (results);
	if (restart > restart_update)
		restart_update = restart;

	/* check blocked */
	array = pk_results_get_package_array (results);
	gpk_update_viewer_check_blocked_packages (array);

	/* check restart */
	if (restart_update == PK_RESTART_ENUM_SYSTEM ||
	    restart_update == PK_RESTART_ENUM_SESSION ||
	    restart_update == PK_RESTART_ENUM_SECURITY_SESSION ||
	    restart_update == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		gpk_update_viewer_check_restart ();
		gpk_update_viewer_quit ();
		goto out;
	} else {
		goto out;
	}

	/* hide close button */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_quit"));
	gtk_widget_hide (widget);

	/* show a new title */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "label_header_title"));
	/* TRANSLATORS: completed all updates */
	text = g_strdup_printf ("<big><b>%s</b></big>", _("Updates installed"));
	gtk_label_set_label (GTK_LABEL(widget), text);
	g_free (text);

	/* do different text depending on if we deselected any */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
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
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "dialog_updates"));
	dialog = gtk_message_dialog_new (GTK_WINDOW(widget), GTK_DIALOG_MODAL,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
					 /* TRANSLATORS: title: all updates installed okay */
					 "%s", _("Updates installed"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog),
						  "%s", message);
	gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_UPDATE);

	/* setup a callback so we autoclose */
	auto_shutdown_id =
		g_timeout_add_seconds (GPK_UPDATE_VIEWER_AUTO_RESTART_TIMEOUT,
				       (GSourceFunc) gpk_update_viewer_auto_shutdown_cb, dialog);
	g_source_set_name_by_id (auto_shutdown_id, "[GpkUpdateViewer] auto-shutdown");

	gtk_dialog_run (GTK_DIALOG(dialog));
	gtk_widget_destroy (dialog);

	/* remove auto-shutdown */
	if (auto_shutdown_id != 0) {
		g_source_remove (auto_shutdown_id);
		auto_shutdown_id = 0;
	}

	/* quit after we successfully updated */
	gpk_update_viewer_quit ();
out:
	/* no longer updating */
	ignore_updates_changed = FALSE;

	if (error_code != NULL)
		g_object_unref (error_code);
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
			gtk_tree_store_set (GTK_TREE_STORE(model), &iter, GPK_UPDATES_COLUMN_PULSE, val + 1, -1);
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
	GSList *row = NULL;

	/* check if already active */
	ref = gtk_tree_row_reference_new (model, path);
	if (ref == NULL)
		goto out;
	row = g_slist_find_custom (active_rows, (gconstpointer)ref, (GCompareFunc)gpk_update_viewer_compare_refs);
	if (row != NULL) {
		g_debug ("already active");
		gtk_tree_row_reference_free (ref);
		goto out;
	}

	/* add poll */
	if (active_row_timeout_id == 0) {
		active_row_timeout_id = g_timeout_add (60, (GSourceFunc)gpk_update_viewer_pulse_active_rows, NULL);
		g_source_set_name_by_id (active_row_timeout_id, "[GpkUpdateViewer] pulse row");
	}
	active_rows = g_slist_prepend (active_rows, ref);
out:
	return;
}

/**
 * gpk_update_viewer_remove_active_row:
 **/
static void
gpk_update_viewer_remove_active_row (GtkTreeModel *model, GtkTreePath *path)
{
	GSList *row;
	GtkTreeRowReference *ref;
	GtkTreeIter iter;

	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_store_set (GTK_TREE_STORE(model), &iter, GPK_UPDATES_COLUMN_PULSE, -1, -1);

	ref = gtk_tree_row_reference_new (model, path);
	row = g_slist_find_custom (active_rows, (gconstpointer)ref, (GCompareFunc)gpk_update_viewer_compare_refs);
	gtk_tree_row_reference_free (ref);
	if (row == NULL) {
		g_warning ("row not already added");
		return;
	}

	active_rows = g_slist_remove_link (active_rows, row);
	gtk_tree_row_reference_free (row->data);
	g_slist_free (row);

	if (active_rows == NULL) {
		g_source_remove (active_row_timeout_id);
		active_row_timeout_id = 0;
	}
}

/**
 * gpk_update_viewer_find_iter_model_cb:
 **/
static gboolean
gpk_update_viewer_find_iter_model_cb (GtkTreeModel *model,
				      GtkTreePath *path,
				      GtkTreeIter *iter,
				      const gchar *package_id)
{
	gchar *package_id_tmp = NULL;
	GtkTreePath **_path = NULL;
	gboolean ret = FALSE;

	gtk_tree_model_get (model, iter,
			    GPK_UPDATES_COLUMN_ID, &package_id_tmp,
			    -1);
	if (package_id_tmp == NULL)
		goto out;

	/* match on the package id */
	if (g_strcmp0 (package_id, package_id_tmp) == 0) {
		_path = (GtkTreePath **) g_object_get_data (G_OBJECT(model), "_path");
		*_path = gtk_tree_path_copy (path);
		ret = TRUE;
	}
out:
	g_free (package_id_tmp);
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
 * gpk_update_view_get_info_headers:
 **/
static const gchar *
gpk_update_view_get_info_headers (PkInfoEnum info)
{
	const gchar *text = NULL;
	switch (info) {
	case PK_INFO_ENUM_LOW:
		/* TRANSLATORS: The type of update */
		text = _("Trivial updates");
		break;
	case PK_INFO_ENUM_IMPORTANT:
		/* TRANSLATORS: The type of update */
		text = _("Important updates");
		break;
	case PK_INFO_ENUM_SECURITY:
		/* TRANSLATORS: The type of update */
		text = _("Security updates");
		break;
	case PK_INFO_ENUM_BUGFIX:
		/* TRANSLATORS: The type of update */
		text = _("Bug fix updates");
		break;
	case PK_INFO_ENUM_ENHANCEMENT:
		/* TRANSLATORS: The type of update */
		text = _("Enhancement updates");
		break;
	case PK_INFO_ENUM_BLOCKED:
		/* TRANSLATORS: The type of update */
		text = _("Blocked updates");
		break;
	default:
		/* TRANSLATORS: The type of update, i.e. unspecified */
		text = _("Other updates");
	}
	return text;
}

/**
 * gpk_update_viewer_get_parent_for_info:
 **/
static void
gpk_update_viewer_get_parent_for_info (PkInfoEnum info, GtkTreeIter *parent)
{
	gboolean is_package;
	gboolean ret = FALSE;
	gboolean valid;
	gchar *package_id_tmp;
	gchar *title;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeView *treeview;
	PkInfoEnum info_tmp;

	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	/* get the first iter in the array */
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* smush some update states together */
	switch (info) {
	case PK_INFO_ENUM_ENHANCEMENT:
	case PK_INFO_ENUM_LOW:
		info = PK_INFO_ENUM_NORMAL;
		break;
	default:
		break;
	}

	/* find out how many we should update */
	while (valid) {
		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_INFO, &info_tmp,
				    GPK_UPDATES_COLUMN_ID, &package_id_tmp,
				    -1);
		is_package = package_id_tmp != NULL;
		g_free (package_id_tmp);

		/* right section? */
		if (!is_package && info_tmp == info) {
			*parent = iter;
			ret = TRUE;
			break;
		}

		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* create */
	if (!ret) {
		title = g_strdup_printf ("<b>%s</b>",
					 gpk_update_view_get_info_headers (info));
		gtk_tree_store_append (array_store_updates, &iter, NULL);
		gtk_tree_store_set (array_store_updates, &iter,
				    GPK_UPDATES_COLUMN_TEXT, title,
				    GPK_UPDATES_COLUMN_ID, NULL,
				    GPK_UPDATES_COLUMN_INFO, info,
				    GPK_UPDATES_COLUMN_SELECT, TRUE,
				    GPK_UPDATES_COLUMN_VISIBLE, FALSE,
				    GPK_UPDATES_COLUMN_CLICKABLE, FALSE,
				    GPK_UPDATES_COLUMN_RESTART, PK_RESTART_ENUM_NONE,
				    GPK_UPDATES_COLUMN_STATUS, PK_INFO_ENUM_UNKNOWN,
				    GPK_UPDATES_COLUMN_SIZE, 0,
				    GPK_UPDATES_COLUMN_SIZE_DISPLAY, 0,
				    GPK_UPDATES_COLUMN_PERCENTAGE, 0,
				    GPK_UPDATES_COLUMN_PULSE, -1,
				    -1);
		*parent = iter;
		g_free (title);
	}
}

/**
 * gpk_update_viewer_progress_cb:
 **/
static void
gpk_update_viewer_progress_cb (PkProgress *progress,
			       PkProgressType type,
			       gpointer user_data)
{
	gboolean allow_cancel;
	PkPackage *package;
	gchar *text;
	gint percentage;
	GtkWidget *widget;
	guint64 transaction_flags;
	PkInfoEnum info;
	PkRoleEnum role;
	PkStatusEnum status;
	gchar *package_id = NULL;
	gchar *summary = NULL;

	g_object_get (progress,
		      "role", &role,
		      "status", &status,
		      "percentage", &percentage,
		      "package", &package,
		      "allow-cancel", &allow_cancel,
		      "transaction-flags", &transaction_flags,
		      NULL);

	if (type == PK_PROGRESS_TYPE_PACKAGE) {

		GtkTreeView *treeview;
		GtkTreeIter iter;
		GtkTreeModel *model;
		GtkTreeViewColumn *column;
		GtkTreePath *path;
		gboolean scroll;

		/* ignore simulation phase */
		if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE))
			goto out;

		/* add the results, not the progress */
		if (role == PK_ROLE_ENUM_GET_UPDATES)
			goto out;

		g_object_get (package,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);

		/* find model */
		treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
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
		if (g_strcmp0 (package_id_last, package_id) != 0) {
			g_free (package_id_last);
			package_id_last = g_strdup (package_id);
		}

		/* update icon */
		path = gpk_update_viewer_model_get_path (model, package_id);
		if (path == NULL) {

			text = gpk_package_id_format_twoline (gtk_widget_get_style_context (GTK_WIDGET (treeview)),
							      package_id,
							      summary);
			g_debug ("adding: id=%s, text=%s", package_id, text);

			/* add to model */
			gtk_tree_store_append (array_store_updates, &iter, NULL);
			gtk_tree_store_set (array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_TEXT, text,
					    GPK_UPDATES_COLUMN_ID, package_id,
					    GPK_UPDATES_COLUMN_INFO, info,
					    GPK_UPDATES_COLUMN_SELECT, TRUE,
					    GPK_UPDATES_COLUMN_VISIBLE, TRUE,
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
				g_warning ("found no package %s", package_id);
				goto out;
			}
		}

		gtk_tree_model_get_iter (model, &iter, path);

		/* if we are adding deps, then select the checkbox */
		if (role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
			gtk_tree_store_set (array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_SELECT, TRUE,
					    -1);
		}

		/* scroll to the active cell */
		scroll = g_settings_get_boolean (settings, GPK_SETTINGS_SCROLL_ACTIVE);
		if (scroll) {
			column = gtk_tree_view_get_column (treeview, 3);
			gtk_tree_view_scroll_to_cell (treeview, path, column, FALSE, 0.0f, 0.0f);
		}

		/* only change the status when we're doing the actual update */
		if (role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
			/* if the info is finished, change the status to past tense */
			if (info == PK_INFO_ENUM_FINISHED) {
				/* clear the remaining size */
				gtk_tree_store_set (array_store_updates, &iter,
						    GPK_UPDATES_COLUMN_SIZE_DISPLAY, 0, -1);

				gtk_tree_model_get (model, &iter,
						    GPK_UPDATES_COLUMN_STATUS, &info, -1);
				/* promote to past tense if present tense */
				if (info < PK_INFO_ENUM_LAST)
					info += PK_INFO_ENUM_LAST;
			}
			gtk_tree_store_set (array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_STATUS, info, -1);
		}

		gtk_tree_path_free (path);

	} else if (type == PK_PROGRESS_TYPE_STATUS) {

		GdkWindow *window;
		const gchar *title;
		GdkDisplay *display;
		GdkCursor *cursor;

		g_debug ("status %s", pk_status_enum_to_string (status));

		/* use correct status pane */
		widget = GTK_WIDGET(gtk_builder_get_object (builder, "hbox_status"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET(gtk_builder_get_object (builder, "hbox_info"));
		gtk_widget_hide (widget);

		/* set cursor back to normal */
		window = gtk_widget_get_window (widget);
		if (status == PK_STATUS_ENUM_FINISHED) {
			gdk_window_set_cursor (window, NULL);
		} else {
			display = gdk_display_get_default ();
			cursor = gdk_cursor_new_for_display (display, GDK_WATCH);
			gdk_window_set_cursor (window, cursor);
			g_object_unref (cursor);
		}

		/* set status */
		widget = GTK_WIDGET(gtk_builder_get_object (builder, "label_status"));
		if (status == PK_STATUS_ENUM_FINISHED) {
			gtk_label_set_label (GTK_LABEL(widget), "");
			widget = GTK_WIDGET(gtk_builder_get_object (builder, "image_progress"));
			gtk_widget_hide (widget);

			widget = GTK_WIDGET(gtk_builder_get_object (builder, "progressbar_progress"));
			gtk_widget_hide (widget);

			widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_quit"));
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
			widget = GTK_WIDGET(gtk_builder_get_object (builder, "image_progress"));
			gtk_image_set_from_icon_name (GTK_IMAGE(widget), gpk_status_enum_to_icon_name (status), GTK_ICON_SIZE_BUTTON);
			gtk_widget_show (widget);
		}

	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {

		widget = GTK_WIDGET(gtk_builder_get_object (builder, "progressbar_progress"));
		gtk_widget_show (widget);
		if (percentage != -1)
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);

	} else if (type == PK_PROGRESS_TYPE_ITEM_PROGRESS) {

#if PK_CHECK_VERSION(0,8,1)
		GtkTreeView *treeview;
		GtkTreeModel *model;
		GtkTreeIter iter;
		GtkTreePath *path;
		guint size;
		guint size_display;
		PkItemProgress *item_progress;

		/* ignore simulation phase */
		if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE))
			goto out;

		g_object_get (progress,
			      "item-progress", &item_progress,
			      NULL);

		treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
		model = gtk_tree_view_get_model (treeview);
		path = gpk_update_viewer_model_get_path (model,
							 pk_item_progress_get_package_id (item_progress));
		if (path == NULL) {
			g_debug ("not found ID for %s",
				 pk_item_progress_get_package_id (item_progress));
			goto out;
		}

		gtk_tree_model_get_iter (model, &iter, path);
		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_SIZE, &size,
				    -1);
		percentage = pk_item_progress_get_percentage (item_progress);
		if (percentage > 0) {
			size_display = size - ((size * percentage) / 100);
			gtk_tree_store_set (array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_PERCENTAGE, percentage,
					    GPK_UPDATES_COLUMN_SIZE_DISPLAY, size_display,
					    -1);
		}
		gtk_tree_path_free (path);
#endif

	} else if (type == PK_PROGRESS_TYPE_ALLOW_CANCEL) {
		gboolean idle;
		widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_quit"));

		/* we have to also check for idle as we might be getting the AllowCancel(false)
		 * signal _after_ the PkClient has been marked as idle */
		g_object_get (task, "idle", &idle, NULL);
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
gpk_update_viewer_client_notify_idle_cb (PkClient *client, GParamSpec *pspec, gpointer user_data)
{
	gboolean idle;
	GtkWidget *widget;

	g_object_get (client,
		      "idle", &idle,
		      NULL);
	/* ensure button is sensitive */
	if (idle) {
		widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_quit"));
		gtk_widget_set_sensitive (widget, TRUE);
	}
}

/**
 * gpk_update_viewer_info_is_update_enum:
 **/
static gboolean
gpk_update_viewer_info_is_update_enum (PkInfoEnum info)
{
	gboolean ret = FALSE;
	switch (info) {
	case PK_INFO_ENUM_LOW:
	case PK_INFO_ENUM_NORMAL:
	case PK_INFO_ENUM_IMPORTANT:
	case PK_INFO_ENUM_SECURITY:
	case PK_INFO_ENUM_BUGFIX:
	case PK_INFO_ENUM_ENHANCEMENT:
	case PK_INFO_ENUM_BLOCKED:
		ret = TRUE;
		break;
	default:
		break;
	}
	return ret;
}

/**
 * gpk_update_viewer_get_install_package_ids:
 **/
static GPtrArray *
gpk_update_viewer_get_install_package_ids (void)
{
	PkInfoEnum info;
	gboolean child_valid;
	gboolean valid;
	gboolean update;
	gchar *package_id;
	GtkTreeIter child_iter;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GPtrArray *array;

	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	array = g_ptr_array_new ();

	/* get the first iter in the array */
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter,
				    GPK_UPDATES_COLUMN_INFO, &info,
				    GPK_UPDATES_COLUMN_SELECT, &update,
				    GPK_UPDATES_COLUMN_ID, &package_id, -1);

		/* if selected, and not added previously because of deps */
		if (package_id != NULL &&
		    update &&
		    gpk_update_viewer_info_is_update_enum (info))
			g_ptr_array_add (array, package_id);
		else
			g_free (package_id);

		/* do for children too */
		child_valid = gtk_tree_model_iter_children (model, &child_iter, &iter);
		while (child_valid) {
			gtk_tree_model_get (model, &child_iter,
					    GPK_UPDATES_COLUMN_INFO, &info,
					    GPK_UPDATES_COLUMN_SELECT, &update,
					    GPK_UPDATES_COLUMN_ID, &package_id, -1);

			/* if selected, and not added previously because of deps */
			if (update && gpk_update_viewer_info_is_update_enum (info))
				g_ptr_array_add (array, package_id);
			else
				g_free (package_id);

			child_valid = gtk_tree_model_iter_next (model, &child_iter);
		}

		valid = gtk_tree_model_iter_next (model, &iter);
	}
	return array;
}

/**
 * gpk_update_viewer_button_install_cb:
 **/
static void
gpk_update_viewer_button_install_cb (GtkWidget *widget, gpointer user_data)
{
	GtkTreeSelection *selection;
	GPtrArray *array = NULL;
	GtkTreeView *treeview;
	gchar **package_ids = NULL;

	/* hide the upgrade viewbox from now on */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "viewport_upgrade"));
	gtk_widget_hide (widget);

	/* hide the held-back notice */
	gtk_widget_hide (info_updates);

	g_debug ("Doing the package updates");

	/* no not allow to be unclicked at install time */
	gpk_update_viewer_packages_set_sensitive (FALSE);

	/* disable button */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_install"));
	gtk_widget_set_sensitive (widget, FALSE);

	/* clear the selection */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_unselect_all (selection);

	/* get the list of updates */
	array = gpk_update_viewer_get_install_package_ids ();
	package_ids = pk_ptr_array_to_strv (array);

	/* the backend is able to do UpdatePackages */
	pk_task_update_packages_async (task, package_ids, cancellable,
				       (PkProgressCallback) gpk_update_viewer_progress_cb, NULL,
				       (GAsyncReadyCallback) gpk_update_viewer_update_packages_cb, NULL);

	/* from now on ignore updates-changed signals */
	ignore_updates_changed = TRUE;

	g_strfreev (package_ids);
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * gpk_update_viewer_button_upgrade_cb:
 **/
static void
gpk_update_viewer_button_upgrade_cb (GtkWidget *widget, gpointer user_data)
{
	gboolean ret;
	GError *error = NULL;

	ret = g_spawn_command_line_async ("/usr/share/PackageKit/pk-upgrade-distro.sh", &error);
	if (!ret) {
		/* TRANSLATORS: the PackageKit request did not complete, and it did not send an error */
		gpk_update_viewer_error_dialog (_("Could not run upgrade script"), NULL, error->message);
		g_error_free (error);
	}
}

/**
 * gpk_update_viewer_check_mobile_broadband:
 **/
static void
gpk_update_viewer_check_mobile_broadband (void)
{
	PkNetworkEnum state;
	const gchar *message;

	/* get network state */
	g_object_get (control,
		      "network-state", &state,
		      NULL);

	/* hide by default */
	gtk_widget_hide (info_mobile);

	/* not on wireless mobile */
	if (state != PK_NETWORK_ENUM_MOBILE)
		goto out;

	/* not when small */
	if (size_total < GPK_UPDATE_VIEWER_MOBILE_SMALL_SIZE)
		goto out;

	/* TRANSLATORS, are we going to cost the user lots of money? */
	message = ngettext ("Connectivity is being provided by wireless broadband, and it may be expensive to update this package.",
			    "Connectivity is being provided by wireless broadband, and it may be expensive to update these packages.",
			    number_total);
	gtk_label_set_label (GTK_LABEL(info_mobile_label), message);

	gtk_info_bar_set_message_type (GTK_INFO_BAR(info_mobile), GTK_MESSAGE_WARNING);
	gtk_widget_show (info_mobile);
out:
	return;
}

/**
 * gpk_update_viewer_update_global_state_recursive:
 **/
static void
gpk_update_viewer_update_global_state_recursive (GtkTreeModel *model, GtkTreeIter *iter)
{
	gboolean selected;
	PkRestartEnum restart;
	guint size;
	gchar *package_id = NULL;
	gboolean child_valid;
	GtkTreeIter child_iter;

	gtk_tree_model_get (model, iter,
			    GPK_UPDATES_COLUMN_SELECT, &selected,
			    GPK_UPDATES_COLUMN_RESTART, &restart,
			    GPK_UPDATES_COLUMN_SIZE, &size,
			    GPK_UPDATES_COLUMN_ID, &package_id,
			    -1);
	if (selected && package_id != NULL) {
		size_total += size;
		number_total++;
		if (restart > restart_worst)
			restart_worst = restart;
	}

	/* child entries */
	child_valid = gtk_tree_model_iter_children (model, &child_iter, iter);
	while (child_valid) {
		gpk_update_viewer_update_global_state_recursive (model, &child_iter);
		child_valid = gtk_tree_model_iter_next (model, &child_iter);
	}
	g_free (package_id);
}

/**
 * gpk_update_viewer_update_global_state:
 **/
static void
gpk_update_viewer_update_global_state (void)
{
	gboolean valid;
	GtkTreeIter iter;
	GtkTreeView *treeview;
	GtkTreeModel *model;

	/* reset to zero */
	size_total = 0;
	number_total = 0;
	restart_worst = PK_RESTART_ENUM_NONE;

	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	/* if there are no entries selected, deselect the button */
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gpk_update_viewer_update_global_state_recursive (model, &iter);
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * gpk_update_viewer_modal_error_with_timeout:
 **/
static void
gpk_update_viewer_modal_error_with_timeout (const gchar *title, const gchar *message)
{
	GtkWidget *dialog;
	GtkWidget *widget;
	gchar *text;

	/* hide close button */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_quit"));
	gtk_widget_hide (widget);

	/* show a new title */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "label_header_title"));
	/* TRANSLATORS: there are no updates */
	text = g_strdup_printf ("<big><b>%s</b></big>", _("There are no updates available"));
	gtk_label_set_label (GTK_LABEL(widget), text);
	g_free (text);

	/* show modal dialog */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "dialog_updates"));
	dialog = gtk_message_dialog_new (GTK_WINDOW(widget), GTK_DIALOG_MODAL,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
					 "%s", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog),
						  "%s", message);
	gtk_window_set_icon_name (GTK_WINDOW(dialog), GPK_ICON_SOFTWARE_UPDATE);

	/* setup a callback so we autoclose */
	auto_shutdown_id =
		g_timeout_add_seconds (GPK_UPDATE_VIEWER_AUTO_RESTART_TIMEOUT,
				       (GSourceFunc) gpk_update_viewer_auto_shutdown_cb, dialog);
	g_source_set_name_by_id (auto_shutdown_id, "[GpkUpdateViewer] shutdown no updates");

	gtk_dialog_run (GTK_DIALOG(dialog));
	gtk_widget_destroy (dialog);

	/* remove auto-shutdown */
	if (auto_shutdown_id != 0) {
		g_source_remove (auto_shutdown_id);
		auto_shutdown_id = 0;
	}

	/* exit the program */
	if (!have_available_distro_upgrades)
		gpk_update_viewer_quit ();
}

/**
 * gpk_update_viewer_reconsider_info:
 **/
static void
gpk_update_viewer_reconsider_info (void)
{
	GtkWidget *widget;
	guint len;
	const gchar *title;
	gchar *text;
	gchar *text_size;
	PkNetworkEnum state;

	/* update global state */
	gpk_update_viewer_update_global_state ();

	/* get network state */
	g_object_get (control,
		      "network-state", &state,
		      NULL);

	/* not when offline */
	g_debug ("network status is %s", pk_network_enum_to_string (state));
	if (state == PK_NETWORK_ENUM_OFFLINE) {
		gpk_update_viewer_modal_error_with_timeout (
				/* TRANSLATORS: title: nothing to do */
				_("No updates are available"),
				/* TRANSLATORS: no network connection, according to PackageKit */
				_("No network connection was detected."));
		goto out;
	}

	/* action button */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_install"));
	gtk_widget_set_sensitive (widget, (number_total > 0));

	/* sensitive */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "scrolledwindow_updates"));
	gtk_widget_set_sensitive (widget, TRUE);
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "scrolledwindow_details"));
	gtk_widget_set_sensitive (widget, TRUE);

	/* set the pluralisation of the button */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_install"));
	/* TRANSLATORS: this is the button text when we have updates */
	title = ngettext ("_Install Update", "_Install Updates", number_total);
	gtk_button_set_label (GTK_BUTTON (widget), title);

	/* no updates */
	if (update_array != NULL) {
		len = update_array->len;
		if (len == 0) {
			gpk_update_viewer_modal_error_with_timeout (
					/* TRANSLATORS: title: nothing to do */
					_("All packages are up to date"),
					/* TRANSLATORS: tell the user the problem */
					_("There are no package updates available for your computer at this time."));
			goto out;
		}
	}

	/* use correct status pane */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "hbox_status"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "hbox_info"));
	gtk_widget_show (widget);

	/* restart */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "label_info"));
	if (restart_worst == PK_RESTART_ENUM_NONE) {
		gtk_widget_hide (widget);
		widget = GTK_WIDGET(gtk_builder_get_object (builder, "image_info"));
		gtk_widget_hide (widget);
	} else {
		gtk_label_set_label (GTK_LABEL(widget), gpk_restart_enum_to_localised_text_future (restart_worst));
		gtk_widget_show (widget);
		widget = GTK_WIDGET(gtk_builder_get_object (builder, "image_info"));
		gtk_image_set_from_icon_name (GTK_IMAGE(widget), gpk_restart_enum_to_icon_name (restart_worst), GTK_ICON_SIZE_BUTTON);
		gtk_widget_show (widget);
	}

	/* header */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "label_header_title"));
	text = g_strdup_printf (ngettext ("There is %i update available",
					  "There are %i updates available", len), len);
	text_size = g_strdup_printf ("<big><b>%s</b></big>", text);
	gtk_label_set_label (GTK_LABEL(widget), text_size);
	g_free (text);
	g_free (text_size);
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "hbox_header"));
	gtk_widget_show (widget);

	/* total */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "label_summary"));
	if (number_total == 0) {
		gtk_label_set_label (GTK_LABEL(widget), "");
	} else {
		if (size_total == 0) {
			/* TRANSLATORS: how many updates are selected in the UI */
			text = g_strdup_printf (ngettext ("%i update selected",
							  "%i updates selected",
							  number_total), number_total);
			gtk_label_set_label (GTK_LABEL(widget), text);
			g_free (text);
		} else {
			text_size = g_format_size (size_total);
			/* TRANSLATORS: how many updates are selected in the UI, and the size of packages to download */
			text = g_strdup_printf (ngettext ("%i update selected (%s)",
							  "%i updates selected (%s)",
							  number_total), number_total, text_size);
			gtk_label_set_label (GTK_LABEL(widget), text);
			g_free (text);
			g_free (text_size);
		}
	}

	widget = GTK_WIDGET(gtk_builder_get_object (builder, "label_summary"));
	gtk_widget_show (widget);
out:
	gpk_update_viewer_check_mobile_broadband ();
}

/**
 * gpk_update_viewer_treeview_update_toggled:
 **/
static void
gpk_update_viewer_treeview_update_toggled (GtkCellRendererToggle *cell, gchar *path_str, gpointer user_data)
{
	GtkTreeIter iter, child_iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	gboolean update;
	gboolean child_valid;
	gchar *package_id;
	GtkTreeView *treeview;
	GtkTreeModel *model;

	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_SELECT, &update,
			    GPK_UPDATES_COLUMN_ID, &package_id, -1);

	/* unstage */
	update ^= 1;

	g_debug ("update %s[%i]", package_id, update);
	g_free (package_id);

	/* set new value */
	gtk_tree_store_set (GTK_TREE_STORE(model), &iter, GPK_UPDATES_COLUMN_SELECT, update, -1);

	/* do the same for any children */
	child_valid = gtk_tree_model_iter_children (model, &child_iter, &iter);
	while (child_valid) {
		gtk_tree_store_set (GTK_TREE_STORE(model), &child_iter,
				    GPK_UPDATES_COLUMN_SELECT, update, -1);
		child_valid = gtk_tree_model_iter_next (model, &child_iter);
	}

	/* clean up */
	gtk_tree_path_free (path);

	/* if there are no entries selected, deselect the button */
	gpk_update_viewer_reconsider_info ();
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
		g_warning ("wrap_width is impossibly small %i", wrap_width);
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
		text = gpk_info_status_enum_to_string (info);
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
gpk_update_viewer_treeview_add_columns_update (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GdkRGBA inactive;

	/* --- column for image and toggle --- */
	column = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), TRUE);
	gtk_tree_view_column_set_sort_column_id (column, GPK_UPDATES_COLUMN_INFO);

	/* select toggle */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled",
			  G_CALLBACK (gpk_update_viewer_treeview_update_toggled), NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer,
					    "active", GPK_UPDATES_COLUMN_SELECT);
	gtk_tree_view_column_add_attribute (column, renderer,
					    "activatable", GPK_UPDATES_COLUMN_CLICKABLE);
	gtk_tree_view_column_add_attribute (column, renderer,
					    "sensitive", GPK_UPDATES_COLUMN_SENSITIVE);
	gtk_tree_view_column_add_attribute (column, renderer,
					    "visible", GPK_UPDATES_COLUMN_VISIBLE);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer,
		      "wrap-mode", PANGO_WRAP_WORD,
		      "ellipsize", PANGO_ELLIPSIZE_END,
		      "xpad", 3,
		      NULL);
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_add_attribute (column, renderer,
					    "markup", GPK_UPDATES_COLUMN_TEXT);

	gtk_tree_view_append_column (treeview, column);
	g_signal_connect (treeview, "size-allocate",
			  G_CALLBACK (gpk_update_viewer_treeview_updates_size_allocate_cb),
			  renderer);

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
	renderer = gtk_cell_renderer_spinner_new ();
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
	gtk_tree_view_column_set_expand (GTK_TREE_VIEW_COLUMN (column), FALSE);
	gtk_tree_view_column_set_sort_column_id (column, GPK_UPDATES_COLUMN_SIZE_DISPLAY);

	/* size */
	renderer = gpk_cell_renderer_size_new ();
	gtk_style_context_get_color (gtk_widget_get_style_context (GTK_WIDGET (treeview)),
				     GTK_STATE_FLAG_INSENSITIVE,
				     &inactive);
	g_object_set (renderer,
		      "alignment", PANGO_ALIGN_RIGHT,
		      "xalign", 1.0f,
		      "foreground-rgba", &inactive,
		      NULL);
	g_object_set (renderer,
		      "value", GPK_UPDATES_COLUMN_SIZE_DISPLAY, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "value", GPK_UPDATES_COLUMN_SIZE_DISPLAY);

	gtk_tree_view_append_column (treeview, column);

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

	g_object_set_data (G_OBJECT (column), "tooltip-id", GINT_TO_POINTER (GPK_UPDATES_COLUMN_SIZE_DISPLAY));
}

#if PK_CHECK_VERSION(0,8,1)
/**
 * gpk_update_viewer_add_description_link_item:
 **/
static void
gpk_update_viewer_add_description_link_item (GtkTextBuffer *buffer,
					     GtkTextIter *iter,
					     const gchar *title,
					     gchar **urls)
{
	GtkTextTag *tag;
	gint i;

	/* insert at end */
	gtk_text_buffer_insert_with_tags_by_name (buffer, iter, title, -1, "para", NULL);
	for (i = 0; urls[i] != NULL; i++) {
		gtk_text_buffer_insert (buffer, iter, "\n", -1);
		gtk_text_buffer_insert (buffer, iter, "• ", -1);
		tag = gtk_text_buffer_create_tag (buffer, NULL,
						  "foreground", "blue",
						  "underline", PANGO_UNDERLINE_SINGLE,
						  NULL);
		g_object_set_data (G_OBJECT (tag), "href", g_strdup (urls[i]));
		gtk_text_buffer_insert_with_tags (buffer, iter, urls[i], -1, tag, NULL);
		gtk_text_buffer_insert (buffer, iter, ".", -1);
	}
	gtk_text_buffer_insert (buffer, iter, "\n", -1);
}
#else
/**
 * gpk_update_viewer_add_description_link_item:
 **/
static void
gpk_update_viewer_add_description_link_item (GtkTextBuffer *buffer,
					     GtkTextIter *iter,
					     const gchar *title,
					     const GPtrArray *array)
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
 * gpk_update_viewer_get_uris:
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
		g_warning ("length not correct, correcting");
		length--;
	}

	/* copy into array */
	for (i=0; i<length; i+=2)
		g_ptr_array_add (array, g_strdup (urls[i]));

	return array;
}
#endif

/**
 * gpk_update_viewer_iso8601_format_locale_date:
 **/
static gchar *
gpk_update_viewer_iso8601_format_locale_date (const gchar *iso_date)
{
	GDate *date = NULL;
	GTimeVal timeval;
	gboolean ret;
	gchar *text = NULL;

	/* not valid */
	if (iso_date == NULL || iso_date[0] == '\0')
		goto out;

	/* parse ISO8601 date */
	ret = g_time_val_from_iso8601 (iso_date, &timeval);
	if (!ret) {
		g_warning ("failed to parse %s, falling back to ISO8601", iso_date);
		text = g_strdup (iso_date);
		goto out;
	}

	/* convert to a date object */
	date = g_date_new ();
	g_date_set_time_val (date, &timeval);

	/* pretty print it */
	text = g_new0 (gchar, 100);
	g_date_strftime (text, 100, "%x", date);
out:
	if (date != NULL)
		g_date_free (date);
	return text;
}

/**
 * gpk_update_viewer_populate_details:
 **/
static void
gpk_update_viewer_populate_details (PkUpdateDetail *item)
{
	GtkTreeView *treeview;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter treeiter;
	PkInfoEnum info;
	gchar *line;
	gchar *line2;
	const gchar *title;
	GtkTextIter iter;
	gboolean has_update_text = FALSE;
	gchar *package_id;
#if PK_CHECK_VERSION(0,8,1)
	gchar **vendor_urls;
	gchar **bugzilla_urls;
	gchar **cve_urls;
#else
	GPtrArray *array;
	gchar *vendor_url;
	gchar *bugzilla_url;
	gchar *cve_url;
#endif
	PkRestartEnum restart;
	gchar *update_text;
	gchar *changelog;
	PkUpdateStateEnum state;
	gchar *issued;
	gchar *updated;
	gchar *issued_locale = NULL;
	gchar *updated_locale = NULL;

	/* get data */
	g_object_get (item,
		      "package-id", &package_id,
#if PK_CHECK_VERSION(0,8,1)
		      "vendor-urls", &vendor_urls,
		      "bugzilla-urls", &bugzilla_urls,
		      "cve-urls", &cve_urls,
#else
		      "vendor-url", &vendor_url,
		      "bugzilla-url", &bugzilla_url,
		      "cve-url", &cve_url,
#endif
		      "restart", &restart,
		      "update-text", &update_text,
		      "changelog", &changelog,
		      "state", &state,
		      "issued", &issued,
		      "updated", &updated,
		      NULL);

	/* get info  */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
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
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("This update will add new features and expand functionality."), -1, "para", NULL);
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

	/* convert ISO time to locale time */
	issued_locale = gpk_update_viewer_iso8601_format_locale_date (issued);
	updated_locale = gpk_update_viewer_iso8601_format_locale_date (updated);

	/* issued and updated */
	if (issued_locale != NULL && updated_locale != NULL) {

		/* TRANSLATORS: this is when the notification was issued and then updated */
		line = g_strdup_printf (_("This notification was issued on %s and last updated on %s."), issued_locale, updated_locale);
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, line, -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
		g_free (line);
	} else if (issued_locale != NULL) {

		/* TRANSLATORS: this is when the update was issued */
		line = g_strdup_printf (_("This notification was issued on %s."), issued_locale);
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, line, -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
		g_free (line);
	}

	/* update text */
	if (!egg_strzero (update_text)) {
		/* convert the bullets */
		line = egg_markdown_parse (markdown, update_text);
		if (!egg_strzero (line)) {
			gtk_text_buffer_insert_markup (text_buffer, &iter, line);
			gtk_text_buffer_insert (text_buffer, &iter, "\n\n", -1);
			has_update_text = TRUE;
		}
		g_free (line);
	}

#if PK_CHECK_VERSION(0,8,1)
	/* add all the links */
	if (vendor_urls != NULL) {
		/* TRANSLATORS: this is a array of vendor URLs */
		title = ngettext ("For more information about this update please visit this website:",
				  "For more information about this update please visit these websites:",
				  g_strv_length (vendor_urls));
		gpk_update_viewer_add_description_link_item (text_buffer, &iter, title, vendor_urls);
	}
	if (bugzilla_urls != NULL) {
		/* TRANSLATORS: this is a array of bugzilla URLs */
		title = ngettext ("For more information about bugs fixed by this update please visit this website:",
				  "For more information about bugs fixed by this update please visit these websites:",
				  g_strv_length (bugzilla_urls));
		gpk_update_viewer_add_description_link_item (text_buffer, &iter, title, bugzilla_urls);
	}
	if (cve_urls != NULL) {
		/* TRANSLATORS: this is a array of CVE (security) URLs */
		title = ngettext ("For more information about this security update please visit this website:",
				  "For more information about this security update please visit these websites:",
				  g_strv_length (cve_urls));
		gpk_update_viewer_add_description_link_item (text_buffer, &iter, title, cve_urls);
	}
#else
	/* add all the links */
	if (!egg_strzero (vendor_url)) {
		array = gpk_update_viewer_get_uris (vendor_url);
		/* TRANSLATORS: this is a array of vendor URLs */
		title = ngettext ("For more information about this update please visit this website:",
				  "For more information about this update please visit these websites:", array->len);
		gpk_update_viewer_add_description_link_item (text_buffer, &iter, title, array);
		g_ptr_array_unref (array);
	}
	if (!egg_strzero (bugzilla_url)) {
		array = gpk_update_viewer_get_uris (bugzilla_url);
		/* TRANSLATORS: this is a array of bugzilla URLs */
		title = ngettext ("For more information about bugs fixed by this update please visit this website:",
				  "For more information about bugs fixed by this update please visit these websites:", array->len);
		gpk_update_viewer_add_description_link_item (text_buffer, &iter, title, array);
		g_ptr_array_unref (array);
	}
	if (!egg_strzero (cve_url)) {
		array = gpk_update_viewer_get_uris (cve_url);
		/* TRANSLATORS: this is a array of CVE (security) URLs */
		title = ngettext ("For more information about this security update please visit this website:",
				  "For more information about this security update please visit these websites:", array->len);
		gpk_update_viewer_add_description_link_item (text_buffer, &iter, title, array);
		g_ptr_array_unref (array);
	}
#endif
	/* reboot */
	if (restart == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: reboot required */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("The computer will have to be restarted after the update for the changes to take effect."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	} else if (restart == PK_RESTART_ENUM_SESSION) {
		/* TRANSLATORS: log out required */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("You will need to log out and back in after the update for the changes to take effect."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	}

	/* state */
	if (state == PK_UPDATE_STATE_ENUM_UNSTABLE) {
		/* TRANSLATORS: this is the stability status of the update */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("The classification of this update is unstable which means it is not designed for production use."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	} else if (state == PK_UPDATE_STATE_ENUM_TESTING) {
		/* TRANSLATORS: this is the stability status of the update */
		gtk_text_buffer_insert_with_tags_by_name (text_buffer, &iter, _("This is a test update, and is not designed for normal use. Please report any problems or regressions you encounter."), -1, "para", NULL);
		gtk_text_buffer_insert (text_buffer, &iter, "\n", -1);
	}

	/* only show changelog if we didn't have any update text */
	if (!has_update_text && !egg_strzero (changelog)) {
		line = egg_markdown_parse (markdown, changelog);
		if (!egg_strzero (line)) {
			/* TRANSLATORS: this is a ChangeLog */
			line2 = g_strdup_printf ("%s\n%s\n", _("The developer logs will be shown as no description is available for this update:"), line);
			gtk_text_buffer_insert_markup (text_buffer, &iter, line2);
			g_free (line2);
		}
		g_free (line);
	}

	g_free (package_id);
#if PK_CHECK_VERSION(0,8,1)
	g_strfreev (vendor_urls);
	g_strfreev (bugzilla_urls);
	g_strfreev (cve_urls);
#else
	g_free (vendor_url);
	g_free (bugzilla_url);
	g_free (cve_url);
#endif
	g_free (update_text);
	g_free (changelog);
	g_free (issued);
	g_free (updated);
	g_free (issued_locale);
	g_free (updated_locale);
}

/**
 * gpk_packages_treeview_clicked_cb:
 **/
static void
gpk_packages_treeview_clicked_cb (GtkTreeSelection *selection, gpointer user_data)
{
	gboolean ret;
	gchar *package_id = NULL;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkWidget *widget;
	PkUpdateDetail *item = NULL;

	/* This will only work in single or browse selection mode! */
	ret = gtk_tree_selection_get_selected (selection, &model, &iter);
	if (!ret)
		goto out;

	gtk_tree_model_get (model, &iter,
			    GPK_UPDATES_COLUMN_UPDATE_DETAIL_OBJ, &item,
			    GPK_UPDATES_COLUMN_ID, &package_id, -1);

	/* make 'Details' insensitive' */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "expander1"));
	gtk_widget_set_sensitive (widget, package_id != NULL);

	/* set loading text */
	if (item != NULL) {
		g_debug ("selected row is: %s, %p", package_id, item);
		gtk_text_buffer_set_text (text_buffer, _("Loading…"), -1);
		gpk_update_viewer_populate_details (item);
	} else {
		gtk_text_buffer_set_text (text_buffer, _("No update details available."), -1);
	}
out:
	g_free (package_id);
}

/**
 * gpk_update_viewer_get_details_cb:
 **/
static void
gpk_update_viewer_get_details_cb (PkClient *client, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array = NULL;
	PkDetails *item;
	guint i;
	guint64 size;
	gchar *package_id;
	GtkWidget *widget;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeView *treeview;
	GtkTreeIter iter;
	PkError *error_code = NULL;
	GtkWindow *window;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		/* TRANSLATORS: the PackageKit request did not complete, and it did not send an error */
		gpk_update_viewer_error_dialog (_("Could not get update details"), NULL, error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get details: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		window = GTK_WINDOW(gtk_builder_get_object (builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
					gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* get data */
	array = pk_results_get_details_array (results);
	if (array->len == 0) {
		/* TRANSLATORS: PackageKit did not send any results for the query... */
		gpk_update_viewer_error_dialog (_("Could not get package details"), _("No results were returned."), NULL);
		goto out;
	}

	/* set data */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* get data */
		g_object_get (item,
			      "package-id", &package_id,
			      "size", &size,
			      NULL);

		path = gpk_update_viewer_model_get_path (model, package_id);
		if (path == NULL) {
			g_debug ("not found ID for details");
		} else {
			gtk_tree_model_get_iter (model, &iter, path);
			gtk_tree_path_free (path);
			gtk_tree_store_set (array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_DETAILS_OBJ, (gpointer) g_object_ref (item),
					    GPK_UPDATES_COLUMN_SIZE, (gint)size,
					    GPK_UPDATES_COLUMN_SIZE_DISPLAY, (gint)size,
					    -1);
			/* in cache */
			if (size == 0)
				gtk_tree_store_set (array_store_updates, &iter,
						    GPK_UPDATES_COLUMN_STATUS, GPK_INFO_ENUM_DOWNLOADED, -1);
		}
		g_free (package_id);
	}

	/* select the first entry in the updates array now we've got data */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "treeview_updates"));
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(widget));
	gtk_tree_selection_unselect_all (selection);
	path = gtk_tree_path_new_first ();
	gtk_tree_selection_select_path (selection, path);
	gtk_tree_path_free (path);

	/* set info */
	gpk_update_viewer_reconsider_info ();
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_update_viewer_get_update_detail_cb:
 **/
static void
gpk_update_viewer_get_update_detail_cb (PkClient *client, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array = NULL;
	PkUpdateDetail *item;
	guint i;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	PkError *error_code = NULL;
	GtkWindow *window;
	gchar *package_id;
	PkRestartEnum restart;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		/* TRANSLATORS: the PackageKit request did not complete, and it did not send an error */
		gpk_update_viewer_error_dialog (_("Could not get update details"), NULL, error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get update details: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		window = GTK_WINDOW(gtk_builder_get_object (builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
					gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* get data */
	array = pk_results_get_update_detail_array (results);
	if (array->len == 0) {
		/* TRANSLATORS: PackageKit did not send any results for the query... */
		gpk_update_viewer_error_dialog (_("Could not get update details"), _("No results were returned."), NULL);
		goto out;
	}

	/* add data */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* get data */
		g_object_get (item,
			      "package-id", &package_id,
			      "restart", &restart,
			      NULL);

		path = gpk_update_viewer_model_get_path (model, package_id);
		if (path == NULL) {
			g_warning ("not found ID for update detail");
		} else {
			gtk_tree_model_get_iter (model, &iter, path);
			gtk_tree_path_free (path);
			gtk_tree_store_set (array_store_updates, &iter,
					    GPK_UPDATES_COLUMN_UPDATE_DETAIL_OBJ, (gpointer) g_object_ref (item),
					    GPK_UPDATES_COLUMN_RESTART, restart, -1);
		}
		g_free (package_id);
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
 * gpk_update_viewer_repo_array_changed_cb:
 **/
static void
gpk_update_viewer_repo_array_changed_cb (PkClient *client, gpointer user_data)
{
	gpk_update_viewer_get_new_update_array ();
}

/**
 * gpk_update_viewer_detail_popup_menu_select_all:
 **/
static void
gpk_update_viewer_detail_popup_menu_select_all (GtkWidget *menuitem, gpointer user_data)
{
	GtkTreeView *treeview;
	gboolean valid;
	GtkTreeIter iter;
	GtkTreeModel *model;
	gboolean child_valid;
	GtkTreeIter child_iter;
	PkInfoEnum info;

	/* get the first iter in the array */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_INFO, &info, -1);
		if (info != PK_INFO_ENUM_BLOCKED)
			gtk_tree_store_set (GTK_TREE_STORE(model), &iter,
					    GPK_UPDATES_COLUMN_SELECT, TRUE, -1);

		/* do for children too */
		child_valid = gtk_tree_model_iter_children (model, &child_iter, &iter);
		while (child_valid) {
			gtk_tree_store_set (GTK_TREE_STORE(model), &child_iter,
					    GPK_UPDATES_COLUMN_SELECT, TRUE, -1);
			child_valid = gtk_tree_model_iter_next (model, &child_iter);
		}

		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* if there are no entries selected, deselect the button */
	gpk_update_viewer_reconsider_info ();
}

/**
 * gpk_update_viewer_detail_popup_menu_select_security:
 **/
static void
gpk_update_viewer_detail_popup_menu_select_security (GtkWidget *menuitem, gpointer user_data)
{
	GtkTreeView *treeview;
	gboolean valid;
	gboolean ret;
	GtkTreeIter iter;
	GtkTreeModel *model;
	gboolean child_valid;
	GtkTreeIter child_iter;
	PkInfoEnum info;

	/* get the first iter in the array */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter, GPK_UPDATES_COLUMN_INFO, &info, -1);
		ret = (info == PK_INFO_ENUM_SECURITY);
		gtk_tree_store_set (GTK_TREE_STORE(model), &iter,
				    GPK_UPDATES_COLUMN_SELECT, ret, -1);

		/* do for children too */
		child_valid = gtk_tree_model_iter_children (model, &child_iter, &iter);
		while (child_valid) {
			gtk_tree_model_get (model, &child_iter, GPK_UPDATES_COLUMN_INFO, &info, -1);
			ret = (info == PK_INFO_ENUM_SECURITY);
			gtk_tree_store_set (GTK_TREE_STORE(model), &child_iter,
					    GPK_UPDATES_COLUMN_SELECT, ret, -1);
			child_valid = gtk_tree_model_iter_next (model, &child_iter);
		}

		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* if there are no entries selected, deselect the button */
	gpk_update_viewer_reconsider_info ();
}

/**
 * gpk_update_viewer_detail_popup_menu_select_none:
 **/
static void
gpk_update_viewer_detail_popup_menu_select_none (GtkWidget *menuitem, gpointer user_data)
{
	GtkTreeView *treeview;
	gboolean valid;
	GtkTreeIter iter;
	gboolean child_valid;
	GtkTreeIter child_iter;
	GtkTreeModel *model;

	/* get the first iter in the array */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter, -1);
		gtk_tree_store_set (GTK_TREE_STORE(model), &iter,
				    GPK_UPDATES_COLUMN_SELECT, FALSE, -1);

		/* do for children too */
		child_valid = gtk_tree_model_iter_children (model, &child_iter, &iter);
		while (child_valid) {
			gtk_tree_store_set (GTK_TREE_STORE(model), &child_iter,
					    GPK_UPDATES_COLUMN_SELECT, FALSE, -1);
			child_valid = gtk_tree_model_iter_next (model, &child_iter);
		}

		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* if there are no entries selected, deselect the button */
	gpk_update_viewer_reconsider_info ();
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
	gboolean child_valid;
	GtkTreeIter child_iter;

	/* get the first iter in the array */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
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

		/* do for children too */
		child_valid = gtk_tree_model_iter_children (model, &child_iter, &iter);
		while (child_valid) {
			gtk_tree_model_get (model, &child_iter,
					    GPK_UPDATES_COLUMN_SELECT, &update,
					    GPK_UPDATES_COLUMN_CLICKABLE, &clickable, -1);
			if (update)
				*none_checked = FALSE;
			else
				*all_checked = FALSE;
			child_valid = gtk_tree_model_iter_next (model, &child_iter);
		}

		valid = gtk_tree_model_iter_next (model, &iter);
	}
	return clickable;
}

/**
 * gpk_update_viewer_detail_popup_menu_create:
 **/
static void
gpk_update_viewer_detail_popup_menu_create (GtkWidget *treeview, GdkEventButton *event, gpointer user_data)
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
		g_debug ("ignoring as we are locked down");
		return;
	}

	if (!all_checked) {
		/* TRANSLATORS: right click menu, select all the updates */
		menuitem = gtk_menu_item_new_with_label (_("Select all"));
		g_signal_connect (menuitem, "activate",
				  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_all), NULL);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	}

	if (!none_checked) {
		/* TRANSLATORS: right click menu, unselect all the updates */
		menuitem = gtk_menu_item_new_with_label (_("Unselect all"));
		g_signal_connect (menuitem, "activate",
				  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_none), NULL);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	}

	/* TRANSLATORS: right click menu, select only security updates */
	menuitem = gtk_menu_item_new_with_label (_("Select security updates"));
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_security), NULL);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);

	/* TRANSLATORS: right click option, ignore this update name, not currently used */
	menuitem = gtk_menu_item_new_with_label (_("Ignore this update"));
	gtk_widget_set_sensitive (GTK_WIDGET(menuitem), FALSE);
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_all), NULL);
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
gpk_update_viewer_detail_button_pressed (GtkWidget *treeview, GdkEventButton *event, gpointer user_data)
{
	GtkTreeSelection *selection;
	GtkTreePath *path;

	/* single click with the right mouse button? */
	if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
		/* we did not handle this */
		return FALSE;
	}

	g_debug ("Single right click on the tree view");

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
	gpk_update_viewer_detail_popup_menu_create (treeview, event, NULL);
	return TRUE;
}

/**
 * gpk_update_viewer_detail_popup_menu:
 **/
static gboolean
gpk_update_viewer_detail_popup_menu (GtkWidget *treeview, gpointer user_data)
{
	gpk_update_viewer_detail_popup_menu_create (treeview, NULL, NULL);
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
	PkPackage *item;
	const gchar *package_id;

	value = g_new0 (gchar *, array->len + 1);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		package_id = pk_package_get_id (item);
		value[i] = g_strdup (package_id);
	}
	return value;
}

/**
 * gpk_update_viewer_get_updates_cb:
 **/
static void
gpk_update_viewer_get_updates_cb (PkClient *client, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	PkPackageSack *sack = NULL;
	GError *error = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_messages = NULL;
	PkPackage *item;
	gchar *text = NULL;
	gboolean selected;
	gboolean sensitive;
	GtkTreeIter iter;
	GtkTreeIter parent;
	guint i;
	gchar **package_ids;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkWidget *widget;
	PkError *error_code = NULL;
	GtkWindow *window;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *summary = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		/* TRANSLATORS: the PackageKit request did not complete, and it did not send an error */
		gpk_update_viewer_error_dialog (_("Could not get updates"), NULL, error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get updates: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		window = GTK_WINDOW(gtk_builder_get_object (builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
					gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* get data */
	sack = pk_results_get_package_sack (results);
	pk_package_sack_sort (sack, PK_PACKAGE_SACK_SORT_TYPE_NAME);
	array = pk_package_sack_get_array (sack);
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "treeview_updates"));
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* get data */
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);

		/* find our parent */
		gpk_update_viewer_get_parent_for_info (info, &parent);

		/* add to array store */
		text = gpk_package_id_format_twoline (gtk_widget_get_style_context (widget),
						      package_id,
						      summary);
		g_debug ("adding: id=%s, text=%s", package_id, text);
		selected = (info != PK_INFO_ENUM_BLOCKED);

		/* only make the checkbox selectable if:
		 *  - we can do UpdatePackages rather than just UpdateSystem
		 *  - the update is not blocked
		 */
		sensitive = selected;
		if (!pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_PACKAGES))
			sensitive = FALSE;

		/* add to model */
		gtk_tree_store_append (array_store_updates, &iter, &parent);
		gtk_tree_store_set (array_store_updates, &iter,
				    GPK_UPDATES_COLUMN_TEXT, text,
				    GPK_UPDATES_COLUMN_ID, package_id,
				    GPK_UPDATES_COLUMN_INFO, info,
				    GPK_UPDATES_COLUMN_SELECT, selected,
				    GPK_UPDATES_COLUMN_SENSITIVE, sensitive,
				    GPK_UPDATES_COLUMN_VISIBLE, TRUE,
				    GPK_UPDATES_COLUMN_CLICKABLE, selected,
				    GPK_UPDATES_COLUMN_RESTART, PK_RESTART_ENUM_NONE,
				    GPK_UPDATES_COLUMN_STATUS, PK_INFO_ENUM_UNKNOWN,
				    GPK_UPDATES_COLUMN_SIZE, 0,
				    GPK_UPDATES_COLUMN_SIZE_DISPLAY, 0,
				    GPK_UPDATES_COLUMN_PERCENTAGE, 0,
				    GPK_UPDATES_COLUMN_PULSE, -1,
				    -1);
		g_free (text);
		g_free (package_id);
		g_free (summary);
	}

	/* get the download sizes */
	if (update_array != NULL)
		g_ptr_array_unref (update_array);
	update_array = pk_results_get_package_array (results);

	/* sort by name */
	treeview = GTK_TREE_VIEW(gtk_builder_get_object (builder, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
					      GPK_UPDATES_COLUMN_INFO,
					      GTK_SORT_DESCENDING);
	gtk_tree_view_expand_all (treeview);

	/* get the download sizes */
	if (update_array->len > 0) {
		package_ids = gpk_update_viewer_packages_to_ids (array);

		/* get the details of all the packages */
		pk_client_get_update_detail_async (PK_CLIENT(task), package_ids, cancellable,
						   (PkProgressCallback) gpk_update_viewer_progress_cb, NULL,
						   (GAsyncReadyCallback) gpk_update_viewer_get_update_detail_cb, NULL);

		/* get the details of all the packages */
		pk_client_get_details_async (PK_CLIENT(task), package_ids, cancellable,
					     (PkProgressCallback) gpk_update_viewer_progress_cb, NULL,
					     (GAsyncReadyCallback) gpk_update_viewer_get_details_cb, NULL);

		g_strfreev (package_ids);
	}

	/* are now able to do action */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_install"));
	gtk_widget_set_sensitive (widget, TRUE);

	/* set info */
	gpk_update_viewer_reconsider_info ();

out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (array_messages != NULL)
		g_ptr_array_unref (array_messages);
	if (sack != NULL)
		g_object_unref (sack);
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
	gtk_tree_store_clear (array_store_updates);
	gtk_text_buffer_set_text (text_buffer, "", -1);

	widget = GTK_WIDGET(gtk_builder_get_object (builder, "label_header_title"));
	/* TRANSLATORS: this is the header */
	text = g_strdup_printf ("<big><b>%s</b></big>", _("Checking for updates…"));
	gtk_label_set_label (GTK_LABEL(widget), text);

	/* only show newest updates? */
	ret = g_settings_get_boolean (settings, GPK_SETTINGS_ONLY_NEWEST);
	if (ret) {
		g_debug ("only showing newest updates");
		filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST, -1);
	}

	/* get new array */
	pk_client_get_updates_async (PK_CLIENT(task), filter, cancellable,
				     (PkProgressCallback) gpk_update_viewer_progress_cb, NULL,
				     (GAsyncReadyCallback) gpk_update_viewer_get_updates_cb, NULL);
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
		case GDK_KEY_Return:
		case GDK_KEY_KP_Enter:
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
		g_object_unref (cursor);
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
	GdkDevice *device;

	window = gtk_widget_get_window (text_view);
	device = gdk_event_get_device ((const GdkEvent *) event);
	if (device == NULL)
		goto out;
	gdk_window_get_device_position (window,
					device,
					&wx, &wy,
					NULL);

	gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (text_view), GTK_TEXT_WINDOW_WIDGET, wx, wy, &bx, &by);
	gpk_update_viewer_textview_set_cursor (GTK_TEXT_VIEW (text_view), bx, by);
out:
	return FALSE;
}

/**
 * gpk_update_viewer_updates_changed_cb:
 **/
static void
gpk_update_viewer_updates_changed_cb (PkControl *_control, gpointer user_data)
{
	/* now try to get newest update array */
	g_debug ("updates changed");
	if (ignore_updates_changed) {
		g_debug ("ignoring");
		return;
	}
	gpk_update_viewer_get_new_update_array ();
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
static PkDistroUpgrade *
gpk_update_viewer_get_distro_upgrades_best (GPtrArray *array)
{
	PkDistroUpgrade *item;
	guint i;
	PkDistroUpgradeEnum state;

	/* find a stable update */
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);

		/* get data */
		g_object_get (item,
			      "state", &state,
			      NULL);

		if (state == PK_DISTRO_UPGRADE_ENUM_STABLE)
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
gpk_update_viewer_get_distro_upgrades_cb (PkClient *client, GAsyncResult *res, gpointer user_data)
{
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array = NULL;
	PkDistroUpgrade *item;
	gchar *text = NULL;
	gchar *text_format = NULL;
	gchar *summary = NULL;
	GtkWidget *widget;
	PkError *error_code = NULL;
	GtkWindow *window;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		/* TRANSLATORS: the PackageKit request did not complete, and it did not send an error */
		gpk_update_viewer_error_dialog (_("Could not get list of distribution upgrades"), NULL, error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get list of distro upgrades: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		window = GTK_WINDOW(gtk_builder_get_object (builder, "dialog_updates"));
		gpk_error_dialog_modal (window, gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
					gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* get data */
	array = pk_results_get_distro_upgrade_array (results);
	item = gpk_update_viewer_get_distro_upgrades_best (array);
	if (item == NULL)
		goto out;

	/* get data */
	g_object_get (item,
		      "summary", &summary,
		      NULL);

	/* only display last (newest) distro */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "label_upgrade"));
	/* TRANSLATORS: new distro available, e.g. F9 to F10 */
	text = g_strdup_printf (_("New distribution upgrade release '%s' is available"), summary);
	text_format = g_strdup_printf ("<b>%s</b>", text);
	gtk_label_set_label (GTK_LABEL(widget), text_format);

	widget = GTK_WIDGET(gtk_builder_get_object (builder, "viewport_upgrade"));
	gtk_widget_show (widget);

	/* don't autoclose when upgrades */
	have_available_distro_upgrades = TRUE;

	/* get model */
	gpk_update_viewer_reconsider_info ();
out:
	g_free (summary);
	g_free (text);
	g_free (text_format);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_update_viewer_get_properties_cb:
 **/
static void
gpk_update_viewer_get_properties_cb (PkControl *_control, GAsyncResult *res, gpointer user_data)
{
	GError *error = NULL;
	gboolean ret;

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	if (!ret) {
		/* TRANSLATORS: backend is broken, and won't tell us what it supports */
		g_print ("%s: %s\n", _("Exiting as backend details could not be retrieved"), error->message);
		g_error_free (error);
		gpk_update_viewer_quit ();
		goto out;
	}

	/* get values */
	g_object_get (control,
		      "roles", &roles,
		      NULL);

	/* get the distro-upgrades if we support it */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		pk_client_get_distro_upgrades_async (PK_CLIENT(task), cancellable,
						     (PkProgressCallback) gpk_update_viewer_progress_cb, NULL,
						     (GAsyncReadyCallback) gpk_update_viewer_get_distro_upgrades_cb, NULL);
	}
out:
	return;
}

/**
 * gpk_update_viewer_notify_network_state_cb:
 **/
static void
gpk_update_viewer_notify_network_state_cb (PkControl *_control, GParamSpec *pspec, gpointer user_data)
{
	gpk_update_viewer_check_mobile_broadband ();
	gpk_update_viewer_get_new_update_array ();
}

/**
 * gpk_update_viewer_activate_cb:
 **/
static void
gpk_update_viewer_activate_cb (GtkApplication *_application, gpointer user_data)
{
	GtkWindow *window;
	window = GTK_WINDOW(gtk_builder_get_object (builder, "dialog_updates"));
	gtk_window_present (window);
}

/**
 * gpk_update_viewer_application_startup_cb:
 **/
static void
gpk_update_viewer_application_startup_cb (GtkApplication *_application, gpointer user_data)
{
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkWidget *label;
	GtkTreeSelection *selection;
	gboolean ret;
	guint retval;
	GError *error = NULL;

	auto_shutdown_id = 0;
	size_total = 0;
	ignore_updates_changed = FALSE;
	restart_update = PK_RESTART_ENUM_NONE;

	settings = g_settings_new (GPK_SETTINGS_SCHEMA);
#ifdef HAVE_SYSTEMD
	proxy = systemd_proxy_new ();
#endif
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
	g_signal_connect (task, "notify::idle",
			  G_CALLBACK (gpk_update_viewer_client_notify_idle_cb), NULL);
	g_object_set (task,
		      "background", FALSE,
		      NULL);

	/* get properties */
	pk_control_get_properties_async (control, NULL, (GAsyncReadyCallback) gpk_update_viewer_get_properties_cb, NULL);

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-update-viewer.ui", &error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out;
	}

	main_window = GTK_WIDGET(gtk_builder_get_object (builder, "dialog_updates"));
	gtk_window_set_icon_name (GTK_WINDOW(main_window), GPK_ICON_SOFTWARE_UPDATE);
	gtk_application_add_window (application, GTK_WINDOW(main_window));

	/* create array stores */
	array_store_updates = gtk_tree_store_new (GPK_UPDATES_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT,
						 G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
						 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
						 G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_INT, G_TYPE_BOOLEAN);
	text_buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_create_tag (text_buffer, "para",
				    "pixels_above_lines", 5,
				    "wrap-mode", GTK_WRAP_WORD,
				    NULL);
	gtk_text_buffer_create_tag (text_buffer, "important",
				    "weight", PANGO_WEIGHT_BOLD,
				    NULL);

	/* no upgrades yet */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "viewport_upgrade"));
	gtk_widget_hide (widget);

	/* description */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "textview_details"));
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), text_buffer);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (widget), FALSE);
	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (widget), 5);
	g_signal_connect (GTK_TEXT_VIEW (widget), "key-press-event", G_CALLBACK (gpk_update_viewer_textview_key_press_event), NULL);
	g_signal_connect (GTK_TEXT_VIEW (widget), "event-after", G_CALLBACK (gpk_update_viewer_textview_event_after), NULL);
	g_signal_connect (GTK_TEXT_VIEW (widget), "motion-notify-event", G_CALLBACK (gpk_update_viewer_textview_motion_notify_event), NULL);
	g_signal_connect (GTK_TEXT_VIEW (widget), "visibility-notify-event", G_CALLBACK (gpk_update_viewer_textview_visibility_notify_event), NULL);

	/* updates */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "treeview_updates"));
	gtk_tree_view_set_search_column (GTK_TREE_VIEW(widget), GPK_UPDATES_COLUMN_TEXT);
	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW(widget), gpk_update_viewer_search_equal_func, NULL, NULL);
	gtk_tree_view_set_level_indentation (GTK_TREE_VIEW(widget), 3);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW(widget), FALSE);
	gtk_tree_view_set_show_expanders (GTK_TREE_VIEW(widget), FALSE);
	gtk_tree_view_set_model (GTK_TREE_VIEW(widget),
				 GTK_TREE_MODEL (array_store_updates));
	gpk_update_viewer_treeview_add_columns_update (GTK_TREE_VIEW(widget));
	g_signal_connect (widget, "popup-menu",
			  G_CALLBACK (gpk_update_viewer_detail_popup_menu), NULL);
	g_signal_connect (widget, "button-press-event",
			  G_CALLBACK (gpk_update_viewer_detail_button_pressed), NULL);

	/* selection */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(widget));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_packages_treeview_clicked_cb), NULL);

	/* bottom UI */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "progressbar_progress"));
	gtk_widget_hide (widget);
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "label_summary"));
	gtk_widget_hide (widget);

	/* set install button insensitive */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_install"));
	gtk_widget_set_sensitive (widget, FALSE);
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_install_cb), NULL);

	/* sensitive */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "scrolledwindow_updates"));
	gtk_widget_set_sensitive (widget, FALSE);
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "scrolledwindow_details"));
	gtk_widget_set_sensitive (widget, FALSE);

	/* close button */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_quit"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_quit_cb), NULL);
	gtk_window_set_focus (GTK_WINDOW(main_window), widget);

	/* upgrade button */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "button_upgrade"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_upgrade_cb), NULL);

	/* set a size, as much as the screen allows */
	ret = gpk_window_set_size_request (GTK_WINDOW(main_window), 700, 1200);
	if (!ret) {
		g_debug ("small form factor mode");
		/* hide the header in SFF mode;
		FIXME: this doesn't work because most window managers simply allocate
		the maximum height without returning False, so this is never called */
		widget = GTK_WIDGET(gtk_builder_get_object (builder, "hbox_header"));
		gtk_widget_hide (widget);
	}

	/* use correct status pane */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "label_status"));
	gtk_widget_set_size_request (widget, -1, 32);
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "label_info"));
	gtk_widget_set_size_request (widget, -1, 32);

	/* add info bars: TODO, fix glade to put these in the ui file */
	info_mobile = gtk_info_bar_new ();
	gtk_widget_set_no_show_all (info_mobile, TRUE);
	info_updates = gtk_info_bar_new ();
	gtk_widget_set_no_show_all (info_updates, TRUE);

	/* pack label into infobar */
	info_mobile_label = gtk_label_new ("");
	widget = gtk_info_bar_get_content_area (GTK_INFO_BAR(info_mobile));
	gtk_container_add (GTK_CONTAINER(widget), info_mobile_label);
	gtk_widget_show (info_mobile_label);

	/* TRANSLATORS: this is when some updates are not being shown as other packages need updating first */
	label = gtk_label_new (_("Other updates are held back as some important system packages need to be installed first."));
	widget = gtk_info_bar_get_content_area (GTK_INFO_BAR(info_updates));
	gtk_container_add (GTK_CONTAINER(widget), label);
	gtk_widget_show (label);

	/* pack infobars into main UI */
	widget = GTK_WIDGET(gtk_builder_get_object (builder, "vbox1"));
	gtk_box_pack_start (GTK_BOX(widget), info_mobile, FALSE, FALSE, 3);
	gtk_box_reorder_child (GTK_BOX(widget), info_mobile, 1);
	gtk_box_pack_start (GTK_BOX(widget), info_updates, FALSE, FALSE, 3);

	/* show window */
	gtk_widget_show (main_window);
out:
	return;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean program_version = FALSE;
	GOptionContext *context;
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

	dbus_g_thread_init ();
	gtk_init (&argc, &argv);

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("Update Packages"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gpk_debug_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* TRANSLATORS: title to pass to the user if there are not enough privs */
	ret = gpk_check_privileged_user (_("Package Updater"), TRUE);
	if (!ret)
		return 1;

	/* are we already activated? */
	application = gtk_application_new ("org.freedesktop.PackageKit.UpdateViewer", 0);
	g_signal_connect (application, "startup",
			  G_CALLBACK (gpk_update_viewer_application_startup_cb), NULL);
	g_signal_connect (application, "activate",
			  G_CALLBACK (gpk_update_viewer_activate_cb), NULL);

	/* run */
	status = g_application_run (G_APPLICATION (application), argc, argv);

	/* we might have visual stuff running, close it down */
	g_cancellable_cancel (cancellable);

	/* remove auto-shutdown */
	if (auto_shutdown_id != 0)
		g_source_remove (auto_shutdown_id);

	if (update_array != NULL)
		g_ptr_array_unref (update_array);
	g_free (package_id_last);
	if (array_store_updates != NULL)
		g_object_unref (array_store_updates);
	if (builder != NULL)
		g_object_unref (builder);
	if (cancellable != NULL)
		g_object_unref (cancellable);
#ifdef HAVE_SYSTEMD
	if (proxy != NULL)
		systemd_proxy_free (proxy);
#endif
	if (control != NULL)
		g_object_unref (control);
	if (settings != NULL)
		g_object_unref (settings);
	if (markdown != NULL)
		g_object_unref (markdown);
	if (task != NULL)
		g_object_unref (task);
	if (text_buffer != NULL)
		g_object_unref (text_buffer);

	g_object_unref (application);
	return status;
}

