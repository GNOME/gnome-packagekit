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
#include <polkit-gnome/polkit-gnome.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-common.h"
#include "gpk-error.h"
#include "gpk-watch.h"
#include "gpk-client.h"
#include "gpk-inhibit.h"
#include "gpk-smart-icon.h"
#include "gpk-consolekit.h"
#include "gpk-enum.h"

static void     gpk_watch_class_init	(GpkWatchClass *klass);
static void     gpk_watch_init		(GpkWatch      *watch);
static void     gpk_watch_finalize	(GObject       *object);

#define GPK_WATCH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_WATCH, GpkWatchPrivate))
#define GPK_WATCH_MAXIMUM_TOOLTIP_LINES		10
#define GPK_WATCH_GCONF_PROXY_HTTP		"/system/http_proxy"
#define GPK_WATCH_GCONF_PROXY_FTP		"/system/proxy"
#define GPK_WATCH_SET_PROXY_RATE_LIMIT		200 /* ms */

struct GpkWatchPrivate
{
	PkControl		*control;
	GpkSmartIcon		*sicon;
	GpkSmartIcon		*sicon_restart;
	GpkInhibit		*inhibit;
	GpkClient		*gclient;
	PkConnection		*pconnection;
	PkTaskList		*tlist;
	GConfClient		*gconf_client;
	gboolean		 show_refresh_in_menu;
	PolKitGnomeAction	*restart_action;
	guint			 set_proxy_timeout;
	gchar			*error_details;
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
 * gpk_watch_refresh_tooltip:
 **/
static gboolean
gpk_watch_refresh_tooltip (GpkWatch *watch)
{
	guint i;
	PkTaskListItem *item;
	guint length;
	GString *status;
	const gchar *localised_status;

	g_return_val_if_fail (GPK_IS_WATCH (watch), FALSE);

	length = pk_task_list_get_size (watch->priv->tlist);
	egg_debug ("refresh tooltip %i", length);
	if (length == 0) {
		gtk_status_icon_set_tooltip (GTK_STATUS_ICON (watch->priv->sicon), "Doing nothing...");
		return TRUE;
	}
	status = g_string_new ("");
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
			g_string_append_printf (status, _("(%i more tasks)"),
						i - GPK_WATCH_MAXIMUM_TOOLTIP_LINES);
			g_string_append_c (status, '\n');
			break;
		}
	}
	if (status->len == 0)
		g_string_append (status, "Doing something...");
	else
		g_string_set_size (status, status->len-1);

	gtk_status_icon_set_tooltip (GTK_STATUS_ICON (watch->priv->sicon), status->str);
	g_string_free (status, TRUE);
	return TRUE;
}

/**
 * gpk_watch_task_list_to_status_bitfield:
 **/
static PkBitfield
gpk_watch_task_list_to_status_bitfield (GpkWatch *watch)
{
	guint i;
	guint length;
	PkBitfield status = 0;
	PkTaskListItem *item;

	g_return_val_if_fail (GPK_IS_WATCH (watch), PK_STATUS_ENUM_UNKNOWN);

	/* shortcut */
	length = pk_task_list_get_size (watch->priv->tlist);
	if (length == 0)
		goto out;

	/* add each status to a list */
	for (i=0; i<length; i++) {
		item = pk_task_list_get_item (watch->priv->tlist, i);
		if (item == NULL) {
			egg_warning ("not found item %i", i);
			break;
		}
		egg_debug ("%s %s", item->tid, pk_status_enum_to_text (item->status));
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
	const gchar *icon;
	PkBitfield status;
	gint value;

	g_return_val_if_fail (GPK_IS_WATCH (watch), FALSE);

	egg_debug ("rescan");
	status = gpk_watch_task_list_to_status_bitfield (watch);

	/* nothing in the list */
	if (status == 0) {
		egg_debug ("no activity");
		gpk_smart_icon_set_icon_name (watch->priv->sicon, NULL);
		return TRUE;
	}

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
					      PK_STATUS_ENUM_FINISHED, -1);

	/* only set if in the list and not unknown */
	if (value != PK_STATUS_ENUM_UNKNOWN && value != -1) {
		icon = gpk_status_enum_to_icon_name (value);
		gpk_smart_icon_set_icon_name (watch->priv->sicon, icon);
	}

	return TRUE;
}

/**
 * gpk_watch_task_list_changed_cb:
 **/
