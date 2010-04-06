/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include <unistd.h>
#include <string.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <fontconfig/fontconfig.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <libnotify/notify.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-common.h"
#include "gpk-dbus.h"
#include "gpk-dbus-task.h"
#include "gpk-desktop.h"
#include "gpk-dialog.h"
#include "gpk-enum.h"
#include "gpk-error.h"
#include "gpk-gnome.h"
#include "gpk-helper-chooser.h"
#include "gpk-helper-run.h"
#include "gpk-language.h"
#include "gpk-modal-dialog.h"
#include "gpk-task.h"
#include "gpk-vendor.h"
#include "gpk-x11.h"

static void gpk_dbus_task_finalize (GObject *object);
static void gpk_dbus_task_progress_cb (PkProgress *progress, PkProgressType type, GpkDbusTask *dtask);

#define GPK_DBUS_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_DBUS_TASK, GpkDbusTaskPrivate))
#define GPK_DBUS_TASK_FINISHED_AUTOCLOSE_DELAY	10 /* seconds */

/**
 * GpkDbusTaskPrivate:
 *
 * Private #GpkDbusTask data
 **/
struct _GpkDbusTaskPrivate
{
	GdkWindow		*parent_window;
	GConfClient		*gconf_client;
	PkTask			*task;
	PkDesktop		*desktop;
	PkControl		*control;
	PkExitEnum		 exit;
	PkBitfield		 roles;
	GpkLanguage		*language;
	GpkModalDialog		*dialog;
	GpkVendor		*vendor;
	gboolean		 show_confirm_search;
	gboolean		 show_confirm_deps;
	gboolean		 show_confirm_install;
	gboolean		 show_progress;
	gboolean		 show_finished;
	gboolean		 show_warning;
	guint			 timestamp;
	gchar			*parent_title;
	gchar			*parent_icon_name;
	gchar			*exec;
	PkError			*cached_error_code;
	gint			 timeout;
	GpkHelperRun		*helper_run;
	GpkHelperChooser	*helper_chooser;
	DBusGMethodInvocation	*context;
	gchar			**package_ids;
	gchar			**files;
	GCancellable		*cancellable;
	PkCatalog		*catalog;
	GpkDbusTaskFinishedCb	 finished_cb;
	gpointer		 finished_userdata;
};

G_DEFINE_TYPE (GpkDbusTask, gpk_dbus_task, G_TYPE_OBJECT)

/**
 * gpk_dbus_task_set_interaction:
 **/
gboolean
gpk_dbus_task_set_interaction (GpkDbusTask *dtask, PkBitfield interact)
{
	g_return_val_if_fail (GPK_IS_DBUS_TASK (dtask), FALSE);

	dtask->priv->show_confirm_search = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_CONFIRM_SEARCH);
	dtask->priv->show_confirm_deps = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_CONFIRM_DEPS);
	dtask->priv->show_confirm_install = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_CONFIRM_INSTALL);
	dtask->priv->show_progress = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_PROGRESS);
	dtask->priv->show_finished = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_FINISHED);
	dtask->priv->show_warning = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_WARNING);

	/* debug */
	egg_debug ("confirm_search:%i, confirm_deps:%i, confirm_install:%i, progress:%i, finished:%i, warning:%i",
		   dtask->priv->show_confirm_search, dtask->priv->show_confirm_deps,
		   dtask->priv->show_confirm_install, dtask->priv->show_progress,
		   dtask->priv->show_finished, dtask->priv->show_warning);

	return TRUE;
}

/**
 * gpk_dbus_task_set_context:
 **/
gboolean
gpk_dbus_task_set_context (GpkDbusTask *dtask, DBusGMethodInvocation *context)
{
	g_return_val_if_fail (GPK_IS_DBUS_TASK (dtask), FALSE);
	g_return_val_if_fail (context != NULL, FALSE);

	dtask->priv->context = context;
	return TRUE;
}

/**
 * gpk_dbus_task_set_xid:
 **/
gboolean
gpk_dbus_task_set_xid (GpkDbusTask *dtask, guint32 xid)
{
	GdkDisplay *display;
	g_return_val_if_fail (GPK_IS_DBUS_TASK (dtask), FALSE);

	display = gdk_display_get_default ();
	dtask->priv->parent_window = gdk_window_foreign_new_for_display (display, xid);
	egg_debug ("parent_window=%p", dtask->priv->parent_window);
	gpk_modal_dialog_set_parent (dtask->priv->dialog, dtask->priv->parent_window);
	return TRUE;
}

/**
 * gpk_dbus_task_set_timestamp:
 **/
gboolean
gpk_dbus_task_set_timestamp (GpkDbusTask *dtask, guint32 timestamp)
{
	g_return_val_if_fail (GPK_IS_DBUS_TASK (dtask), FALSE);
	dtask->priv->timestamp = timestamp;
	return TRUE;
}

static void gpk_dbus_task_install_package_ids (GpkDbusTask *dtask);

/**
 * gpk_dbus_task_dbus_return_error:
 **/
static void
gpk_dbus_task_dbus_return_error (GpkDbusTask *dtask, const GError *error)
{
	g_return_if_fail (error != NULL);

	/* already sent or never setup */
	if (dtask->priv->context == NULL) {
		egg_error ("context does not exist, cannot return: %s", error->message);
		goto out;
	}

	/* send error */
	egg_debug ("sending async return error in response to %p: %s", dtask->priv->context, error->message);
	dbus_g_method_return_error (dtask->priv->context, error);

	/* set context NULL just in case we try to repeat */
	dtask->priv->context = NULL;

	/* do the finish callback */
	if (dtask->priv->finished_cb)
		dtask->priv->finished_cb (dtask, dtask->priv->finished_userdata);
out:
	/* we can't touch dtask now, as it might have been unreffed in the finished callback */
	return;
}

/**
 * gpk_dbus_task_dbus_return_value:
 **/
static void
gpk_dbus_task_dbus_return_value (GpkDbusTask *dtask, gboolean ret)
{
	/* already sent or never setup */
	if (dtask->priv->context == NULL) {
		egg_error ("context does not exist, cannot return %i", ret);
		goto out;
	}

	/* send error */
	egg_debug ("sending async return in response to %p: %i", dtask->priv->context, ret);
	dbus_g_method_return (dtask->priv->context, ret);

	/* set context NULL just in case we try to repeat */
	dtask->priv->context = NULL;

	/* do the finish callback */
	if (dtask->priv->finished_cb)
		dtask->priv->finished_cb (dtask, dtask->priv->finished_userdata);
out:
	/* we can't touch dtask now, as it might have been unreffed in the finished callback */
	return;
}

/**
 * gpk_dbus_task_chooser_event_cb:
 **/
static void
gpk_dbus_task_chooser_event_cb (GpkHelperChooser *helper_chooser, GtkResponseType type, const gchar *package_id, GpkDbusTask *dtask)
{
	GError *error_dbus = NULL;

	/* selected nothing */
	if (type != GTK_RESPONSE_YES || package_id == NULL) {

		/* failed */
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not choose anything to install");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);

		if (dtask->priv->show_warning) {
			gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			/* TRANSLATORS: we failed to install */
			gpk_modal_dialog_set_title (dtask->priv->dialog, _("Failed to install software"));
			/* TRANSLATORS: we didn't select any applications that were returned */
			gpk_modal_dialog_set_message (dtask->priv->dialog, _("No applications were chosen to be installed"));
			gpk_modal_dialog_present (dtask->priv->dialog);
			gpk_modal_dialog_run (dtask->priv->dialog);
		}
		goto out;
	}

	/* install this specific package */
	dtask->priv->package_ids = pk_package_ids_from_id (package_id);

	/* install these packages with deps */
	gpk_dbus_task_install_package_ids (dtask);
out:
	return;
}

/**
 * gpk_dbus_task_libnotify_cb:
 **/
static void
gpk_dbus_task_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data)
{
	GpkDbusTask *task = GPK_DBUS_TASK (data);
	gchar *details;

	if (task->priv->cached_error_code == NULL) {
		egg_warning ("called show error with no error!");
		return;
	}
	if (g_strcmp0 (action, "show-error-details") == 0) {
		details = g_markup_escape_text (pk_error_get_details (task->priv->cached_error_code), -1);
		/* TRANSLATORS: detailed text about the error */
		gpk_error_dialog (_("Error details"), _("Package Manager error details"), details);
		g_free (details);
	} else {
		egg_warning ("unknown action id: %s", action);
	}
}

/**
 * gpk_dbus_task_error_msg:
 **/
static void
gpk_dbus_task_error_msg (GpkDbusTask *dtask, const gchar *title, GError *error)
{
	GtkWindow *window;
	/* TRANSLATORS: default fallback error -- this should never happen */
	const gchar *message = _("Unknown error. Please refer to the detailed report and report in your distribution bugtracker.");
	const gchar *details = NULL;

	if (!dtask->priv->show_warning)
		return;

	/* setup UI */
	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);

	/* print a proper error if we have it */
	if (error != NULL) {
		if (error->code == PK_CLIENT_ERROR_DECLINED_SIMULATION)
			return;
		if (error->code == PK_CLIENT_ERROR_FAILED_AUTH ||
		    g_str_has_prefix (error->message, "org.freedesktop.packagekit.")) {
			/* TRANSLATORS: failed authentication */
			message = _("You don't have the necessary privileges to perform this action.");
			gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-permissions");
		} else if (error->code == PK_CLIENT_ERROR_CANNOT_START_DAEMON) {
			/* TRANSLATORS: could not start system service */
			message = _("The packagekitd service could not be started.");
			gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-no-service");
		} else if (error->code == PK_CLIENT_ERROR_INVALID_INPUT) {
			/* TRANSLATORS: the user tried to query for something invalid */
			message = _("The query is not valid.");
			details = error->message;
		} else if (error->code == PK_CLIENT_ERROR_INVALID_FILE) {
			/* TRANSLATORS: the user tried to install a file that was not compatable or broken */
			message = _("The file is not valid.");
			details = error->message;
		} else {
			details = error->message;
		}
	}

	/* it's a normal UI, not a backtrace so keep in the UI */
	if (details == NULL) {
		gpk_modal_dialog_set_title (dtask->priv->dialog, title);
		gpk_modal_dialog_set_message (dtask->priv->dialog, message);
		gpk_modal_dialog_present_with_time (dtask->priv->dialog, dtask->priv->timestamp);
		gpk_modal_dialog_run (dtask->priv->dialog);
		return;
	}

	/* hide the main window */
	window = gpk_modal_dialog_get_window (dtask->priv->dialog);
	gpk_error_dialog_modal_with_time (window, title, message, details, dtask->priv->timestamp);
}

/**
 * gpk_dbus_task_handle_error:
 **/
static void
gpk_dbus_task_handle_error (GpkDbusTask *dtask, PkError *error_code)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *title;
	const gchar *message;
	NotifyNotification *notification;
	GtkWidget *widget;

	/* ignore some errors */
	if (pk_error_get_code (error_code) == PK_ERROR_ENUM_NO_LICENSE_AGREEMENT ||
	    pk_error_get_code (error_code) == PK_ERROR_ENUM_PROCESS_KILL ||
	    pk_error_get_code (error_code) == PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
		egg_warning ("ignoring %s", pk_error_enum_to_text (pk_error_get_code (error_code)));
	}

	egg_debug ("code was %s", pk_error_enum_to_text (pk_error_get_code (error_code)));

	/* use a modal dialog if showing progress, else use libnotify */
	title = gpk_error_enum_to_localised_text (pk_error_get_code (error_code));
	message = gpk_error_enum_to_localised_message (pk_error_get_code (error_code));
	if (dtask->priv->show_progress) {
		widget = GTK_WIDGET (gpk_modal_dialog_get_window (dtask->priv->dialog));
		gpk_error_dialog_modal (GTK_WINDOW (widget), title, message, pk_error_get_details (error_code));
		return;
	}

	/* save this globally */
	if (dtask->priv->cached_error_code != NULL)
		g_object_unref (dtask->priv->cached_error_code);
	dtask->priv->cached_error_code = g_object_ref (error_code);

	/* do the bubble */
	notification = notify_notification_new (title, message, "help-browser", NULL);
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, "show-error-details",
					/* TRANSLATORS: button: show details about the error */
					_("Show details"), gpk_dbus_task_libnotify_cb, dtask, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_dbus_task_get_code_from_gerror:
 **/
