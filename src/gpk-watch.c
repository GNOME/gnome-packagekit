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
#include <gconf/gconf-client.h>
#include <libnotify/notify.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-common.h"
#include "gpk-session.h"
#include "gpk-error.h"
#include "gpk-watch.h"
#include "gpk-modal-dialog.h"
#include "gpk-inhibit.h"
#include "gpk-consolekit.h"
#include "gpk-enum.h"

static void     gpk_watch_finalize	(GObject       *object);

#define GPK_WATCH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_WATCH, GpkWatchPrivate))
#define GPK_WATCH_MAXIMUM_TOOLTIP_LINES		10
#define GPK_WATCH_GCONF_PROXY_HTTP		"/system/http_proxy"
#define GPK_WATCH_GCONF_PROXY_FTP		"/system/proxy"
#define GPK_WATCH_SET_PROXY_RATE_LIMIT		200 /* ms */

struct GpkWatchPrivate
{
	PkControl		*control;
	GtkStatusIcon		*status_icon;
	GPtrArray		*cached_messages;
	GPtrArray		*restart_package_names;
	NotifyNotification	*notification_cached_messages;
	GpkInhibit		*inhibit;
	GpkModalDialog		*dialog;
	PkClient		*client_primary;
	PkConnection		*pconnection;
	PkTaskList		*tlist;
	PkRestartEnum		 restart;
	GConfClient		*gconf_client;
	guint			 set_proxy_timeout;
	gchar			*error_details;
	gboolean		 hide_warning;
};

typedef struct {
	PkMessageEnum	 type;
	gchar		*tid;
	gchar		*details;
} GpkWatchCachedMessage;

