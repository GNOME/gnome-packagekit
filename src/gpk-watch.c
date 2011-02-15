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

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <libnotify/notify.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-string.h"
#include "egg-console-kit.h"

#include "gpk-common.h"
#include "gpk-enum.h"
#include "gpk-error.h"
#include "gpk-inhibit.h"
#include "gpk-modal-dialog.h"
#include "gpk-session.h"
#include "gpk-task.h"
#include "gpk-watch.h"

static void     gpk_watch_finalize	(GObject       *object);

#define GPK_WATCH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_WATCH, GpkWatchPrivate))

struct GpkWatchPrivate
{
	PkControl		*control;
	GPtrArray		*restart_package_names;
	NotifyNotification	*notification_message;
	NotifyNotification	*notification_restart;
	GpkInhibit		*inhibit;
	PkTask			*task;
	PkTransactionList	*tlist;
	PkRestartEnum		 restart;
	GSettings		*settings;
	gchar			*error_details;
	EggConsoleKit		*console;
	GCancellable		*cancellable;
	GPtrArray		*array_progress;
	gchar			*transaction_id;
};

G_DEFINE_TYPE (GpkWatch, gpk_watch, G_TYPE_OBJECT)

/**
 * gpk_watch_class_init:
 * @klass: The GpkWatchClass
 **/
static void
gpk_watch_class_init (GpkWatchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_watch_finalize;
	g_type_class_add_private (klass, sizeof (GpkWatchPrivate));
}

/**
 * gpk_watch_libnotify_cb:
 **/
static void
gpk_watch_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data)
{
	gboolean ret;
	GError *error = NULL;
	GpkWatch *watch = GPK_WATCH (data);

	if (g_strcmp0 (action, "do-not-show-notify-complete") == 0) {
		g_debug ("set %s to FALSE", GPK_SETTINGS_NOTIFY_COMPLETED);
		g_settings_set_boolean (watch->priv->settings, GPK_SETTINGS_NOTIFY_COMPLETED, FALSE);

	} else if (g_strcmp0 (action, "show-error-details") == 0) {
		/* TRANSLATORS: The detailed error if the user clicks "more info" */
		gpk_error_dialog (_("Error details"), _("Package manager error details"), watch->priv->error_details);

	} else if (g_strcmp0 (action, "logout") == 0) {
		GpkSession *session;
		session = gpk_session_new ();
		gpk_session_logout (session);
		g_object_unref (session);

	} else if (g_strcmp0 (action, "restart") == 0) {

		/* restart using ConsoleKit */
		ret = egg_console_kit_restart (watch->priv->console, &error);
		if (!ret) {
			g_warning ("restarting failed: %s", error->message);
			g_error_free (error);
		}

	} else {
		g_warning ("unknown action id: %s", action);
	}
}

/**
 * gpk_watch_lookup_progress_from_transaction_id:
 **/
static PkProgress *
gpk_watch_lookup_progress_from_transaction_id (GpkWatch *watch, const gchar *transaction_id)
{
	GPtrArray *array;
	guint i;
	gchar *tid_tmp;
	gboolean ret;
	PkProgress *progress;

	array = watch->priv->array_progress;
	for (i=0; i<array->len; i++) {
		progress = g_ptr_array_index (array, i);
		g_object_get (progress,
			      "transaction-id", &tid_tmp,
			      NULL);
		ret = (g_strcmp0 (transaction_id, tid_tmp) == 0);
		g_free (tid_tmp);
		if (ret)
			goto out;
	}
	progress = NULL;
out:
	return progress;
}

#if PK_CHECK_VERSION(0,6,4)
/**
 * gpk_watch_set_root_cb:
 **/
static void
gpk_watch_set_root_cb (GObject *object, GAsyncResult *res, GpkWatch *watch)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	gboolean ret;

	/* get the result */
	ret = pk_control_set_root_finish (control, res, &error);
	if (!ret) {
		g_warning ("failed to set install root: %s", error->message);
		g_error_free (error);
		return;
	}
}

/**
 * gpk_watch_set_root:
 **/
static void
gpk_watch_set_root (GpkWatch *watch)
{
	gchar *root;

	/* get install root */
	root = g_settings_get_string (watch->priv->settings, GPK_SETTINGS_INSTALL_ROOT);
	if (root == NULL) {
		g_warning ("could not read install root");
		goto out;
	}

	pk_control_set_root_async (watch->priv->control, root, watch->priv->cancellable,
				   (GAsyncReadyCallback) gpk_watch_set_root_cb, watch);
out:
	g_free (root);
}
#else
static void gpk_watch_set_root (GpkWatch *watch) {}
#endif