static gint
gpk_dbus_task_get_code_from_gerror (const GError *error)
{
	gint code = GPK_DBUS_ERROR_INTERNAL_ERROR;

	if (error->domain != PK_CLIENT_ERROR) {
		egg_error ("Not a PkError error code");
		goto out;
	}

	/* standard return codes */
	switch (error->code) {
	case PK_CLIENT_ERROR_NO_TID:
	case PK_CLIENT_ERROR_ALREADY_TID:
	case PK_CLIENT_ERROR_ROLE_UNKNOWN:
	case PK_CLIENT_ERROR_CANNOT_START_DAEMON:
	case PK_CLIENT_ERROR_NOT_SUPPORTED:
		code = GPK_DBUS_ERROR_INTERNAL_ERROR;
		break;
	case PK_CLIENT_ERROR_INVALID_INPUT:
	case PK_CLIENT_ERROR_INVALID_FILE:
	case PK_CLIENT_ERROR_FAILED:
		code = GPK_DBUS_ERROR_FAILED;
		break;
	case PK_CLIENT_ERROR_DECLINED_SIMULATION:
		code = GPK_DBUS_ERROR_CANCELLED;
		break;
	case PK_CLIENT_ERROR_FAILED_AUTH:
		code = GPK_DBUS_ERROR_FORBIDDEN;
		break;
	default:
		break;
	}
out:
	return code;
}

/**
 * gpk_dbus_task_get_code_from_pkerror:
 **/
static gint
gpk_dbus_task_get_code_from_pkerror (PkError *error_code)
{
	gint code = GPK_DBUS_ERROR_FAILED;

	switch (pk_error_get_code (error_code)) {
	case PK_ERROR_ENUM_TRANSACTION_CANCELLED:
		code = GPK_DBUS_ERROR_CANCELLED;
		break;
	default:
		break;
	}
	return code;
}

/**
 * gpk_dbus_task_install_packages_cb:
 **/
static void
gpk_dbus_task_install_packages_cb (PkTask *task, GAsyncResult *res, GpkDbusTask *dtask)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	PkResults *results = NULL;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		/* TRANSLATORS: error: failed to install, detailed error follows */
		gpk_dbus_task_error_msg (dtask, _("Failed to install package"), error);
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "%s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error);
		g_error_free (error_dbus);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to install package: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_pkerror (error_code), "%s", pk_error_get_details (error_code));
		gpk_dbus_task_handle_error (dtask, error_code);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* we're done */
	gpk_dbus_task_dbus_return_value (dtask, TRUE);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_dbus_task_install_package_ids:
 * @task: a valid #GpkDbusTask instance
 **/
static void
gpk_dbus_task_install_package_ids (GpkDbusTask *dtask)
{
	GtkWindow *window;
	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, GPK_MODAL_DIALOG_PACKAGE_PADDING);
	/* TRANSLATORS: title: installing packages */
	gpk_modal_dialog_set_title (dtask->priv->dialog, _("Installing packages"));
	if (dtask->priv->show_progress)
		gpk_modal_dialog_present (dtask->priv->dialog);

	/* ensure parent is set */
	window = gpk_modal_dialog_get_window (dtask->priv->dialog);
	gpk_task_set_parent_window (GPK_TASK (dtask->priv->task), window);

	/* install async */
	pk_task_install_packages_async (dtask->priv->task, dtask->priv->package_ids, NULL,
					(PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
					(GAsyncReadyCallback) gpk_dbus_task_install_packages_cb, dtask);
}

/**
 * gpk_dbus_task_set_status:
 **/
static gboolean
gpk_dbus_task_set_status (GpkDbusTask *dtask, PkStatusEnum status)
{
	g_return_val_if_fail (GPK_IS_DBUS_TASK (dtask), FALSE);

	/* do we force progress? */
	if (!dtask->priv->show_progress) {
		if (status == PK_STATUS_ENUM_DOWNLOAD_REPOSITORY ||
		    status == PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST ||
		    status == PK_STATUS_ENUM_DOWNLOAD_FILELIST ||
		    status == PK_STATUS_ENUM_DOWNLOAD_CHANGELOG ||
		    status == PK_STATUS_ENUM_DOWNLOAD_GROUP ||
		    status == PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO ||
		    status == PK_STATUS_ENUM_REFRESH_CACHE) {
			gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
			gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-progress");
			gpk_modal_dialog_present_with_time (dtask->priv->dialog, 0);
		}
	}

	/* ignore */
	if (!dtask->priv->show_progress) {
		egg_warning ("not showing progress");
		return FALSE;
	}

	/* set icon */
	gpk_modal_dialog_set_image_status (dtask->priv->dialog, status);

	/* set label */
	gpk_modal_dialog_set_title (dtask->priv->dialog, gpk_status_enum_to_localised_text (status));

	/* spin */
	if (status == PK_STATUS_ENUM_WAIT)
		gpk_modal_dialog_set_percentage (dtask->priv->dialog, -1);

	/* do visual stuff when finished */
	if (status == PK_STATUS_ENUM_FINISHED) {
		/* make insensitive */
		gpk_modal_dialog_set_allow_cancel (dtask->priv->dialog, FALSE);

		/* stop spinning */
		gpk_modal_dialog_set_percentage (dtask->priv->dialog, 100);
	}
	return TRUE;
}

/**
 * gpk_dbus_task_button_close_cb:
 **/
static void
gpk_dbus_task_button_close_cb (GtkWidget *widget, GpkDbusTask *dtask)
{
	/* close, don't abort */
	gpk_modal_dialog_close (dtask->priv->dialog);
}

/**
 * gpk_dbus_task_button_cancel_cb:
 **/
static void
gpk_dbus_task_button_cancel_cb (GtkWidget *widget, GpkDbusTask *dtask)
{
	/* we might have a transaction running */
	g_cancellable_cancel (dtask->priv->cancellable);
}

/**
 * gpk_dbus_task_install_files_cb:
 **/
static void
gpk_dbus_task_install_files_cb (PkTask *task, GAsyncResult *res, GpkDbusTask *dtask)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	PkResults *results = NULL;
	guint length;
	const gchar *title;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		/* TRANSLATORS: error: failed to install, detailed error follows */
		length = g_strv_length (dtask->priv->files);
		title = ngettext ("Failed to install file", "Failed to install files", length);
		gpk_dbus_task_error_msg (dtask, title, error);
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "%s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error);
		g_error_free (error_dbus);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to install file: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		gpk_dbus_task_handle_error (dtask, error_code);
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_pkerror (error_code), "%s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* we're done */
	gpk_dbus_task_dbus_return_value (dtask, TRUE);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_dbus_task_ptr_array_to_bullets:
 *
 * splits the strings up nicely
 *
 * Return value: a newly allocated string
 **/
static gchar *
gpk_dbus_task_ptr_array_to_bullets (GPtrArray *array, const gchar *prefix)
{
	GString *string;
	guint i;
	gchar *text;

	/* don't use a bullet for one item */
	if (array->len == 1) {
		if (prefix != NULL)
			return g_strdup_printf ("%s\n\n%s", prefix, (const gchar *) g_ptr_array_index (array, 0));
		else
			return g_strdup (g_ptr_array_index (array, 0));
	}

	string = g_string_new (prefix);
	if (prefix != NULL)
		g_string_append (string, "\n\n");

	/* prefix with bullet and suffix with newline */
	for (i=0; i<array->len; i++) {
		text = (gchar *) g_ptr_array_index (array, i);
		g_string_append_printf (string, "• %s\n", text);
	}

	/* remove last \n */
	g_string_set_size (string, string->len - 1);

	text = g_string_free (string, FALSE);
	return text;
}

/**
 * gpk_dbus_task_install_package_files_verify:
 *
 * Allow the user to confirm the action
 *
 * Return value: %TRUE if the method succeeded
 **/
static gboolean
gpk_dbus_task_install_package_files_verify (GpkDbusTask *dtask, GPtrArray *array, GError **error)
{
	GtkResponseType button;
	const gchar *title;
	gchar *message;
	gboolean ret = TRUE;

	/* TRANSLATORS: title: confirm the user want's to install a local file */
	title = ngettext ("Do you want to install this file?",
			  "Do you want to install these files?", array->len);
	message = gpk_dbus_task_ptr_array_to_bullets (array, NULL);

	/* show UI */
	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, 0);
	gpk_modal_dialog_set_title (dtask->priv->dialog, title);
	gpk_modal_dialog_set_message (dtask->priv->dialog, message);
	/* TRANSLATORS: title: installing local files */
	gpk_modal_dialog_set_action (dtask->priv->dialog, _("Install"));
	gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-install-files");
	gpk_modal_dialog_present_with_time (dtask->priv->dialog, dtask->priv->timestamp);
	button = gpk_modal_dialog_run (dtask->priv->dialog);
	g_free (message);

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK) {
		g_set_error_literal (error, GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "Aborted");
		ret = FALSE;
		goto out;
	}
out:
	return ret;
}

/**
 * gpk_dbus_task_confirm_action:
 * @task: a valid #GpkDbusTask instance
 **/
static gboolean
gpk_dbus_task_confirm_action (GpkDbusTask *dtask, const gchar *title, const gchar *message, const gchar *action)
{
	GtkResponseType button;

	/* check the user wanted to call this method */
	if (!dtask->priv->show_confirm_search)
		return TRUE;

	/* setup UI */
	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, 0);
	gpk_modal_dialog_set_action (dtask->priv->dialog, action);

	/* set icon */
	if (dtask->priv->parent_icon_name != NULL)
		gpk_modal_dialog_set_image (dtask->priv->dialog, dtask->priv->parent_icon_name);
	else
		gpk_modal_dialog_set_image (dtask->priv->dialog, "emblem-system");

	gpk_modal_dialog_set_title (dtask->priv->dialog, title);
	gpk_modal_dialog_set_message (dtask->priv->dialog, message);
	gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-application-confirm");
	gpk_modal_dialog_present_with_time (dtask->priv->dialog, dtask->priv->timestamp);
	button = gpk_modal_dialog_run (dtask->priv->dialog);

	/* close, we're going to fail the method */
	if (button != GTK_RESPONSE_OK) {
		gpk_modal_dialog_close (dtask->priv->dialog);
		return FALSE;
	}

	return TRUE;
}

/**
 * gpk_dbus_task_progress_cb:
 **/
static void
gpk_dbus_task_progress_cb (PkProgress *progress, PkProgressType type, GpkDbusTask *dtask)
{
	gboolean allow_cancel;
	gint percentage;
	guint remaining_time;
	PkStatusEnum status;
	gchar *package_id = NULL;
	gchar *text;

	/* optimise */
	if (!dtask->priv->show_progress)
		goto out;

	g_object_get (progress,
		      "allow-cancel", &allow_cancel,
		      "percentage", &percentage,
		      "remaining-time", &remaining_time,
		      "status", &status,
		      "package-id", &package_id,
		      NULL);

	if (type == PK_PROGRESS_TYPE_PACKAGE_ID) {
		egg_debug ("_package");
	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		gpk_modal_dialog_set_percentage (dtask->priv->dialog, percentage);
	} else if (type == PK_PROGRESS_TYPE_ALLOW_CANCEL) {
		gpk_modal_dialog_set_allow_cancel (dtask->priv->dialog, allow_cancel);
	} else if (type == PK_PROGRESS_TYPE_STATUS) {
		gpk_dbus_task_set_status (dtask, status);

		if (status == PK_STATUS_ENUM_FINISHED) {
			/* stop spinning */
			gpk_modal_dialog_set_percentage (dtask->priv->dialog, 100);
		}
	} else if (type == PK_PROGRESS_TYPE_REMAINING_TIME) {
		gpk_modal_dialog_set_remaining (dtask->priv->dialog, remaining_time);
	} else if (type == PK_PROGRESS_TYPE_PACKAGE_ID) {
		text = gpk_package_id_format_twoline (package_id, NULL); //TODO: need summary
		gpk_modal_dialog_set_message (dtask->priv->dialog, text);
		g_free (text);
	}
out:
	g_free (package_id);
}

/**
 * gpk_dbus_task_is_installed_resolve_cb:
 **/
static void
gpk_dbus_task_is_installed_resolve_cb (PkClient *client, GAsyncResult *res, GpkDbusTask *dtask)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	PkResults *results = NULL;
	GPtrArray *array = NULL;
	gboolean ret;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "failed to resolve: %s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		egg_warning ("failed to resolve: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to resolve: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_pkerror (error_code), "failed to resolve: %s", pk_error_get_details (error_code));
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* get results */
	array = pk_results_get_package_array (results);
	ret = (array->len > 0);
	gpk_dbus_task_dbus_return_value (dtask, ret);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_dbus_task_is_installed:
 **/
