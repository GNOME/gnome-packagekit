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

/**
 * SECTION:gpk-client
 * @short_description: GObject class for libpackagekit-gnome client access
 *
 * A nice GObject to use for installing software in GNOME applications
 */

#include "config.h"

#include <unistd.h>
#include <string.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <polkit-gnome/polkit-gnome.h>
#include <libnotify/notify.h>

#include <pk-client.h>
#include <pk-package-id.h>
#include <pk-package-ids.h>
#include <pk-extra.h>
#include <pk-common.h>
#include <pk-control.h>
#include <pk-catalog.h>
#include <pk-distro-upgrade-obj.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-client.h"
#include "gpk-client-eula.h"
#include "gpk-client-signature.h"
#include "gpk-client-untrusted.h"
#include "gpk-client-chooser.h"
#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-error.h"
#include "gpk-consolekit.h"
#include "gpk-animated-icon.h"
#include "gpk-client-dialog.h"
#include "gpk-dialog.h"
#include "gpk-enum.h"
#include "gpk-x11.h"

static void     gpk_client_class_init	(GpkClientClass *klass);
static void     gpk_client_init		(GpkClient      *gclient);
static void     gpk_client_finalize	(GObject	*object);

#define GPK_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_CLIENT, GpkClientPrivate))
#define GPK_CLIENT_FINISHED_AUTOCLOSE_DELAY	10 /* seconds */
/**
 * GpkClientPrivate:
 *
 * Private #GpkClient data
 **/
struct _GpkClientPrivate
{
	PkClient		*client_action;
	PkClient		*client_resolve;
	PkClient		*client_secondary;
	GConfClient		*gconf_client;
	GpkClientDialog		*dialog;
	guint			 finished_timer_id;
	PkExtra			*extra;
	PkControl		*control;
	PkBitfield		 roles;
	gboolean		 using_secondary_client;
	gboolean		 retry_untrusted_value;
	gboolean		 show_confirm;
	gboolean		 show_progress;
	gboolean		 show_finished;
	gboolean		 show_warning;
	gchar			**files_array;
	PkExitEnum		 exit;
	GdkWindow		*parent_window;
	GPtrArray		*upgrade_array;
	guint			 timestamp;
	gchar			*parent_title;
	gchar			*parent_icon_name;
	GMainLoop		*loop;
};

enum {
	GPK_CLIENT_QUIT,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkClient, gpk_client, G_TYPE_OBJECT)

/**
 * gpk_client_error_quark:
 *
 * Return value: Our personal error quark.
 **/
GQuark
gpk_client_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("gpk_client_error");
	return quark;
}

/**
 * gpk_client_error_get_type:
 **/
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
gpk_client_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (GPK_CLIENT_ERROR_FAILED, "Failed"),
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("PkClientError", values);
	}
	return etype;
}

/**
 * gpk_install_finished_timeout:
 **/
static gboolean
gpk_install_finished_timeout (gpointer data)
{
	GpkClient *gclient = (GpkClient *) data;

	/* debug so we can catch polling */
	egg_debug ("polling check");

	/* hide window manually to get it out of the way */
	gpk_client_dialog_close (gclient->priv->dialog);

	/* the timer will be done */
	gclient->priv->finished_timer_id = 0;

	egg_debug ("quitting due to timeout");
	g_signal_emit (gclient, signals [GPK_CLIENT_QUIT], 0);
	return FALSE;
}

/**
 * gpk_client_set_interaction:
 **/
void
gpk_client_set_interaction (GpkClient *gclient, PkBitfield interact)
{
	g_return_if_fail (GPK_IS_CLIENT (gclient));
	gclient->priv->show_confirm = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_CONFIRM);
	gclient->priv->show_progress = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_PROGRESS);
	gclient->priv->show_finished = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_FINISHED);
	gclient->priv->show_warning = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_WARNING);
}

/**
 * gpk_client_libnotify_cb:
 **/
static void
gpk_client_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data)
{
	gboolean ret;
	GError *error = NULL;
	GpkClient *gclient = GPK_CLIENT (data);

	if (egg_strequal (action, "do-not-show-complete-restart")) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_UPDATE_COMPLETE_RESTART);
		gconf_client_set_bool (gclient->priv->gconf_client, GPK_CONF_NOTIFY_UPDATE_COMPLETE_RESTART, FALSE, NULL);
	} else if (egg_strequal (action, "do-not-show-complete")) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_UPDATE_COMPLETE);
		gconf_client_set_bool (gclient->priv->gconf_client, GPK_CONF_NOTIFY_UPDATE_COMPLETE, FALSE, NULL);
	} else if (egg_strequal (action, "do-not-show-update-started")) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_UPDATE_STARTED);
		gconf_client_set_bool (gclient->priv->gconf_client, GPK_CONF_NOTIFY_UPDATE_STARTED, FALSE, NULL);
	} else if (egg_strequal (action, "cancel")) {
		/* try to cancel */
		ret = pk_client_cancel (gclient->priv->client_action, &error);
		if (!ret) {
			egg_warning ("failed to cancel client: %s", error->message);
			g_error_free (error);
		}
	} else if (egg_strequal (action, "restart-computer")) {
		/* restart using gnome-power-manager */
		ret = gpk_restart_system ();
		if (!ret)
			egg_warning ("failed to reboot");
	} else {
		egg_warning ("unknown action id: %s", action);
	}
}

/**
 * gpk_client_finished_no_progress:
 **/
static void
gpk_client_finished_no_progress (PkClient *client, PkExitEnum exit_code, guint runtime, GpkClient *gclient)
{
	gboolean ret;
	GError *error = NULL;
	NotifyNotification *notification;
	PkRestartEnum restart;
	guint i;
	guint length;
	PkPackageList *list;
	const PkPackageObj *obj;
	GString *message_text;
	guint skipped_number = 0;
	const gchar *message;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	/* check we got some packages */
	list = pk_client_get_package_list (client);
	length = pk_package_list_get_size (list);
	egg_debug ("length=%i", length);
	if (length == 0) {
		egg_debug ("no updates");
		return;
	}

	message_text = g_string_new ("");

	/* find any we skipped */
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		egg_debug ("%s, %s, %s", pk_info_enum_to_text (obj->info),
			  obj->id->name, obj->summary);
		if (obj->info == PK_INFO_ENUM_BLOCKED) {
			skipped_number++;
			g_string_append_printf (message_text, "<b>%s</b> - %s\n",
						obj->id->name, obj->summary);
		}
	}
	g_object_unref (list);

	/* notify the user if there were skipped entries */
	if (skipped_number > 0) {
		message = ngettext (_("One package was skipped:"),
				    _("Some packages were skipped:"), skipped_number);
		g_string_prepend (message_text, message);
		g_string_append_c (message_text, '\n');
	}

	/* add a message that we need to restart */
	restart = pk_client_get_require_restart (client);
	if (restart != PK_RESTART_ENUM_NONE) {
		message = gpk_restart_enum_to_localised_text (restart);

		/* add a gap if we are putting both */
		if (skipped_number > 0)
			g_string_append (message_text, "\n");

		g_string_append (message_text, message);
		g_string_append_c (message_text, '\n');
	}

	/* trim off extra newlines */
	if (message_text->len != 0)
		g_string_set_size (message_text, message_text->len-1);

	/* do we do the notification? */
	ret = gconf_client_get_bool (gclient->priv->gconf_client, GPK_CONF_NOTIFY_UPDATE_COMPLETE, NULL);
	if (!ret) {
		egg_debug ("ignoring due to GConf");
		return;
	}

	/* do the bubble */
	notification = notify_notification_new (_("The system update has completed"), message_text->str, "help-browser", NULL);
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	if (restart == PK_RESTART_ENUM_SYSTEM) {
		notify_notification_add_action (notification, "restart",
						_("Restart computer now"), gpk_client_libnotify_cb, gclient, NULL);
		notify_notification_add_action (notification, "do-not-show-complete-restart",
						_("Do not show this again"), gpk_client_libnotify_cb, gclient, NULL);
	} else {
		notify_notification_add_action (notification, "do-not-show-complete",
						_("Do not show this again"), gpk_client_libnotify_cb, gclient, NULL);
	}
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
	g_string_free (message_text, TRUE);
}

/**
 * gpk_client_finished_cb:
 **/
static void
gpk_client_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, GpkClient *gclient)
{
	PkRoleEnum role = PK_ROLE_ENUM_UNKNOWN;
	PkPackageList *list;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	/* save this so we can return a proper error value */
	gclient->priv->exit = exit;

	/* stop timers, animations and that sort of thing */

	pk_client_get_role (client, &role, NULL, NULL);
	/* do nothing */
	if (role == PK_ROLE_ENUM_GET_UPDATES)
		goto out;

	/* stop spinning */
	gpk_client_dialog_set_percentage (gclient->priv->dialog, 100);

	/* do we show a libnotify window instead? */
	if (!gclient->priv->show_progress) {
		gpk_client_finished_no_progress (client, exit, runtime, gclient);
		goto out;
	}

	if (exit == PK_EXIT_ENUM_SUCCESS && gclient->priv->show_finished) {
		list = pk_client_get_package_list (client);
		gpk_client_dialog_set_message (gclient->priv->dialog, _("The following packages were installed:"));
		gpk_client_dialog_set_package_list (gclient->priv->dialog, list);
		gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_FINISHED, GPK_CLIENT_DIALOG_PACKAGE_LIST, 0);
		g_object_unref (list);
		gclient->priv->finished_timer_id = g_timeout_add_seconds (GPK_CLIENT_FINISHED_AUTOCLOSE_DELAY,
									  gpk_install_finished_timeout, gclient);
	} else {
		gpk_client_dialog_close (gclient->priv->dialog);
	}

out:
	/* only quit if there is not another transaction scheduled to be finished */
	if (!gclient->priv->using_secondary_client) {
		egg_debug ("quitting due to finished");
		if (g_main_loop_is_running (gclient->priv->loop))
			g_main_loop_quit (gclient->priv->loop);
	}
}

/**
 * gpk_client_set_status:
 **/
static gboolean
gpk_client_set_status (GpkClient *gclient, PkStatusEnum status)
{
	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* do we force progress? */
	if (!gclient->priv->show_progress) {
		if (status == PK_STATUS_ENUM_DOWNLOAD_REPOSITORY ||
		    status == PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST ||
		    status == PK_STATUS_ENUM_DOWNLOAD_FILELIST ||
		    status == PK_STATUS_ENUM_DOWNLOAD_CHANGELOG ||
		    status == PK_STATUS_ENUM_DOWNLOAD_GROUP ||
		    status == PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO ||
		    status == PK_STATUS_ENUM_REFRESH_CACHE) {
			gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-progress");
			gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0, 0);
		}
	}

	/* ignore */
	if (!gclient->priv->show_progress)
		return FALSE;

	/* set icon */
	gpk_client_dialog_set_image_status (gclient->priv->dialog, status);

	/* set label */
	gpk_client_dialog_set_title (gclient->priv->dialog, gpk_status_enum_to_localised_text (status));

	/* spin */
	if (status == PK_STATUS_ENUM_WAIT)
		gpk_client_dialog_set_percentage (gclient->priv->dialog, PK_CLIENT_PERCENTAGE_INVALID);

	/* do visual stuff when finished */
	if (status == PK_STATUS_ENUM_FINISHED) {
		/* make insensitive */
		gpk_client_dialog_set_allow_cancel (gclient->priv->dialog, FALSE);

		/* stop spinning */
		gpk_client_dialog_set_percentage (gclient->priv->dialog, 100);
	}
	return TRUE;
}