enum {
	GPK_WATCH_COLUMN_TEXT,
	GPK_WATCH_COLUMN_TID,
	GPK_WATCH_COLUMN_DETAILS,
	GPK_WATCH_COLUMN_LAST
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
 * gpk_watch_cached_message_free:
 **/
static void
gpk_watch_cached_message_free (GpkWatchCachedMessage *cached_message)
{
	if (cached_message == NULL)
		return;
	g_free (cached_message->tid);
	g_free (cached_message->details);
	g_free (cached_message);
}

/**
 * gpk_watch_refresh_tooltip:
 **/
static gboolean
gpk_watch_refresh_tooltip (GpkWatch *watch)
{
	guint i;
	PkTaskListItem *item;
	guint length;
	guint len;
	GString *status;
	const gchar *trailer;
	const gchar *localised_status;
	gchar *package_loc;
	gchar **packages;

	g_return_val_if_fail (GPK_IS_WATCH (watch), FALSE);

	status = g_string_new ("");
	length = pk_task_list_get_size (watch->priv->tlist);
	egg_debug ("refresh tooltip %i", length);
	if (length == 0) {

		/* any restart required? */
		if (watch->priv->restart != PK_RESTART_ENUM_NONE) {
			g_string_append (status, gpk_restart_enum_to_localised_text (watch->priv->restart));
			g_string_append_c (status, '\n');

			len = watch->priv->restart_package_names->len;

			if (len > 0) {
				packages = pk_ptr_array_to_strv (watch->priv->restart_package_names);
				package_loc = gpk_strv_join_locale (packages);
				g_strfreev (packages);
				if (package_loc != NULL) {
					/* TRANSLATORS: a list of packages is shown that need to restarted */
					g_string_append_printf (status, ngettext ("This is due to the %s package being updated.",
										  "This is due to the following packages being updated: %s", len), package_loc);
				} else {
					/* TRANSLATORS: over 5 packages require the system to be restarted, don't list them all here */
					g_string_append_printf (status, ngettext ("This is because %i package has been updated.",
										  "This is because %i packages have been updated.", len), len);
				}
				g_string_append_c (status, '\n');
				g_free (package_loc);
			}

			/* remove final \n */
			if (status->len > 0)
				g_string_set_size (status, status->len - 1);

			goto out;
		}

		/* do we have any cached messages to show? */
		len = watch->priv->cached_messages->len;
		if (len > 0) {
			g_string_append_printf (status, ngettext ("%i message from the package manager", "%i messages from the package manager", len), len);
			goto out;
		}

		egg_debug ("nothing to show");
		goto out;
	}
	for (i=0; i<length; i++) {
		item = pk_task_list_get_item (watch->priv->tlist, i);
		if (item == NULL) {
			egg_warning ("not found item %i", i);
			break;
		}
		localised_status = gpk_status_enum_to_localised_text (item->status);

		/* should we display the text */
		if (item->role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
		    egg_strzero (item->text)) {
			g_string_append_printf (status, "%s\n", localised_status);
		} else {
			/* display the package name, not the package_id */
			g_string_append_printf (status, "%s: %s\n", localised_status, item->text);
		}
		/* don't fill the screen with a giant tooltip */
		if (i > GPK_WATCH_MAXIMUM_TOOLTIP_LINES) {
			/* TRANSLATORS: if the menu won't fit, inform the user there are a few more things waiting */
			trailer = ngettext ("(%i more task)", "(%i more tasks)", i - GPK_WATCH_MAXIMUM_TOOLTIP_LINES);
			g_string_append_printf (status, "%s\n", trailer);
			break;
		}
	}
	if (status->len == 0)
		g_string_append (status, "Doing something...");
	else
		g_string_set_size (status, status->len-1);

out:
#if GTK_CHECK_VERSION(2,15,0)
	gtk_status_icon_set_tooltip_text (watch->priv->status_icon, status->str);
#else
	gtk_status_icon_set_tooltip (watch->priv->status_icon, status->str);
#endif
	g_string_free (status, TRUE);
	return TRUE;
}

/**
 * gpk_watch_task_list_to_status_bitfield:
 **/
static PkBitfield
gpk_watch_task_list_to_status_bitfield (GpkWatch *watch)
{
	gboolean ret;
	gboolean active;
	gboolean watch_active;
	guint i;
	guint length;
	PkBitfield status = 0;
	PkTaskListItem *item;

	g_return_val_if_fail (GPK_IS_WATCH (watch), PK_STATUS_ENUM_UNKNOWN);

	/* shortcut */
	length = pk_task_list_get_size (watch->priv->tlist);
	if (length == 0)
		goto out;

	/* do we watch active transactions */
	watch_active = gconf_client_get_bool (watch->priv->gconf_client, GPK_CONF_WATCH_ACTIVE_TRANSACTIONS, NULL);

	/* add each status to a list */
	for (i=0; i<length; i++) {
		item = pk_task_list_get_item (watch->priv->tlist, i);
		if (item == NULL) {
			egg_warning ("not found item %i", i);
			break;
		}

		/* only show an icon for this if the application isn't still on the bus */
		ret = pk_client_is_caller_active (item->monitor, &active, NULL);

		/* if we failed to get data, assume bad things happened */
		if (!ret)
			active = TRUE;

		/* add to bitfield calculation */
		egg_debug ("%s %s (active:%i)", item->tid, pk_status_enum_to_text (item->status), active);
		if (!active || watch_active)
			pk_bitfield_add (status, item->status);
	}
out:
	return status;
}

/**
 * gpk_watch_refresh_icon:
 **/
static gboolean
gpk_watch_refresh_icon (GpkWatch *watch)
{
	const gchar *icon_name = NULL;
	PkBitfield status;
	gint value = -1;
	guint len;

	g_return_val_if_fail (GPK_IS_WATCH (watch), FALSE);

	egg_debug ("rescan");
	status = gpk_watch_task_list_to_status_bitfield (watch);

	/* something in list */
	if (status != 0) {
		/* get the most important icon */
		value = pk_bitfield_contain_priority (status,
						      PK_STATUS_ENUM_REFRESH_CACHE,
						      PK_STATUS_ENUM_LOADING_CACHE,
						      PK_STATUS_ENUM_CANCEL,
						      PK_STATUS_ENUM_INSTALL,
						      PK_STATUS_ENUM_REMOVE,
						      PK_STATUS_ENUM_CLEANUP,
						      PK_STATUS_ENUM_OBSOLETE,
						      PK_STATUS_ENUM_SETUP,
						      PK_STATUS_ENUM_RUNNING,
						      PK_STATUS_ENUM_UPDATE,
						      PK_STATUS_ENUM_DOWNLOAD,
						      PK_STATUS_ENUM_DOWNLOAD_REPOSITORY,
						      PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST,
						      PK_STATUS_ENUM_DOWNLOAD_FILELIST,
						      PK_STATUS_ENUM_DOWNLOAD_CHANGELOG,
						      PK_STATUS_ENUM_DOWNLOAD_GROUP,
						      PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO,
						      PK_STATUS_ENUM_SCAN_APPLICATIONS,
						      PK_STATUS_ENUM_GENERATE_PACKAGE_LIST,
						      PK_STATUS_ENUM_QUERY,
						      PK_STATUS_ENUM_INFO,
						      PK_STATUS_ENUM_DEP_RESOLVE,
						      PK_STATUS_ENUM_ROLLBACK,
						      PK_STATUS_ENUM_TEST_COMMIT,
						      PK_STATUS_ENUM_COMMIT,
						      PK_STATUS_ENUM_REQUEST,
						      PK_STATUS_ENUM_SIG_CHECK,
						      PK_STATUS_ENUM_CLEANUP,
						      PK_STATUS_ENUM_REPACKAGING,
						      PK_STATUS_ENUM_WAIT,
						      PK_STATUS_ENUM_WAITING_FOR_LOCK,
						      PK_STATUS_ENUM_FINISHED, -1);
	}

	/* only set if in the list and not unknown */
	if (value != PK_STATUS_ENUM_UNKNOWN && value != -1) {
		icon_name = gpk_status_enum_to_icon_name (value);
		goto out;
	}

	/* any restart required? */
	if (watch->priv->restart != PK_RESTART_ENUM_NONE &&
	    watch->priv->hide_warning == FALSE) {
		icon_name = gpk_restart_enum_to_dialog_icon_name (watch->priv->restart);
		goto out;
	}

	/* do we have any cached messages to show? */
	len = watch->priv->cached_messages->len;
	if (len > 0) {
		icon_name = "pk-setup";
		goto out;
	}

out:
	/* no icon, hide */
	if (icon_name == NULL) {
		gtk_status_icon_set_visible (watch->priv->status_icon, FALSE);
		return FALSE;
	}
	gtk_status_icon_set_from_icon_name (watch->priv->status_icon, icon_name);
	gtk_status_icon_set_visible (watch->priv->status_icon, TRUE);
	return TRUE;
}

/**
 * gpk_watch_task_list_changed_cb:
 **/
static void
gpk_watch_task_list_changed_cb (PkTaskList *tlist, GpkWatch *watch)
{
	g_return_if_fail (GPK_IS_WATCH (watch));

	gpk_watch_refresh_icon (watch);
	gpk_watch_refresh_tooltip (watch);
}

/**
 * gpk_watch_libnotify_cb:
 **/
static void
gpk_watch_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data)
{
	GpkWatch *watch = GPK_WATCH (data);

	if (egg_strequal (action, "do-not-show-notify-complete")) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_COMPLETED);
		gconf_client_set_bool (watch->priv->gconf_client, GPK_CONF_NOTIFY_COMPLETED, FALSE, NULL);

	} else if (egg_strequal (action, "show-error-details")) {
		/* TRANSLATORS: The detailed error if the user clicks "more info" */
		gpk_error_dialog (_("Error details"), _("Package manager error details"), watch->priv->error_details);

	} else {
		egg_warning ("unknown action id: %s", action);
	}
}

/**
 * gpk_watch_task_list_finished_cb:
 **/