void
gpk_dbus_task_is_installed (GpkDbusTask *dtask, const gchar *package_name, GpkDbusTaskFinishedCb finished_cb, gpointer userdata)
{
	gchar **package_names = NULL;

	g_return_if_fail (GPK_IS_DBUS_TASK (dtask));
	g_return_if_fail (package_name != NULL);

	/* save callback information */
	dtask->priv->finished_cb = finished_cb;
	dtask->priv->finished_userdata = userdata;

	/* get the package list for the installed packages */
	package_names = g_strsplit (package_name, "|", 1);
	pk_client_resolve_async (PK_CLIENT(dtask->priv->task), pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_names, NULL,
				 (PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
				 (GAsyncReadyCallback) gpk_dbus_task_is_installed_resolve_cb, dtask);
	g_strfreev (package_names);
}

/**
 * gpk_dbus_task_search_file_search_file_cb:
 **/
static void
gpk_dbus_task_search_file_search_file_cb (PkClient *client, GAsyncResult *res, GpkDbusTask *dtask)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	PkResults *results = NULL;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;
	gchar **split = NULL;
	PkPackage *item;
	PkInfoEnum info;
	gchar *package_id = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "failed to search file: %s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		egg_warning ("failed to resolve: %s", error->message);
		g_error_free (error);
		g_error_free (error_dbus);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to resolve: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_pkerror (error_code), "failed to search file: %s", pk_error_get_details (error_code));
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* get results */
	array = pk_results_get_package_array (results);
	if (array->len == 0) {
		egg_warning ("no packages");
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "failed to find any packages");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* get first item */
	item = g_ptr_array_index (array, 0);
	g_object_get (item,
		      "info", &info,
		      "package-id", &package_id,
		      NULL);
	split = pk_package_id_split (package_id);

	/* send error */
	egg_debug ("sending async return in response to %p", dtask->priv->context);
	dbus_g_method_return (dtask->priv->context, (info == PK_INFO_ENUM_INSTALLED), split[PK_PACKAGE_ID_NAME]);

	/* set context NULL just in case we try to repeat */
	dtask->priv->context = NULL;
out:
	g_free (package_id);
	g_strfreev (split);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_dbus_task_search_file:
 **/
void
gpk_dbus_task_search_file (GpkDbusTask *dtask, const gchar *search_file, GpkDbusTaskFinishedCb finished_cb, gpointer userdata)
{
	gchar **values = NULL;

	g_return_if_fail (GPK_IS_DBUS_TASK (dtask));
	g_return_if_fail (search_file != NULL);

	/* save callback information */
	dtask->priv->finished_cb = finished_cb;
	dtask->priv->finished_userdata = userdata;

	/* get the package list for the installed packages */
	egg_debug ("package_name=%s", search_file);
	values = g_strsplit (search_file, "&", -1);
	pk_client_search_files_async (PK_CLIENT(dtask->priv->task), pk_bitfield_value (PK_FILTER_ENUM_NEWEST), values, NULL,
				     (PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
				     (GAsyncReadyCallback) gpk_dbus_task_search_file_search_file_cb, dtask);
	g_strfreev (values);
}

/**
 * gpk_dbus_task_install_package_files:
 * @task: a valid #GpkDbusTask instance
 * @file_rel: a file such as <literal>./hal-devel-0.10.0.rpm</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a file locally, and get the deps from the repositories.
 * This is useful for double clicking on a .rpm or .deb file.
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_package_files (GpkDbusTask *dtask, gchar **files_rel, GpkDbusTaskFinishedCb finished_cb, gpointer userdata)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	gboolean ret;
	GPtrArray *array;
	guint len;

	g_return_if_fail (GPK_IS_DBUS_TASK (dtask));
	g_return_if_fail (files_rel != NULL);

	/* save callback information */
	dtask->priv->finished_cb = finished_cb;
	dtask->priv->finished_userdata = userdata;

	array = pk_strv_to_ptr_array (files_rel);

	/* check the user wanted to call this method */
	if (dtask->priv->show_confirm_search) {
		ret = gpk_dbus_task_install_package_files_verify (dtask, array, &error);
		if (!ret) {
			error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "failed to verify files: %s", error->message);
			gpk_dbus_task_dbus_return_error (dtask, error_dbus);
			g_error_free (error);
			g_error_free (error_dbus);
			goto out;
		}
	}

	/* check for deps */
	dtask->priv->files = pk_ptr_array_to_strv (array);

	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	len = g_strv_length (dtask->priv->files);
	/* TRANSLATORS: title: installing a local file */
	gpk_modal_dialog_set_title (dtask->priv->dialog, ngettext ("Install local file", "Install local files", len));
	if (dtask->priv->show_progress)
		gpk_modal_dialog_present_with_time (dtask->priv->dialog, dtask->priv->timestamp);

	/* install async */
	pk_task_install_files_async (dtask->priv->task, dtask->priv->files, NULL,
				     (PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
				     (GAsyncReadyCallback) gpk_dbus_task_install_files_cb, dtask);

	/* wait for async reply */
out:
	g_ptr_array_unref (array);
}

/**
 * gpk_dbus_task_install_package_names_resolve_cb:
 **/
static void
gpk_dbus_task_install_package_names_resolve_cb (PkTask *task, GAsyncResult *res, GpkDbusTask *dtask, GpkDbusTaskFinishedCb finished_cb, gpointer userdata)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	PkResults *results = NULL;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;
	gchar *package_id = NULL;
	gchar *title;
	gchar *info_url;
	PkPackage *item;
	GtkResponseType button;
	guint i;
	gboolean already_installed = FALSE;
	PkInfoEnum info;
	gchar *package_id_tmp = NULL;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "failed to resolve: %s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error);
		g_error_free (error_dbus);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_pkerror (error_code), "failed to resolve: %s", pk_error_get_details (error_code));
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* found nothing? */
	array = pk_results_get_package_array (results);
	if (array->len == 0) {
		if (!dtask->priv->show_warning) {
			/* TRANSLATORS: couldn't resolve name to package */
			title = g_strdup_printf (_("Could not find packages"));
			info_url = gpk_vendor_get_not_found_url (dtask->priv->vendor, GPK_VENDOR_URL_TYPE_DEFAULT);
			/* only show the "more info" button if there is a valid link */
			if (info_url != NULL)
				gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, GPK_MODAL_DIALOG_BUTTON_ACTION);
			else
				gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			gpk_modal_dialog_set_title (dtask->priv->dialog, title);
			/* TRANSLATORS: message: could not find */
			gpk_modal_dialog_set_message (dtask->priv->dialog, _("The packages could not be found in any software source"));
			gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-package-not-found");
			/* TRANSLATORS: button: a link to the help file */
			gpk_modal_dialog_set_action (dtask->priv->dialog, _("More information"));
			gpk_modal_dialog_present (dtask->priv->dialog);
			button = gpk_modal_dialog_run (dtask->priv->dialog);
			if (button == GTK_RESPONSE_OK)
				gpk_gnome_open (info_url);
			g_free (info_url);
			g_free (title);
		}
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_NO_PACKAGES_FOUND, "no package found");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* see what we've got already */
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id_tmp,
			      NULL);
		if (info == PK_INFO_ENUM_INSTALLED) {
			already_installed = TRUE;
		} else if (info == PK_INFO_ENUM_AVAILABLE) {
			egg_debug ("package '%s' resolved", package_id_tmp);
			package_id = g_strdup (package_id_tmp);
			//TODO: we need to list these in a gpk-dbus_task-chooser
		}
		g_free (package_id_tmp);
	}

	/* already installed? */
	if (already_installed) {
		if (dtask->priv->show_warning) {
			/* TRANSLATORS: title: package is already installed */
			gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			gpk_modal_dialog_set_title (dtask->priv->dialog, _("Failed to install packages"));
			/* TRANSLATORS: message: package is already installed */
			gpk_modal_dialog_set_message (dtask->priv->dialog, _("The package is already installed"));
			gpk_modal_dialog_present (dtask->priv->dialog);
			gpk_modal_dialog_run (dtask->priv->dialog);
		}
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "package already found");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* got junk? */
	if (package_id == NULL) {
		if (dtask->priv->show_warning) {
			gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			/* TRANSLATORS: failed to install, shouldn't be shown */
			gpk_modal_dialog_set_title (dtask->priv->dialog, _("Failed to install package"));
			/* TRANSLATORS: the search gave us the wrong result. internal error. barf. */
			gpk_modal_dialog_set_message (dtask->priv->dialog, _("Incorrect response from search"));
			gpk_modal_dialog_present (dtask->priv->dialog);
			gpk_modal_dialog_run (dtask->priv->dialog);
		}
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "incorrect response from search");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* convert to data */
	dtask->priv->package_ids = pk_package_array_to_strv (array);

	/* install these packages with deps */
	gpk_dbus_task_install_package_ids (dtask);
out:
	g_free (package_id);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_dbus_task_install_package_names:
 * @task: a valid #GpkDbusTask instance
 * @package: a pakage name such as <literal>hal-info</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a package of the newest and most correct version.
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_package_names (GpkDbusTask *dtask, gchar **packages, GpkDbusTaskFinishedCb finished_cb, gpointer userdata)
{
	gboolean ret;
	GError *error_dbus = NULL;
	gchar *message;
	gchar *text;
	guint len;
	guint i;
	GString *string;

	g_return_if_fail (GPK_IS_DBUS_TASK (dtask));
	g_return_if_fail (packages != NULL);

	/* save callback information */
	dtask->priv->finished_cb = finished_cb;
	dtask->priv->finished_userdata = userdata;

	/* optional */
	if (!dtask->priv->show_confirm_install) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	string = g_string_new ("");
	len = g_strv_length (packages);

	/* don't use a bullet for one item */
	if (len == 1) {
		g_string_append_printf (string, "%s\n", packages[0]);
	} else {
		for (i=0; i<len; i++)
			g_string_append_printf (string, "• %s\n", packages[i]);
	}
	/* display messagebox  */
	text = g_string_free (string, FALSE);

	/* check user wanted operation */
	message = g_strdup_printf ("%s\n\n%s\n%s",
				   /* TRANSLATORS: a program needs a package, for instance openoffice-clipart */
				   ngettext ("An additional package is required:", "Additional packages are required:", len),
				   text,
				   /* TRANSLATORS: ask the user if it's okay to search */
				   ngettext ("Do you want to search for and install this package now?", "Do you want to search for and install these packages now?", len));
	g_free (text);

	/* make title using application name */
	if (dtask->priv->parent_title != NULL) {
		/* TRANSLATORS: string is a program name, e.g. "Movie Player" */
		text = g_strdup_printf (ngettext ("%s wants to install a package", "%s wants to install packages", len), dtask->priv->parent_title);
	} else {
		/* TRANSLATORS: a random program which we can't get the name wants to do something */
		text = g_strdup (ngettext ("A program wants to install a package", "A program wants to install packages", len));
	}

	/* TRANSLATORS: button: confirm to search for packages */
	ret = gpk_dbus_task_confirm_action (dtask, text, message, _("Install"));
	g_free (text);
	g_free (message);
	if (!ret) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to search");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

skip_checks:
	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	/* TRANSLATORS: title, searching */
	gpk_modal_dialog_set_title (dtask->priv->dialog, _("Searching for packages"));
	gpk_modal_dialog_set_image_status (dtask->priv->dialog, PK_STATUS_ENUM_WAIT);
	gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-finding-packages");
	if (dtask->priv->show_progress)
		gpk_modal_dialog_present (dtask->priv->dialog);

	/* find out if we can find a package */
	pk_client_resolve_async (PK_CLIENT(dtask->priv->task), pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH, PK_FILTER_ENUM_NEWEST, -1), packages, NULL,
			         (PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
				 (GAsyncReadyCallback) gpk_dbus_task_install_package_names_resolve_cb, dtask);

	/* wait for async reply */
out:
	return;
}

/**
 * gpk_dbus_task_install_provide_files_search_file_cb:
 **/