/**
 * gpk_client_progress_changed_cb:
 **/
static void
gpk_client_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				guint elapsed, guint remaining, GpkClient *gclient)
{
	/* ignore */
	if (!gclient->priv->show_progress)
		return;
	gpk_client_dialog_set_percentage (gclient->priv->dialog, percentage);
}

/**
 * gpk_client_status_changed_cb:
 **/
static void
gpk_client_status_changed_cb (PkClient *client, PkStatusEnum status, GpkClient *gclient)
{
	if (gclient->priv->show_progress)
		gpk_client_set_status (gclient, status);
}

/**
 * gpk_client_error_code_cb:
 **/
static void
gpk_client_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, GpkClient *gclient)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *title;
	const gchar *message;
	NotifyNotification *notification;
	GtkWidget *widget;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	/* have we handled? */
	if (code == PK_ERROR_ENUM_GPG_FAILURE ||
	    code == PK_ERROR_ENUM_NO_LICENSE_AGREEMENT) {
		if (gclient->priv->using_secondary_client) {
			egg_debug ("ignoring error as handled");
			return;
		}
		egg_warning ("did not auth");
	}

	/* have we handled? */
	if (code == PK_ERROR_ENUM_BAD_GPG_SIGNATURE ||
	    code == PK_ERROR_ENUM_MISSING_GPG_SIGNATURE) {
		egg_debug ("handle and requeue");
		gclient->priv->retry_untrusted_value = gpk_client_untrusted_show (code);
		return;
	}

	/* ignore some errors */
	if (code == PK_ERROR_ENUM_PROCESS_KILL ||
	    code == PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
		egg_debug ("error ignored %s\n%s", pk_error_enum_to_text (code), details);
		return;
	}

	egg_debug ("code was %s", pk_error_enum_to_text (code));

	/* use a modal dialog if showing progress, else use libnotify */
	title = gpk_error_enum_to_localised_text (code);
	message = gpk_error_enum_to_localised_message (code);
	if (gclient->priv->show_progress) {
		widget = GTK_WIDGET (gpk_client_dialog_get_window (gclient->priv->dialog));
		gpk_error_dialog_modal (GTK_WINDOW (widget), title, message, details);
		return;
	}

	/* do the bubble */
	notification = notify_notification_new (title, message, "help-browser", NULL);
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_client_package_cb:
 **/
static void
gpk_client_package_cb (PkClient *client, const PkPackageObj *obj, GpkClient *gclient)
{
	gchar *text;
	g_return_if_fail (GPK_IS_CLIENT (gclient));

	if (!gclient->priv->show_progress)
		return;

	text = gpk_package_id_format_twoline (obj->id, obj->summary);
	gpk_client_dialog_set_message (gclient->priv->dialog, text);
	g_free (text);
}

/**
 * pk_client_distro_upgrade_cb:
 **/
static void
pk_client_distro_upgrade_cb (PkClient *client, const PkDistroUpgradeObj *obj, GpkClient *gclient)
{
	g_return_if_fail (GPK_IS_CLIENT (gclient));

	/* copy into array */
	g_ptr_array_add (gclient->priv->upgrade_array, pk_distro_upgrade_obj_copy (obj));
	egg_debug ("%s, %s, %s", obj->name, pk_update_state_enum_to_text (obj->state), obj->summary);
}

/**
 * gpk_client_files_cb:
 **/
static void
gpk_client_files_cb (PkClient *client, const gchar *package_id,
		     const gchar *filelist, GpkClient *gclient)
{
	g_return_if_fail (GPK_IS_CLIENT (gclient));

	/* free old array and set new */
	g_strfreev (gclient->priv->files_array);

	/* no data, eugh */
	if (egg_strzero (filelist)) {
		gclient->priv->files_array = NULL;
		return;
	}

	/* set new */
	gclient->priv->files_array = g_strsplit (filelist, ";", 0);
}

/**
 * gpk_client_allow_cancel_cb:
 **/
static void
gpk_client_allow_cancel_cb (PkClient *client, gboolean allow_cancel, GpkClient *gclient)
{
	gpk_client_dialog_set_allow_cancel (gclient->priv->dialog, allow_cancel);
}

/**
 * pk_client_button_close_cb:
 **/
static void
pk_client_button_close_cb (GtkWidget *widget, GpkClient *gclient)
{
	/* stop the timers if running */
	if (gclient->priv->finished_timer_id != 0)
		g_source_remove (gclient->priv->finished_timer_id);

	/* close, don't abort */
	gpk_client_dialog_close (gclient->priv->dialog);
}

/**
 * pk_client_button_cancel_cb:
 **/
static void
pk_client_button_cancel_cb (GtkWidget *widget, GpkClient *gclient)
{
	gboolean ret;
	GError *error = NULL;

	/* we might have a transaction running */
	ret = pk_client_cancel (gclient->priv->client_action, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_client_error_msg:
 **/
static void
gpk_client_error_msg (GpkClient *gclient, const gchar *title, GError *error)
{
	GtkWindow *window;
	const gchar *message = _("Unknown error. Please refer to the detailed report and report in bugzilla.");
	const gchar *details = NULL;

	if (!gclient->priv->show_warning)
		return;

	/* print a proper error if we have it */
	if (error != NULL) {
		if (error->code == PK_CLIENT_ERROR_FAILED_AUTH ||
		    g_str_has_prefix (error->message, "org.freedesktop.packagekit.")) {
			message = _("You don't have the necessary privileges to perform this action");
			gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-permissions");
		} else if (error->code == PK_CLIENT_ERROR_CANNOT_START_DAEMON) {
			message = _("The packagekitd service could not be started");
			gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-no-service");
		} else {
			details = error->message;
			gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
		}
	}

	/* it's a normal UI, not a backtrace so keep in the UI */
	if (details == NULL) {
		gpk_client_dialog_set_title (gclient->priv->dialog, title);
		gpk_client_dialog_set_message (gclient->priv->dialog, message);
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, gclient->priv->timestamp);
		gpk_client_dialog_run (gclient->priv->dialog);
		return;
	}

	/* hide the main window */
	window = gpk_client_dialog_get_window (gclient->priv->dialog);
	gpk_error_dialog_modal_with_time (window, title, message, details, gclient->priv->timestamp);
}

/**
 * gpk_client_error_set:
 *
 * Sets the correct error code (if allowed) and print to the screen
 * as a warning.
 **/
static gboolean
gpk_client_error_set (GError **error, gint code, const gchar *format, ...)
{
	va_list args;
	gchar *buffer = NULL;
	gboolean ret = TRUE;

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	/* dumb */
	if (error == NULL) {
		egg_warning ("No error set, so can't set: %s", buffer);
		ret = FALSE;
		goto out;
	}

	/* already set */
	if (*error != NULL) {
		egg_warning ("not NULL error!");
		g_clear_error (error);
	}

	/* propogate */
	g_set_error (error, GPK_CLIENT_ERROR, code, "%s", buffer);

out:
	g_free(buffer);
	return ret;
}

/**
 * gpk_client_install_local_files_internal:
 **/
static gboolean
gpk_client_install_local_files_internal (GpkClient *gclient, gboolean trusted,
					 gchar **files_rel, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	guint length;
	const gchar *title;

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset action client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		return FALSE;
	}

	/* install local file */
	ret = pk_client_install_files (gclient->priv->client_action, trusted, files_rel, &error_local);
	if (ret)
		return TRUE;

	/* wait for an answer */
	g_main_loop_run (gclient->priv->loop);

	length = g_strv_length (files_rel);
	title = ngettext ("Failed to install file", "Failed to install files", length);
	gpk_client_error_msg (gclient, title, error_local);
	gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
	g_error_free (error_local);
	return FALSE;
}

/**
 * gpk_client_set_error_from_exit_enum:
 **/
static gboolean
gpk_client_set_error_from_exit_enum (PkExitEnum exit, GError **error)
{
	/* trivial case */
	if (exit == PK_EXIT_ENUM_SUCCESS)
		return TRUE;

	/* set the correct error type */
	if (exit == PK_EXIT_ENUM_FAILED)
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "Unspecified failure");
	else if (exit == PK_EXIT_ENUM_CANCELLED)
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "Transaction was cancelled");
	else if (exit == PK_EXIT_ENUM_KEY_REQUIRED)
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "A key was required but not provided");
	else if (exit == PK_EXIT_ENUM_EULA_REQUIRED)
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "A EULA was not agreed to");
	else if (exit == PK_EXIT_ENUM_KILLED)
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "The transaction was killed");
	else
		egg_error ("unknown exit code");
	return FALSE;
}

/**
 * _g_ptr_array_to_bullets:
 *
 * splits the strings up nicely
 *
 * Return value: a newly allocated string
 **/