static void
gpk_watch_task_list_finished_cb (PkTaskList *tlist, PkClient *client, PkExitEnum exit_enum, guint runtime, GpkWatch *watch)
{
	guint i;
	gboolean ret;
	gboolean value;
	PkRoleEnum role;
	PkRestartEnum restart;
	GError *error = NULL;
	gchar *text = NULL;
	gchar *message = NULL;
	NotifyNotification *notification;
#if PK_CHECK_VERSION(0,5,0)
	const PkRequireRestartObj *obj;
	PkObjList *array;
#else
	PkPackageId *id;
	const GPtrArray *array;
#endif

	g_return_if_fail (GPK_IS_WATCH (watch));

	/* get the role */
	ret = pk_client_get_role (client, &role, &text, NULL);
	if (!ret) {
		egg_warning ("cannot get role");
		goto out;
	}
	egg_debug ("role=%s, text=%s", pk_role_enum_to_text (role), text);

	/* show an icon if the user needs to reboot */
	if (role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		/* if more important than what we are already showing, then update the icon */
		restart = pk_client_get_require_restart (client);
		if (restart > watch->priv->restart) {
#if PK_CHECK_VERSION(0,5,0)
			/* list packages requiring this */
			array = pk_client_get_require_restart_list (client);
			if (array == NULL) {
				egg_warning ("no data about restarts, perhaps not buffered");
				goto no_data;
			}
			for (i=0; i<array->len; i++) {
				obj = pk_obj_list_index (array, i);
				if (obj->restart >= restart)
					g_ptr_array_add (watch->priv->restart_package_names, g_strdup (obj->id->name));
			}
			g_object_unref (array);
no_data:
#else
			/* list packages requiring this */
			array = pk_client_get_require_restart_list (client);
			for (i=0; i<array->len; i++) {
				id = g_ptr_array_index (array, i);
				g_ptr_array_add (watch->priv->restart_package_names, g_strdup (id->name));
			}
#endif
			/* save new restart */
			watch->priv->restart = restart;
		}
	}

	/* is it worth showing a UI? */
	if (runtime < 3000) {
		egg_debug ("no notification, too quick");
		goto out;
	}

	/* is it worth showing a UI? */
	if (exit_enum != PK_EXIT_ENUM_SUCCESS) {
		egg_debug ("not notifying, as didn't complete okay");
		goto out;
	}

	/* are we accepting notifications */
	value = gconf_client_get_bool (watch->priv->gconf_client, GPK_CONF_NOTIFY_COMPLETED, NULL);
	if (!value) {
		egg_debug ("not showing notification as prevented in gconf");
		goto out;
	}

	/* is caller able to handle the messages itself? */
	ret = pk_client_is_caller_active (client, &value, &error);
	if (!ret) {
		egg_warning ("could not get caller active status: %s", error->message);
		g_error_free (error);
		goto out;
	}
	if (value) {
		egg_debug ("not showing notification as caller is still present");
		goto out;
	}

	if (role == PK_ROLE_ENUM_REMOVE_PACKAGES)
		/* TRANSLATORS: This is the message in the libnotify body */
		message = g_strdup_printf (_("Package '%s' has been removed"), text);
	else if (role == PK_ROLE_ENUM_INSTALL_PACKAGES)
		/* TRANSLATORS: This is the message in the libnotify body */
		message = g_strdup_printf (_("Package '%s' has been installed"), text);
	else if (role == PK_ROLE_ENUM_UPDATE_SYSTEM)
		/* TRANSLATORS: This is the message in the libnotify body */
		message = g_strdup (_("System has been updated"));

	/* nothing of interest */
	if (message == NULL)
		goto out;

	/* TRANSLATORS: title: an action has finished, and we are showing the libnotify bubble */
	notification = notify_notification_new (_("Task completed"), message, "help-browser", NULL);
	notify_notification_set_timeout (notification, 5000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, "do-not-show-notify-complete",
					_("Do not show this again"), gpk_watch_libnotify_cb, watch, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}

out:
	g_free (message);
	g_free (text);
}

/**
 * gpk_watch_error_code_cb:
 **/
static void
gpk_watch_error_code_cb (PkTaskList *tlist, PkClient *client, PkErrorCodeEnum error_code, const gchar *details, GpkWatch *watch)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *title;
	gchar *title_prefix;
	const gchar *message;
	gboolean is_active;
	gboolean value;
	NotifyNotification *notification;

	g_return_if_fail (GPK_IS_WATCH (watch));

	title = gpk_error_enum_to_localised_text (error_code);

	/* if the client dbus connection is still active */
	pk_client_is_caller_active (client, &is_active, NULL);

	/* do we ignore this error? */
	if (is_active) {
		egg_debug ("client active so leaving error %s\n%s", title, details);
		return;
	}

	/* ignore some errors */
	if (error_code == PK_ERROR_ENUM_NOT_SUPPORTED ||
	    error_code == PK_ERROR_ENUM_NO_NETWORK ||
	    error_code == PK_ERROR_ENUM_PROCESS_KILL ||
	    error_code == PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
		egg_debug ("error ignored %s\n%s", title, details);
		return;
	}

	/* are we accepting notifications */
	value = gconf_client_get_bool (watch->priv->gconf_client, GPK_CONF_NOTIFY_ERROR, NULL);
	if (!value) {
		egg_debug ("not showing notification as prevented in gconf");
		return;
	}

	/* we need to format this */
	message = gpk_error_enum_to_localised_message (error_code);

	/* save this globally */
	g_free (watch->priv->error_details);
	watch->priv->error_details = g_markup_escape_text (details, -1);

	/* TRANSLATORS: Prefix to the title shown in the libnotify popup */
	title_prefix = g_strdup_printf ("%s: %s", _("Package Manager"), title);

	/* do the bubble */
	notification = notify_notification_new (title_prefix, message, "help-browser", NULL);
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, "show-error-details",
					/* TRANSLATORS: This is a link in a libnotify bubble that shows the detailed error */
					_("Show details"), gpk_watch_libnotify_cb, watch, NULL);

	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
	g_free (title_prefix);
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

	/* get from gconf */
	ignored_str = gconf_client_get_string (watch->priv->gconf_client, GPK_CONF_IGNORED_MESSAGES, NULL);
	if (ignored_str == NULL) {
		egg_warning ("could not read ignored list");
		goto out;
	}

	/* nothing in list, common case */
	if (egg_strzero (ignored_str)) {
		egg_debug ("nothing in ignored list");
		goto out;
	}

	/* split using "," */
	ignored = g_strsplit (ignored_str, ",", 0);

	/* remove any ignored pattern matches */
	message_str = pk_message_enum_to_text (message);
	for (i=0; ignored[i] != NULL; i++) {
		ret = g_pattern_match_simple (ignored[i], message_str);
		if (ret) {
			egg_debug ("match %s for %s, ignoring", ignored[i], message_str);
			break;
		}
	}
out:
	g_free (ignored_str);
	g_strfreev (ignored);
	return ret;
}

/**
 * gpk_watch_message_cb:
 **/