static void
gpk_dbus_task_install_provide_files_search_file_cb (PkClient *client, GAsyncResult *res, GpkDbusTask *dtask)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	PkResults *results = NULL;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;
	gchar *info_url;
	PkPackage *item;
	GtkResponseType button;
	guint i;
	gboolean already_installed = FALSE;
	gchar *text;
	gchar **split;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *package_id_tmp = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "failed to resolve: %s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error);
		g_error_free (error_dbus);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_pkerror (error_code), "failed to resolve: %s", pk_error_get_details (error_code));
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* get results */
	array = pk_results_get_package_array (results);

	/* found nothing? */
	if (array->len == 0) {
		if (dtask->priv->show_warning) {
			info_url = gpk_vendor_get_not_found_url (dtask->priv->vendor, GPK_VENDOR_URL_TYPE_DEFAULT);
			/* only show the "more info" button if there is a valid link */
			if (info_url != NULL)
				gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, GPK_MODAL_DIALOG_BUTTON_ACTION);
			else
				gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			/* TRANSLATORS: failed to fild the package for thefile */
			gpk_modal_dialog_set_title (dtask->priv->dialog, _("Failed to find package"));
			/* TRANSLATORS: nothing found */
			gpk_modal_dialog_set_message (dtask->priv->dialog, _("The file could not be found in any packages"));
			gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-package-not-found");
			/* TRANSLATORS: button: show the user a button to get more help finding stuff */
			gpk_modal_dialog_set_action (dtask->priv->dialog, _("More information"));
			gpk_modal_dialog_present (dtask->priv->dialog);
			button = gpk_modal_dialog_run (dtask->priv->dialog);
			if (button == GTK_RESPONSE_OK)
				gpk_gnome_open (info_url);
			g_free (info_url);
		}
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_NO_PACKAGES_FOUND, "no files found");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* see what we've got already */
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id_tmp,
			      NULL);
		if (info == PK_INFO_ENUM_INSTALLED) {
			already_installed = TRUE;
			package_id = g_strdup (package_id_tmp);
		} else if (info == PK_INFO_ENUM_AVAILABLE) {
			egg_debug ("package '%s' resolved to:", package_id_tmp);
			package_id = g_strdup (package_id_tmp);
		}
		g_free (package_id_tmp);
	}

	/* already installed? */
	if (already_installed) {
		if (dtask->priv->show_warning) {
			split = pk_package_id_split (package_id);
			/* TRANSLATORS: we've already got a package that provides this file */
			text = g_strdup_printf (_("The %s package already provides this file"), split[PK_PACKAGE_ID_NAME]);
			gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			/* TRANSLATORS: title */
			gpk_modal_dialog_set_title (dtask->priv->dialog, _("Failed to install file"));
			gpk_modal_dialog_set_message (dtask->priv->dialog, text);
			gpk_modal_dialog_present (dtask->priv->dialog);
			gpk_modal_dialog_run (dtask->priv->dialog);
			g_free (text);
			g_strfreev (split);
		}
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "already provided");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* convert to data */
	dtask->priv->package_ids = pk_package_ids_from_id (package_id);

	/* install these packages with deps */
	gpk_dbus_task_install_package_ids (dtask);
out:
	g_free (package_id);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_dbus_task_install_provide_files:
 * @task: a valid #GpkDbusTask instance
 * @full_path: a file path name such as <literal>/usr/sbin/packagekitd</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a package which provides a file on the system.
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_provide_files (GpkDbusTask *dtask, gchar **full_paths, GpkDbusTaskFinishedCb finished_cb, gpointer userdata)
{
	gboolean ret;
	GError *error_dbus = NULL;
	guint len;
	guint i;
	gchar *text;
	gchar *message;
	GString *string;

	g_return_if_fail (GPK_IS_DBUS_TASK (dtask));
	g_return_if_fail (full_paths != NULL);

	/* save callback information */
	dtask->priv->finished_cb = finished_cb;
	dtask->priv->finished_userdata = userdata;

	/* optional */
	if (!dtask->priv->show_confirm_search) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	string = g_string_new ("");
	len = g_strv_length (full_paths);

	/* don't use a bullet for one item */
	if (len == 1) {
		g_string_append_printf (string, "%s\n", full_paths[0]);
	} else {
		for (i=0; i<len; i++)
			g_string_append_printf (string, "• %s\n", full_paths[i]);
	}
	/* display messagebox  */
	text = g_string_free (string, FALSE);

	/* check user wanted operation */
	message = g_strdup_printf ("%s\n\n%s\n\n%s",
				   /* TRANSLATORS: a program wants to install a file, e.g. /lib/moo.so */
				   ngettext ("The following file is required:", "The following files are required:", len),
				   text,
				   /* TRANSLATORS: confirm with the user */
				   ngettext ("Do you want to search for this file now?", "Do you want to search for these files now?", len));

	/* make title using application name */
	if (dtask->priv->parent_title != NULL) {
		/* TRANSLATORS: string is a program name, e.g. "Movie Player" */
		text = g_strdup_printf (ngettext ("%s wants to install a file", "%s wants to install files", len), dtask->priv->parent_title);
	} else {
		/* TRANSLATORS: a random program which we can't get the name wants to do something */
		text = g_strdup (ngettext ("A program wants to install a file", "A program wants to install files", len));
	}

	/* TRANSLATORS: button: confirm to search for packages */
	ret = gpk_dbus_task_confirm_action (dtask, text, message, _("Install"));
	g_free (text);
	g_free (message);
	if (!ret) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to search");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

skip_checks:
	/* TRANSLATORS: searching for the package that provides the file */
	gpk_modal_dialog_set_title (dtask->priv->dialog, _("Searching for file"));
	gpk_modal_dialog_set_image_status (dtask->priv->dialog, PK_STATUS_ENUM_WAIT);

	/* do search */
	pk_client_search_files_async (PK_CLIENT(dtask->priv->task), pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH, PK_FILTER_ENUM_NEWEST, -1), full_paths, NULL,
			             (PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
				     (GAsyncReadyCallback) gpk_dbus_task_install_provide_files_search_file_cb, dtask);

	/* wait for async reply */
out:
	return;
}

/**
 * gpk_dbus_task_install_gstreamer_resources_confirm:
 **/
static gboolean
gpk_dbus_task_install_gstreamer_resources_confirm (GpkDbusTask *dtask, gchar **codec_names)
{
	guint i;
	guint len;
	const gchar *text;
	gchar **parts;
	gboolean ret;
	GString *string;
	gchar *title;
	gchar *message;
	gboolean is_decoder = FALSE;
	gboolean is_encoder = FALSE;

	len = g_strv_length (codec_names);

	/* find out what type of request this is */
	for (i=0; i<len; i++) {
		parts = g_strsplit (codec_names[i], "|", 2);
		if (g_str_has_prefix (parts[1], "gstreamer0.10(decoder"))
			is_decoder = TRUE;
		if (g_str_has_prefix (parts[1], "gstreamer0.10(encoder"))
			is_encoder = TRUE;
		g_strfreev (parts);
	}

	/* TRANSLATORS: we are listing the plugins in a box */
	text = ngettext ("The following plugin is required:", "The following plugins are required:", len);
	string = g_string_new ("");
	g_string_append_printf (string, "%s\n\n", text);

	/* don't use a bullet for one item */
	if (len == 1) {
		parts = g_strsplit (codec_names[0], "|", 2);
		g_string_append_printf (string, "%s\n", parts[0]);
		g_strfreev (parts);
	} else {
		for (i=0; i<len; i++) {
			parts = g_strsplit (codec_names[i], "|", 2);
			g_string_append_printf (string, "• %s\n", parts[0]);
			g_strfreev (parts);
		}
	}

	/* TRANSLATORS: ask for confirmation */
	message = ngettext ("Do you want to search for this now?", "Do you want to search for these now?", len);
	g_string_append_printf (string, "\n%s\n", message);

	/* remove last \n */
	g_string_set_size (string, string->len - 1);

	/* display messagebox  */
	message = g_string_free (string, FALSE);

	/* make title using application name */
	if (dtask->priv->parent_title != NULL) {
		if (is_decoder && !is_encoder) {
			/* TRANSLATORS: a program wants to decode something (unknown) -- string is a program name, e.g. "Movie Player" */
			title = g_strdup_printf (ngettext ("%s requires an additional plugin to decode this file",
							   "%s requires additional plugins to decode this file", len), dtask->priv->parent_title);
		} else if (!is_decoder && is_encoder) {
			/* TRANSLATORS: a program wants to encode something (unknown) -- string is a program name, e.g. "Movie Player" */
			title = g_strdup_printf (ngettext ("%s requires an additional plugin to encode this file",
							   "%s requires additional plugins to encode this file", len), dtask->priv->parent_title);
		} else {
			/* TRANSLATORS: a program wants to do something (unknown) -- string is a program name, e.g. "Movie Player" */
			title = g_strdup_printf (ngettext ("%s requires an additional plugin for this operation",
							   "%s requires additional plugins for this operation", len), dtask->priv->parent_title);
		}
	} else {
		if (is_decoder && !is_encoder) {
			/* TRANSLATORS: a random program which we can't get the name wants to decode something */
			title = g_strdup (ngettext ("A program requires an additional plugin to decode this file",
						    "A program requires additional plugins to decode this file", len));
		} else if (!is_decoder && is_encoder) {
			/* TRANSLATORS: a random program which we can't get the name wants to encode something */
			title = g_strdup (ngettext ("A program requires an additional plugin to encode this file",
						    "A program requires additional plugins to encode this file", len));
		} else {
			/* TRANSLATORS: a random program which we can't get the name wants to do something (unknown) */
			title = g_strdup (ngettext ("A program requires an additional plugin for this operation",
						    "A program requires additional plugins for this operation", len));
		}
	}

	/* TRANSLATORS: button: confirm to search for packages */
	ret = gpk_dbus_task_confirm_action (dtask, title, message, _("Search"));
	g_free (title);
	g_free (message);

	return ret;
}

/**
 * gpk_dbus_task_codec_what_provides_cb:
 **/
static void
gpk_dbus_task_codec_what_provides_cb (PkClient *client, GAsyncResult *res, GpkDbusTask *dtask)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	PkResults *results = NULL;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;
	GtkResponseType button;
	gchar *info_url;
	const gchar *title;
	const gchar *message;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "failed to resolve: %s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error);
		g_error_free (error_dbus);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_pkerror (error_code), "failed to resolve: %s", pk_error_get_details (error_code));
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* get results */
	array = pk_results_get_package_array (results);

	/* found nothing? */
	if (array->len == 0) {
		if (dtask->priv->show_warning) {
			info_url = gpk_vendor_get_not_found_url (dtask->priv->vendor, GPK_VENDOR_URL_TYPE_CODEC);
			/* only show the "more info" button if there is a valid link */
			if (info_url != NULL)
				gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, GPK_MODAL_DIALOG_BUTTON_ACTION);
			else
				gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			/* TRANSLATORS: failed to search for codec */
			gpk_modal_dialog_set_title (dtask->priv->dialog, _("Failed to search for plugin"));
			/* TRANSLATORS: no software sources have the wanted codec */
			gpk_modal_dialog_set_message (dtask->priv->dialog, _("Could not find plugin in any configured software source"));
			gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-package-not-found");

			/* TRANSLATORS: button text */
			gpk_modal_dialog_set_action (dtask->priv->dialog, _("More information"));
			gpk_modal_dialog_present (dtask->priv->dialog);
			button = gpk_modal_dialog_run (dtask->priv->dialog);
			if (button == GTK_RESPONSE_OK)
				gpk_gnome_open (info_url);
			g_free (info_url);
		}
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_NO_PACKAGES_FOUND, "failed to find codec");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* optional */
	if (!dtask->priv->show_confirm_install) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks2;
	}

	title = ngettext ("Install the following plugin", "Install the following plugins", array->len);
	message = ngettext ("Do you want to install this package now?", "Do you want to install these packages now?", array->len);

	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, GPK_MODAL_DIALOG_PACKAGE_LIST);
	gpk_modal_dialog_set_package_list (dtask->priv->dialog, array);
	gpk_modal_dialog_set_title (dtask->priv->dialog, title);
	gpk_modal_dialog_set_message (dtask->priv->dialog, message);
	gpk_modal_dialog_set_image (dtask->priv->dialog, "dialog-information");
	/* TRANSLATORS: button: install codecs */
	gpk_modal_dialog_set_action (dtask->priv->dialog, _("Install"));
	gpk_modal_dialog_present_with_time (dtask->priv->dialog, dtask->priv->timestamp);
	button = gpk_modal_dialog_run (dtask->priv->dialog);

	/* close, we're going to fail the method */
	if (button != GTK_RESPONSE_OK) {
		gpk_modal_dialog_close (dtask->priv->dialog);
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to download");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

skip_checks2:
	/* install with deps */
	dtask->priv->package_ids = pk_package_array_to_strv (array);
	gpk_dbus_task_install_package_ids (dtask);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_dbus_task_install_gstreamer_resources:
 * @task: a valid #GpkDbusTask instance
 * @codecs: a codec_type such as <literal>application/text</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a application to handle a gstreamer request
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_gstreamer_resources (GpkDbusTask *dtask, gchar **codec_names, GpkDbusTaskFinishedCb finished_cb, gpointer userdata)
{
	gboolean ret = TRUE;
	GError *error_dbus = NULL;
	gchar **parts = NULL;
	gchar *message = NULL;
	GPtrArray *array_title = NULL;
	GPtrArray *array_search = NULL;
	gchar **search = NULL;
	gchar **title = NULL;
	gchar *title_str = NULL;
	guint i;

	g_return_if_fail (GPK_IS_DBUS_TASK (dtask));
	g_return_if_fail (codec_names != NULL);

	/* save callback information */
	dtask->priv->finished_cb = finished_cb;
	dtask->priv->finished_userdata = userdata;

	/* check it's not session wide banned in gconf */
	ret = gconf_client_get_bool (dtask->priv->gconf_client, GPK_CONF_ENABLE_CODEC_HELPER, NULL);
	if (!ret) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FORBIDDEN, "not enabled in GConf : %s", GPK_CONF_ENABLE_CODEC_HELPER);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* optional */
	if (!dtask->priv->show_confirm_search) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* confirm */
	ret = gpk_dbus_task_install_gstreamer_resources_confirm (dtask, codec_names);
	if (!ret) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to search");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

skip_checks:
	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, GPK_MODAL_DIALOG_PACKAGE_PADDING);
	/* TRANSLATORS: search for codec */
	gpk_modal_dialog_set_title (dtask->priv->dialog, _("Searching for plugins"));
	gpk_modal_dialog_set_image_status (dtask->priv->dialog, PK_STATUS_ENUM_WAIT);
	gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-finding-packages");

	/* setup the UI */
	if (dtask->priv->show_progress)
		gpk_modal_dialog_present (dtask->priv->dialog);

	/* get the request */
	array_title = g_ptr_array_new_with_free_func (g_free);
	array_search = g_ptr_array_new_with_free_func (g_free);
	for (i=0; codec_names[i] != NULL; i++) {
		parts = g_strsplit (codec_names[i], "|", 2);
		g_ptr_array_add (array_title, g_strdup (parts[0]));
		g_ptr_array_add (array_search, g_strdup (parts[1]));
		g_strfreev (parts);
	}

	/* TRANSLATORS: title, searching for codecs */
	title = pk_ptr_array_to_strv (array_title);
	title_str = g_strjoinv (", ", title);
	message = g_strdup_printf (_("Searching for plugin: %s"), title_str);
	gpk_modal_dialog_set_message (dtask->priv->dialog, message);

	/* get codec packages */
	search = pk_ptr_array_to_strv (array_search);
	pk_client_what_provides_async (PK_CLIENT(dtask->priv->task), pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED, PK_FILTER_ENUM_ARCH, PK_FILTER_ENUM_NEWEST, -1),
				       PK_PROVIDES_ENUM_CODEC, search, NULL,
			               (PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
				       (GAsyncReadyCallback) gpk_dbus_task_codec_what_provides_cb, dtask);