static gchar *
_g_ptr_array_to_bullets (GPtrArray *array, const gchar *prefix)
{
	GString *string;
	guint i;
	gchar *text;

	string = g_string_new (prefix);
	if (prefix != NULL)
		g_string_append_c (string, '\n');

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
 * _g_ptr_array_copy_deep:
 *
 * Deep copy a GPtrArray of strings
 *
 * Return value: A new GPtrArray
 **/
static GPtrArray *
_g_ptr_array_copy_deep (GPtrArray *array)
{
	guint i;
	const gchar *data;
	GPtrArray *array_new;

	array_new = g_ptr_array_new ();
	for (i=0; i<array->len; i++) {
		data = (const gchar *) g_ptr_array_index (array, i);
		g_ptr_array_add (array_new, g_strdup (data));
	}
	return array_new;
}

/**
 * gpk_check_permissions:
 * @filename: a filename to check
 * @euid: the effective user ID to check for, or the output of geteuid()
 * @egid: the effective group ID to check for, or the output of getegid()
 * @mode: bitfield of R_OK, W_OK, XOK
 *
 * Like, access but a bit more accurate - access will let root do anything.
 * Does not get read-only or no-exec filesystems right.
 *
 * Return value: %TRUE if the file has access perms
 **/
static gboolean
gpk_check_permissions (const gchar *filename, guint euid, guint egid, guint mode)
{
	struct stat statbuf;

	if (stat (filename, &statbuf) == 0) {
		if ((mode & R_OK) &&
		    !((statbuf.st_mode & S_IROTH) ||
		      ((statbuf.st_mode & S_IRUSR) && euid == statbuf.st_uid) ||
		      ((statbuf.st_mode & S_IRGRP) && egid == statbuf.st_gid)))
			return FALSE;
		if ((mode & W_OK) &&
		    !((statbuf.st_mode & S_IWOTH) ||
		      ((statbuf.st_mode & S_IWUSR) && euid == statbuf.st_uid) ||
		      ((statbuf.st_mode & S_IWGRP) && egid == statbuf.st_gid)))
			return FALSE;
		if ((mode & X_OK) &&
		    !((statbuf.st_mode & S_IXOTH) ||
		      ((statbuf.st_mode & S_IXUSR) && euid == statbuf.st_uid) ||
		      ((statbuf.st_mode & S_IXGRP) && egid == statbuf.st_gid)))
			return FALSE;
		return TRUE;
	}
	return FALSE;
}

/**
 * gpk_client_install_local_files_copy_private:
 *
 * Allow the user to confirm the package copy to /tmp
 *
 * Return value: %TRUE if the method succeeded
 **/
static gboolean
gpk_client_install_local_files_copy_private (GpkClient *gclient, GPtrArray *array, GError **error)
{
	guint i;
	gchar *data;
	gboolean ret;
	GPtrArray *array_new;
	GPtrArray *array_missing;
	const gchar *message_part;
	const gchar *title;
	gchar *message;
	GtkResponseType button;

	/* see if root has access to this file, in case we have to copy it
	 * somewhere where it does.
	 * See https://bugzilla.redhat.com/show_bug.cgi?id=456094 */
	array_missing = g_ptr_array_new ();
	for (i=0; i<array->len; i++) {
		data = (gchar *) g_ptr_array_index (array, i);
		ret = gpk_check_permissions (data, 0, 0, R_OK);
		if (!ret)
			g_ptr_array_add (array_missing, g_strdup (data));
	}

	/* optional */
	if (gclient->priv->show_confirm && array_missing->len > 0) {
		title = ngettext (_("Do you want to copy this file?"),
				  _("Do you want to copy these files?"), array_missing->len);
		message_part = ngettext (_("One package file has to be copied to a non-private location so it can be installed:"),
					 _("Some package files have to be copied to a non-private location so they can be installed:"),
					 array_missing->len);
		message = _g_ptr_array_to_bullets (array_missing, message_part);

		/* show UI */
		gpk_client_dialog_set_title (gclient->priv->dialog, title);
		gpk_client_dialog_set_message (gclient->priv->dialog, message);
		gpk_client_dialog_set_image (gclient->priv->dialog, "dialog-warning");
		gpk_client_dialog_set_action (gclient->priv->dialog, _("Copy file"));
		gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-installing-private-files");
		g_free (message);

		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_CONFIRM, 0, gclient->priv->timestamp);
		button = gpk_client_dialog_run (gclient->priv->dialog);
		/* did we click no or exit the window? */
		if (button != GTK_RESPONSE_OK) {
			gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "Aborted the copy");
			ret = FALSE;
			goto out;
		}
	}

	/* copy, and re-allocate so we can pass back the same array */
	array_new = _g_ptr_array_copy_deep (array);
	g_ptr_array_remove_range (array, 0, array->len);

	/* now we have the okay to copy the files, do so */
	ret = TRUE;
	for (i=0; i<array_new->len; i++) {
		gchar *command;
		gchar *dest;
		gchar *dest_path;
		gint retval;
		GError *error = NULL;

		data = (gchar *) g_ptr_array_index (array_new, i);
		ret = gpk_check_permissions (data, 0, 0, R_OK);
		if (ret) {
			/* just copy over the name */
			g_ptr_array_add (array, g_strdup (data));
		} else {
			/* get the final location */
			dest = g_path_get_basename (data);
			dest_path = g_strdup_printf ("/tmp/%s", dest);

			command = g_strdup_printf ("cp \"%s\" \"%s\"", data, dest_path);
			egg_debug ("command=%s", command);
			ret = g_spawn_command_line_sync (command, NULL, NULL, NULL, &error);

			/* we failed */
			if (!ret) {
				egg_warning ("failed to copy %s: %s", data, error->message);
				g_error_free (error);
				break;
			}

			/* make this readable by root */
			retval = g_chmod (dest_path, 0644);
			if (retval < 0) {
				ret = FALSE;
				egg_warning ("failed to chmod %s", dest_path);
				break;
			}

			/* add the modified file item */
			g_ptr_array_add (array, g_strdup (dest_path));

			g_free (dest);
			g_free (dest_path);
			g_free (command);
		}
	}

	/* did we fail to copy the files */
	if (!ret) {
		title = ngettext (_("The file could not be copied"),
				  _("The files could not be copied"), array_missing->len);

		/* show UI */
		gpk_client_dialog_set_title (gclient->priv->dialog, title);
		gpk_client_dialog_set_message (gclient->priv->dialog, "");
		gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, gclient->priv->timestamp);
		gpk_client_dialog_run (gclient->priv->dialog);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "files not copied");
		ret = FALSE;
		goto out;
	}
out:
	g_ptr_array_foreach (array_missing, (GFunc) g_free, NULL);
	g_ptr_array_free (array_missing, TRUE);
	return ret;
}

/**
 * gpk_client_install_local_files_verify:
 *
 * Allow the user to confirm the action
 *
 * Return value: %TRUE if the method succeeded
 **/
static gboolean
gpk_client_install_local_files_verify (GpkClient *gclient, GPtrArray *array, GError **error)
{
	GtkResponseType button;
	const gchar *title;
	gchar *message;
	gboolean ret = TRUE;

	title = ngettext (_("Do you want to install this file?"),
			  _("Do you want to install these files?"), array->len);
	message = _g_ptr_array_to_bullets (array, NULL);

	/* show UI */
	gpk_client_dialog_set_title (gclient->priv->dialog, title);
	gpk_client_dialog_set_message (gclient->priv->dialog, message);
	gpk_client_dialog_set_action (gclient->priv->dialog, _("Install"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-install-files");
	gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_CONFIRM, 0, gclient->priv->timestamp);
	button = gpk_client_dialog_run (gclient->priv->dialog);
	g_free (message);

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK) {
		title = ngettext (_("The file was not installed"),
				  _("The files were not installed"), array->len);
		gpk_client_dialog_set_title (gclient->priv->dialog, title);
		gpk_client_dialog_set_message (gclient->priv->dialog, "");
		gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, gclient->priv->timestamp);
		gpk_client_dialog_run (gclient->priv->dialog);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "Aborted");
		ret = FALSE;
		goto out;
	}
out:
	return ret;
}

/**
 * gpk_client_install_local_files_check_exists:
 *
 * Skip files that are not present
 *
 * Return value: %TRUE if the method succeeded
 **/
static gboolean
gpk_client_install_local_files_check_exists (GpkClient *gclient, GPtrArray *array, GError **error)
{
	guint i;
	gchar *data;
	gboolean ret;
	GPtrArray *array_missing;
	const gchar *message_part;
	const gchar *title;
	gchar *message;

	array_missing = g_ptr_array_new ();

	/* find missing */
	for (i=0; i<array->len; i++) {
		data = (gchar *) g_ptr_array_index (array, i);
		ret = g_file_test (data, G_FILE_TEST_EXISTS);
		if (!ret)
			g_ptr_array_add (array_missing, g_strdup (data));
	}

	/* warn, set error and quit */
	ret = TRUE;
	if (array_missing->len > 0) {
		title = ngettext (_("File was not found!"),
				  _("Files were not found!"), array_missing->len);

		message_part = ngettext (_("The following file was not found:"),
					 _("The following files were not found:"), array_missing->len);
		message = _g_ptr_array_to_bullets (array_missing, message_part);

		/* show UI */
		gpk_client_dialog_set_title (gclient->priv->dialog, title);
		gpk_client_dialog_set_message (gclient->priv->dialog, message);
		gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, gclient->priv->timestamp);
		gpk_client_dialog_run (gclient->priv->dialog);

		g_free (message);

		ret = FALSE;
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "some files did not exist");
		goto out;
	}

out:
	g_ptr_array_foreach (array_missing, (GFunc) g_free, NULL);
	g_ptr_array_free (array_missing, TRUE);
	return ret;
}

/**
 * gpk_client_confirm_action:
 * @gclient: a valid #GpkClient instance
 **/
static gboolean
gpk_client_confirm_action (GpkClient *gclient, const gchar *title, const gchar *message)
{
	GtkResponseType button;
	gchar *title_name;

	/* make title */
	if (gclient->priv->parent_title != NULL)
		title_name = g_strdup_printf ("%s %s", gclient->priv->parent_title, title);
	else {
		/* translator comment -- string is an action, e.g. "wants to install a codec" */
		title_name = g_strdup_printf (_("A program %s"), title);
	}

	/* set icon */
	if (gclient->priv->parent_icon_name != NULL)
		gpk_client_dialog_set_image (gclient->priv->dialog, gclient->priv->parent_icon_name);
	else
		gpk_client_dialog_set_image (gclient->priv->dialog, "emblem-system");

	gpk_client_dialog_set_title (gclient->priv->dialog, title_name);
	gpk_client_dialog_set_message (gclient->priv->dialog, message);
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-application-confirm");
	gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_CONFIRM, 0, gclient->priv->timestamp);

	g_free (title_name);
	button = gpk_client_dialog_run (gclient->priv->dialog);

	/* close, we're going to fail the method */
	if (button != GTK_RESPONSE_OK) {
		gpk_client_dialog_close (gclient->priv->dialog);
		return FALSE;
	}

	return TRUE;
}

/**
 * gpk_client_install_local_files:
 * @gclient: a valid #GpkClient instance
 * @file_rel: a file such as <literal>./hal-devel-0.10.0.rpm</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a file locally, and get the deps from the repositories.
 * This is useful for double clicking on a .rpm or .deb file.
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_install_local_files (GpkClient *gclient, gchar **files_rel, GError **error)
{
	gboolean ret;
	gchar **files = NULL;
	GPtrArray *array;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (files_rel != NULL, FALSE);

	array = pk_strv_to_ptr_array (files_rel);

	/* check the user wanted to call this method */
	if (gclient->priv->show_confirm) {
		ret = gpk_client_install_local_files_verify (gclient, array, error);
		if (!ret)
			goto out;
	}

	/* check all files exist and are readable by the local user */
	ret = gpk_client_install_local_files_check_exists (gclient, array, error);
	if (!ret)
		goto out;

	/* check all files exist and are readable by the local user */
	ret = gpk_client_install_local_files_copy_private (gclient, array, error);
	if (!ret)
		goto out;

	files = pk_ptr_array_to_strv (array);
	gclient->priv->retry_untrusted_value = FALSE;
	ret = gpk_client_install_local_files_internal (gclient, TRUE, files, error);
	if (!ret)
		goto out;

	/* set title */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Install local file"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0, 0);

	/* wait */
	g_main_loop_run (gclient->priv->loop);

	/* do we need to try again with better auth? */
	if (gclient->priv->retry_untrusted_value) {
		ret = gpk_client_install_local_files_internal (gclient, FALSE, files, error);
		if (!ret)
			goto out;
		/* wait again */
		g_main_loop_run (gclient->priv->loop);
	}

	/* fail the transaction and set the correct error */
	ret = gpk_client_set_error_from_exit_enum (gclient->priv->exit, error);

out:
	g_strfreev (files);
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);
	return ret;
}