static void
gpk_watch_task_list_changed_cb (PkTaskList *tlist, GpkWatch *watch)
{
	g_return_if_fail (GPK_IS_WATCH (watch));

	if (pk_task_list_contains_role (tlist, PK_ROLE_ENUM_REFRESH_CACHE) ||
	    pk_task_list_contains_role (tlist, PK_ROLE_ENUM_UPDATE_PACKAGES) ||
	    pk_task_list_contains_role (tlist, PK_ROLE_ENUM_UPDATE_SYSTEM))
		watch->priv->show_refresh_in_menu = FALSE;
	else
		watch->priv->show_refresh_in_menu = TRUE;

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
 * gpk_watch_finished_cb:
 **/
static void
gpk_watch_finished_cb (PkTaskList *tlist, PkClient *client, PkExitEnum exit, guint runtime, GpkWatch *watch)
{
	gboolean ret;
	gboolean value;
	PkRoleEnum role;
	PkRestartEnum restart;
	GError *error = NULL;
	gchar *text = NULL;
	gchar *message = NULL;
	const gchar *restart_message;
	const gchar *icon_name;
	NotifyNotification *notification;

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
		restart = pk_client_get_require_restart (client);
		if (restart == PK_RESTART_ENUM_SYSTEM ||
		    restart == PK_RESTART_ENUM_SESSION) {
			restart_message = gpk_restart_enum_to_localised_text (restart);
			icon_name = gpk_restart_enum_to_icon_name (restart);
			gtk_status_icon_set_tooltip (GTK_STATUS_ICON (watch->priv->sicon_restart), restart_message);
			gpk_smart_icon_set_icon_name (watch->priv->sicon_restart, icon_name);
		}
	}

	/* is it worth showing a UI? */
	if (runtime < 3000) {
		egg_debug ("no notification, too quick");
		goto out;
	}

	/* is it worth showing a UI? */
	if (exit != PK_EXIT_ENUM_SUCCESS) {
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

	/* do the bubble */
	notification = notify_notification_new (title, message, "help-browser", NULL);
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
}

/**
 * gpk_watch_message_cb:
 **/
static void
gpk_watch_message_cb (PkTaskList *tlist, PkClient *client, PkMessageEnum message, const gchar *details, GpkWatch *watch)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *title;
	const gchar *filename;
	gchar *escaped_details;
	gboolean value;
	NotifyNotification *notification;

	g_return_if_fail (GPK_IS_WATCH (watch));

        /* are we accepting notifications */
        value = gconf_client_get_bool (watch->priv->gconf_client, GPK_CONF_NOTIFY_MESSAGE, NULL);
        if (!value) {
                egg_debug ("not showing notification as prevented in gconf");
                return;
        }

	title = gpk_message_enum_to_localised_text (message);
	filename = gpk_message_enum_to_icon_name (message);

	/* we need to format this */
	escaped_details = g_markup_escape_text (details, -1);

	/* do the bubble */
	notification = notify_notification_new (title, escaped_details, "help-browser", NULL);
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}

	g_free (escaped_details);
}

/**
 * gpk_watch_about_dialog_url_cb:
 **/
static void
gpk_watch_about_dialog_url_cb (GtkAboutDialog *about, const char *address, gpointer data)
{
	GError *error = NULL;
	gboolean ret;
	char *cmdline;
	GdkScreen *gscreen;
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
		/* TRANSLATORS: We couldn't launch the tool, normally a packaging problem */
		gpk_error_dialog (_("Internal error"), _("Failed to show url"), error->message);
		g_error_free (error);
	}