out:
	if (array_title != NULL)
		g_ptr_array_unref (array_title);
	if (array_search != NULL)
		g_ptr_array_unref (array_search);
	g_strfreev (search);
	g_strfreev (title);
	g_free (message);
	g_free (title_str);
}

/**
 * gpk_dbus_task_mime_what_provides_cb:
 **/
static void
gpk_dbus_task_mime_what_provides_cb (PkClient *client, GAsyncResult *res, GpkDbusTask *dtask)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	PkResults *results = NULL;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;
	gchar *info_url;
	GtkResponseType button;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		/* TRANSLATORS: we failed to find the package, this shouldn't happen */
		gpk_dbus_task_error_msg (dtask, _("Failed to search for provides"), error);
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "failed to search for provides: %s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error);
		g_error_free (error_dbus);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_pkerror (error_code), "failed to search for provides: %s", pk_error_get_details (error_code));
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* get results */
	array = pk_results_get_package_array (results);

	/* found nothing? */
	if (array->len == 0) {
		if (dtask->priv->show_warning) {
			info_url = gpk_vendor_get_not_found_url (dtask->priv->vendor, GPK_VENDOR_URL_TYPE_MIME);
			/* only show the "more info" button if there is a valid link */
			if (info_url != NULL)
				gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, GPK_MODAL_DIALOG_BUTTON_ACTION);
			else
				gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			/* TRANSLATORS: title */
			gpk_modal_dialog_set_title (dtask->priv->dialog, _("Failed to find software"));
			/* TRANSLATORS: nothing found in the software sources that helps */
			gpk_modal_dialog_set_message (dtask->priv->dialog, _("No new applications can be found to handle this type of file"));
			gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-package-not-found");
			/* TRANSLATORS: button: show the user a button to get more help finding stuff */
			gpk_modal_dialog_set_action (dtask->priv->dialog, _("More information"));
			gpk_modal_dialog_present (dtask->priv->dialog);
			button = gpk_modal_dialog_run (dtask->priv->dialog);
			if (button == GTK_RESPONSE_OK)
				gpk_gnome_open (info_url);
			g_free (info_url);
		}
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_NO_PACKAGES_FOUND, "nothing was found to handle mime type");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* populate a chooser */
	gpk_helper_chooser_show (dtask->priv->helper_chooser, array);

	gpk_dbus_task_dbus_return_value (dtask, TRUE);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_dbus_task_install_mime_types:
 * @task: a valid #GpkDbusTask instance
 * @mime_type: a mime_type such as <literal>application/text</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a application to handle a mime type
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_mime_types (GpkDbusTask *dtask, gchar **mime_types, GpkDbusTaskFinishedCb finished_cb, gpointer userdata)
{
	gboolean ret;
	GError *error_dbus = NULL;
	guint len;
	gchar *message = NULL;
	gchar *text = NULL;

	g_return_if_fail (GPK_IS_DBUS_TASK (dtask));
	g_return_if_fail (mime_types != NULL);

	/* save callback information */
	dtask->priv->finished_cb = finished_cb;
	dtask->priv->finished_userdata = userdata;

	/* check it's not session wide banned in gconf */
	ret = gconf_client_get_bool (dtask->priv->gconf_client, GPK_CONF_ENABLE_MIME_TYPE_HELPER, NULL);
	if (!ret) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FORBIDDEN, "not enabled in GConf : %s", GPK_CONF_ENABLE_MIME_TYPE_HELPER);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* optional */
	if (!dtask->priv->show_confirm_search) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* make sure the user wants to do action */
	message = g_strdup_printf ("%s\n\n%s\n\n%s",
				    /* TRANSLATORS: message: mime type opener required */
				   _("An additional program is required to open this type of file:"),
				   mime_types[0],
				   /* TRANSLATORS: message: confirm with the user */
				   _("Do you want to search for a program to open this file type now?"));

	/* hardcode for now as we only support one mime type at a time */
	len = 1;

	/* make title using application name */
	if (dtask->priv->parent_title != NULL) {
		/* TRANSLATORS: string is a program name, e.g. "Movie Player" */
		text = g_strdup_printf (ngettext ("%s requires a new mime type", "%s requires new mime types", len), dtask->priv->parent_title);
	} else {
		/* TRANSLATORS: a random program which we can't get the name wants to do something */
		text = g_strdup (ngettext ("A program requires a new mime type", "A program requires new mime types", len));
	}

	/* TRANSLATORS: button: confirm to search for packages */
	ret = gpk_dbus_task_confirm_action (dtask, text, message, _("Search"));
	if (!ret) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to search");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

skip_checks:
	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	/* TRANSLATORS: title: searching for mime type handlers */
	gpk_modal_dialog_set_title (dtask->priv->dialog, _("Searching for file handlers"));
	gpk_modal_dialog_set_image_status (dtask->priv->dialog, PK_STATUS_ENUM_WAIT);
	gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-finding-packages");

	/* setup the UI */
	if (dtask->priv->show_progress)
		gpk_modal_dialog_present (dtask->priv->dialog);

	/* action */
	pk_client_what_provides_async (PK_CLIENT(dtask->priv->task), pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED, PK_FILTER_ENUM_ARCH, PK_FILTER_ENUM_NEWEST, -1),
				       PK_PROVIDES_ENUM_MIMETYPE, mime_types, NULL,
			               (PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
				       (GAsyncReadyCallback) gpk_dbus_task_mime_what_provides_cb, dtask);
	/* wait for async reply */
out:
	g_free (text);
	g_free (message);
}

/**
 * gpk_dbus_task_font_tag_to_lang:
 **/
static gchar *
gpk_dbus_task_font_tag_to_lang (const gchar *tag)
{
	gchar *lang = NULL;
#if 0
	*** We do not yet enable this code due to a few bugs in fontconfig ***
	http://bugs.freedesktop.org/show_bug.cgi?id=18846 and
	http://bugs.freedesktop.org/show_bug.cgi?id=18847

	FcPattern *pat = NULL;
	FcChar8 *fclang;
	FcResult res;

	/* parse the tag */
	pat = FcNameParse ((FcChar8 *) tag);
	if (pat == NULL) {
		egg_warning ("cannot parse: '%s'", tag);
		goto out;
	}
	FcPatternPrint (pat);
	res = FcPatternGetString (pat, FC_LANG, 0, &fclang);
	if (res != FcResultMatch) {
		egg_warning ("failed to get string for: '%s': %i", tag, res);
		goto out;
	}
	lang = g_strdup ((gchar *) fclang);
out:
	if (pat != NULL)
		FcPatternDestroy (pat);
#else
	guint len;

	/* verify we have enough to remove prefix */
	len = strlen (tag);
	if (len < 7)
		goto out;
	/* this is a bodge */
	lang = g_strdup (&tag[6]);
out:
#endif
	return lang;
}


/**
 * gpk_dbus_task_font_tag_to_localised_name:
 **/
static gchar *
gpk_dbus_task_font_tag_to_localised_name (GpkDbusTask *dtask, const gchar *tag)
{
	gchar *lang;
	gchar *language = NULL;
	gchar *name;

	/* use fontconfig to get the language code */
	lang = gpk_dbus_task_font_tag_to_lang (tag);
	if (lang == NULL) {
		/* TRANSLATORS: we could not parse the ISO639 code from the fontconfig tag name */
		name = g_strdup_printf ("%s: %s", _("Language tag not parsed"), tag);
		goto out;
	}

	/* convert to localisable name */
	language = gpk_language_iso639_to_language (dtask->priv->language, lang);
	if (language == NULL) {
		/* TRANSLATORS: we could not find en_US string for ISO639 code */
		name = g_strdup_printf ("%s: %s", _("Language code not matched"), lang);
		goto out;
	}

	/* get translation, or return untranslated string */
	name = g_strdup (dgettext("iso_639", language));
	if (name == NULL)
		name = g_strdup (language);
out:
	g_free (lang);
	g_free (language);
	return name;
}

/**
 * gpk_dbus_task_fontconfig_what_provides_cb:
 **/
static void
gpk_dbus_task_fontconfig_what_provides_cb (PkClient *client, GAsyncResult *res, GpkDbusTask *dtask)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	PkResults *results = NULL;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;
	gchar *title;
	gchar *info_url;
	GtkResponseType button;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		/* TRANSLATORS: we failed to find the package, this shouldn't happen */
//		gpk_dbus_task_error_msg (dtask, _("Failed to search for provides"), error);
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "failed to search for provides: %s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error);
		g_error_free (error_dbus);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		/* TRANSLATORS: we failed to find the package, this shouldn't happen */