/**
 * gpk_client_remove_package_ids:
 * @gclient: a valid #GpkClient instance
 * @package_id: a package_id such as <literal>hal-info;0.20;i386;fedora</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_remove_package_ids (GpkClient *gclient, gchar **package_ids, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GtkResponseType button;
	PkPackageList *list;
	guint length;
	gchar *title;
	gchar *message;
	gchar *name;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* are we dumb and can't check for depends? */
	if (!pk_bitfield_contain (gclient->priv->roles, PK_ROLE_ENUM_GET_REQUIRES)) {
		egg_warning ("skipping depends check");
		goto skip_checks;
	}

	/* optional */
	if (!gclient->priv->show_confirm) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* get the packages we depend on */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Finding packages we require"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-finding-requires");

	/* setup the UI */
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0, 0);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_resolve, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset resolve client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* find out if this would force removal of other packages */
	ret = pk_client_get_requires (gclient->priv->client_resolve, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_ids, TRUE, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Could not work out what packages would also be removed"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* these are the new packages */
	list = pk_client_get_package_list (gclient->priv->client_resolve);

	/* no deps */
	length = pk_package_list_get_size (list);
	if (length == 0)
		goto skip_checks;

	/* sort by package_id */
	pk_package_list_sort (list);

	/* title */
	title = g_strdup_printf (ngettext ("%i additional package also has to be removed",
					   "%i additional packages also have to be removed",
					   length), length);

	/* message */
	name = gpk_dialog_package_id_name_join_locale (package_ids);
	length = g_strv_length (package_ids);
	message = g_strdup_printf (ngettext ("To remove %s other packages that depend on it must also be removed.",
					     "To remove %s other packages that depend on them must also be removed.",
					     length), name);
	g_free (name);

	/* show UI */
	gpk_client_dialog_set_package_list (gclient->priv->dialog, list);
	gpk_client_dialog_set_title (gclient->priv->dialog, title);
	gpk_client_dialog_set_message (gclient->priv->dialog, message);
	gpk_client_dialog_set_action (gclient->priv->dialog, _("Remove"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-remove-other-packages");
	gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_CONFIRM, GPK_CLIENT_DIALOG_PACKAGE_LIST, gclient->priv->timestamp);
	g_free (title);
	g_free (message);
	button = gpk_client_dialog_run (gclient->priv->dialog);

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK) {
		gpk_client_dialog_close (gclient->priv->dialog);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "did not agree to additional requires");
		ret = FALSE;
		goto out;
	}

skip_checks:
	/* set title */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Remove packages"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
	gpk_client_dialog_set_message (gclient->priv->dialog, "");

	/* setup the UI */
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, GPK_CLIENT_DIALOG_PACKAGE_PADDING, 0);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset action client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* try to remove the package_ids */
	ret = pk_client_remove_packages (gclient->priv->client_action, package_ids, TRUE, FALSE, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to remove package"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* wait for an answer */
	g_main_loop_run (gclient->priv->loop);

	/* fail the transaction and set the correct error */
	ret = gpk_client_set_error_from_exit_enum (gclient->priv->exit, error);

out:
	return ret;
}

/**
 * gpk_client_install_package_ids:
 * @gclient: a valid #GpkClient instance
 * @package_id: a package_id such as <literal>hal-info;0.20;i386;fedora</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_install_package_ids (GpkClient *gclient, gchar **package_ids, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GtkResponseType button;
	PkPackageList *list;
	guint length;
	gchar *name;
	gchar *title;
	gchar *message;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* are we dumb and can't check for depends? */
	if (!pk_bitfield_contain (gclient->priv->roles, PK_ROLE_ENUM_GET_DEPENDS)) {
		egg_warning ("skipping depends check");
		goto skip_checks;
	}

	/* have we previously said we don't want to be shown the confirmation */
	ret = gconf_client_get_bool (gclient->priv->gconf_client, GPK_CONF_SHOW_DEPENDS, NULL);
	if (!ret) {
		egg_debug ("we've said we don't want the dep dialog");
		goto skip_checks;
	}

	/* optional */
	if (!gclient->priv->show_confirm) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* set title */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Finding packages we depend on"));
	gpk_client_dialog_set_message (gclient->priv->dialog, "");
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-finding-depends");

	/* setup the UI */
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, GPK_CLIENT_DIALOG_PACKAGE_PADDING, 0);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_resolve, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset resolve client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* find out if this would drag in other packages */
	ret = pk_client_get_depends (gclient->priv->client_resolve, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), package_ids, TRUE, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Could not work out what packages would be also installed"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* these are the new packages */
	list = pk_client_get_package_list (gclient->priv->client_resolve);
	length = pk_package_list_get_size (list);
	if (length == 0)
		goto skip_checks;

	/* title */
	title = g_strdup_printf (ngettext ("%i additional package also has to be installed",
					   "%i additional packages also have to be installed",
					   length), length);

	/* message */
	name = gpk_dialog_package_id_name_join_locale (package_ids);
	message = g_strdup_printf (ngettext ("To install %s, an additional package also has to be downloaded.",
					     "To install %s, additional packages also have to be downloaded.",
					     length), name);
	g_free (name);

	gpk_client_dialog_set_package_list (gclient->priv->dialog, list);
	gpk_client_dialog_set_title (gclient->priv->dialog, title);
	gpk_client_dialog_set_message (gclient->priv->dialog, message);
	gpk_client_dialog_set_action (gclient->priv->dialog, _("Install"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-install-other-packages");
	gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_CONFIRM, GPK_CLIENT_DIALOG_PACKAGE_LIST, gclient->priv->timestamp);
	button = gpk_client_dialog_run (gclient->priv->dialog);

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK) {
		gpk_client_dialog_close (gclient->priv->dialog);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "did not agree to additional deps");
		ret = FALSE;
		goto out;
	}

skip_checks:
	/* try to install the package_id */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Installing packages"));
	gpk_client_dialog_set_message (gclient->priv->dialog, "");
	gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, GPK_CLIENT_DIALOG_PACKAGE_PADDING, 0);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset action client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	ret = pk_client_install_packages (gclient->priv->client_action, package_ids, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to install package"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* wait for an answer */
	g_main_loop_run (gclient->priv->loop);

	/* fail the transaction and set the correct error */
	ret = gpk_client_set_error_from_exit_enum (gclient->priv->exit, error);

out:
	return ret;
}

/**
 * gpk_client_install_package_names:
 * @gclient: a valid #GpkClient instance
 * @package: a pakage name such as <literal>hal-info</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a package of the newest and most correct version.
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_install_package_names (GpkClient *gclient, gchar **packages, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar **package_ids = NULL;
	gchar *message;
	const PkPackageObj *obj;
	PkPackageList *list;
	PkPackageId *id = NULL;
	gchar *text;
	gboolean already_installed = FALSE;
	gchar *title;
	guint len;
	guint i;
	GString *string;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (packages != NULL, FALSE);

	/* optional */
	if (!gclient->priv->show_confirm) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	string = g_string_new ("");
	len = g_strv_length (packages);
	for (i=0; i<len; i++)
		g_string_append_printf (string, "• <i>%s</i>\n", packages[i]);

	/* display messagebox  */
	text = g_string_free (string, FALSE);

	/* check user wanted operation */
	message = g_strdup_printf ("%s\n\n%s\n%s", _("An additional file is required"),
				   text, _("Do you want to search for this file now?"));
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Package installer"));
	gpk_client_dialog_set_action (gclient->priv->dialog, _("Install"));
	ret = gpk_client_confirm_action (gclient, _("wants to install packages"), message);
	g_free (text);
	g_free (message);
	if (!ret) {
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "did not agree to search");
		ret = FALSE;
		goto out;
	}

skip_checks:
	/* set title */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Searching for packages"));
	gpk_client_dialog_set_image_status (gclient->priv->dialog, PK_STATUS_ENUM_WAIT);
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-finding-packages");
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0, 0);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_resolve, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset resolve client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* find out if we can find a package */
	ret = pk_client_resolve (gclient->priv->client_resolve, PK_FILTER_ENUM_NONE, packages, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Incorrect response from search"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* found nothing? */
	list = pk_client_get_package_list (gclient->priv->client_resolve);
	len = pk_package_list_get_size (list);
	if (len == 0) {
		if (gclient->priv->show_warning) {
			//FIXME: shows package_id in UI
			text = pk_package_ids_to_text (packages);
			title = g_strdup_printf (_("Could not find %s"), text);
			gpk_client_dialog_set_title (gclient->priv->dialog, _("Failed to find package"));
			gpk_client_dialog_set_message (gclient->priv->dialog, _("The packages could not be found in any software source"));
			gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-package-not-found");
			gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, 0);
			gpk_client_dialog_run (gclient->priv->dialog);
			g_free (text);
			g_free (title);
		}
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "no package found");
		ret = FALSE;
		goto out;
	}

	/* see what we've got already */
	for (i=0; i<len; i++) {
		obj = pk_package_list_get_obj (list, i);
		if (obj->info == PK_INFO_ENUM_INSTALLED) {
			already_installed = TRUE;
		} else if (obj->info == PK_INFO_ENUM_AVAILABLE) {
			egg_debug ("package '%s' resolved", obj->id->name);
			id = obj->id;
			//TODO: we need to list these in a gpk-client-chooser
		}
	}

	/* already installed? */
	if (already_installed) {
		if (gclient->priv->show_warning) {
			//FIXME: shows package_id in UI
			text = pk_package_ids_to_text (packages);
			title = g_strdup_printf (_("Failed to install %s"), text);
			gpk_client_dialog_set_title (gclient->priv->dialog, _("Failed to install package"));
			gpk_client_dialog_set_message (gclient->priv->dialog, _("The package is already installed"));
			gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
			gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, 0);
			gpk_client_dialog_run (gclient->priv->dialog);
			g_free (text);
			g_free (title);
		}
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "package already found");
		ret = FALSE;
		goto out;
	}

	/* got junk? */
	if (id == NULL) {
		if (gclient->priv->show_warning) {
			gpk_client_dialog_set_title (gclient->priv->dialog, _("Failed to install package"));
			gpk_client_dialog_set_message (gclient->priv->dialog, _("Incorrect response from search"));
			gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
			gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, 0);
			gpk_client_dialog_run (gclient->priv->dialog);
		}
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "package already found");
		ret = FALSE;
		goto out;
	}

	/* convert to data */
	package_ids = pk_package_list_to_strv (list);
	g_object_unref (list);

	/* install these packages */
	ret = gpk_client_install_package_ids (gclient, package_ids, &error_local);
	if (!ret) {
		/* copy error message */
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * gpk_client_install_provide_file:
 * @gclient: a valid #GpkClient instance
 * @full_path: a file path name such as <literal>/usr/sbin/packagekitd</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a package which provides a file on the system.
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_install_provide_file (GpkClient *gclient, const gchar *full_path, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	guint len;
	guint i;
	gboolean already_installed = FALSE;
	gchar *package_id = NULL;
	PkPackageList *list = NULL;
	const PkPackageObj *obj;
	PkPackageId *id = NULL;
	gchar **package_ids = NULL;
	gchar *text;
	gchar *message;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (full_path != NULL, FALSE);

	/* optional */
	if (!gclient->priv->show_confirm) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* check user wanted operation */
	message = g_strdup_printf ("%s\n\n• %s\n\n%s", _("The following file is required:"),
				   full_path, _("Do you want to search for this now?"));
	gpk_client_dialog_set_title (gclient->priv->dialog, _("File installer"));
	gpk_client_dialog_set_action (gclient->priv->dialog, _("Install"));
	ret = gpk_client_confirm_action (gclient, _("wants to install a file"), message);
	g_free (message);
	if (!ret) {
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "did not agree to search");
		ret = FALSE;
		goto out;
	}

skip_checks:
	/* set title */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Searching for file"));
	gpk_client_dialog_set_image_status (gclient->priv->dialog, PK_STATUS_ENUM_WAIT);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_resolve, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset resolve client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* do search */
	ret = pk_client_search_file (gclient->priv->client_resolve, PK_FILTER_ENUM_NONE, full_path, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to search for file"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* found nothing? */
	list = pk_client_get_package_list (gclient->priv->client_resolve);
	len = pk_package_list_get_size (list);
	if (len == 0) {
		if (gclient->priv->show_warning) {
			gpk_client_dialog_set_title (gclient->priv->dialog, _("Failed to find package"));
			gpk_client_dialog_set_message (gclient->priv->dialog, _("The file could not be found in any packages"));
			gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-package-not-found");
			gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, 0);
			gpk_client_dialog_run (gclient->priv->dialog);
		}
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "no files found");
		ret = FALSE;
		goto out;
	}

	/* see what we've got already */
	for (i=0; i<len; i++) {
		obj = pk_package_list_get_obj (list, i);
		if (obj->info == PK_INFO_ENUM_INSTALLED) {
			already_installed = TRUE;
			id = obj->id;
		} else if (obj->info == PK_INFO_ENUM_AVAILABLE) {
			egg_debug ("package '%s' resolved to:", obj->id->name);
			id = obj->id;
		}
	}

	/* already installed? */
	if (already_installed) {
		if (gclient->priv->show_warning) {
			text = g_strdup_printf (_("The %s package already provides the file %s"), id->name, full_path);
			gpk_client_dialog_set_title (gclient->priv->dialog, _("Failed to install file"));
			gpk_client_dialog_set_message (gclient->priv->dialog, text);
			gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
			gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, 0);
			gpk_client_dialog_run (gclient->priv->dialog);
			g_free (text);
		}
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "already provided");
		ret = FALSE;
		goto out;
	}

	/* install this specific package */
	package_id = pk_package_id_to_string (id);
	package_ids = pk_package_ids_from_id (package_id);
	ret = gpk_client_install_package_ids (gclient, package_ids, error);