out:
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
	gtk_about_dialog_set_email_hook (gpk_watch_about_dialog_url_cb, "mailto:", NULL);

	gtk_window_set_default_icon_name (GPK_ICON_SOFTWARE_LOG);
	gtk_show_about_dialog (NULL,
			       "version", VERSION,
			       "copyright", "Copyright \xc2\xa9 2007-2008 Richard Hughes",
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
gpk_watch_popup_menu_cb (GtkStatusIcon *status_icon,
			guint          button,
			guint32        timestamp,
			GpkWatch       *watch)
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
 * gpk_watch_restart_cb:
 **/
static void
gpk_watch_restart_cb (PolKitGnomeAction *action, gpointer data)
{
	gpk_restart_system ();
}

/**
 * gpk_watch_refresh_cache_cb:
 **/
static void
gpk_watch_refresh_cache_cb (GtkMenuItem *item, gpointer data)
{
	gboolean ret;
	GpkWatch *watch = GPK_WATCH (data);
	GError *error = NULL;

	g_return_if_fail (GPK_IS_WATCH (watch));

	egg_debug ("refresh cache");
	gpk_client_set_interaction (watch->priv->gclient, GPK_CLIENT_INTERACT_ALWAYS);
	ret = gpk_client_refresh_cache (watch->priv->gclient, &error);
	if (!ret) {
		egg_warning ("%s", error->message);
		g_error_free (error);
	}
}

/**
 * pk_monitor_action_unref_cb:
 **/
static void
pk_monitor_action_unref_cb (GpkClient *gclient, GpkWatch *watch)
{
	g_return_if_fail (GPK_IS_WATCH (watch));
	g_object_unref (gclient);
}

/**
 * gpk_watch_menu_job_status_cb:
 **/
static void
gpk_watch_menu_job_status_cb (GtkMenuItem *item, GpkWatch *watch)
{
	gchar *tid;
	GpkClient *gclient = NULL;

	g_return_if_fail (GPK_IS_WATCH (watch));

	/* find the job we should bind to */
	tid = (gchar *) g_object_get_data (G_OBJECT (item), "tid");
	if (egg_strzero(tid) || tid[0] != '/') {
		egg_warning ("invalid job, maybe transaction already removed");
		return;
	}

	/* launch the UI */
	gclient = gpk_client_new ();
	gpk_client_set_interaction (gclient, GPK_CLIENT_INTERACT_WARNING_PROGRESS);
	g_signal_connect (gclient, "quit", G_CALLBACK (pk_monitor_action_unref_cb), watch);
	gpk_client_monitor_tid (gclient, tid);
}

/**
 * gpk_watch_populate_menu_with_jobs:
 **/
static void
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

	g_return_if_fail (GPK_IS_WATCH (watch));

	length = pk_task_list_get_size (watch->priv->tlist);
	if (length == 0)
		return;

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
}

/**
 * gpk_watch_activate_status_cb:
 * @button: Which buttons are pressed
 *
 * Callback when the icon is clicked
 **/
static void
gpk_watch_activate_status_cb (GtkStatusIcon *status_icon,
			     GpkWatch       *watch)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *widget;
	GtkWidget *image;

	g_return_if_fail (GPK_IS_WATCH (watch));

	egg_debug ("icon left clicked");

	/* add jobs as drop down */
	gpk_watch_populate_menu_with_jobs (watch, menu);

	/* force a refresh if we are not updating or refreshing */
	if (watch->priv->show_refresh_in_menu) {

		/* Separator for HIG? */
		widget = gtk_separator_menu_item_new ();
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);

		/* TRANSLATORS: This is a right click menu item, and will refresh all the package lists */
		widget = gtk_image_menu_item_new_with_mnemonic (_("_Refresh Software List"));
		image = gtk_image_new_from_icon_name ("view-refresh", GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
		g_signal_connect (G_OBJECT (widget), "activate",
				  G_CALLBACK (gpk_watch_refresh_cache_cb), watch);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
	}

	/* show the menu */
	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gtk_status_icon_position_menu, status_icon,
			1, gtk_get_current_event_time());
}

/**
 * gpk_watch_hide_restart_cb:
 **/
static void
gpk_watch_hide_restart_cb (GtkMenuItem *item, gpointer data)
{
	GpkWatch *watch = GPK_WATCH (data);

	g_return_if_fail (GPK_IS_WATCH (watch));

	/* just hide it */
	gpk_smart_icon_set_icon_name (watch->priv->sicon_restart, NULL);
}

/**
 * gpk_watch_activate_status_restart_cb:
 * @button: Which buttons are pressed
 *
 * Callback when the icon is clicked
 **/