static void
gpk_watch_message_cb (PkTaskList *tlist, PkClient *client, PkMessageEnum message, const gchar *details, GpkWatch *watch)
{
	gboolean ret;
	GError *error = NULL;
	gboolean value;
	NotifyNotification *notification;
	GpkWatchCachedMessage *cached_message;

	g_return_if_fail (GPK_IS_WATCH (watch));

	/* is ignored */
	ret = gpk_watch_is_message_ignored (watch, message);
	if (ret) {
		egg_debug ("igoring message");
		return;
	}

	/* add to list */
	cached_message = g_new0 (GpkWatchCachedMessage, 1);
	cached_message->type = message;
	cached_message->tid = pk_client_get_tid (client);
	cached_message->details = g_strdup (details);
	g_ptr_array_add (watch->priv->cached_messages, cached_message);

	/* close existing */
	if (watch->priv->notification_cached_messages != NULL) {
		ret = notify_notification_close (watch->priv->notification_cached_messages, &error);
		if (!ret) {
			egg_warning ("error: %s", error->message);
			g_error_free (error);
			error = NULL;
		}
	}

	/* are we accepting notifications */
	value = gconf_client_get_bool (watch->priv->gconf_client, GPK_CONF_NOTIFY_MESSAGE, NULL);
	if (!value) {
		egg_debug ("not showing notification as prevented in gconf");
		return;
	}

	/* do the bubble */
	notification = notify_notification_new_with_status_icon (_("New package manager message"), NULL, "emblem-important", watch->priv->status_icon);
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
	watch->priv->notification_cached_messages = notification;
}

/**
 * gpk_watch_about_dialog_url_cb:
 **/
static void
gpk_watch_about_dialog_url_cb (GtkAboutDialog *about, const char *address, gpointer data)
{
	GError *error = NULL;
	gboolean ret;
	GdkScreen *gscreen;
	gchar *url;
	gchar *protocol = (gchar*) data;

	if (protocol != NULL)
		url = g_strconcat (protocol, address, NULL);
	else
		url = g_strdup (address);

	gscreen = gtk_window_get_screen (GTK_WINDOW (about));

	ret = gtk_show_uri (gscreen, url, gtk_get_current_event_time (), &error);

	if (!ret) {
		/* TRANSLATORS: We couldn't launch the tool, normally a packaging problem */
		gpk_error_dialog (_("Internal error"), _("Failed to show url"), error->message);
		g_error_free (error);
	}

	g_free (url);
}

/**
 * gpk_watch_show_about_cb:
 **/
static void
gpk_watch_show_about_cb (GtkMenuItem *item, gpointer data)
{
	const char *authors[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *documenters[] = {
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
	const char *translators = _("translator-credits");
	char *license_trans;

	/* Translators comment: put your own name here to appear in the about dialog. */
	if (!strcmp (translators, "translator-credits"))
		translators = NULL;

	license_trans = g_strconcat (_(license[0]), "\n\n", _(license[1]), "\n\n",
				     _(license[2]), "\n\n", _(license[3]), "\n",  NULL);

	gtk_about_dialog_set_url_hook (gpk_watch_about_dialog_url_cb, NULL, NULL);
	gtk_about_dialog_set_email_hook (gpk_watch_about_dialog_url_cb, (gpointer) "mailto:", NULL);

	gtk_window_set_default_icon_name (GPK_ICON_SOFTWARE_LOG);
	gtk_show_about_dialog (NULL,
			       "version", VERSION,
			       "copyright", "Copyright \xc2\xa9 2007-2009 Richard Hughes",
			       "license", license_trans,
			       "website-label", _("PackageKit Website"),
			       "website", "www.packagekit.org",
			       "comments", "PackageKit",
			       "authors", authors,
			       "documenters", documenters,
			       "translator-credits", translators,
			       "logo-icon-name", GPK_ICON_SOFTWARE_LOG,
			       NULL);
	g_free (license_trans);
}

/**
 * gpk_watch_popup_menu_cb:
 *
 * Display the popup menu.
 **/
static void
gpk_watch_popup_menu_cb (GtkStatusIcon *status_icon, guint button, guint32 timestamp, GpkWatch *watch)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;
	GtkWidget *image;

	g_return_if_fail (GPK_IS_WATCH (watch));
	egg_debug ("icon right clicked");

	/* TRANSLATORS: this is the right click menu item */
	item = gtk_image_menu_item_new_with_mnemonic (_("_About"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (gpk_watch_show_about_cb), watch);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* show the menu */
	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gtk_status_icon_position_menu, status_icon,
			button, timestamp);
	if (button == 0)
		gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
}

/**
 * gpk_watch_menu_show_messages_cb:
 **/
static void
gpk_watch_menu_show_messages_cb (GtkMenuItem *item, gpointer data)
{
	GpkWatch *watch = GPK_WATCH (data);
	GtkBuilder *builder;
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkListStore *list_store;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	GtkTreeModel *model;
	guint i;
	GpkWatchCachedMessage *cached_message;
	guint retval;
	GError *error = NULL;

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-repo.ui", &error);
	if (error != NULL) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out_build;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_repo"));
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SOFTWARE_LOG);
	gtk_window_set_title (GTK_WINDOW (main_window), _("Package Manager Messages"));

	/* set a size, if the screen allows */
	gpk_window_set_size_request (GTK_WINDOW(main_window), 500, 200);

	/* Get the main window quit */
	g_signal_connect_swapped (main_window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_help"));
	gtk_widget_hide (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "checkbutton_detail"));
	gtk_widget_hide (widget);

	/* create list stores */
	list_store = gtk_list_store_new (GPK_WATCH_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* create repo tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_repo"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget), GTK_TREE_MODEL (list_store));

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "yalign", 0.0, NULL);
	g_object_set (renderer, "wrap-mode", PANGO_WRAP_WORD, NULL);
	g_object_set (renderer, "wrap-width", 400, NULL);

	/* TRANSLATORS: column for the message type */
	column = gtk_tree_view_column_new_with_attributes (_("Message"), renderer,
							   "markup", GPK_WATCH_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_WATCH_COLUMN_TEXT);
	gtk_tree_view_append_column (GTK_TREE_VIEW(widget), column);

	/* column for details */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "yalign", 0.0, NULL);
	g_object_set (renderer, "wrap-mode", PANGO_WRAP_WORD, NULL);
	g_object_set (renderer, "wrap-width", 400, NULL);

	/* TRANSLATORS: column for the message description */
	column = gtk_tree_view_column_new_with_attributes (_("Details"), renderer,
							   "markup", GPK_WATCH_COLUMN_DETAILS, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_WATCH_COLUMN_TEXT);
	gtk_tree_view_append_column (GTK_TREE_VIEW(widget), column);

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW(widget));

	/* add items to treeview */
	model = gtk_tree_view_get_model (GTK_TREE_VIEW(widget));
	for (i=0; i<watch->priv->cached_messages->len; i++) {
		cached_message = g_ptr_array_index (watch->priv->cached_messages, i);
		gtk_list_store_append (GTK_LIST_STORE(model), &iter);
		gtk_list_store_set (list_store, &iter,
				    GPK_WATCH_COLUMN_TEXT, gpk_message_enum_to_localised_text (cached_message->type),
				    GPK_WATCH_COLUMN_TID, cached_message->tid,
				    GPK_WATCH_COLUMN_DETAILS, cached_message->details,
				    -1);
	}

	/* show window */
	gtk_widget_show (main_window);

	/* focus back to the close button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	gtk_widget_grab_focus (widget);

	/* wait */
	gtk_main ();

	gtk_widget_hide (main_window);

	g_ptr_array_foreach (watch->priv->cached_messages, (GFunc) gpk_watch_cached_message_free, NULL);
	g_ptr_array_set_size (watch->priv->cached_messages, 0);

	g_object_unref (list_store);