out:
	if (list != NULL)
		g_object_unref (list);
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * gpk_client_install_gstreamer_codec_part:
 **/
PkPackageObj *
gpk_client_install_gstreamer_codec_part (GpkClient *gclient, const gchar *codec_name, const gchar *codec_desc, GError **error)
{
	PkPackageList *list = NULL;
	gboolean ret;
	PkPackageObj *new_obj = NULL;
	const PkPackageObj *obj;
	guint len;
	gchar *title;
	gchar *codec_name_formatted;

	/* reset */
	ret = pk_client_reset (gclient->priv->client_resolve, error);
	if (!ret)
		return NULL;

	codec_name_formatted = g_strdup_printf ("<i>%s</i>", codec_name);
	title = g_strdup_printf (_("Searching for plugin: %s"), codec_name_formatted);
	gpk_client_dialog_set_message (gclient->priv->dialog, title);
	g_free (title);
	g_free (codec_name_formatted);

	/* get codec packages */
	ret = pk_client_what_provides (gclient->priv->client_resolve, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), PK_PROVIDES_ENUM_CODEC, codec_desc, error);
	if (!ret)
		return NULL;

	list = pk_client_get_package_list (gclient->priv->client_resolve);
	len = pk_package_list_get_size (list);

	/* found nothing? */
	if (len == 0) {
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "failed to find: %s", codec_desc);
		goto out;
	}

	/* gstreamer-ffmpeg and gstreamer-plugins-ugly both provide mp3 playback, choose one */
	if (len > 1)
		egg_warning ("choosing one of the provides as more than one match");

	/* always use the first one */
	obj = pk_package_list_get_obj (list, 0);
	if (obj == NULL)
		egg_error ("obj cannot be NULL");

	/* copy the object */
	new_obj = pk_package_obj_copy (obj);
out:
	if (list != NULL)
		g_object_unref (list);
	return new_obj;
}

/**
 * gpk_client_install_gstreamer_codecs_confirm:
 **/
static gboolean
gpk_client_install_gstreamer_codecs_confirm (GpkClient *gclient, gchar **codec_name_strings)
{
	guint i;
	guint len;
	gchar *text;
	gchar **parts;
	gboolean ret;
	GString *string;
	const gchar *title;
	const gchar *message;

	len = g_strv_length (codec_name_strings);
	title = ngettext ("An additional plugin is required to play this content", "Additional plugins are required to play this content", len);
	message = ngettext ("The following plugin is required:", "The following plugins are required:", len);

	string = g_string_new ("");
	g_string_append_printf (string, "%s\n%s\n\n", title, message);
	for (i=0; i<len; i++) {
		parts = g_strsplit (codec_name_strings[i], "|", 2);
		g_string_append_printf (string, "• <i>%s</i>\n", parts[0]);
		g_strfreev (parts);
	}

	/* trailer */
	message = ngettext ("Do you want to search for this now?", "Do you want to search for these now?", len);
	g_string_append_printf (string, "\n%s\n", message);

	/* remove last \n */
	g_string_set_size (string, string->len - 1);

	/* display messagebox  */
	text = g_string_free (string, FALSE);

	gpk_client_dialog_set_title (gclient->priv->dialog, _("Plugin installer"));
	gpk_client_dialog_set_action (gclient->priv->dialog, _("Search"));
	ret = gpk_client_confirm_action (gclient, _("requires additional plugins"), text);
	g_free (text);

	return ret;
}

/**
 * gpk_client_install_gstreamer_codecs:
 * @gclient: a valid #GpkClient instance
 * @codecs: a codec_type such as <literal>application/text</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a application to handle a mime type
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_install_gstreamer_codecs (GpkClient *gclient, gchar **codec_name_strings, GError **error)
{
	guint i;
	guint len;
	PkPackageObj *obj_new;
	gboolean ret = TRUE;
	gchar **parts;
	GError *error_local = NULL;
	GtkResponseType button;
	PkPackageList *list = NULL;
	gchar **package_ids = NULL;
	const gchar *title;
	const gchar *message;

	/* check it's not session wide banned in gconf */
	ret = gconf_client_get_bool (gclient->priv->gconf_client, GPK_CONF_ENABLE_CODEC_HELPER, NULL);
	if (!ret) {
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "not enabled in GConf : %s", GPK_CONF_ENABLE_CODEC_HELPER);
		ret = FALSE;
		goto out;
	}

	/* optional */
	if (!gclient->priv->show_confirm) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* confirm */
	ret = gpk_client_install_gstreamer_codecs_confirm (gclient, codec_name_strings);
	if (!ret) {
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "did not agree to search");
		ret = FALSE;
		goto out;
	}

skip_checks:
	/* set title */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Searching for plugins"));
	gpk_client_dialog_set_image_status (gclient->priv->dialog, PK_STATUS_ENUM_WAIT);
	gpk_client_dialog_set_message (gclient->priv->dialog, "");
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-finding-packages");

	/* setup the UI */
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, GPK_CLIENT_DIALOG_PACKAGE_PADDING, 0);

	/* save the objects to download in a list */
	list = pk_package_list_new ();

	len = g_strv_length (codec_name_strings);
	for (i=0; i<len; i++) {
		parts = g_strsplit (codec_name_strings[i], "|", 2);
		if (g_strv_length (parts) != 2) {
			egg_warning ("invalid line '%s', expecting a | delimiter", codec_name_strings[i]);
			continue;
		}
		obj_new = gpk_client_install_gstreamer_codec_part (gclient, parts[0], parts[1], &error_local);
		if (obj_new == NULL) {
			if (gclient->priv->show_warning) {
				gpk_client_dialog_set_title (gclient->priv->dialog, _("Failed to search for plugin"));
				gpk_client_dialog_set_message (gclient->priv->dialog, _("Could not find plugin in any configured software source"));
				gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-package-not-found");
				gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, 0);
				gpk_client_dialog_run (gclient->priv->dialog);
			}
			gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
			g_error_free (error_local);
			ret = FALSE;
		}
		if (obj_new != NULL)
			pk_package_list_add_obj (list, obj_new);

		pk_package_obj_free (obj_new);
		g_strfreev (parts);
		if (!ret)
			break;
	}

	/* don't prompt to install if any failed */
	//TODO: install partial
	if (!ret)
		goto out;

	/* optional */
	if (!gclient->priv->show_confirm) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks2;
	}

	title = ngettext ("Install the following plugin", "Install the following plugins", len);
	message = ngettext ("Do you want to install this package now?", "Do you want to install these packages now?", len);

	gpk_client_dialog_set_package_list (gclient->priv->dialog, list);
	gpk_client_dialog_set_title (gclient->priv->dialog, title);
	gpk_client_dialog_set_message (gclient->priv->dialog, message);
	gpk_client_dialog_set_image (gclient->priv->dialog, "dialog-information");
	gpk_client_dialog_set_action (gclient->priv->dialog, _("Install"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
	gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_CONFIRM, GPK_CLIENT_DIALOG_PACKAGE_LIST, gclient->priv->timestamp);
	button = gpk_client_dialog_run (gclient->priv->dialog);

	/* close, we're going to fail the method */
	if (button != GTK_RESPONSE_OK) {
		gpk_client_dialog_close (gclient->priv->dialog);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "did not agree to download");
		ret = FALSE;
		goto out;
	}

skip_checks2:
	/* convert to list of package id's */
	package_ids = pk_package_list_to_strv (list);
	ret = gpk_client_install_package_ids (gclient, package_ids, error);

out:
	if (!ret)
		gpk_client_dialog_close (gclient->priv->dialog);
	g_strfreev (package_ids);
	if (list != NULL)
		g_object_unref (list);
	return ret;
}

/**
 * gpk_client_install_mime_type:
 * @gclient: a valid #GpkClient instance
 * @mime_type: a mime_type such as <literal>application/text</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a application to handle a mime type
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_install_mime_type (GpkClient *gclient, const gchar *mime_type, GError **error)
{
	gboolean ret;
	PkPackageList *list = NULL;
	GError *error_local = NULL;
	gchar *package_id = NULL;
	gchar **package_ids = NULL;
	guint len;
	GtkWindow *window;
	gchar *message;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (mime_type != NULL, FALSE);

	/* check it's not session wide banned in gconf */
	ret = gconf_client_get_bool (gclient->priv->gconf_client, GPK_CONF_ENABLE_MIME_TYPE_HELPER, NULL);
	if (!ret) {
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "not enabled in GConf : %s", GPK_CONF_ENABLE_MIME_TYPE_HELPER);
		ret = FALSE;
		goto out;
	}

	/* optional */
	if (!gclient->priv->show_confirm) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* make sure the user wants to do action */
	message = g_strdup_printf ("%s\n\n• %s\n\n%s",
				   _("An additional program is required to open this type of file:"),
				   mime_type, _("Do you want to search for a program to open this file type now?"));
	gpk_client_dialog_set_title (gclient->priv->dialog, _("File type installer"));
	gpk_client_dialog_set_action (gclient->priv->dialog, _("Search"));
	ret = gpk_client_confirm_action (gclient, _("requires a new mime type"), message);
	g_free (message);
	if (!ret) {
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "did not agree to search");
		ret = FALSE;
		goto out;
	}