/**
 * gpk_watch_key_changed_cb:
 *
 * We might have to do things when the keys change; do them here.
 **/
static void
gpk_watch_key_changed_cb (GSettings *client, const gchar *key, GpkWatch *watch)
{
	g_debug ("keys have changed");
	gpk_watch_set_root (watch);
}

/**
 * gpk_watch_set_connected:
 **/
static void
gpk_watch_set_connected (GpkWatch *watch, gboolean connected)
{
	if (!connected)
		return;

	/* daemon has just appeared */
	g_debug ("dameon has just appeared");
	gpk_watch_set_root (watch);
}

/**
 * gpk_watch_notify_connected_cb:
 **/
static void
gpk_watch_notify_connected_cb (PkControl *control, GParamSpec *pspec, GpkWatch *watch)
{
	gboolean connected;
	g_object_get (control, "connected", &connected, NULL);
	gpk_watch_set_connected (watch, connected);
}

/**
 * gpk_watch_notify_locked_cb:
 **/
static void
gpk_watch_notify_locked_cb (PkControl *control, GParamSpec *pspec, GpkWatch *watch)
{
	gboolean locked;
	g_object_get (control, "locked", &locked, NULL);
	if (locked)
		gpk_inhibit_create (watch->priv->inhibit);
	else
		gpk_inhibit_remove (watch->priv->inhibit);
}

/**
 * gpk_watch_is_message_ignored:
 **/
static gboolean
gpk_watch_is_message_ignored (GpkWatch *watch, PkMessageEnum message)
{
	guint i;
	gboolean ret = FALSE;
	gchar *ignored_str;
	gchar **ignored = NULL;
	const gchar *message_str;

	/* get from settings */
	ignored_str = g_settings_get_string (watch->priv->settings, GPK_SETTINGS_IGNORED_MESSAGES);
	if (ignored_str == NULL) {
		g_warning ("could not read ignored list");
		goto out;
	}

	/* nothing in list, common case */
	if (egg_strzero (ignored_str)) {
		g_debug ("nothing in ignored list");
		goto out;
	}

	/* split using "," */
	ignored = g_strsplit (ignored_str, ",", 0);

	/* remove any ignored pattern matches */
	message_str = pk_message_enum_to_text (message);
	for (i=0; ignored[i] != NULL; i++) {
		ret = g_pattern_match_simple (ignored[i], message_str);
		if (ret) {
			g_debug ("match %s for %s, ignoring", ignored[i], message_str);
			break;
		}
	}
out:
	g_free (ignored_str);
	g_strfreev (ignored);
	return ret;
}

/**
 * gpk_watch_process_messages_cb:
 **/
static void
gpk_watch_process_messages_cb (PkMessage *item, GpkWatch *watch)
{
	gboolean ret;
	GError *error = NULL;
	gboolean value;
	NotifyNotification *notification;
	PkMessageEnum type;
	gchar *details;

	g_return_if_fail (GPK_IS_WATCH (watch));

	/* get data */
	g_object_get (item,
		      "type", &type,
		      "details", &details,
		      NULL);

	/* this is message unrecognised */
	if (gpk_message_enum_to_localised_text (type) == NULL) {
		g_warning ("message unrecognized, and thus ignored: %i", type);
		goto out;
	}

	/* is ignored */
	ret = gpk_watch_is_message_ignored (watch, type);
	if (ret) {
		g_debug ("ignoring message");
		goto out;
	}

	/* close existing */
	if (watch->priv->notification_message != NULL) {
		ret = notify_notification_close (watch->priv->notification_message, &error);
		if (!ret) {
			g_warning ("error: %s", error->message);
			g_clear_error (&error);
		}
	}

	/* are we accepting notifications */
	value = g_settings_get_boolean (watch->priv->settings, GPK_SETTINGS_NOTIFY_MESSAGE);
	if (!value) {
		g_debug ("not showing notification as prevented in settings");
		goto out;
	}

	/* do the bubble */
	notification = notify_notification_new (gpk_message_enum_to_localised_text (type),
						details,
						"emblem-important");
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		g_warning ("error: %s", error->message);
		g_error_free (error);
	}
	watch->priv->notification_message = notification;
out:
	g_free (details);
}

/**
 * gpk_watch_process_error_code:
 **/