//		gpk_dbus_task_error_msg (dtask, _("Failed to search for provides"), pk_error_get_details (error_code));
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_pkerror (error_code), "failed to search for provides: %s", pk_error_get_details (error_code));
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* get results */
	array = pk_results_get_package_array (results);

	/* found nothing? */
	if (array->len == 0) {
		if (dtask->priv->show_warning) {
			info_url = gpk_vendor_get_not_found_url (dtask->priv->vendor, GPK_VENDOR_URL_TYPE_FONT);
			/* TRANSLATORS: title: cannot find in sources */
			title = ngettext ("Failed to find font", "Failed to find fonts", array->len);
			/* only show the "more info" button if there is a valid link */
			if (info_url != NULL)
				gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, GPK_MODAL_DIALOG_BUTTON_ACTION);
			else
				gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			gpk_modal_dialog_set_title (dtask->priv->dialog, title);
			/* TRANSLATORS: message: tell the user we suck */
			gpk_modal_dialog_set_message (dtask->priv->dialog, _("No new fonts can be found for this document"));
			gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-package-not-found");
			/* TRANSLATORS: button: show the user a button to get more help finding stuff */
			gpk_modal_dialog_set_action (dtask->priv->dialog, _("More information"));
			gpk_modal_dialog_present (dtask->priv->dialog);
			button = gpk_modal_dialog_run (dtask->priv->dialog);
			if (button == GTK_RESPONSE_OK)
				gpk_gnome_open (info_url);
			g_free (info_url);
		}
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_NO_PACKAGES_FOUND, "failed to find font");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* optional */
	if (!dtask->priv->show_confirm_install) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* TRANSLATORS: title: show a list of fonts */
	title = ngettext ("Do you want to install this package now?", "Do you want to install these packages now?", array->len);
	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, GPK_MODAL_DIALOG_PACKAGE_LIST);
	gpk_modal_dialog_set_package_list (dtask->priv->dialog, array);
	gpk_modal_dialog_set_title (dtask->priv->dialog, title);
	gpk_modal_dialog_set_message (dtask->priv->dialog, title);
	gpk_modal_dialog_set_image (dtask->priv->dialog, "dialog-information");
	/* TRANSLATORS: button: install a font */
	gpk_modal_dialog_set_action (dtask->priv->dialog, _("Install"));
	gpk_modal_dialog_present_with_time (dtask->priv->dialog, dtask->priv->timestamp);
	button = gpk_modal_dialog_run (dtask->priv->dialog);

	/* close, we're going to fail the method */
	if (button != GTK_RESPONSE_OK) {
		gpk_modal_dialog_close (dtask->priv->dialog);
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to download");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

skip_checks:
	/* convert to list of package id's */
	dtask->priv->package_ids = pk_package_array_to_strv (array);
	gpk_dbus_task_install_package_ids (dtask);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_dbus_task_install_check_exec_ignored:
 *
 * Returns %FALSE if the executed program is in the ignored list.
 **/
static gboolean
gpk_dbus_task_install_check_exec_ignored (GpkDbusTask *dtask)
{
	gchar *ignored_str;
	gchar **ignored = NULL;
	gboolean ret = TRUE;
	GError *error = NULL;
	guint i;

	/* check it's not session wide banned in gconf */
	ignored_str = gconf_client_get_string (dtask->priv->gconf_client, GPK_CONF_IGNORED_DBUS_REQUESTS, &error);
	if (ignored_str == NULL) {
		egg_warning ("failed to get ignored requests: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check each one */
	ignored = g_strsplit (ignored_str, ",", -1);
	for (i=0; ignored[i] != NULL; i++) {
		if (g_strcmp0 (dtask->priv->exec, ignored[i]) == 0) {
			ret = FALSE;
			break;
		}
	}
out:
	g_free (ignored_str);
	g_strfreev (ignored);
	return ret;
}

/**
 * gpk_dbus_task_install_fontconfig_resources:
 * @task: a valid #GpkDbusTask instance
 * @fonts: font description such as <literal>lang:fr</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a application to handle a mime type
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_fontconfig_resources (GpkDbusTask *dtask, gchar **fonts, GpkDbusTaskFinishedCb finished_cb, gpointer userdata)
{
	gboolean ret;
	GPtrArray *array = NULL;
//	GtkResponseType button;
//	gchar *info_url;
	GError *error_dbus = NULL;
	guint i;
	guint len;
	guint size;
	gchar *text;
	gchar *message;
	const gchar *title;
	const gchar *title_part;
	GString *string;

	g_return_if_fail (GPK_IS_DBUS_TASK (dtask));
	g_return_if_fail (fonts != NULL);

	/* save callback information */
	dtask->priv->finished_cb = finished_cb;
	dtask->priv->finished_userdata = userdata;

	/* if this program banned? */
	ret = gpk_dbus_task_install_check_exec_ignored (dtask);
	if (!ret) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FORBIDDEN, "skipping ignored program: %s", dtask->priv->exec);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* get number of fonts to install */
	len = g_strv_length (fonts);

	/* check it's not session wide banned in gconf */
	ret = gconf_client_get_bool (dtask->priv->gconf_client, GPK_CONF_ENABLE_FONT_HELPER, NULL);
	if (!ret) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FORBIDDEN, "not enabled in GConf : %s", GPK_CONF_ENABLE_FONT_HELPER);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* optional */
	if (!dtask->priv->show_confirm_search) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* check we got valid data */
	for (i=0; i<len; i++) {
		/* correct prefix */
		if (!g_str_has_prefix (fonts[i], ":lang=")) {
			error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "not recognised prefix: '%s'", fonts[i]);
			gpk_dbus_task_dbus_return_error (dtask, error_dbus);
			g_error_free (error_dbus);
			goto out;
		}
		/* no lang code */
		size = strlen (fonts[i]);
		if (size < 7 || size > 20) {
			error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "lang tag malformed: '%s'", fonts[i]);
			gpk_dbus_task_dbus_return_error (dtask, error_dbus);
			g_error_free (error_dbus);
			goto out;
		}
	}

	string = g_string_new ("");

	/* don't use a bullet for one item */
	if (len == 1) {
		text = gpk_dbus_task_font_tag_to_localised_name (dtask, fonts[0]);
		g_string_append_printf (string, "%s\n", text);
		g_free (text);
	} else {
		for (i=0; i<len; i++) {
			text = gpk_dbus_task_font_tag_to_localised_name (dtask, fonts[i]);
			g_string_append_printf (string, "• %s\n", text);
			g_free (text);
		}
	}
	/* display messagebox  */
	text = g_string_free (string, FALSE);

	/* TRANSLATORS: we need to download a new font package to display a document */
	title = ngettext ("An additional font is required to view this document correctly.",
			  "Additional fonts are required to view this document correctly.", len);

	/* TRANSLATORS: we need to download a new font package to display a document */
	title_part = ngettext ("Do you want to search for a suitable package now?",
			       "Do you want to search for suitable packages now?", len);

	/* check user wanted operation */
	message = g_strdup_printf ("%s\n\n%s\n%s", title, text, title_part);
	g_free (text);

	/* make title using application name */
	if (dtask->priv->parent_title != NULL) {
		/* TRANSLATORS: string is a program name, e.g. "Movie Player" */
		text = g_strdup_printf (ngettext ("%s wants to install a font", "%s wants to install fonts", len), dtask->priv->parent_title);
	} else {
		/* TRANSLATORS: a random program which we can't get the name wants to do something */
		text = g_strdup (ngettext ("A program wants to install a font", "A program wants to install fonts", len));
	}

	/* TRANSLATORS: button: confirm to search for packages */
	ret = gpk_dbus_task_confirm_action (dtask, text, message, _("Search"));
	g_free (text);
	g_free (message);
	if (!ret) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to search");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

skip_checks:
	/* TRANSLATORS: title to show when searching for font files */
	title = ngettext ("Searching for font", "Searching for fonts", len);
	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	gpk_modal_dialog_set_title (dtask->priv->dialog, title);
	gpk_modal_dialog_set_image_status (dtask->priv->dialog, PK_STATUS_ENUM_WAIT);
	gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-finding-packages");

	/* setup the UI */
	if (dtask->priv->show_progress)
		gpk_modal_dialog_present (dtask->priv->dialog);

	/* do each one */
	pk_client_what_provides_async (PK_CLIENT(dtask->priv->task), pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED, PK_FILTER_ENUM_ARCH, PK_FILTER_ENUM_NEWEST, -1),
				       PK_PROVIDES_ENUM_FONT, fonts, NULL,
			               (PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
				       (GAsyncReadyCallback) gpk_dbus_task_fontconfig_what_provides_cb, dtask);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * gpk_dbus_task_catalog_lookup_cb:
 **/
static void
gpk_dbus_task_catalog_lookup_cb (GObject *object, GAsyncResult *res, GpkDbusTask *dtask)
{
	PkCatalog *catalog = PK_CATALOG (object);
	GError *error = NULL;
	GError *error_dbus = NULL;
	GPtrArray *array = NULL;
	GtkResponseType button;

	/* get the results */
	array = pk_catalog_lookup_finish (catalog, res, &error);
	if (array == NULL) {
		if (dtask->priv->show_warning) {
			/* TRANSLATORS: title: we've already got all these packages installed */
			gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			gpk_modal_dialog_set_title (dtask->priv->dialog, _("Could not process catalog"));
			gpk_modal_dialog_set_help_id (dtask->priv->dialog, NULL);
			gpk_modal_dialog_present_with_time (dtask->priv->dialog, dtask->priv->timestamp);
			gpk_modal_dialog_run (dtask->priv->dialog);
		}
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "failed to parse catalog: %s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error);
		g_error_free (error_dbus);
		goto out;
	}

	/* nothing to do? */
	if (array->len == 0) {
		/* show UI */
		if (dtask->priv->show_warning) {
			/* TRANSLATORS: title: we've already got all these packages installed */
			gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			gpk_modal_dialog_set_title (dtask->priv->dialog, _("No packages need to be installed"));
			gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-catalog-none-required");
			gpk_modal_dialog_present_with_time (dtask->priv->dialog, dtask->priv->timestamp);
			gpk_modal_dialog_run (dtask->priv->dialog);
		}
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "No packages need to be installed");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* optional */
	if (!dtask->priv->show_confirm_install) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, GPK_MODAL_DIALOG_PACKAGE_LIST);
	/* TRANSLATORS: title: allow user to confirm */
	gpk_modal_dialog_set_title (dtask->priv->dialog, _("Install packages in catalog?"));
	/* TRANSLATORS: display a list of packages to install */
	gpk_modal_dialog_set_message (dtask->priv->dialog, _("The following packages are marked to be installed from the catalog:"));
	gpk_modal_dialog_set_image (dtask->priv->dialog, "dialog-question");
	gpk_modal_dialog_set_package_list (dtask->priv->dialog, array);
	/* TRANSLATORS: button: install packages in catalog */
	gpk_modal_dialog_set_action (dtask->priv->dialog, _("Install"));
	gpk_modal_dialog_present_with_time (dtask->priv->dialog, dtask->priv->timestamp);
	button = gpk_modal_dialog_run (dtask->priv->dialog);

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "Action was cancelled");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

skip_checks:
	/* convert to list of package id's */
	dtask->priv->package_ids = pk_package_array_to_strv (array);
	gpk_dbus_task_install_package_ids (dtask);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * gpk_dbus_task_remove_packages_cb:
 **/
static void
gpk_dbus_task_remove_packages_cb (PkTask *task, GAsyncResult *res, GpkDbusTask *dtask)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	PkResults *results = NULL;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		/* TRANSLATORS: error: failed to remove, detailed error follows */
		gpk_dbus_task_error_msg (dtask, _("Failed to remove package"), error);
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "failed to remove package: %s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error);
		g_error_free (error_dbus);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_pkerror (error_code), "failed to remove package: %s", pk_error_get_details (error_code));
		gpk_dbus_task_handle_error (dtask, error_code);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* we're done */
	gpk_dbus_task_dbus_return_value (dtask, TRUE);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_dbus_task_printer_driver_what_provides_cb:
 **/