skip_checks:
	/* set title */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Searching for file handlers"));
	gpk_client_dialog_set_image_status (gclient->priv->dialog, PK_STATUS_ENUM_WAIT);
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-finding-packages");

	/* setup the UI */
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0, 0);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_resolve, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset resolve client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* action */
	ret = pk_client_what_provides (gclient->priv->client_resolve, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED),
				       PK_PROVIDES_ENUM_MIMETYPE, mime_type, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to search for provides"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* found nothing? */
	list = pk_client_get_package_list (gclient->priv->client_resolve);
	len = pk_package_list_get_size (list);
	if (len == 0) {
		if (gclient->priv->show_warning) {
			gpk_client_dialog_set_title (gclient->priv->dialog, _("Failed to find software"));
			gpk_client_dialog_set_message (gclient->priv->dialog, _("No new applications can be found to handle this type of file"));
			gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-package-not-found");
			gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, 0);
			gpk_client_dialog_run (gclient->priv->dialog);
		}
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "nothing was found to handle mime type");
		ret = FALSE;
		goto out;
	}

	/* populate a chooser */
	window = gpk_client_dialog_get_window (gclient->priv->dialog);
	package_id = gpk_client_chooser_show (window, list, _("Applications that can open this type of file"));

	/* selected nothing */
	if (package_id == NULL) {
		if (gclient->priv->show_warning) {
			gpk_client_dialog_set_title (gclient->priv->dialog, _("Failed to install software"));
			gpk_client_dialog_set_message (gclient->priv->dialog, _("No applications were chosen to be installed"));
			gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
			gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, 0);
			gpk_client_dialog_run (gclient->priv->dialog);
		}
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "user chose nothing");
		ret = FALSE;
		goto out;
	}

	/* install this specific package */
	package_ids = pk_package_ids_from_id (package_id);
	ret = gpk_client_install_package_ids (gclient, package_ids, error);
out:
	if (list != NULL)
		g_object_unref (list);
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * gpk_client_install_font:
 * @gclient: a valid #GpkClient instance
 * @font_desc: a font description such as <literal>lang:en_GB</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a application to handle a mime type
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_install_font (GpkClient *gclient, const gchar *font_desc, GError **error)
{
	gboolean ret;
	PkPackageList *list = NULL;
	GtkResponseType button;
	GError *error_local = NULL;
	gchar **package_ids = NULL;
	guint len;
	gchar *message;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (font_desc != NULL, FALSE);

	/* check it's not session wide banned in gconf */
	ret = gconf_client_get_bool (gclient->priv->gconf_client, GPK_CONF_ENABLE_FONT_HELPER, NULL);
	if (!ret) {
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "not enabled in GConf : %s", GPK_CONF_ENABLE_FONT_HELPER);
		ret = FALSE;
		goto out;
	}

	/* optional */
	if (!gclient->priv->show_confirm) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* check user wanted operation */
	message = g_strdup_printf ("%s\n\n%s", _("An additional font is required to view this file correctly"),
				   _("Do you want to search for a suitable font now?"));
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Font installer"));
	gpk_client_dialog_set_action (gclient->priv->dialog, _("Search"));
	ret = gpk_client_confirm_action (gclient, _("wants to install a font"), message);
	g_free (message);
	if (!ret) {
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "did not agree to search");
		ret = FALSE;
		goto out;
	}

skip_checks:
	/* set title */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Searching for fonts"));
	gpk_client_dialog_set_image_status (gclient->priv->dialog, PK_STATUS_ENUM_WAIT);
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-finding-packages");

	/* setup the UI */
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0, 0);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_resolve, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset resolve client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* action */
	ret = pk_client_what_provides (gclient->priv->client_resolve, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED),
				       PK_PROVIDES_ENUM_FONT, font_desc, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to search for provides"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* found nothing? */
	list = pk_client_get_package_list (gclient->priv->client_resolve);
	len = pk_package_list_get_size (list);
	if (len == 0) {
		if (gclient->priv->show_warning) {
			gpk_client_dialog_set_title (gclient->priv->dialog, _("Failed to find font"));
			gpk_client_dialog_set_message (gclient->priv->dialog, _("No new fonts can be found for this document"));
			gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-package-not-found");
			gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, 0);
			gpk_client_dialog_run (gclient->priv->dialog);
		}
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "failed to find font");
		ret = FALSE;
		goto out;
	}

	/* optional */
	if (!gclient->priv->show_confirm) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks2;
	}

	gpk_client_dialog_set_package_list (gclient->priv->dialog, list);
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Install the following fonts"));
	gpk_client_dialog_set_message (gclient->priv->dialog, _("Do you want to install these packages now?"));
	gpk_client_dialog_set_image (gclient->priv->dialog, "dialog-information");
	gpk_client_dialog_set_action (gclient->priv->dialog, _("Install"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
	gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_CONFIRM, GPK_CLIENT_DIALOG_PACKAGE_LIST, gclient->priv->timestamp);
	button = gpk_client_dialog_run (gclient->priv->dialog);

	/* close, we're going to fail the method */
	if (button != GTK_RESPONSE_OK) {
		gpk_client_dialog_close (gclient->priv->dialog);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "did not agree to download");
		ret = FALSE;
		goto out;
	}

skip_checks2:
	/* convert to list of package id's */
	package_ids = pk_package_list_to_strv (list);
	ret = gpk_client_install_package_ids (gclient, package_ids, error);

out:
	if (!ret)
		gpk_client_dialog_close (gclient->priv->dialog);
	g_strfreev (package_ids);
	if (list != NULL)
		g_object_unref (list);
	return ret;
}

/**
 * gpk_client_catalog_progress_cb:
 **/
static void
gpk_client_catalog_progress_cb (PkCatalog *catalog, PkCatalogProgress mode, const gchar *text, GpkClient *gclient)
{
	gchar *message = NULL;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	if (mode == PK_CATALOG_PROGRESS_PACKAGES)
		message = g_strdup_printf (_("Finding package name: %s"), text);
	else if (mode == PK_CATALOG_PROGRESS_FILES)
		message = g_strdup_printf (_("Finding file name: %s"), text);
	else if (mode == PK_CATALOG_PROGRESS_PROVIDES)
		message = g_strdup_printf (_("Finding a package to provide: %s"), text);

	gpk_client_set_status (gclient, PK_STATUS_ENUM_QUERY);
	gpk_client_dialog_set_message (gclient->priv->dialog, message);
	g_free (message);
}

/**
 * gpk_client_install_catalogs:
 **/
gboolean
gpk_client_install_catalogs (GpkClient *gclient, gchar **filenames, GError **error)
{
	GtkResponseType button;
	gchar **package_ids = NULL;
	gchar *message;
	const gchar *title;
	gboolean ret;
	const PkPackageObj *obj;
	PkPackageList *list;
	PkCatalog *catalog;
	GString *string;
	gchar *text;
	guint len;
	guint i;

	len = g_strv_length (filenames);

	/* optional */
	if (!gclient->priv->show_confirm) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	title = ngettext (_("Do you want to install this catalog?"),
			  _("Do you want to install these catalogs?"), len);
	message = g_strjoinv ("\n", filenames);

	/* show UI */
	gpk_client_dialog_set_title (gclient->priv->dialog, title);
	gpk_client_dialog_set_message (gclient->priv->dialog, message);
	gpk_client_dialog_set_action (gclient->priv->dialog, _("Install"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-install-catalogs");
	gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_CONFIRM, 0, gclient->priv->timestamp);
	button = gpk_client_dialog_run (gclient->priv->dialog);
	g_free (message);

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK)
		return FALSE;

skip_checks:
	/* set title */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Install catalogs"));
	gpk_client_set_status (gclient, PK_STATUS_ENUM_WAIT);

	/* setup the UI */
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0, 0);

	/* get files to be installed */
	catalog = pk_catalog_new ();
	g_signal_connect (catalog, "progress", G_CALLBACK (gpk_client_catalog_progress_cb), gclient);
	list = pk_catalog_process_files (catalog, filenames);
	g_object_unref (catalog);

	/* nothing to do? */
	len = pk_package_list_get_size (list);
	if (len == 0) {
		/* show UI */
		if (gclient->priv->show_warning) {
			gpk_client_dialog_set_title (gclient->priv->dialog, _("No packages need to be installed"));
			gpk_client_dialog_set_message (gclient->priv->dialog, "");
			gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-catalog-none-required");
			gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_WARNING, 0, gclient->priv->timestamp);
			gpk_client_dialog_run (gclient->priv->dialog);
		}
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "No packages need to be installed");
		ret = FALSE;
		goto out;
	}

	/* optional */
	if (!gclient->priv->show_confirm) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks2;
	}

	/* process package list */
	string = g_string_new (_("The following packages are marked to be installed from the catalog:"));
	g_string_append (string, "\n\n");
	for (i=0; i<len; i++) {
		obj = pk_package_list_get_obj (list, i);
		text = gpk_package_id_format_oneline (obj->id, obj->summary);
		g_string_append_printf (string, "%s\n", text);
		g_free (text);
	}
	/* remove last \n */
	g_string_set_size (string, string->len - 1);

	/* display messagebox  */
	text = g_string_free (string, FALSE);

	/* show UI */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Install packages in catalog?"));
	gpk_client_dialog_set_message (gclient->priv->dialog, text);
	gpk_client_dialog_set_image (gclient->priv->dialog, "dialog-question");
	gpk_client_dialog_set_action (gclient->priv->dialog, _("Install"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
	gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_CONFIRM, 0, gclient->priv->timestamp);
	button = gpk_client_dialog_run (gclient->priv->dialog);

	g_free (text);

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK) {
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "Action was cancelled");
		ret = FALSE;
		goto out;
	}

skip_checks2:
	/* convert to list of package id's */
	package_ids = pk_package_list_to_strv (list);
	ret = gpk_client_install_package_ids (gclient, package_ids, error);

out:
	g_strfreev (package_ids);
	g_object_unref (list);

	return ret;
}

/**
 * gpk_client_update_system:
 **/