static void
gpk_watch_process_error_code (GpkWatch *watch, PkError *error_code)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *title;
	gchar *title_prefix = NULL;
	const gchar *message;
	gboolean value;
	NotifyNotification *notification;
	PkErrorEnum code;

	g_return_if_fail (GPK_IS_WATCH (watch));

	code = pk_error_get_code (error_code);
	title = gpk_error_enum_to_localised_text (code);

	/* ignore some errors */
	if (code == PK_ERROR_ENUM_NOT_SUPPORTED ||
	    code == PK_ERROR_ENUM_NO_NETWORK ||
	    code == PK_ERROR_ENUM_PROCESS_KILL ||
	    code == PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
		g_debug ("error ignored %s%s", title, pk_error_get_details (error_code));
		goto out;
	}

	/* are we accepting notifications */
	value = g_settings_get_boolean (watch->priv->settings, GPK_SETTINGS_NOTIFY_ERROR);
	if (!value) {
		g_debug ("not showing notification as prevented in settings");
		goto out;
	}

	/* we need to format this */
	message = gpk_error_enum_to_localised_message (code);

	/* save this globally */
	g_free (watch->priv->error_details);
	watch->priv->error_details = g_markup_escape_text (pk_error_get_details (error_code), -1);

	/* TRANSLATORS: Prefix to the title shown in the libnotify popup */
	title_prefix = g_strdup_printf ("%s: %s", _("Package Manager"), title);

	/* do the bubble */
	notification = notify_notification_new (title_prefix, message, NULL);
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, "show-error-details",
					/* TRANSLATORS: This is a link in a libnotify bubble that shows the detailed error */
					_("Show details"), gpk_watch_libnotify_cb, watch, NULL);

	ret = notify_notification_show (notification, &error);
	if (!ret) {
		g_warning ("error: %s", error->message);
		g_error_free (error);
	}
out:
	g_free (title_prefix);
}

/**
 * gpk_watch_process_require_restart_cb:
 **/
static void
gpk_watch_process_require_restart_cb (PkRequireRestart *item, GpkWatch *watch)
{
	GPtrArray *array = NULL;
	GPtrArray *names = NULL;
	const gchar *name;
	gboolean ret;
	GError *error = NULL;
	gchar **split = NULL;
	guint i;
	PkRestartEnum restart;
	gchar *package_id = NULL;
	NotifyNotification *notification;
	const gchar *title;
	const gchar *message;
	const gchar *icon;

	/* get data */
	g_object_get (item,
		      "restart", &restart,
		      "package-id", &package_id,
		      NULL);

	/* if less important than what we are already showing */
	if (restart <= watch->priv->restart) {
		g_debug ("restart already %s, not processing %s",
			   pk_restart_enum_to_text (watch->priv->restart),
			   pk_restart_enum_to_text (restart));
		goto out;
	}

	/* save new restart */
	watch->priv->restart = restart;

	/* add name if not already in the list */
	split = pk_package_id_split (package_id);
	names = watch->priv->restart_package_names;
	for (i=0; i<names->len; i++) {
		name = g_ptr_array_index (names, i);
		if (g_strcmp0 (name, split[PK_PACKAGE_ID_NAME]) == 0) {
			g_debug ("already got %s", name);
			goto out;
		}
	}

	/* localised title */
	title = gpk_restart_enum_to_localised_text (restart);
	message = gpk_restart_enum_to_localised_text (restart);
	icon = gpk_restart_enum_to_dialog_icon_name (restart);

	/* close existing */
	if (watch->priv->notification_restart != NULL) {
		ret = notify_notification_close (watch->priv->notification_restart, &error);
		if (!ret) {
			g_warning ("error: %s", error->message);
			g_clear_error (&error);
		}
	}

	/* do the bubble */
	notification = notify_notification_new (title, message, icon);
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	if (restart == PK_RESTART_ENUM_APPLICATION ||
	    restart == PK_RESTART_ENUM_SESSION ||
	    restart == PK_RESTART_ENUM_SECURITY_SESSION) {
		/* we can't handle application restarting this in a sane way yet */
		notify_notification_add_action (notification, "logout",
						/* TRANSLATORS: log out of the session */
						_("Log out"), gpk_watch_libnotify_cb, watch, NULL);
	} else if (restart == PK_RESTART_ENUM_SYSTEM ||
		   restart == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		notify_notification_add_action (notification, "restart",
						/* TRANSLATORS: restart the computer */
						_("Restart"), gpk_watch_libnotify_cb, watch, NULL);
	} else {
		g_warning ("failed to handle restart action: %s", pk_restart_enum_to_string (restart));
	}
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		g_warning ("error: %s", error->message);
		g_error_free (error);
	}
	watch->priv->notification_restart = notification;

	/* add to list */
	g_debug ("adding %s to restart list", split[PK_PACKAGE_ID_NAME]);
	g_ptr_array_add (names, g_strdup (split[PK_PACKAGE_ID_NAME]));