static void
gpk_dbus_task_printer_driver_what_provides_cb (PkClient *client, GAsyncResult *res, GpkDbusTask *dtask)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	PkResults *results = NULL;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;
	GtkResponseType button;
	const gchar *title;
	const gchar *message;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "failed to resolve: %s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error);
		g_error_free (error_dbus);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_pkerror (error_code), "failed to resolve: %s", pk_error_get_details (error_code));
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* get results */
	array = pk_results_get_package_array (results);

	/* found nothing?  No problem*/
	if (array->len == 0) {
		gpk_modal_dialog_close (dtask->priv->dialog);
		gpk_dbus_task_dbus_return_value (dtask, FALSE);
		goto out;
	}

	/* optional */
	if (!dtask->priv->show_confirm_install) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks2;
	}

	title = ngettext ("Install the following driver", "Install the following drivers", array->len);
	message = ngettext ("Do you want to install this package now?", "Do you want to install these packages now?", array->len);

	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, GPK_MODAL_DIALOG_PACKAGE_LIST);
	gpk_modal_dialog_set_package_list (dtask->priv->dialog, array);
	gpk_modal_dialog_set_title (dtask->priv->dialog, title);
	gpk_modal_dialog_set_message (dtask->priv->dialog, message);
	gpk_modal_dialog_set_image (dtask->priv->dialog, "dialog-information");
	/* TRANSLATORS: button: install printer drivers */
	gpk_modal_dialog_set_action (dtask->priv->dialog, _("Install"));
	gpk_modal_dialog_present_with_time (dtask->priv->dialog, dtask->priv->timestamp);
	button = gpk_modal_dialog_run (dtask->priv->dialog);

	/* close, we're going to fail the method */
	if (button != GTK_RESPONSE_OK) {
		gpk_modal_dialog_close (dtask->priv->dialog);
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to download");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

skip_checks2:
	/* install with deps */
	dtask->priv->package_ids = pk_package_array_to_strv (array);
	gpk_dbus_task_install_package_ids (dtask);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_dbus_task_install_printer_drivers:
 * @task: a valid #GpkDbusTask instance
 * @device_ids: list of Device IDs such as <literal>MFG:Foo Inc;MDL:Bar 3000;</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install printer drivers for a given set of models.
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_printer_drivers (GpkDbusTask *dtask, gchar **device_ids, GpkDbusTaskFinishedCb finished_cb, gpointer userdata)
{
	guint i, j;
	guint len;
	guint n_tags;
	guint n_fields;
	gchar **fields;
	gchar *mfg;
	gchar *mdl;
	gchar *tag;
	gchar **tags;

	g_return_if_fail (GPK_IS_DBUS_TASK (dtask));
	g_return_if_fail (device_ids != NULL);

	/* save callback information */
	dtask->priv->finished_cb = finished_cb;
	dtask->priv->finished_userdata = userdata;

	gpk_modal_dialog_setup (dtask->priv->dialog,
				GPK_MODAL_DIALOG_PAGE_PROGRESS,
				GPK_MODAL_DIALOG_PACKAGE_PADDING);
	gpk_modal_dialog_set_title (dtask->priv->dialog,
				    _("Searching for packages"));
	gpk_modal_dialog_set_image_status (dtask->priv->dialog,
					   PK_STATUS_ENUM_WAIT);
	gpk_modal_dialog_set_help_id (dtask->priv->dialog,
				      "dialog-finding-packages");

	/* setup the UI */
	if (dtask->priv->show_progress)
		gpk_modal_dialog_present (dtask->priv->dialog);

	len = g_strv_length (device_ids);
	if (len > 1)
		/* hardcode for now as we only support one at a time */
		len = 1;

	/* make a list of provides tags */
	tags = g_new0 (gchar *, len);
	n_tags = 0;
	for (i=0; i<len; i++) {
		gchar *p, *ltag;
		fields = g_strsplit (device_ids[i], ";", 0);
		n_fields = g_strv_length (fields);
		mfg = mdl = NULL;
		for (j=0; j<n_fields && (!mfg || !mdl); j++) {
			if (g_str_has_prefix (fields[j], "MFG:"))
				mfg = g_strdup (fields[j] + 4);
			else if (g_str_has_prefix (fields[j], "MDL:"))
				mdl = g_strdup (fields[j] + 4);
		}
		g_strfreev (fields);

		if (!mfg || !mdl) {
			egg_warning("invalid line '%s', missing field",
				    device_ids[i]);
			continue;
		}

		tag = g_strconcat (mfg, ";", mdl, ";", NULL);
		ltag = g_ascii_strdown (tag, -1);
		g_free (tag);

		/* Replace spaces with underscores */
		for (p = ltag; *p != '\0'; p++)
			if (*p == ' ')
				*p = '_';

		tags[n_tags++] = g_strdup (ltag);
		g_free (ltag);
	}

	if (n_tags == 0) {
		gpk_dbus_task_dbus_return_value (dtask, FALSE);
		goto out;
	}

	tags = g_renew (gchar *, tags, n_tags);

	/* get driver packages */
	pk_client_what_provides_async (PK_CLIENT(dtask->priv->task),
				       pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED,
							       PK_FILTER_ENUM_ARCH,
							       PK_FILTER_ENUM_NEWEST,
							       -1),
				       PK_PROVIDES_ENUM_POSTSCRIPT_DRIVER,
				       tags, NULL,
				       (PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
				       (GAsyncReadyCallback) gpk_dbus_task_printer_driver_what_provides_cb, dtask);

 out:
	g_strfreev (tags);
}

/**
 * gpk_dbus_task_remove_package_ids:
 * @task: a valid #GpkDbusTask instance
 **/
static void
gpk_dbus_task_remove_package_ids (GpkDbusTask *dtask)
{
	GtkWindow *window;

	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, GPK_MODAL_DIALOG_PACKAGE_PADDING);
	/* TRANSLATORS: title: removing packages */
	gpk_modal_dialog_set_title (dtask->priv->dialog, _("Removing packages"));
	if (dtask->priv->show_progress)
		gpk_modal_dialog_present (dtask->priv->dialog);

	/* ensure parent is set */
	window = gpk_modal_dialog_get_window (dtask->priv->dialog);
	gpk_task_set_parent_window (GPK_TASK (dtask->priv->task), window);

	/* remove async */
	pk_task_remove_packages_async (dtask->priv->task, dtask->priv->package_ids, TRUE, TRUE, NULL,
					(PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
					(GAsyncReadyCallback) gpk_dbus_task_remove_packages_cb, dtask);
}

/**
 * gpk_dbus_task_remove_package_by_file_search_file_cb:
 **/
static void
gpk_dbus_task_remove_package_by_file_search_file_cb (PkClient *client, GAsyncResult *res, GpkDbusTask *dtask)
{
	GError *error = NULL;
	GError *error_dbus = NULL;
	PkResults *results = NULL;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_gerror (error), "failed to search by file: %s", error->message);
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		egg_warning ("failed to resolve: %s", error->message);
		g_error_free (error);
		g_error_free (error_dbus);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, gpk_dbus_task_get_code_from_pkerror (error_code), "failed to search by file: %s", pk_error_get_details (error_code));
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* get results */
	array = pk_results_get_package_array (results);

	/* found nothing? */
	if (array->len == 0) {
		if (dtask->priv->show_warning) {
			gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			/* TRANSLATORS: failed to fild the package for thefile */
			gpk_modal_dialog_set_title (dtask->priv->dialog, _("Failed to find package for this file"));
			/* TRANSLATORS: nothing found */
			gpk_modal_dialog_set_message (dtask->priv->dialog, _("The file could not be found in any packages"));
			gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-package-not-found");
			/* TRANSLATORS: button: show the user a button to get more help finding stuff */
			gpk_modal_dialog_present (dtask->priv->dialog);
			gpk_modal_dialog_run (dtask->priv->dialog);
		}
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_NO_PACKAGES_FOUND, "no packages found for this file");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

	/* convert to data */
	dtask->priv->package_ids = pk_package_array_to_strv (array);

	/* remove these packages with deps */
	gpk_dbus_task_remove_package_ids (dtask);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_dbus_task_remove_package_by_file:
 * @task: a valid #GpkDbusTask instance
 * @full_path: a file path name such as <literal>/usr/sbin/packagekitd</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Remove a package which provides a file on the system.
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_remove_package_by_file (GpkDbusTask *dtask, gchar **full_paths, GpkDbusTaskFinishedCb finished_cb, gpointer userdata)
{
	gboolean ret;
	GError *error_dbus = NULL;
	guint len;
	guint i;
	gchar *text;
	gchar *message;
	GString *string;

	g_return_if_fail (GPK_IS_DBUS_TASK (dtask));
	g_return_if_fail (full_paths != NULL);

	/* save callback information */
	dtask->priv->finished_cb = finished_cb;
	dtask->priv->finished_userdata = userdata;

	/* optional */
	if (!dtask->priv->show_confirm_search) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	string = g_string_new ("");
	len = g_strv_length (full_paths);

	/* don't use a bullet for one item */
	if (len == 1) {
		g_string_append_printf (string, "%s\n", full_paths[0]);
	} else {
		for (i=0; i<len; i++)
			g_string_append_printf (string, "• %s\n", full_paths[i]);
	}
	/* display messagebox  */
	text = g_string_free (string, FALSE);

	/* check user wanted operation */
	message = g_strdup_printf ("%s\n\n%s\n\n%s",
				   /* TRANSLATORS: a program wants to remove a file, e.g. /lib/moo.so */
				   ngettext ("The following file will be removed:", "The following files will be removed:", len),
				   text,
				   /* TRANSLATORS: confirm with the user */
				   ngettext ("Do you want to remove this file now?", "Do you want to remove these files now?", len));

	/* make title using application name */
	if (dtask->priv->parent_title != NULL) {
		/* TRANSLATORS: string is a program name, e.g. "Movie Player" */
		text = g_strdup_printf (ngettext ("%s wants to remove a file", "%s wants to remove files", len), dtask->priv->parent_title);
	} else {
		/* TRANSLATORS: a random program which we can't get the name wants to do something */
		text = g_strdup (ngettext ("A program wants to remove a file", "A program wants to remove files", len));
	}

	/* TRANSLATORS: button: confirm to search for packages */
	ret = gpk_dbus_task_confirm_action (dtask, text, message, _("Remove"));
	g_free (text);
	g_free (message);
	if (!ret) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to search");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

skip_checks:
	/* TRANSLATORS: searching for the package that provides the file */
	gpk_modal_dialog_set_title (dtask->priv->dialog, _("Searching for file"));
	gpk_modal_dialog_set_image_status (dtask->priv->dialog, PK_STATUS_ENUM_WAIT);

	/* do search */
	pk_client_search_files_async (PK_CLIENT(dtask->priv->task), pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH, PK_FILTER_ENUM_NEWEST, PK_FILTER_ENUM_INSTALLED, -1), full_paths, NULL,
			             (PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
				     (GAsyncReadyCallback) gpk_dbus_task_remove_package_by_file_search_file_cb, dtask);

	/* wait for async reply */
out:
	return;
}

/**
 * gpk_dbus_task_install_catalogs:
 **/
void
gpk_dbus_task_install_catalogs (GpkDbusTask *dtask, gchar **filenames, GpkDbusTaskFinishedCb finished_cb, gpointer userdata)
{
	GError *error_dbus = NULL;
	GtkResponseType button;
	gchar *message = NULL;
	const gchar *title;
	guint len;

	g_return_if_fail (GPK_IS_DBUS_TASK (dtask));
	g_return_if_fail (filenames != NULL);

	/* save callback information */
	dtask->priv->finished_cb = finished_cb;
	dtask->priv->finished_userdata = userdata;

	len = g_strv_length (filenames);

	/* optional */
	if (!dtask->priv->show_confirm_search) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* TRANSLATORS: title to install package catalogs */
	title = ngettext ("Do you want to install this catalog?",
			  "Do you want to install these catalogs?", len);
	message = g_strjoinv ("\n", filenames);

	/* show UI */
	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, 0);
	gpk_modal_dialog_set_title (dtask->priv->dialog, title);
	gpk_modal_dialog_set_message (dtask->priv->dialog, message);
	/* TRANSLATORS: button: install catalog */
	gpk_modal_dialog_set_action (dtask->priv->dialog, _("Install"));
	gpk_modal_dialog_set_help_id (dtask->priv->dialog, "dialog-install-catalogs");
	gpk_modal_dialog_present_with_time (dtask->priv->dialog, dtask->priv->timestamp);
	button = gpk_modal_dialog_run (dtask->priv->dialog);

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK) {
		error_dbus = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to install");
		gpk_dbus_task_dbus_return_error (dtask, error_dbus);
		g_error_free (error_dbus);
		goto out;
	}

skip_checks:
	gpk_modal_dialog_setup (dtask->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	/* TRANSLATORS: title: install package catalogs, that is, instructions for installing */
	gpk_modal_dialog_set_title (dtask->priv->dialog, _("Install catalogs"));
	gpk_dbus_task_set_status (dtask, PK_STATUS_ENUM_WAIT);

	/* setup the UI */
	if (dtask->priv->show_progress)
		gpk_modal_dialog_present (dtask->priv->dialog);

	/* lookup catalog */
	pk_catalog_lookup_async (dtask->priv->catalog, filenames[0], NULL,
			 	 (PkProgressCallback) gpk_dbus_task_progress_cb, dtask,
				 (GAsyncReadyCallback) gpk_dbus_task_catalog_lookup_cb, dtask);
out:
	g_free (message);
}

/**
 * gpk_dbus_task_get_package_for_exec:
 **/