gboolean
gpk_client_update_system (GpkClient *gclient, GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	gchar *text = NULL;
	gchar *message = NULL;
	NotifyNotification *notification;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset action client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* set title */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("System update"));
	gpk_client_dialog_set_message (gclient->priv->dialog, "");
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-update-system");

	/* wrap update, but handle all the GPG and EULA stuff */
	ret = pk_client_update_system (gclient->priv->client_action, &error_local);
	if (!ret) {
		/* display and set */
		gpk_client_error_msg (gclient, _("Failed to update system"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* setup the UI */
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, GPK_CLIENT_DIALOG_PACKAGE_PADDING, 0);

	/* if we are not showing UI, then notify the user what we are doing (just on the active terminal) */
	ret = gconf_client_get_bool (gclient->priv->gconf_client, GPK_CONF_NOTIFY_CRITICAL, NULL);
	if (!gclient->priv->show_progress && ret) {
		/* do the bubble */
		notification = notify_notification_new (_("Updates are being installed"),
							_("Updates are being automatically installed on your computer"),
							"software-update-urgent", NULL);
		notify_notification_set_timeout (notification, 15000);
		notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
		notify_notification_add_action (notification, "cancel",
						_("Cancel update"), gpk_client_libnotify_cb, gclient, NULL);
		notify_notification_add_action (notification, "do-not-show-update-started",
						_("Do not show this again"), gpk_client_libnotify_cb, gclient, NULL);
		ret = notify_notification_show (notification, &error_local);
		if (!ret) {
			egg_warning ("error: %s", error_local->message);
			g_error_free (error_local);
		}
	}

	/* wait for an answer */
	g_main_loop_run (gclient->priv->loop);

	/* fail the transaction and set the correct error */
	ret = gpk_client_set_error_from_exit_enum (gclient->priv->exit, error);

out:
	g_free (message);
	g_free (text);
	return ret;
}

/**
 * gpk_client_refresh_cache:
 **/
gboolean
gpk_client_refresh_cache (GpkClient *gclient, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *text = NULL;
	gchar *message = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset action client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* set title */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Refresh package lists"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-refresh");

	/* wrap update, but handle all the GPG and EULA stuff */
	ret = pk_client_refresh_cache (gclient->priv->client_action, TRUE, &error_local);
	if (!ret) {
		/* display and set */
		gpk_client_error_msg (gclient, _("Failed to update package lists"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* setup the UI */
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0, 0);

	/* wait for an answer */
	g_main_loop_run (gclient->priv->loop);

	/* fail the transaction and set the correct error */
	ret = gpk_client_set_error_from_exit_enum (gclient->priv->exit, error);

out:
	g_free (message);
	g_free (text);
	return ret;
}

/**
 * gpk_client_get_updates:
 **/
PkPackageList *
gpk_client_get_updates (GpkClient *gclient, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	PkPackageList *list = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset action client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* wrap update, but handle all the GPG and EULA stuff */
	ret = pk_client_get_updates (gclient->priv->client_action, PK_FILTER_ENUM_NONE, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to get updates"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* ignore this if it's uninteresting */
	if (gclient->priv->show_progress) {
		gpk_client_dialog_set_title (gclient->priv->dialog, _("Getting update lists"));
		gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0, 0);
	}

	/* wait for an answer */
	g_main_loop_run (gclient->priv->loop);

	/* copy from client to local */
	list = pk_client_get_package_list (gclient->priv->client_action);
out:
	return list;
}

/**
 * gpk_client_get_distro_upgrades:
 **/
const GPtrArray *
gpk_client_get_distro_upgrades (GpkClient *gclient, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* are we not able to do this? */
	if (!pk_bitfield_contain (gclient->priv->roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "Backend does not support GetDistroUpgrades");
		return NULL;
	}

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset action client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		return NULL;
	}

	/* clear old data */
	if (gclient->priv->upgrade_array->len > 0) {
		g_ptr_array_foreach (gclient->priv->upgrade_array, (GFunc) pk_distro_upgrade_obj_free, NULL);
		g_ptr_array_remove_range (gclient->priv->upgrade_array, 0, gclient->priv->upgrade_array->len);
	}

	/* wrap update, but handle all the GPG and EULA stuff */
	ret = pk_client_get_distro_upgrades (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Getting update lists failed"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* setup the UI */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Getting distribution upgrade information"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-get-upgrades");
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0, 0);

	/* wait for an answer */
	g_main_loop_run (gclient->priv->loop);
out:
	return gclient->priv->upgrade_array;
}

/**
 * gpk_client_get_file_list:
 **/
gchar **
gpk_client_get_file_list (GpkClient *gclient, const gchar *package_id, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar **package_ids;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset action client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		return FALSE;
	}

	/* wrap get files */
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_get_files (gclient->priv->client_action, package_ids, &error_local);
	g_strfreev (package_ids);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Getting file list failed"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* setup the UI */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Getting file lists"));
	gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0, 0);

	/* wait for an answer */
	g_main_loop_run (gclient->priv->loop);

	/* fail the transaction and set the correct error */
	ret = gpk_client_set_error_from_exit_enum (gclient->priv->exit, error);

out:
	/* return the file list */
	return g_strdupv (gclient->priv->files_array);
}

/**
 * gpk_client_update_packages:
 **/
gboolean
gpk_client_update_packages (GpkClient *gclient, gchar **package_ids, GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset action client"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* wrap update, but handle all the GPG and EULA stuff */
	ret = pk_client_update_packages (gclient->priv->client_action, package_ids, &error_local);
	if (!ret) {
		/* display and set */
		gpk_client_error_msg (gclient, _("Failed to update packages"), error_local);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* setup the UI */
	gpk_client_dialog_set_title (gclient->priv->dialog, _("Update packages"));
	gpk_client_dialog_set_message (gclient->priv->dialog, "");
	gpk_client_dialog_set_help_id (gclient->priv->dialog, "dialog-update-packages");
	if (gclient->priv->show_progress)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, GPK_CLIENT_DIALOG_PACKAGE_PADDING, 0);

	/* wait for an answer */
	g_main_loop_run (gclient->priv->loop);

	/* fail the transaction and set the correct error */
	ret = gpk_client_set_error_from_exit_enum (gclient->priv->exit, error);

out:
	return ret;
}

/**
 * gpk_client_repo_signature_required_cb:
 **/
static void
gpk_client_repo_signature_required_cb (PkClient *client, const gchar *package_id, const gchar *repository_name,
				       const gchar *key_url, const gchar *key_userid, const gchar *key_id,
				       const gchar *key_fingerprint, const gchar *key_timestamp,
				       PkSigTypeEnum type, GpkClient *gclient)
{
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	ret = gpk_client_signature_show (package_id, repository_name, key_url, key_userid,
					 key_id, key_fingerprint, key_timestamp);
	/* disagreed with auth */
	if (!ret)
		return;

	/* install signature */
	egg_debug ("install signature %s", key_id);
	ret = pk_client_reset (gclient->priv->client_secondary, &error);
	if (!ret) {
		widget = GTK_WIDGET (gpk_client_dialog_get_window (gclient->priv->dialog));
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Failed to install signature"),
					_("The client could not be reset"), error->message);
		g_error_free (error);
		return;
	}
	/* this is asynchronous, else we get into livelock */
	ret = pk_client_install_signature (gclient->priv->client_secondary, PK_SIGTYPE_ENUM_GPG,
					   key_id, package_id, &error);
	gclient->priv->using_secondary_client = ret;
	if (!ret) {
		widget = GTK_WIDGET (gpk_client_dialog_get_window (gclient->priv->dialog));
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Failed to install signature"),
					_("The method failed"), error->message);
		g_error_free (error);
	}
}

/**
 * gpk_client_eula_required_cb:
 **/
static void
gpk_client_eula_required_cb (PkClient *client, const gchar *eula_id, const gchar *package_id,
			     const gchar *vendor_name, const gchar *license_agreement, GpkClient *gclient)
{
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;

	/* do a helper */
	widget = GTK_WIDGET (gpk_client_dialog_get_window (gclient->priv->dialog));
	ret = gpk_client_eula_show (GTK_WINDOW (widget), eula_id, package_id, vendor_name, license_agreement);

	/* disagreed with auth */
	if (!ret)
		return;

	/* install signature */
	egg_debug ("accept EULA %s", eula_id);
	ret = pk_client_reset (gclient->priv->client_secondary, &error);
	if (!ret) {
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Failed to accept EULA"),
					_("The client could not be reset"), error->message);
		g_error_free (error);
		return;
	}

	/* this is asynchronous, else we get into livelock */
	ret = pk_client_accept_eula (gclient->priv->client_secondary, eula_id, &error);
	if (!ret) {
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Failed to accept EULA"),
					_("The method failed"), error->message);
		g_error_free (error);
	}
	gclient->priv->using_secondary_client = ret;
}

/**
 * gpk_client_secondary_now_requeue:
 **/
static gboolean
gpk_client_secondary_now_requeue (GpkClient *gclient)
{
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* go back to the UI */
	gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);
	gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, GPK_CLIENT_DIALOG_PACKAGE_PADDING, 0);
	gclient->priv->using_secondary_client = FALSE;

	egg_debug ("trying to requeue install");
	ret = pk_client_requeue (gclient->priv->client_action, &error);
	if (!ret) {
		widget = GTK_WIDGET (gpk_client_dialog_get_window (gclient->priv->dialog));
		gpk_error_dialog_modal (GTK_WINDOW (widget), _("Failed to install"),
					_("The install task could not be requeued"), error->message);
		g_error_free (error);
	}

	return FALSE;
}

/**
 * gpk_client_secondary_finished_cb:
 **/
static void
gpk_client_secondary_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, GpkClient *gclient)
{
	g_return_if_fail (GPK_IS_CLIENT (gclient));
	/* we have to do this idle add, else we get into deadlock */
	g_idle_add ((GSourceFunc) gpk_client_secondary_now_requeue, gclient);
}

/**
 * pk_common_get_role_text:
 **/
static gchar *
pk_common_get_role_text (PkClient *client)
{
	const gchar *role_text;
	gchar *text;
	gchar *message;
	PkRoleEnum role;
	GError *error = NULL;
	gboolean ret;

	/* get role and text */
	ret = pk_client_get_role (client, &role, &text, &error);
	if (!ret) {
		egg_warning ("failed to get role: %s", error->message);
		g_error_free (error);
		return NULL;
	}

	/* backup */
	role_text = gpk_role_enum_to_localised_present (role);

	if (!egg_strzero (text) && role != PK_ROLE_ENUM_UPDATE_PACKAGES)
		message = g_strdup_printf ("%s: %s", role_text, text);
	else
		message = g_strdup_printf ("%s", role_text);
	g_free (text);

	return message;
}

/**
 * gpk_client_monitor_tid:
 **/