out:
	g_free (package_id);
	g_strfreev (split);
	if (array != NULL)
		g_object_unref (array);
}

/**
 * gpk_watch_adopt_cb:
 **/
static void
gpk_watch_adopt_cb (PkClient *client, GAsyncResult *res, GpkWatch *watch)
{
	const gchar *message = NULL;
	gboolean caller_active;
	gboolean ret;
	gchar *transaction_id = NULL;
	GError *error = NULL;
	GPtrArray *array;
	guint elapsed_time;
	NotifyNotification *notification;
	PkProgress *progress = NULL;
	PkResults *results;
	PkRoleEnum role;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to adopt: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get data about the transaction */
	g_object_get (results,
		      "role", &role,
		      "progress", &progress,
		      NULL);

	/* get data */
	g_object_get (progress,
		      "transaction-id", &transaction_id,
		      "caller-active", &caller_active,
		      "elapsed-time", &elapsed_time,
		      NULL);

	g_debug ("%s finished (%s)", transaction_id, pk_role_enum_to_text (role));

	/* get the error */
	error_code = pk_results_get_error_code (results);

	/* process messages */
	if (error_code == NULL) {
		array = pk_results_get_message_array (results);
		g_ptr_array_foreach (array, (GFunc) gpk_watch_process_messages_cb, watch);
		g_ptr_array_unref (array);
	}

	/* only process errors if caller is no longer on the bus */
	if (error_code != NULL && !caller_active)
		gpk_watch_process_error_code (watch, error_code);

	/* process restarts */
	if (!caller_active) {
		array = pk_results_get_require_restart_array (results);
		g_ptr_array_foreach (array, (GFunc) gpk_watch_process_require_restart_cb, watch);
		g_ptr_array_unref (array);
	}

	/* are we accepting notifications */
	ret = g_settings_get_boolean (watch->priv->settings, GPK_SETTINGS_NOTIFY_COMPLETED);
	if (!ret) {
		g_debug ("not showing notification as prevented in settings");
		goto out;
	}

	/* is it worth showing a UI? */
	if (elapsed_time < 3000) {
		g_debug ("no notification, too quick");
		goto out;
	}

	/* is caller able to handle the messages itself? */
	if (caller_active) {
		g_debug ("not showing notification as caller is still present");
		goto out;
	}

	if (role == PK_ROLE_ENUM_REMOVE_PACKAGES)
		/* TRANSLATORS: This is the message in the libnotify body */
		message = _("Packages have been removed");
	else if (role == PK_ROLE_ENUM_INSTALL_PACKAGES)
		/* TRANSLATORS: This is the message in the libnotify body */
		message = _("Packages have been installed");
	else if (role == PK_ROLE_ENUM_UPDATE_SYSTEM)
		/* TRANSLATORS: This is the message in the libnotify body */
		message = _("System has been updated");

	/* nothing of interest */
	if (message == NULL)
		goto out;

	/* TRANSLATORS: title: an action has finished, and we are showing the libnotify bubble */
	notification = notify_notification_new (_("Task completed"),
						message, NULL);
	notify_notification_set_timeout (notification, 5000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, "do-not-show-notify-complete",
					_("Do not show this again"), gpk_watch_libnotify_cb, watch, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		g_warning ("error: %s", error->message);
		g_error_free (error);
	}
out:
	g_free (transaction_id);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (progress != NULL)
		g_object_unref (progress);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_watch_progress_cb:
 **/
static void
gpk_watch_progress_cb (PkProgress *progress, PkProgressType type, GpkWatch *watch)
{
	GPtrArray *array;
	guint i;
	gboolean ret = FALSE;
	PkProgress *progress_tmp;
	gchar *text = NULL;

	/* add if not already in list */
	array = watch->priv->array_progress;
	for (i=0; i<array->len; i++) {
		progress_tmp = g_ptr_array_index (array, i);
		if (progress_tmp == progress)
			ret = TRUE;
	}
	if (!ret) {
		g_debug ("adding progress %p", progress);
		g_ptr_array_add (array, g_object_ref (progress));
	}
	g_free (text);
}

/**
 * gpk_watch_transaction_list_added_cb:
 **/
static void
gpk_watch_transaction_list_added_cb (PkTransactionList *tlist, const gchar *transaction_id, GpkWatch *watch)
{
	PkProgress *progress;

	/* find progress */
	progress = gpk_watch_lookup_progress_from_transaction_id (watch, transaction_id);
	if (progress != NULL) {
		g_warning ("already added: %s", transaction_id);
		return;
	}
	g_debug ("added: %s", transaction_id);
	pk_client_adopt_async (PK_CLIENT(watch->priv->task), transaction_id, watch->priv->cancellable,
			       (PkProgressCallback) gpk_watch_progress_cb, watch,
			       (GAsyncReadyCallback) gpk_watch_adopt_cb, watch);
}

/**
 * gpk_watch_transaction_list_removed_cb:
 **/
static void
gpk_watch_transaction_list_removed_cb (PkTransactionList *tlist, const gchar *transaction_id, GpkWatch *watch)
{
	PkProgress *progress;

	/* find progress */
	progress = gpk_watch_lookup_progress_from_transaction_id (watch, transaction_id);
	if (progress == NULL) {
		g_warning ("could not find: %s", transaction_id);
		return;
	}
	g_debug ("removed: %s", transaction_id);
	g_ptr_array_remove_fast (watch->priv->array_progress, progress);
}

/**
 * gpk_check_update_get_properties_cb:
 **/
static void
gpk_check_update_get_properties_cb (GObject *object, GAsyncResult *res, GpkWatch *watch)
{
	gboolean connected;
	GError *error = NULL;
	PkControl *control = PK_CONTROL(object);
	gboolean ret;

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	if (!ret) {
		/* TRANSLATORS: backend is broken, and won't tell us what it supports */
		g_warning ("details could not be retrieved: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get values */
	g_object_get (control,
		      "connected", &connected,
		      NULL);

	/* coldplug daemon */
	gpk_watch_set_connected (watch, connected);
out:
	return;
}

/**
 * gpk_watch_init:
 * @watch: This class instance
 **/
static void
gpk_watch_init (GpkWatch *watch)
{
	watch->priv = GPK_WATCH_GET_PRIVATE (watch);
	watch->priv->error_details = NULL;
	watch->priv->notification_message = NULL;
	watch->priv->restart = PK_RESTART_ENUM_NONE;
	watch->priv->console = egg_console_kit_new ();
	watch->priv->cancellable = g_cancellable_new ();
	watch->priv->array_progress = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	watch->priv->settings = g_settings_new (GPK_SETTINGS_SCHEMA);
	g_signal_connect (watch->priv->settings, "changed", G_CALLBACK (gpk_watch_key_changed_cb), watch);

	watch->priv->restart_package_names = g_ptr_array_new_with_free_func (g_free);
	watch->priv->task = PK_TASK(gpk_task_new ());
	g_object_set (watch->priv->task,
		      "background", TRUE,
		      NULL);

	/* we need to get ::locked */
	watch->priv->control = pk_control_new ();
	g_signal_connect (watch->priv->control, "notify::locked",
			  G_CALLBACK (gpk_watch_notify_locked_cb), watch);
	g_signal_connect (watch->priv->control, "notify::connected",
			  G_CALLBACK (gpk_watch_notify_connected_cb), watch);

	/* get properties */
	pk_control_get_properties_async (watch->priv->control, NULL, (GAsyncReadyCallback) gpk_check_update_get_properties_cb, watch);

	/* do session inhibit */
	watch->priv->inhibit = gpk_inhibit_new ();

	watch->priv->tlist = pk_transaction_list_new ();
	g_signal_connect (watch->priv->tlist, "added",
			  G_CALLBACK (gpk_watch_transaction_list_added_cb), watch);
	g_signal_connect (watch->priv->tlist, "removed",
			  G_CALLBACK (gpk_watch_transaction_list_removed_cb), watch);

	/* set the root */
	gpk_watch_set_root (watch);
}

/**
 * gpk_watch_finalize:
 * @object: The object to finalize
 **/
static void
gpk_watch_finalize (GObject *object)
{
	GpkWatch *watch;

	g_return_if_fail (GPK_IS_WATCH (object));

	watch = GPK_WATCH (object);

	g_return_if_fail (watch->priv != NULL);

	g_free (watch->priv->error_details);
	g_object_unref (watch->priv->cancellable);
	g_object_unref (watch->priv->task);
	g_object_unref (watch->priv->console);
	g_object_unref (watch->priv->control);
	g_object_unref (watch->priv->settings);
	g_object_unref (watch->priv->inhibit);
	g_object_unref (watch->priv->tlist);
	g_ptr_array_unref (watch->priv->array_progress);
	g_ptr_array_unref (watch->priv->restart_package_names);

	G_OBJECT_CLASS (gpk_watch_parent_class)->finalize (object);
}

/**
 * gpk_watch_new:
 *
 * Return value: a new GpkWatch object.
 **/
GpkWatch *
gpk_watch_new (void)
{
	GpkWatch *watch;
	watch = g_object_new (GPK_TYPE_WATCH, NULL);
	return GPK_WATCH (watch);
}