static gchar *
gpk_dbus_task_get_package_for_exec (GpkDbusTask *dtask, const gchar *exec)
{
	const gchar *package_id;
	gchar *package = NULL;
	GError *error = NULL;
	GPtrArray *array = NULL;
	PkPackage *item;
	PkResults *results = NULL;
	gchar **values = NULL;
	gchar **split = NULL;

	/* find the package name */
	values = g_strsplit (exec, "&", -1);
	results = pk_client_search_files (PK_CLIENT(dtask->priv->task), pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), values, NULL,
					 (PkProgressCallback) gpk_dbus_task_progress_cb, dtask, &error);
	if (results == NULL) {
		egg_warning ("failed to search file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get the list of packages */
	array = pk_results_get_package_array (results);

	/* nothing found */
	if (array->len == 0) {
		egg_debug ("cannot find installed package that provides : %s", exec);
		goto out;
	}

	/* check we have one */
	if (array->len != 1)
		egg_warning ("not one return, using first");

	/* copy name */
	item = g_ptr_array_index (array, 0);
	package_id = pk_package_get_id (item);
	split = pk_package_id_split (package_id);
	package = g_strdup (split[0]);
	egg_debug ("got package %s", package);
out:
	g_strfreev (values);
	g_strfreev (split);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return package;
}

/**
 * gpk_dbus_task_path_is_trusted:
 **/
static gboolean
gpk_dbus_task_path_is_trusted (const gchar *exec)
{
	/* special case the plugin helper -- it's trusted */
	if (g_strcmp0 (exec, "/usr/libexec/gst-install-plugins-helper") == 0 ||
	    g_strcmp0 (exec, "/usr/libexec/pk-gstreamer-install") == 0)
		return TRUE;
	return FALSE;
}

/**
 * gpk_dbus_task_set_exec:
 *
 * This sets the package name of the application that is trying to install
 * software, e.g. "totem" and is used for the PkDesktop lookup to provide
 * a translated name and icon.
 **/
gboolean
gpk_dbus_task_set_exec (GpkDbusTask *dtask, const gchar *exec)
{
	GpkX11 *x11;
	gchar *package = NULL;

	g_return_val_if_fail (GPK_IS_DBUS_TASK (dtask), FALSE);

	/* old values invalid */
	g_free (dtask->priv->exec);
	g_free (dtask->priv->parent_title);
	g_free (dtask->priv->parent_icon_name);
	dtask->priv->exec = g_strdup (exec);
	dtask->priv->parent_title = NULL;
	dtask->priv->parent_icon_name = NULL;

	/* is the binary trusted, i.e. can we probe it's window properties */
	if (gpk_dbus_task_path_is_trusted (exec)) {
		egg_debug ("using application window properties");
		/* get from window properties */
		x11 = gpk_x11_new ();
		gpk_x11_set_window (x11, dtask->priv->parent_window);
		dtask->priv->parent_title = gpk_x11_get_title (x11);
		g_object_unref (x11);
		goto out;
	}

	/* get from installed database */
	package = gpk_dbus_task_get_package_for_exec (dtask, exec);
	egg_debug ("got package %s", package);

	/* try to get from PkDesktop */
	if (package != NULL) {
		dtask->priv->parent_title = gpk_desktop_guess_localised_name (dtask->priv->desktop, package);
		dtask->priv->parent_icon_name = gpk_desktop_guess_icon_name (dtask->priv->desktop, package);
		/* fallback to package name */
		if (dtask->priv->parent_title == NULL) {
			egg_debug ("did not get localised description for %s", package);
			dtask->priv->parent_title = g_strdup (package);
		}
	}

	/* fallback to exec - eugh... */
	if (dtask->priv->parent_title == NULL) {
		egg_debug ("did not get package for %s, using exec basename", package);
		dtask->priv->parent_title = g_path_get_basename (exec);
	}
out:
	g_free (package);
	egg_debug ("got name=%s, icon=%s", dtask->priv->parent_title, dtask->priv->parent_icon_name);
	return TRUE;
}

/**
 * gpk_dbus_task_class_init:
 * @klass: The #GpkDbusTaskClass
 **/
static void
gpk_dbus_task_class_init (GpkDbusTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_dbus_task_finalize;
	g_type_class_add_private (klass, sizeof (GpkDbusTaskPrivate));
}

/**
 * gpk_dbus_task_init:
 * @task: a valid #GpkDbusTask instance
 **/
static void
gpk_dbus_task_init (GpkDbusTask *dtask)
{
	gboolean ret;
	GtkWindow *main_window;

	dtask->priv = GPK_DBUS_TASK_GET_PRIVATE (dtask);

	dtask->priv->package_ids = NULL;
	dtask->priv->files = NULL;
	dtask->priv->parent_window = NULL;
	dtask->priv->parent_title = NULL;
	dtask->priv->exec = NULL;
	dtask->priv->parent_icon_name = NULL;
	dtask->priv->cached_error_code = NULL;
	dtask->priv->context = NULL;
	dtask->priv->cancellable = g_cancellable_new ();
	dtask->priv->exit = PK_EXIT_ENUM_FAILED;
	dtask->priv->show_confirm_search = TRUE;
	dtask->priv->show_confirm_deps = TRUE;
	dtask->priv->show_confirm_install = TRUE;
	dtask->priv->show_progress = TRUE;
	dtask->priv->show_finished = TRUE;
	dtask->priv->show_warning = TRUE;
	dtask->priv->timestamp = 0;

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* only initialise if the application didn't do it before */
	if (!notify_is_initted ())
		notify_init ("gpk-dbus_task");

	dtask->priv->vendor = gpk_vendor_new ();
	dtask->priv->dialog = gpk_modal_dialog_new ();
	main_window = gpk_modal_dialog_get_window (dtask->priv->dialog);
	gpk_modal_dialog_set_window_icon (dtask->priv->dialog, "pk-package-installed");
	g_signal_connect (dtask->priv->dialog, "cancel",
			  G_CALLBACK (gpk_dbus_task_button_cancel_cb), dtask);
	g_signal_connect (dtask->priv->dialog, "close",
			  G_CALLBACK (gpk_dbus_task_button_close_cb), dtask);

	/* helpers */
	dtask->priv->helper_run = gpk_helper_run_new ();
	gpk_helper_run_set_parent (dtask->priv->helper_run, main_window);

	dtask->priv->helper_chooser = gpk_helper_chooser_new ();
	g_signal_connect (dtask->priv->helper_chooser, "event", G_CALLBACK (gpk_dbus_task_chooser_event_cb), dtask);
	gpk_helper_chooser_set_parent (dtask->priv->helper_chooser, main_window);

	/* map ISO639 to language names */
	dtask->priv->language = gpk_language_new ();
	gpk_language_populate (dtask->priv->language, NULL);

	/* use gconf for session settings */
	dtask->priv->gconf_client = gconf_client_get_default ();

	/* get actions */
	dtask->priv->control = pk_control_new ();
	dtask->priv->task = PK_TASK(gpk_task_new ());
	dtask->priv->roles = pk_control_get_properties (dtask->priv->control, NULL, NULL);
	dtask->priv->catalog = pk_catalog_new ();

	/* used for icons and translations */
	dtask->priv->desktop = pk_desktop_new ();
	ret = pk_desktop_open_database (dtask->priv->desktop, NULL);
	if (!ret)
		egg_warning ("failed to open desktop database");
}

/**
 * gpk_dbus_task_finalize:
 * @object: The object to finalize
 **/
static void
gpk_dbus_task_finalize (GObject *object)
{
	GpkDbusTask *dtask;
	GError *error;

	g_return_if_fail (GPK_IS_DBUS_TASK (object));

	dtask = GPK_DBUS_TASK (object);
	g_return_if_fail (dtask->priv != NULL);

	/* no reply was sent */
	if (dtask->priv->context != NULL) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "context never was returned");
		gpk_dbus_task_dbus_return_error (dtask, error);
		g_error_free (error);
	}

	g_free (dtask->priv->parent_title);
	g_free (dtask->priv->parent_icon_name);
	g_free (dtask->priv->exec);
	if (dtask->priv->cached_error_code != NULL)
		g_object_unref (dtask->priv->cached_error_code);
	g_strfreev (dtask->priv->files);
	g_strfreev (dtask->priv->package_ids);
	g_object_unref (PK_CLIENT(dtask->priv->task));
	g_object_unref (dtask->priv->control);
	g_object_unref (dtask->priv->desktop);
	g_object_unref (dtask->priv->gconf_client);
	g_object_unref (dtask->priv->dialog);
	g_object_unref (dtask->priv->vendor);
	g_object_unref (dtask->priv->language);
	g_object_unref (dtask->priv->cancellable);
	g_object_unref (dtask->priv->helper_run);
	g_object_unref (dtask->priv->helper_chooser);
	g_object_unref (dtask->priv->catalog);

	G_OBJECT_CLASS (gpk_dbus_task_parent_class)->finalize (object);
}

/**
 * gpk_dbus_task_new:
 *
 * PkClient is a nice GObject wrapper for gnome-packagekit and makes installing software easy
 *
 * Return value: A new %GpkDbusTask instance
 **/
GpkDbusTask *
gpk_dbus_task_new (void)
{
	GpkDbusTask *dtask;
	dtask = g_object_new (GPK_TYPE_DBUS_TASK, NULL);
	return GPK_DBUS_TASK (dtask);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpk_dbus_task_test (gpointer data)
{
	EggTest *test = (EggTest *) data;
	GpkDbusTask *dtask;
	gchar *lang;
	gchar *language;
	gchar *package;
	gboolean ret;
#if 0
	const gchar *fonts[] = { ":lang=mn", NULL };
	GError *error;
#endif

	if (egg_test_start (test, "GpkChooser") == FALSE)
		return;

	/************************************************************/
	egg_test_title (test, "get GpkDbusTask object");
	dtask = gpk_dbus_task_new ();
	if (dtask != NULL)
		egg_test_success (test, NULL);
	else
		egg_warning (NULL);

	/************************************************************/
	egg_test_title (test, "convert tag to lang");
	lang = gpk_dbus_task_font_tag_to_lang (":lang=mn");
	if (g_strcmp0 (lang, "mn") == 0)
		egg_test_success (test, NULL);
	else
		egg_warning ("lang '%s'", lang);
	g_free (lang);

	/************************************************************/
	egg_test_title (test, "convert tag to language");
	language = gpk_dbus_task_font_tag_to_localised_name (dtask, ":lang=mn");
	if (g_strcmp0 (language, "Mongolian") == 0)
		egg_test_success (test, NULL);
	else
		egg_warning ("language '%s'", language);
	g_free (language);

	/************************************************************/
	egg_test_title (test, "test trusted path");
	ret = gpk_dbus_task_path_is_trusted ("/usr/libexec/gst-install-plugins-helper");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_warning ("failed to identify trusted");

	/************************************************************/
	egg_test_title (test, "test trusted path");
	ret = gpk_dbus_task_path_is_trusted ("/usr/bin/totem");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_warning ("identify untrusted as trusted!");

	/************************************************************/
	egg_test_title (test, "get package for exec");
	package = gpk_dbus_task_get_package_for_exec (dtask, "/usr/bin/totem");
	if (g_strcmp0 (package, "totem") == 0)
		egg_test_success (test, NULL);
	else
		egg_warning ("package '%s'", package);
	g_free (package);

	/************************************************************/
	egg_test_title (test, "set exec");
	ret = gpk_dbus_task_set_exec (dtask, "/usr/bin/totem");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_warning ("failed to set exec");

#if 0
	/************************************************************/
	egg_test_title (test, "install fonts (no UI)");
	error = NULL;
	gpk_dbus_task_set_interaction (dtask, GPK_CLIENT_INTERACT_NEVER);
	ret = gpk_dbus_task_install_fontconfig_resources (dtask, (gchar**)fonts, &error);
	if (ret)
		egg_test_success (test, NULL);
	else {
		/* success until we can do the server parts */
		egg_test_success (test, "failed to install font : %s", error->message);
		g_error_free (error);
	}

	/************************************************************/
	egg_test_title (test, "install fonts (if found)");
	error = NULL;
	gpk_dbus_task_set_interaction (dtask, pk_bitfield_from_enums (GPK_CLIENT_INTERACT_CONFIRM_SEARCH, GPK_CLIENT_INTERACT_FINISHED, -1));
	ret = gpk_dbus_task_install_fontconfig_resources (dtask, (gchar**)fonts, &error);
	if (ret)
		egg_test_success (test, NULL);
	else {
		/* success until we can do the server parts */
		egg_test_success (test, "failed to install font : %s", error->message);
		g_error_free (error);
	}

	/************************************************************/
	egg_test_title (test, "install fonts (always)");
	error = NULL;
	gpk_dbus_task_set_interaction (dtask, GPK_CLIENT_INTERACT_ALWAYS);
	ret = gpk_dbus_task_install_fontconfig_resources (dtask, (gchar**)fonts, &error);
	if (ret)
		egg_test_success (test, NULL);
	else {
		/* success until we can do the server parts */
		egg_test_success (test, "failed to install font : %s", error->message);
		g_error_free (error);
	}
#endif

	egg_test_end (test);
}
#endif