static void
gpk_watch_activate_status_restart_cb (GtkStatusIcon *status_icon, GpkWatch *watch)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *widget;
	GtkWidget *image;

	g_return_if_fail (GPK_IS_WATCH (watch));

	egg_debug ("icon left clicked");

	/* restart computer */
	widget = gtk_action_create_menu_item (GTK_ACTION (watch->priv->restart_action));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);

	/* TRANSLATORS: This hides the 'restart required' icon */
	widget = gtk_image_menu_item_new_with_mnemonic (_("_Hide this icon"));
	image = gtk_image_new_from_icon_name ("dialog-information", GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
	g_signal_connect (G_OBJECT (widget), "activate",
			  G_CALLBACK (gpk_watch_hide_restart_cb), watch);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);

	/* show the menu */
	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gtk_status_icon_position_menu, status_icon,
			1, gtk_get_current_event_time());
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, GpkWatch *watch)
{
	g_return_if_fail (GPK_IS_WATCH (watch));
	egg_debug ("connected=%i", connected);
	if (connected) {
		gpk_watch_refresh_icon (watch);
		gpk_watch_refresh_tooltip (watch);
	} else {
		gpk_smart_icon_set_icon_name (watch->priv->sicon, NULL);
	}
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
gchar *
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
 * gpk_watch_init:
 * @watch: This class instance
 **/
static void
gpk_watch_init (GpkWatch *watch)
{
	GtkStatusIcon *status_icon;
	PolKitAction *pk_action;
	PolKitGnomeAction *restart_action;

	watch->priv = GPK_WATCH_GET_PRIVATE (watch);
	watch->priv->error_details = NULL;

	watch->priv->show_refresh_in_menu = TRUE;
	watch->priv->gconf_client = gconf_client_get_default ();

	watch->priv->sicon = gpk_smart_icon_new ();
	gpk_smart_icon_set_priority (watch->priv->sicon, 1);

	watch->priv->sicon_restart = gpk_smart_icon_new ();
	gpk_smart_icon_set_priority (watch->priv->sicon_restart, 3);

	watch->priv->set_proxy_timeout = 0;
	watch->priv->gclient = gpk_client_new ();

	/* we need to get ::locked */
	watch->priv->control = pk_control_new ();
	g_signal_connect (watch->priv->control, "locked",
			  G_CALLBACK (gpk_watch_locked_cb), watch);

	/* do session inhibit */
	watch->priv->inhibit = gpk_inhibit_new ();

	/* right click actions are common */
	status_icon = GTK_STATUS_ICON (watch->priv->sicon);
	g_signal_connect_object (G_OBJECT (status_icon),
				 "popup_menu", G_CALLBACK (gpk_watch_popup_menu_cb), watch, 0);
	g_signal_connect_object (G_OBJECT (status_icon),
				 "activate", G_CALLBACK (gpk_watch_activate_status_cb), watch, 0);

	/* provide the user with a way to restart */
	status_icon = GTK_STATUS_ICON (watch->priv->sicon_restart);
	g_signal_connect_object (G_OBJECT (status_icon),
				 "activate", G_CALLBACK (gpk_watch_activate_status_restart_cb), watch, 0);

	watch->priv->tlist = pk_task_list_new ();
	g_signal_connect (watch->priv->tlist, "changed",
			  G_CALLBACK (gpk_watch_task_list_changed_cb), watch);
	g_signal_connect (watch->priv->tlist, "status-changed",
			  G_CALLBACK (gpk_watch_task_list_changed_cb), watch);
	g_signal_connect (watch->priv->tlist, "finished",
			  G_CALLBACK (gpk_watch_finished_cb), watch);
	g_signal_connect (watch->priv->tlist, "error-code",
			  G_CALLBACK (gpk_watch_error_code_cb), watch);
	g_signal_connect (watch->priv->tlist, "message",
			  G_CALLBACK (gpk_watch_message_cb), watch);

	watch->priv->pconnection = pk_connection_new ();
	g_signal_connect (watch->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), watch);
	if (pk_connection_valid (watch->priv->pconnection))
		pk_connection_changed_cb (watch->priv->pconnection, TRUE, watch);

	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.consolekit.system.restart");

	restart_action = polkit_gnome_action_new_default ("restart-system", pk_action,
							  /* TRANSLATORS: This button restarts the computer after an update */
							  _("_Restart computer"), NULL);
	g_object_set (restart_action,
		      "no-icon-name", "gnome-shutdown",
		      "auth-icon-name", "gnome-shutdown",
		      "yes-icon-name","gnome-shutdown",
		      "self-blocked-icon-name", "gnome-shutdown",
		      NULL);
	polkit_action_unref (pk_action);
	g_signal_connect (restart_action, "activate",
			  G_CALLBACK (gpk_watch_restart_cb), NULL);
	watch->priv->restart_action = restart_action;

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

	g_free (watch->priv->error_details);
	g_object_unref (watch->priv->sicon);
	g_object_unref (watch->priv->inhibit);
	g_object_unref (watch->priv->tlist);
	g_object_unref (watch->priv->control);
	g_object_unref (watch->priv->gclient);
	g_object_unref (watch->priv->pconnection);
	g_object_unref (watch->priv->gconf_client);
	g_object_unref (watch->priv->restart_action);

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