out_build:
	g_object_unref (builder);

	/* refresh UI */
	gpk_watch_refresh_icon (watch);
	gpk_watch_refresh_tooltip (watch);
}

/**
 * gpk_watch_get_role_text:
 **/
static gchar *
gpk_watch_get_role_text (PkClient *client)
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
 * gpk_watch_progress_changed_cb:
 **/
static void
gpk_watch_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				guint elapsed, guint remaining, GpkWatch *watch)
{
	gpk_modal_dialog_set_percentage (watch->priv->dialog, percentage);
	gpk_modal_dialog_set_remaining (watch->priv->dialog, remaining);
}

/**
 * gpk_watch_set_status:
 **/
static gboolean
gpk_watch_set_status (GpkWatch *watch, PkStatusEnum status)
{
	/* do we force progress? */
	if (status == PK_STATUS_ENUM_DOWNLOAD_REPOSITORY ||
	    status == PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST ||
	    status == PK_STATUS_ENUM_DOWNLOAD_FILELIST ||
	    status == PK_STATUS_ENUM_DOWNLOAD_CHANGELOG ||
	    status == PK_STATUS_ENUM_DOWNLOAD_GROUP ||
	    status == PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO ||
	    status == PK_STATUS_ENUM_REFRESH_CACHE) {
		gpk_modal_dialog_setup (watch->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	}

	/* set icon */
	gpk_modal_dialog_set_image_status (watch->priv->dialog, status);

	/* set label */
	gpk_modal_dialog_set_title (watch->priv->dialog, gpk_status_enum_to_localised_text (status));

	/* spin */
	if (status == PK_STATUS_ENUM_WAIT)
		gpk_modal_dialog_set_percentage (watch->priv->dialog, PK_CLIENT_PERCENTAGE_INVALID);

	/* do visual stuff when finished */
	if (status == PK_STATUS_ENUM_FINISHED) {
		/* make insensitive */
		gpk_modal_dialog_set_allow_cancel (watch->priv->dialog, FALSE);

		/* stop spinning */
		gpk_modal_dialog_set_percentage (watch->priv->dialog, 100);
	}
	return TRUE;
}

/**
 * gpk_watch_status_changed_cb:
 **/
static void
gpk_watch_status_changed_cb (PkClient *client, PkStatusEnum status, GpkWatch *watch)
{
	gpk_watch_set_status (watch, status);
}

/**
 * gpk_watch_package_cb:
 **/
static void
gpk_watch_package_cb (PkClient *client, const PkPackageObj *obj, GpkWatch *watch)
{
	gchar *text;
	text = gpk_package_id_format_twoline (obj->id, obj->summary);
	gpk_modal_dialog_set_message (watch->priv->dialog, text);
	g_free (text);
}

/**
 * gpk_watch_monitor_tid:
 **/
static gboolean
gpk_watch_monitor_tid (GpkWatch *watch, const gchar *tid)
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

	/* reset client */
	ret = pk_client_reset (watch->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	ret = pk_client_set_tid (watch->priv->client_primary, tid, &error);
	if (!ret) {
		egg_warning ("could not set tid: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* fill in role */
	text = gpk_watch_get_role_text (watch->priv->client_primary);
	gpk_modal_dialog_set_title (watch->priv->dialog, text);
	g_free (text);

	/* coldplug */
	ret = pk_client_get_status (watch->priv->client_primary, &status, NULL);
	/* no such transaction? */
	if (!ret) {
		egg_warning ("could not get status");
		return FALSE;
	}

	/* are we cancellable? */
	pk_client_get_allow_cancel (watch->priv->client_primary, &allow_cancel, NULL);
	gpk_modal_dialog_set_allow_cancel (watch->priv->dialog, allow_cancel);

	/* coldplug */
	ret = pk_client_get_progress (watch->priv->client_primary,
				      &percentage, &subpercentage, &elapsed, &remaining, NULL);
	if (ret) {
		gpk_watch_progress_changed_cb (watch->priv->client_primary, percentage,
						subpercentage, elapsed, remaining, watch);
	} else {
		egg_warning ("GetProgress failed");
		gpk_watch_progress_changed_cb (watch->priv->client_primary,
						PK_CLIENT_PERCENTAGE_INVALID,
						PK_CLIENT_PERCENTAGE_INVALID, 0, 0, watch);
	}

	/* get the role */
	ret = pk_client_get_role (watch->priv->client_primary, &role, NULL, &error);
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
		gpk_modal_dialog_setup (watch->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	else
		gpk_modal_dialog_setup (watch->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, GPK_MODAL_DIALOG_PACKAGE_PADDING);

	/* set the status */
	gpk_watch_set_status (watch, status);

	/* do the best we can, and get the last package */
	ret = pk_client_get_package (watch->priv->client_primary, &package_id, NULL);
	if (ret) {
		PkPackageId *id;
		PkPackageObj *obj;

		id = pk_package_id_new_from_string (package_id);
		if (id != NULL) {
			obj = pk_package_obj_new (PK_INFO_ENUM_UNKNOWN, id, NULL);
			egg_warning ("package_id=%s", package_id);
			gpk_watch_package_cb (watch->priv->client_primary, obj, watch);
			pk_package_obj_free (obj);
		}
		pk_package_id_free (id);
	}

	gpk_modal_dialog_present (watch->priv->dialog);

	return TRUE;
}

/**
 * gpk_watch_menu_job_status_cb:
 **/
static void
gpk_watch_menu_job_status_cb (GtkMenuItem *item, GpkWatch *watch)
{
	gchar *tid;

	g_return_if_fail (GPK_IS_WATCH (watch));

	/* find the job we should bind to */
	tid = (gchar *) g_object_get_data (G_OBJECT (item), "tid");
	if (egg_strzero(tid) || tid[0] != '/') {
		egg_warning ("invalid job, maybe transaction already removed");
		return;
	}

	/* launch the UI */
	gpk_watch_monitor_tid (watch, tid);
}

/**
 * gpk_watch_populate_menu_with_jobs:
 **/
static guint
gpk_watch_populate_menu_with_jobs (GpkWatch *watch, GtkMenu *menu)
{
	guint i;
	PkTaskListItem *item;
	GtkWidget *widget;
	GtkWidget *image;
	const gchar *localised_status;
	const gchar *localised_role;
	const gchar *icon_name;
	gchar *text;
	guint length;

	g_return_val_if_fail (GPK_IS_WATCH (watch), 0);

	length = pk_task_list_get_size (watch->priv->tlist);
	if (length == 0)
		goto out;

	/* do a menu item for each job */
	for (i=0; i<length; i++) {
		item = pk_task_list_get_item (watch->priv->tlist, i);
		if (item == NULL) {
			egg_warning ("not found item %i", i);
			break;
		}
		localised_role = gpk_role_enum_to_localised_present (item->role);
		localised_status = gpk_status_enum_to_localised_text (item->status);

		icon_name = gpk_status_enum_to_icon_name (item->status);
		if (!egg_strzero (item->text) &&
		    item->role != PK_ROLE_ENUM_UPDATE_PACKAGES)
			text = g_strdup_printf ("%s %s (%s)", localised_role, item->text, localised_status);
		else
			text = g_strdup_printf ("%s (%s)", localised_role, localised_status);

		/* add a job */
		widget = gtk_image_menu_item_new_with_mnemonic (text);

		/* we need the job ID so we know what transaction to show */
		g_object_set_data (G_OBJECT (widget), "tid", (gpointer) item->tid);

		image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
		g_signal_connect (G_OBJECT (widget), "activate",
				  G_CALLBACK (gpk_watch_menu_job_status_cb), watch);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
		g_free (text);
	}
out:
	return length;
}

/**
 * gpk_watch_menu_hide_restart_cb:
 **/
static void
gpk_watch_menu_hide_restart_cb (GtkMenuItem *item, gpointer data)
{
	GpkWatch *watch = GPK_WATCH (data);
	g_return_if_fail (GPK_IS_WATCH (watch));

	/* hide */
	watch->priv->hide_warning = TRUE;
	gpk_watch_refresh_icon (watch);
}

/**
 * gpk_watch_menu_log_out_cb:
 **/
static void
gpk_watch_menu_log_out_cb (GtkMenuItem *item, gpointer data)
{
	GpkWatch *watch = GPK_WATCH (data);
	GpkSession *session;
	g_return_if_fail (GPK_IS_WATCH (watch));

	/* just ask for logout */
	session = gpk_session_new ();
	gpk_session_logout (session);
	g_object_unref (session);
}

/**
 * gpk_watch_menu_restart_cb:
 **/
static void
gpk_watch_menu_restart_cb (GtkMenuItem *item, gpointer data)
{
	gpk_restart_system ();
}

/**
 * gpk_watch_activate_status_cb:
 * @button: Which buttons are pressed
 *
 * Callback when the icon is clicked
 **/
static void
gpk_watch_activate_status_cb (GtkStatusIcon *status_icon, GpkWatch *watch)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *widget;
	GtkWidget *image;
	guint len;
	gboolean show_hide = FALSE;

	g_return_if_fail (GPK_IS_WATCH (watch));

	egg_debug ("icon left clicked");

	/* add jobs as drop down */
	len = gpk_watch_populate_menu_with_jobs (watch, menu);

	/* any messages to show? */
	len = watch->priv->cached_messages->len;
	if (len > 0) {
		/* TRANSLATORS: messages from the transaction */
		widget = gtk_image_menu_item_new_with_mnemonic (_("_Show messages"));
		image = gtk_image_new_from_icon_name ("edit-paste", GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
		g_signal_connect (G_OBJECT (widget), "activate",
				  G_CALLBACK (gpk_watch_menu_show_messages_cb), watch);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
		show_hide = TRUE;
	}

	/* log out session */
	if (watch->priv->restart == PK_RESTART_ENUM_SESSION ||
	    watch->priv->restart == PK_RESTART_ENUM_SECURITY_SESSION) {
		/* TRANSLATORS: log out of the session */
		widget = gtk_image_menu_item_new_with_mnemonic (_("_Log out"));
		image = gtk_image_new_from_icon_name ("system-log-out", GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
		g_signal_connect (G_OBJECT (widget), "activate",
				  G_CALLBACK (gpk_watch_menu_log_out_cb), watch);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
		show_hide = TRUE;
	}

	/* restart computer */
	if (watch->priv->restart == PK_RESTART_ENUM_SYSTEM ||
	    watch->priv->restart == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		/* TRANSLATORS: this menu item restarts the computer after an update */
		widget = gtk_image_menu_item_new_with_mnemonic (_("_Restart computer"));
		image = gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
		g_signal_connect (G_OBJECT (widget), "activate",
				  G_CALLBACK (gpk_watch_menu_restart_cb), watch);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
		show_hide = TRUE;
	}

	/* anything we're allowed to hide? */
	if (show_hide) {
		/* TRANSLATORS: This hides the 'restart required' icon */
		widget = gtk_image_menu_item_new_with_mnemonic (_("_Hide this icon"));
		image = gtk_image_new_from_icon_name ("dialog-information", GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
		g_signal_connect (G_OBJECT (widget), "activate",
				  G_CALLBACK (gpk_watch_menu_hide_restart_cb), watch);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
	}

	/* show the menu */
	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gtk_status_icon_position_menu, status_icon,
			1, gtk_get_current_event_time());
}

/**
 * gpk_watch_locked_cb:
 **/
static void
gpk_watch_locked_cb (PkClient *client, gboolean is_locked, GpkWatch *watch)
{
	g_return_if_fail (GPK_IS_WATCH (watch));

	egg_debug ("setting locked %i, doing g-p-m (un)inhibit", is_locked);
	if (is_locked)
		gpk_inhibit_create (watch->priv->inhibit);
	else
		gpk_inhibit_remove (watch->priv->inhibit);
}

/**
 * gpk_watch_get_proxy_ftp:
 * Return value: server.lan:8080
 **/
static gchar *
gpk_watch_get_proxy_ftp (GpkWatch *watch)
{
	gchar *mode = NULL;
	gchar *connection = NULL;
	gchar *host = NULL;
	gint port;

	g_return_val_if_fail (GPK_IS_WATCH (watch), NULL);

	/* common case, a direct connection */
	mode = gconf_client_get_string (watch->priv->gconf_client, "/system/proxy/mode", NULL);
	if (egg_strequal (mode, "none")) {
		egg_debug ("not using session proxy");
		goto out;
	}

	host = gconf_client_get_string (watch->priv->gconf_client, "/system/proxy/ftp_host", NULL);
	if (egg_strzero (host)) {
		egg_debug ("no hostname for ftp proxy");
		goto out;
	}
	port = gconf_client_get_int (watch->priv->gconf_client, "/system/proxy/ftp_port", NULL);

	/* ftp has no username or password */
	if (port == 0)
		connection = g_strdup (host);
	else
		connection = g_strdup_printf ("%s:%i", host, port);
out:
	g_free (mode);
	g_free (host);
	return connection;
}

/**
 * gpk_watch_get_proxy_ftp:
 * Return value: username:password@server.lan:8080
 **/
static gchar *
gpk_watch_get_proxy_http (GpkWatch *watch)
{
	gchar *mode = NULL;
	gchar *host = NULL;
	gchar *auth = NULL;
	gchar *connection = NULL;
	gchar *proxy_http = NULL;
	gint port;
	gboolean ret;

	g_return_val_if_fail (GPK_IS_WATCH (watch), NULL);

	/* common case, a direct connection */
	mode = gconf_client_get_string (watch->priv->gconf_client, "/system/proxy/mode", NULL);
	if (egg_strequal (mode, "none")) {
		egg_debug ("not using session proxy");
		goto out;
	}

	/* do we use this? */
	ret = gconf_client_get_bool (watch->priv->gconf_client, "/system/http_proxy/use_http_proxy", NULL);
	if (!ret) {
		egg_debug ("not using http proxy");
		goto out;
	}

	/* http has 4 parameters */
	host = gconf_client_get_string (watch->priv->gconf_client, "/system/http_proxy/host", NULL);
	if (egg_strzero (host)) {
		egg_debug ("no hostname for http proxy");
		goto out;
	}

	/* user and password are both optional */
	ret = gconf_client_get_bool (watch->priv->gconf_client, "/system/http_proxy/use_authentication", NULL);
	if (ret) {
		gchar *user = NULL;
		gchar *password = NULL;

		user = gconf_client_get_string (watch->priv->gconf_client, "/system/http_proxy/authentication_user", NULL);
		password = gconf_client_get_string (watch->priv->gconf_client, "/system/http_proxy/authentication_password", NULL);

		if (user != NULL && password != NULL)
			auth = g_strdup_printf ("%s:%s", user, password);
		else if (user != NULL)
			auth = g_strdup (user);
		else if (password != NULL)
			auth = g_strdup_printf (":%s", user);

		g_free (user);
		g_free (password);
	}

	/* port is optional too */
	port = gconf_client_get_int (watch->priv->gconf_client, "/system/http_proxy/port", NULL);
	if (port == 0)
		connection = g_strdup (host);
	else
		connection = g_strdup_printf ("%s:%i", host, port);

	/* the whole auth section is optional */
	if (egg_strzero (auth))
		proxy_http = g_strdup (connection);
	else
		proxy_http = g_strdup_printf ("%s@%s", auth, connection);
out:
	g_free (mode);
	g_free (connection);
	g_free (auth);
	g_free (host);
	return proxy_http;
}

/**
 * gpk_watch_set_proxies_ratelimit:
 **/
static gboolean
gpk_watch_set_proxies_ratelimit (GpkWatch *watch)
{
	gchar *proxy_http;
	gchar *proxy_ftp;
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (GPK_IS_WATCH (watch), FALSE);

	/* debug so we can catch polling */
	egg_debug ("polling check");

	proxy_http = gpk_watch_get_proxy_http (watch);
	proxy_ftp = gpk_watch_get_proxy_ftp (watch);

	egg_debug ("set proxy_http=%s, proxy_ftp=%s", proxy_http, proxy_ftp);
	ret = pk_control_set_proxy (watch->priv->control, proxy_http, proxy_ftp, &error);
	if (!ret) {
		egg_warning ("setting proxy failed: %s", error->message);
		g_error_free (error);
	}

	g_free (proxy_http);
	g_free (proxy_ftp);

	/* we can run again */
	watch->priv->set_proxy_timeout = 0;
	return FALSE;
}

/**
 * gpk_watch_set_proxies:
 **/
static gboolean
gpk_watch_set_proxies (GpkWatch *watch)
{
	if (watch->priv->set_proxy_timeout != 0) {
		egg_debug ("already scheduled");
		return FALSE;
	}
	watch->priv->set_proxy_timeout = g_timeout_add (GPK_WATCH_SET_PROXY_RATE_LIMIT,
							(GSourceFunc) gpk_watch_set_proxies_ratelimit, watch);
	return TRUE;
}

/**
 * gpk_watch_gconf_key_changed_cb:
 *
 * We might have to do things when the gconf keys change; do them here.
 **/
static void
gpk_watch_gconf_key_changed_cb (GConfClient *client, guint cnxn_id, GConfEntry *entry, GpkWatch *watch)
{
	egg_debug ("keys have changed");
	/* set the proxy */
	gpk_watch_set_proxies (watch);
}

/**
 * gpk_watch_allow_cancel_cb:
 **/
static void
gpk_watch_allow_cancel_cb (PkClient *client, gboolean allow_cancel, GpkWatch *watch)
{
	gpk_modal_dialog_set_allow_cancel (watch->priv->dialog, allow_cancel);
}

/**
 * gpk_watch_finished_cb:
 **/
static void
gpk_watch_finished_cb (PkClient *client, PkExitEnum exit_enum, guint runtime, GpkWatch *watch)
{
	g_return_if_fail (GPK_IS_WATCH (watch));

	/* stop spinning */
	gpk_modal_dialog_set_percentage (watch->priv->dialog, 100);

	/* autoclose if success */
	if (exit_enum == PK_EXIT_ENUM_SUCCESS) {
		gpk_modal_dialog_close (watch->priv->dialog);
	}
}

/**
 * gpk_watch_button_close_cb:
 **/
static void
gpk_watch_button_close_cb (GtkWidget *widget, GpkWatch *watch)
{
	/* close, don't abort */
	gpk_modal_dialog_close (watch->priv->dialog);
}

/**
 * gpk_watch_button_cancel_cb:
 **/
static void
gpk_watch_button_cancel_cb (GtkWidget *widget, GpkWatch *watch)
{
	gboolean ret;
	GError *error = NULL;

	/* we might have a transaction running */
	ret = pk_client_cancel (watch->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_watch_connection_changed_cb:
 **/
static void
gpk_watch_connection_changed_cb (PkConnection *pconnection, gboolean connected, GpkWatch *watch)
{
	g_return_if_fail (GPK_IS_WATCH (watch));
	egg_debug ("connected=%i", connected);
	if (connected) {
		gpk_watch_refresh_icon (watch);
		gpk_watch_refresh_tooltip (watch);
		gpk_watch_set_proxies (watch);
	} else {
		gtk_status_icon_set_visible (watch->priv->status_icon, FALSE);
	}
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
	watch->priv->notification_cached_messages = NULL;
	watch->priv->restart = PK_RESTART_ENUM_NONE;
	watch->priv->hide_warning = FALSE;

	watch->priv->gconf_client = gconf_client_get_default ();

	watch->priv->status_icon = gtk_status_icon_new ();
	watch->priv->set_proxy_timeout = 0;
	watch->priv->cached_messages = g_ptr_array_new ();
	watch->priv->restart_package_names = g_ptr_array_new ();

	watch->priv->client_primary = pk_client_new ();
	g_signal_connect (watch->priv->client_primary, "finished",
			  G_CALLBACK (gpk_watch_finished_cb), watch);
	g_signal_connect (watch->priv->client_primary, "progress-changed",
			  G_CALLBACK (gpk_watch_progress_changed_cb), watch);
	g_signal_connect (watch->priv->client_primary, "status-changed",
			  G_CALLBACK (gpk_watch_status_changed_cb), watch);
	g_signal_connect (watch->priv->client_primary, "package",
			  G_CALLBACK (gpk_watch_package_cb), watch);
	g_signal_connect (watch->priv->client_primary, "allow-cancel",
			  G_CALLBACK (gpk_watch_allow_cancel_cb), watch);

	watch->priv->dialog = gpk_modal_dialog_new ();
	gpk_modal_dialog_set_window_icon (watch->priv->dialog, "pk-package-installed");
	g_signal_connect (watch->priv->dialog, "cancel",
			  G_CALLBACK (gpk_watch_button_cancel_cb), watch);
	g_signal_connect (watch->priv->dialog, "close",
			  G_CALLBACK (gpk_watch_button_close_cb), watch);

	/* we need to get ::locked */
	watch->priv->control = pk_control_new ();
	g_signal_connect (watch->priv->control, "locked",
			  G_CALLBACK (gpk_watch_locked_cb), watch);

	/* do session inhibit */
	watch->priv->inhibit = gpk_inhibit_new ();

	/* right click actions are common */
	g_signal_connect_object (G_OBJECT (watch->priv->status_icon),
				 "popup_menu", G_CALLBACK (gpk_watch_popup_menu_cb), watch, 0);
	g_signal_connect_object (G_OBJECT (watch->priv->status_icon),
				 "activate", G_CALLBACK (gpk_watch_activate_status_cb), watch, 0);

	watch->priv->tlist = pk_task_list_new ();
	g_signal_connect (watch->priv->tlist, "changed",
			  G_CALLBACK (gpk_watch_task_list_changed_cb), watch);
	g_signal_connect (watch->priv->tlist, "status-changed",
			  G_CALLBACK (gpk_watch_task_list_changed_cb), watch);
	g_signal_connect (watch->priv->tlist, "finished",
			  G_CALLBACK (gpk_watch_task_list_finished_cb), watch);
	g_signal_connect (watch->priv->tlist, "error-code",
			  G_CALLBACK (gpk_watch_error_code_cb), watch);
	g_signal_connect (watch->priv->tlist, "message",
			  G_CALLBACK (gpk_watch_message_cb), watch);

	watch->priv->pconnection = pk_connection_new ();
	g_signal_connect (watch->priv->pconnection, "connection-changed",
			  G_CALLBACK (gpk_watch_connection_changed_cb), watch);
	if (pk_connection_valid (watch->priv->pconnection))
		gpk_watch_connection_changed_cb (watch->priv->pconnection, TRUE, watch);

	/* watch proxy keys */
	gconf_client_add_dir (watch->priv->gconf_client, GPK_WATCH_GCONF_PROXY_HTTP,
			      GCONF_CLIENT_PRELOAD_NONE, NULL);
	gconf_client_add_dir (watch->priv->gconf_client, GPK_WATCH_GCONF_PROXY_FTP,
			      GCONF_CLIENT_PRELOAD_NONE, NULL);
	gconf_client_notify_add (watch->priv->gconf_client, GPK_WATCH_GCONF_PROXY_HTTP,
				 (GConfClientNotifyFunc) gpk_watch_gconf_key_changed_cb, watch, NULL, NULL);
	gconf_client_notify_add (watch->priv->gconf_client, GPK_WATCH_GCONF_PROXY_FTP,
				 (GConfClientNotifyFunc) gpk_watch_gconf_key_changed_cb, watch, NULL, NULL);

	/* set the proxy */
	gpk_watch_set_proxies (watch);
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

	/* we might we waiting for a proxy update */
	if (watch->priv->set_proxy_timeout != 0)
		g_source_remove (watch->priv->set_proxy_timeout);

	/* free cached messages */
	g_ptr_array_foreach (watch->priv->cached_messages, (GFunc) gpk_watch_cached_message_free, NULL);
	g_ptr_array_free (watch->priv->cached_messages, TRUE);

	/* free cached restart names */
	g_ptr_array_foreach (watch->priv->restart_package_names, (GFunc) g_free, NULL);
	g_ptr_array_free (watch->priv->restart_package_names, TRUE);

	g_free (watch->priv->error_details);
	g_object_unref (watch->priv->status_icon);
	g_object_unref (watch->priv->inhibit);
	g_object_unref (watch->priv->tlist);
	g_object_unref (watch->priv->control);
	g_object_unref (watch->priv->pconnection);
	g_object_unref (watch->priv->gconf_client);
	g_object_unref (watch->priv->client_primary);
	g_object_unref (watch->priv->dialog);

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