gboolean
gpk_client_monitor_tid (GpkClient *gclient, const gchar *tid)
{
	PkStatusEnum status;
	gboolean ret;
	gboolean allow_cancel;
	gchar *text;
	gchar *package_id = NULL;
	guint percentage;
	guint subpercentage;
	guint elapsed;
	guint remaining;
	GError *error = NULL;
	PkRoleEnum role;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	ret = pk_client_set_tid (gclient->priv->client_action, tid, &error);
	if (!ret) {
		egg_warning ("could not set tid: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* fill in role */
	text = pk_common_get_role_text (gclient->priv->client_action);
	gpk_client_dialog_set_title (gclient->priv->dialog, text);
	g_free (text);
	gpk_client_dialog_set_help_id (gclient->priv->dialog, NULL);

	/* coldplug */
	ret = pk_client_get_status (gclient->priv->client_action, &status, NULL);
	/* no such transaction? */
	if (!ret) {
		egg_warning ("could not get status");
		return FALSE;
	}
	gpk_client_set_status (gclient, status);

	/* are we cancellable? */
	pk_client_get_allow_cancel (gclient->priv->client_action, &allow_cancel, NULL);
	gpk_client_dialog_set_allow_cancel (gclient->priv->dialog, allow_cancel);

	/* coldplug */
	ret = pk_client_get_progress (gclient->priv->client_action,
				      &percentage, &subpercentage, &elapsed, &remaining, NULL);
	if (ret) {
		gpk_client_progress_changed_cb (gclient->priv->client_action, percentage,
						subpercentage, elapsed, remaining, gclient);
	} else {
		egg_warning ("GetProgress failed");
		gpk_client_progress_changed_cb (gclient->priv->client_action,
						PK_CLIENT_PERCENTAGE_INVALID,
						PK_CLIENT_PERCENTAGE_INVALID, 0, 0, gclient);
	}

	/* do the best we can */
	ret = pk_client_get_package (gclient->priv->client_action, &package_id, NULL);
	if (ret) {
		PkPackageId *id;
		PkPackageObj *obj;

		id = pk_package_id_new_from_string (package_id);
		if (id != NULL) {
			obj = pk_package_obj_new (PK_INFO_ENUM_UNKNOWN, id, NULL);
			gpk_client_package_cb (gclient->priv->client_action, obj, gclient);
			pk_package_obj_free (obj);
		}
		pk_package_id_free (id);
	}

	/* get the role */
	ret = pk_client_get_role (gclient->priv->client_action, &role, NULL, &error);
	if (!ret) {
		egg_warning ("failed to get role: %s", error->message);
		g_error_free (error);
	}

	/* setup the UI */
	if (role == PK_ROLE_ENUM_SEARCH_NAME ||
	    role == PK_ROLE_ENUM_SEARCH_GROUP ||
	    role == PK_ROLE_ENUM_SEARCH_DETAILS ||
	    role == PK_ROLE_ENUM_SEARCH_FILE ||
	    role == PK_ROLE_ENUM_SEARCH_NAME ||
	    role == PK_ROLE_ENUM_GET_UPDATES)
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, 0, 0);
	else
		gpk_client_dialog_show_page (gclient->priv->dialog, GPK_CLIENT_DIALOG_PAGE_PROGRESS, GPK_CLIENT_DIALOG_PACKAGE_PADDING, 0);

	/* wait for an answer */
	g_main_loop_run (gclient->priv->loop);

	return TRUE;
}

/**
 * gpk_client_get_package_for_exec:
 **/
static gchar *
gpk_client_get_package_for_exec (GpkClient *gclient, const gchar *exec)
{
	gchar *package = NULL;
	gboolean ret;
	GError *error = NULL;
	guint length;
	PkPackageList *list = NULL;
	const PkPackageObj *obj;

	/* reset client */
	ret = pk_client_reset (gclient->priv->client_resolve, &error);
	if (!ret) {
		egg_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find the package name */
	ret = pk_client_search_file (gclient->priv->client_resolve, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), exec, &error);
	if (!ret) {
		egg_warning ("failed to search file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get the list of packages */
	list = pk_client_get_package_list (gclient->priv->client_resolve);
	length = pk_package_list_get_size (list);

	/* nothing found */
	if (length == 0) {
		egg_debug ("cannot find installed package that provides : %s", exec);
		goto out;
	}

	/* check we have one */
	if (length != 1)
		egg_warning ("not one return, using first");

	/* copy name */
	obj = pk_package_list_get_obj (list, 0);
	package = g_strdup (obj->id->name);
	egg_debug ("got package %s", package);

out:
	/* use the exec name if we can't find an installed package */
	if (list != NULL)
		g_object_unref (list);
	return package;
}

/**
 * gpk_client_path_is_trusted:
 **/
static gboolean
gpk_client_path_is_trusted (const gchar *exec)
{
	/* special case the plugin helper -- it's trusted */
	if (egg_strequal (exec, "/usr/libexec/gst-install-plugins-helper") ||
	    egg_strequal (exec, "/usr/libexec/pk-gstreamer-install"))
		return TRUE;
#if 0
	/* debugging code, should never be run... */
	if (egg_strequal (exec, "/usr/bin/python") ||
	    egg_strequal (exec, "/home/hughsie/Code/PackageKit/contrib/gstreamer-plugin/pk-gstreamer-install"))
		return TRUE;
#endif
	return FALSE;
}

/**
 * gpk_client_set_parent_exec:
 *
 * This sets the package name of the application that is trying to install
 * software, e.g. "totem" and is used for the PkExtra lookup to provide
 * a translated name and icon.
 **/
gboolean
gpk_client_set_parent_exec (GpkClient *gclient, const gchar *exec)
{
	GpkX11 *x11;
	gchar *package;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* old values invalid */
	g_free (gclient->priv->parent_title);
	g_free (gclient->priv->parent_icon_name);
	gclient->priv->parent_title = NULL;
	gclient->priv->parent_icon_name = NULL;

	/* is the binary trusted, i.e. can we probe it's window properties */
	if (gpk_client_path_is_trusted (exec)) {
		egg_debug ("using application window properties");
		/* get from window properties */
		x11 = gpk_x11_new ();
		gpk_x11_set_window (x11, gclient->priv->parent_window);
		gclient->priv->parent_title = gpk_x11_get_title (x11);
		g_object_unref (x11);
		goto out;
	}

	/* get from installed database */
	package = gpk_client_get_package_for_exec (gclient, exec);
	egg_debug ("got package %s", package);

	/* try to get from PkExtra */
	if (package != NULL) {
		gclient->priv->parent_title = g_strdup (pk_extra_get_summary (gclient->priv->extra, package));
		gclient->priv->parent_icon_name = g_strdup (pk_extra_get_icon_name (gclient->priv->extra, package));
		/* fallback to package name */
		if (gclient->priv->parent_title == NULL) {
			egg_debug ("did not get localised description for %s", package);
			gclient->priv->parent_title = g_strdup (package);
		}
	}

	/* fallback to exec - eugh... */
	if (gclient->priv->parent_title == NULL) {
		egg_debug ("did not get package for %s, using exec basename", package);
		gclient->priv->parent_title = g_path_get_basename (exec);
	}
out:
	egg_debug ("got name=%s, icon=%s", gclient->priv->parent_title, gclient->priv->parent_icon_name);
	return TRUE;
}

/**
 * gpk_client_set_parent:
 **/
gboolean
gpk_client_set_parent (GpkClient *gclient, GtkWindow *window)
{
	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	gclient->priv->parent_window = GTK_WIDGET (window)->window;
	egg_debug ("parent_window=%p", gclient->priv->parent_window);
	gpk_client_dialog_set_parent (gclient->priv->dialog, gclient->priv->parent_window);
	return TRUE;
}

/**
 * gpk_client_set_parent_xid:
 **/
gboolean
gpk_client_set_parent_xid (GpkClient *gclient, guint32 xid)
{
	GdkDisplay *display;
	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	display = gdk_display_get_default ();
	gclient->priv->parent_window = gdk_window_foreign_new_for_display (display, xid);
	egg_debug ("parent_window=%p", gclient->priv->parent_window);
	gpk_client_dialog_set_parent (gclient->priv->dialog, gclient->priv->parent_window);
	return TRUE;
}

/**
 * gpk_client_update_timestamp:
 **/
gboolean
gpk_client_update_timestamp (GpkClient *gclient, guint32 timestamp)
{
	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	gclient->priv->timestamp = timestamp;
	return TRUE;
}

/**
 * gpk_client_class_init:
 * @klass: The #GpkClientClass
 **/
static void
gpk_client_class_init (GpkClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_client_finalize;
	g_type_class_add_private (klass, sizeof (GpkClientPrivate));
	signals [GPK_CLIENT_QUIT] =
		g_signal_new ("quit",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * gpk_client_init:
 * @gclient: a valid #GpkClient instance
 **/
static void
gpk_client_init (GpkClient *gclient)
{
	gboolean ret;

	gclient->priv = GPK_CLIENT_GET_PRIVATE (gclient);

	gclient->priv->files_array = NULL;
	gclient->priv->parent_window = NULL;
	gclient->priv->parent_title = NULL;
	gclient->priv->parent_icon_name = NULL;
	gclient->priv->using_secondary_client = FALSE;
	gclient->priv->exit = PK_EXIT_ENUM_FAILED;
	gclient->priv->show_confirm = TRUE;
	gclient->priv->show_progress = TRUE;
	gclient->priv->show_finished = TRUE;
	gclient->priv->show_warning = TRUE;
	gclient->priv->finished_timer_id = 0;
	gclient->priv->timestamp = 0;
	gclient->priv->loop = g_main_loop_new (NULL, FALSE);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   PK_DATA G_DIR_SEPARATOR_S "icons");

	gclient->priv->dialog = gpk_client_dialog_new ();
	gpk_client_dialog_set_window_icon (gclient->priv->dialog, "pk-package-installed");
	g_signal_connect (gclient->priv->dialog, "cancel",
			  G_CALLBACK (pk_client_button_cancel_cb), gclient);
	g_signal_connect (gclient->priv->dialog, "close",
			  G_CALLBACK (pk_client_button_close_cb), gclient);

	/* use gconf for session settings */
	gclient->priv->gconf_client = gconf_client_get_default ();

	/* get actions */
	gclient->priv->control = pk_control_new ();
	gclient->priv->roles = pk_control_get_actions (gclient->priv->control, NULL);

	gclient->priv->client_action = pk_client_new ();
	pk_client_set_use_buffer (gclient->priv->client_action, TRUE, NULL);
	g_signal_connect (gclient->priv->client_action, "finished",
			  G_CALLBACK (gpk_client_finished_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "progress-changed",
			  G_CALLBACK (gpk_client_progress_changed_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "status-changed",
			  G_CALLBACK (gpk_client_status_changed_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "error-code",
			  G_CALLBACK (gpk_client_error_code_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "package",
			  G_CALLBACK (gpk_client_package_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "allow-cancel",
			  G_CALLBACK (gpk_client_allow_cancel_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "repo-signature-required",
			  G_CALLBACK (gpk_client_repo_signature_required_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "eula-required",
			  G_CALLBACK (gpk_client_eula_required_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "files",
			  G_CALLBACK (gpk_client_files_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "distro-upgrade",
			  G_CALLBACK (pk_client_distro_upgrade_cb), gclient);

	gclient->priv->client_resolve = pk_client_new ();
	g_signal_connect (gclient->priv->client_resolve, "status-changed",
			  G_CALLBACK (gpk_client_status_changed_cb), gclient);
	pk_client_set_use_buffer (gclient->priv->client_resolve, TRUE, NULL);
	pk_client_set_synchronous (gclient->priv->client_resolve, TRUE, NULL);

	/* this is asynchronous, else we get into livelock */
	gclient->priv->client_secondary = pk_client_new ();
	g_signal_connect (gclient->priv->client_secondary, "finished",
			  G_CALLBACK (gpk_client_secondary_finished_cb), gclient);

	/* used for icons and translations */
	gclient->priv->extra = pk_extra_new ();
	ret = pk_extra_set_database (gclient->priv->extra, NULL);
	if (!ret)
		egg_warning ("failed to set extra database");
	pk_extra_set_locale (gclient->priv->extra, NULL);

	/* cache the upgrade array */
	gclient->priv->upgrade_array = g_ptr_array_new ();
}

/**
 * gpk_client_finalize:
 * @object: The object to finalize
 **/
static void
gpk_client_finalize (GObject *object)
{
	GpkClient *gclient;

	g_return_if_fail (GPK_IS_CLIENT (object));

	gclient = GPK_CLIENT (object);
	g_return_if_fail (gclient->priv != NULL);

	/* stop the timers if running */
	if (gclient->priv->finished_timer_id != 0)
		g_source_remove (gclient->priv->finished_timer_id);

	g_free (gclient->priv->parent_title);
	g_free (gclient->priv->parent_icon_name);
	g_ptr_array_foreach (gclient->priv->upgrade_array, (GFunc) pk_distro_upgrade_obj_free, NULL);
	g_ptr_array_free (gclient->priv->upgrade_array, TRUE);
	g_strfreev (gclient->priv->files_array);
	g_object_unref (gclient->priv->client_action);
	g_object_unref (gclient->priv->client_resolve);
	g_object_unref (gclient->priv->client_secondary);
	g_object_unref (gclient->priv->control);
	g_object_unref (gclient->priv->extra);
	g_object_unref (gclient->priv->gconf_client);
	g_object_unref (gclient->priv->dialog);
	g_main_loop_unref (gclient->priv->loop);

	G_OBJECT_CLASS (gpk_client_parent_class)->finalize (object);
}

/**
 * gpk_client_new:
 *
 * PkClient is a nice GObject wrapper for gnome-packagekit and makes installing software easy
 *
 * Return value: A new %GpkClient instance
 **/
GpkClient *
gpk_client_new (void)
{
	GpkClient *gclient;
	gclient = g_object_new (GPK_TYPE_CLIENT, NULL);
	return GPK_CLIENT (gclient);
}

