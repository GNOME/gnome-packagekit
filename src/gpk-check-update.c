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
#ifdef HAVE_NOTIFY
#include <libnotify/notify.h>
#endif
#include <packagekit-glib2/packagekit.h>
#include <canberra-gtk.h>
#include <gio/gio.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-dbus-monitor.h"

#include "gpk-auto-refresh.h"
#include "gpk-check-update.h"
#include "gpk-common.h"
#include "gpk-enum.h"
#include "gpk-error.h"
#include "gpk-gnome.h"
#include "gpk-task.h"

static void     gpk_check_update_finalize	(GObject	     *object);

#ifdef HAVE_NOTIFY
static void	gpk_check_update_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data);
#endif

#define GPK_CHECK_UPDATE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_CHECK_UPDATE, GpkCheckUpdatePrivate))

/* the maximum number of lines of data on the libnotify widget */
#define GPK_CHECK_UPDATE_MAX_NUMBER_SECURITY_ENTRIES	7

/* the amount of time after ::UpdatesChanged we refresh the update list */
#define GPK_CHECK_UPDATE_UPDATES_CHANGED_TIMEOUT	3 /* seconds */

/* the amoutn of time we check for updates after an UpdateSystem call failed */
#define	GPK_CHECK_UPDATE_FAILED_TASK_RECHECK_DELAY	60*60 /* seconds */

struct GpkCheckUpdatePrivate
{
	GtkStatusIcon		*status_icon;
	gboolean		 icon_inhibit_update_in_progress;
	gboolean		 icon_inhibit_network_offline;
	gboolean		 icon_inhibit_update_viewer_connected;
	GIcon			*gicon;
	PkTransactionList	*tlist;
	PkControl		*control;
	GpkAutoRefresh		*arefresh;
	PkTask			*task;
	GSettings		*settings;
	guint			 number_updates_critical_last_shown;
	NotifyNotification	*notification_updates_available;
	NotifyNotification	*notification_error;
	EggDbusMonitor		*dbus_monitor_viewer;
	guint			 updates_changed_id;
	GCancellable		*cancellable;
	PkError			*error_code;
	GVolumeMonitor		*volume_monitor;
};

G_DEFINE_TYPE (GpkCheckUpdate, gpk_check_update, G_TYPE_OBJECT)

/**
 * gpk_check_update_set_icon_visibility:
 **/
static gboolean
gpk_check_update_set_icon_visibility (GpkCheckUpdate *cupdate)
{
	gboolean ret = FALSE;

	/* check we have data */
	if (cupdate->priv->gicon == NULL) {
		egg_debug ("not showing icon as nothing to show");
		goto out;
	}

	/* check we have no inhibits */
	if (cupdate->priv->icon_inhibit_update_in_progress) {
		egg_debug ("not showing icon as update in progress");
		goto out;
	}
	if (cupdate->priv->icon_inhibit_network_offline) {
		egg_debug ("not showing icon as network offline");
		goto out;
	}
	if (cupdate->priv->icon_inhibit_update_viewer_connected) {
		egg_debug ("not showing icon as update viewer showing");
		goto out;
	}

	/* all okay, show icon */
	ret = TRUE;
out:
	/* show or hide icon */
	if (ret) {
		gtk_status_icon_set_from_gicon (cupdate->priv->status_icon, cupdate->priv->gicon);
		gtk_status_icon_set_visible (cupdate->priv->status_icon, TRUE);
	} else {
		gtk_status_icon_set_visible (cupdate->priv->status_icon, FALSE);
	}
	return ret;
}

/**
 * gpk_check_update_set_gicon:
 **/
static void
gpk_check_update_set_gicon (GpkCheckUpdate *cupdate, GIcon *gicon)
{
	if (cupdate->priv->gicon != NULL) {
		g_object_unref (cupdate->priv->gicon);
		cupdate->priv->gicon = NULL;
	}
	if (gicon != NULL)
		cupdate->priv->gicon = g_object_ref (gicon);
	gpk_check_update_set_icon_visibility (cupdate);
}

/**
 * gpk_check_update_class_init:
 * @klass: The GpkCheckUpdateClass
 **/
static void
gpk_check_update_class_init (GpkCheckUpdateClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gpk_check_update_finalize;

	g_type_class_add_private (klass, sizeof (GpkCheckUpdatePrivate));
}

/**
 * gpk_check_update_show_help_cb:
 **/
static void
gpk_check_update_show_help_cb (GtkMenuItem *item, GpkCheckUpdate *cupdate)
{
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));
	gpk_gnome_help ("update-icon");
}

/**
 * gpk_check_update_show_preferences_cb:
 **/
static void
gpk_check_update_show_preferences_cb (GtkMenuItem *item, GpkCheckUpdate *cupdate)
{
	const gchar *command = "gpk-prefs";
	if (!g_spawn_command_line_async (command, NULL))
		egg_warning ("Couldn't execute command: %s", command);
}

/**
 * gpk_check_update_show_about_cb:
 **/
static void
gpk_check_update_show_about_cb (GtkMenuItem *item, GpkCheckUpdate *icon)
{
	const char *authors[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *documenters[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *license[] = {
		N_("Licensed under the GNU General Public License Version 2"),
		N_("PackageKit is free software; you can redistribute it and/or "
		   "modify it under the terms of the GNU General Public License "
		   "as published by the Free Software Foundation; either version 2 "
		   "of the License, or (at your option) any later version."),
		N_("PackageKit is distributed in the hope that it will be useful, "
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of "
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
		   "GNU General Public License for more details."),
		N_("You should have received a copy of the GNU General Public License "
		   "along with this program; if not, write to the Free Software "
		   "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA "
		   "02110-1301, USA.")
	};
	const char *translators = _("translator-credits");
	char *license_trans;

	/* Translators comment: put your own name here to appear in the about dialog. */
	if (!strcmp (translators, "translator-credits"))
		translators = NULL;

	license_trans = g_strconcat (_(license[0]), "\n\n", _(license[1]), "\n\n",
				     _(license[2]), "\n\n", _(license[3]), "\n",  NULL);

	gtk_window_set_default_icon_name (GPK_ICON_SOFTWARE_UPDATE);
	gtk_show_about_dialog (NULL,
			       "version", VERSION,
			       "copyright", "Copyright \xc2\xa9 2007-2009 Richard Hughes",
			       "license", license_trans,
			       "wrap-license", TRUE,
				/* TRANSLATORS: website label */
			       "website-label", _("PackageKit Website"),
			       "website", "http://www.packagekit.org",
			       "comments", "PackageKit",
			       "authors", authors,
			       "documenters", documenters,
			       "translator-credits", translators,
			       "logo-icon-name", GPK_ICON_SOFTWARE_UPDATE,
			       NULL);
	g_free (license_trans);
}

/**
 * gpk_check_update_popup_menu_cb:
 *
 * Display the popup menu.
 **/
static void
gpk_check_update_popup_menu_cb (GtkStatusIcon *status_icon, guint button, guint32 timestamp, GpkCheckUpdate *icon)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;
	GtkWidget *image;

	egg_debug ("icon right clicked");

	/* TRANSLATORS: context menu to open the preferences */
	item = gtk_image_menu_item_new_with_mnemonic (_("_Preferences"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_PREFERENCES, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (gpk_check_update_show_preferences_cb), icon);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* Separator for HIG? */
	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* TRANSLATORS: context menu to open the offline help file */
	item = gtk_image_menu_item_new_with_mnemonic (_("_Help"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_HELP, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (gpk_check_update_show_help_cb), icon);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* TRANSLATORS: context menu to show the about screen */
	item = gtk_image_menu_item_new_with_mnemonic (_("_About"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (gpk_check_update_show_about_cb), icon);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* show the menu */
	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gtk_status_icon_position_menu, status_icon,
			button, timestamp);
	if (button == 0)
		gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
}

static void gpk_check_update_query_updates (GpkCheckUpdate *cupdate);

/**
 * gpk_check_update_get_updates_post_update_cb:
 **/
static gboolean
gpk_check_update_get_updates_post_update_cb (GpkCheckUpdate *cupdate)
{
	g_return_val_if_fail (GPK_IS_CHECK_UPDATE (cupdate), FALSE);

	/* debug so we can catch polling */
	egg_debug ("post updates check");

	gpk_check_update_query_updates (cupdate);
	return FALSE;
}


/**
 * gpk_check_update_finished_notify:
 **/
static void
gpk_check_update_finished_notify (GpkCheckUpdate *cupdate, PkResults *results)
{
	gboolean ret;
	GError *error = NULL;
	NotifyNotification *notification;
	PkRestartEnum restart;
	guint i;
	GPtrArray *array;
	PkPackage *item;
	GString *message_text = NULL;
	guint skipped_number = 0;
	const gchar *message;
	gchar **split;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *summary = NULL;


	/* check we got some packages */
	array = pk_results_get_package_array (results);
	egg_debug ("length=%i", array->len);
	if (array->len == 0) {
		egg_debug ("no updates");
		goto out;
	}

	message_text = g_string_new ("");

	/* find any we skipped */
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);

		split = pk_package_id_split (package_id);
		egg_debug ("%s, %s, %s", pk_info_enum_to_text (info),
			   split[PK_PACKAGE_ID_NAME], summary);
		if (info == PK_INFO_ENUM_BLOCKED) {
			skipped_number++;
			g_string_append_printf (message_text, "<b>%s</b> - %s\n",
						split[PK_PACKAGE_ID_NAME], summary);
		}
		g_free (package_id);
		g_free (summary);
		g_strfreev (split);
	}

	/* notify the user if there were skipped entries */
	if (skipped_number > 0) {
		/* TRANSLATORS: we did the update, but some updates were skipped and not applied */
		message = ngettext ("One package was skipped:",
				    "Some packages were skipped:", skipped_number);
		g_string_prepend (message_text, message);
		g_string_append_c (message_text, '\n');
	}

	/* add a message that we need to restart */
	restart = pk_results_get_require_restart_worst (results);
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
	ret = g_settings_get_boolean (cupdate->priv->settings, GPK_SETTINGS_NOTIFY_UPDATE_COMPLETE);
	if (!ret) {
		egg_debug ("ignoring due to GSettings");
		goto out;
	}

	/* TRANSLATORS: title: system update completed all okay */
	notification = notify_notification_new_with_status_icon (_("The system update has completed"),
								 message_text->str, "help-browser",
								 cupdate->priv->status_icon);
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	if (restart == PK_RESTART_ENUM_SYSTEM) {
		notify_notification_add_action (notification, "restart",
						/* TRANSLATORS: restart computer as system packages need update */
						_("Restart computer now"), gpk_check_update_libnotify_cb, cupdate, NULL);
//		notify_notification_add_action (notification, "do-not-show-complete-restart",
//						/* TRANSLATORS: don't show this option again (for restart) */
//						_("Do not show this again"), gpk_check_update_libnotify_cb, cupdate, NULL);
	} else {
		notify_notification_add_action (notification, "do-not-show-complete",
						/* TRANSLATORS: don't show this option again (when finished)  */
						_("Do not show this again"), gpk_check_update_libnotify_cb, cupdate, NULL);
	}
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
out:
	if (message_text != NULL)
		g_string_free (message_text, TRUE);
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * gpk_check_update_show_error:
 **/
static void
gpk_check_update_show_error (GpkCheckUpdate *cupdate, PkError *error_code)
{
	gboolean ret;
	GError *error = NULL;
	PkErrorEnum error_enum;
	const gchar *title;
	const gchar *message;
	NotifyNotification *notification = NULL;

	/* ignore some errors */
	error_enum = pk_error_get_code (error_code);
	if (error_enum == PK_ERROR_ENUM_PROCESS_KILL)
		goto out;
	if (error_enum == PK_ERROR_ENUM_TRANSACTION_CANCELLED)
		goto out;
	if (error_enum == PK_ERROR_ENUM_CANNOT_GET_LOCK)
		goto out;

	/* get localised versions */
	title = gpk_error_enum_to_localised_text (error_enum);
	message = gpk_error_enum_to_localised_message (error_enum);

	/* close any existing notification */
	if (cupdate->priv->notification_error != NULL) {
		notify_notification_close (cupdate->priv->notification_error, NULL);
		cupdate->priv->notification_error = NULL;
	}

	/* save in case we do more details */
	if (cupdate->priv->error_code != NULL)
		g_object_unref (cupdate->priv->error_code);
	cupdate->priv->error_code = g_object_ref (error_code);

	/* do the bubble */
	egg_debug ("title=%s, message=%s", title, message);
	notification = notify_notification_new_with_status_icon (title, message, "help-browser", cupdate->priv->status_icon);
	if (notification == NULL) {
		egg_warning ("failed to get bubble");
		goto out;
	}
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_CRITICAL);
	notify_notification_add_action (notification, "show-error-details",
					/* TRANSLATORS: button: show more details about the error */
					_("Show details"), gpk_check_update_libnotify_cb, cupdate, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
	/* track so we can prevent doubled notifications */
	cupdate->priv->notification_error = g_object_ref (notification);
out:
	if (notification != NULL)
		g_object_unref (notification);
	return;
}

/**
 * gpk_check_update_update_system_finished_cb:
 **/
static void
gpk_check_update_update_system_finished_cb (PkTask *task, GAsyncResult *res, GpkCheckUpdate *cupdate)
{
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;
	guint timer_id;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	if (results == NULL) {
		egg_warning ("failed to update system: %s", error->message);
		g_error_free (error);

		/* we failed, so re-get the update list */
		gpk_check_update_set_gicon (cupdate, NULL);
		timer_id = g_timeout_add_seconds (GPK_CHECK_UPDATE_FAILED_TASK_RECHECK_DELAY,
						  (GSourceFunc) gpk_check_update_get_updates_post_update_cb, cupdate);
#if GLIB_CHECK_VERSION(2,25,8)
		g_source_set_name_by_id (timer_id, "[GpkCheckUpdate] failed");
#endif
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to update system: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		gpk_check_update_show_error (cupdate, error_code);
		goto out;
	}

	/* play the sound, using sounds from the naming spec */
	ca_context_play (ca_gtk_context_get (), 0,
			 /* TODO: add a new sound to the spec */
			 CA_PROP_EVENT_ID, "complete-download",
			 /* TRANSLATORS: this is the application name for libcanberra */
			 CA_PROP_APPLICATION_NAME, _("GNOME PackageKit Update Icon"),
			 /* TRANSLATORS: this is the sound description */
			 CA_PROP_EVENT_DESCRIPTION, _("Updated successfully"), NULL);

	/* notify */
	gpk_check_update_finished_notify (cupdate, results);
	cupdate->priv->number_updates_critical_last_shown = 0;
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_check_update_activate_update_cb:
 *
 * Callback when the icon is left clicked
 **/
static void
gpk_check_update_activate_update_cb (GtkStatusIcon *status_icon, GpkCheckUpdate *icon)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *command = "gpk-update-viewer";

	ret = g_spawn_command_line_async (command, &error);
	if (!ret) {
		egg_warning ("Couldn't execute %s: %s", command, error->message);
		g_error_free (error);
	}
}

#ifdef HAVE_NOTIFY
/**
 * gpk_check_update_libnotify_cb:
 **/
static void
gpk_check_update_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *title;
	const gchar *message;
	const gchar *details;

	GpkCheckUpdate *cupdate = GPK_CHECK_UPDATE (data);

	if (g_strcmp0 (action, "do-not-show-complete-restart") == 0) {
		egg_debug ("set %s to FALSE", GPK_SETTINGS_NOTIFY_UPDATE_COMPLETE_RESTART);
		g_settings_set_boolean (cupdate->priv->settings, GPK_SETTINGS_NOTIFY_UPDATE_COMPLETE_RESTART, FALSE);
	} else if (g_strcmp0 (action, "do-not-show-complete") == 0) {
		egg_debug ("set %s to FALSE", GPK_SETTINGS_NOTIFY_UPDATE_COMPLETE);
		g_settings_set_boolean (cupdate->priv->settings, GPK_SETTINGS_NOTIFY_UPDATE_COMPLETE, FALSE);
	} else if (g_strcmp0 (action, "do-not-show-update-started") == 0) {
		egg_debug ("set %s to FALSE", GPK_SETTINGS_NOTIFY_UPDATE_STARTED);
		g_settings_set_boolean (cupdate->priv->settings, GPK_SETTINGS_NOTIFY_UPDATE_STARTED, FALSE);
	} else if (g_strcmp0 (action, "do-not-show-notify-critical") == 0) {
		egg_debug ("set %s to FALSE", GPK_SETTINGS_NOTIFY_CRITICAL);
		g_settings_set_boolean (cupdate->priv->settings, GPK_SETTINGS_NOTIFY_CRITICAL, FALSE);
	} else if (g_strcmp0 (action, "do-not-show-update-not-battery") == 0) {
		egg_debug ("set %s to FALSE", GPK_SETTINGS_NOTIFY_UPDATE_NOT_BATTERY);
		g_settings_set_boolean (cupdate->priv->settings, GPK_SETTINGS_NOTIFY_UPDATE_NOT_BATTERY, FALSE);
	} else if (g_strcmp0 (action, "distro-upgrade-do-not-show-available") == 0) {
		egg_debug ("set %s to FALSE", GPK_SETTINGS_NOTIFY_DISTRO_UPGRADES);
		g_settings_set_boolean (cupdate->priv->settings, GPK_SETTINGS_NOTIFY_DISTRO_UPGRADES, FALSE);
	} else if (g_strcmp0 (action, "show-error-details") == 0) {
		title = gpk_error_enum_to_localised_text (pk_error_get_code (cupdate->priv->error_code));
		message = gpk_error_enum_to_localised_message (pk_error_get_code (cupdate->priv->error_code));
		details = pk_error_get_details (cupdate->priv->error_code);
		gpk_error_dialog (title, message, details);
	} else if (g_strcmp0 (action, "cancel") == 0) {
		/* try to cancel */
		g_cancellable_cancel (cupdate->priv->cancellable);
	} else if (g_strcmp0 (action, "update-all-packages") == 0) {
		pk_task_update_system_async (cupdate->priv->task, cupdate->priv->cancellable, NULL, NULL,
					     (GAsyncReadyCallback) gpk_check_update_update_system_finished_cb, cupdate);
	} else if (g_strcmp0 (action, "show-update-viewer") == 0) {
		ret = g_spawn_command_line_async (BINDIR "/gpk-update-viewer", &error);
		if (!ret) {
			egg_warning ("Failure launching update viewer: %s", error->message);
			g_error_free (error);
		}
	} else if (g_strcmp0 (action, "distro-upgrade-info") == 0) {
		ret = g_spawn_command_line_async (DATADIR "/PackageKit/pk-upgrade-distro.sh", &error);
		if (!ret) {
			egg_warning ("Failure launching pk-upgrade-distro.sh: %s", error->message);
			g_error_free (error);
		}
	} else {
		egg_warning ("unknown action id: %s", action);
	}
	return;
}
#endif

/**
 * gpk_check_update_critical_updates_warning:
 **/
static void
gpk_check_update_critical_updates_warning (GpkCheckUpdate *cupdate, GPtrArray *array)
{
	const gchar *title;
	const gchar *message;
	gboolean ret;
	GError *error = NULL;
	NotifyNotification *notification;

	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* do we do the notification? */
	ret = g_settings_get_boolean (cupdate->priv->settings, GPK_SETTINGS_NOTIFY_CRITICAL);
	if (!ret) {
		egg_debug ("ignoring due to GSettings");
		return;
	}

	/* if the number of critical updates is the same as the last notification,
	 * then skip the notifcation as we don't want to bombard the user every hour */
	if (array->len == cupdate->priv->number_updates_critical_last_shown) {
		egg_debug ("ignoring as user ignored last warning");
		return;
	}

	/* save for comparison later */
	cupdate->priv->number_updates_critical_last_shown = array->len;

	/* TRANSLATORS: title in the libnotify popup */
	title = ngettext ("Security update available", "Security updates available", array->len);

	/* TRANSLATORS: message when there are security updates */
	message = ngettext ("An important update is available for your computer:",
			    "Important updates are available for your computer:", array->len);

	/* close any existing notification */
	if (cupdate->priv->notification_updates_available != NULL) {
		notify_notification_close (cupdate->priv->notification_updates_available, NULL);
		cupdate->priv->notification_updates_available = NULL;
	}

	/* do the bubble */
	egg_debug ("title=%s, message=%s", title, message);
	notification = notify_notification_new_with_status_icon (title, message, "help-browser", cupdate->priv->status_icon);
	if (notification == NULL) {
		egg_warning ("failed to get bubble");
		return;
	}
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_CRITICAL);
	notify_notification_add_action (notification, "show-update-viewer",
					/* TRANSLATORS: button: open the update viewer to install updates*/
					_("Install updates"), gpk_check_update_libnotify_cb, cupdate, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
	/* track so we can prevent doubled notifications */
	cupdate->priv->notification_updates_available = notification;
}

/**
 * gpk_check_update_get_status_icon:
 **/
static GIcon *
gpk_check_update_get_status_icon (GpkCheckUpdate *cupdate, GPtrArray *array)
{
	guint i;
	PkPackage *package;
	PkInfoEnum info;
	GIcon *icon = NULL;

	/* look for any urgent updates */
	for (i = 0; i< array->len; i++) {
		package = g_ptr_array_index (array, i);
		info = pk_package_get_info (package);
		if (info == PK_INFO_ENUM_SECURITY ||
		    info == PK_INFO_ENUM_IMPORTANT) {
			icon = g_themed_icon_new_with_default_fallbacks ("software-update-urgent-symbolic");
			goto out;
		}
	}

	/* look for any normal updates */
	for (i = 0; i< array->len; i++) {
		package = g_ptr_array_index (array, i);
		info = pk_package_get_info (package);
		if (info == PK_INFO_ENUM_BUGFIX ||
		    info == PK_INFO_ENUM_NORMAL ||
		    info == PK_INFO_ENUM_ENHANCEMENT ||
		    info == PK_INFO_ENUM_LOW) {
			icon = g_themed_icon_new_with_default_fallbacks ("software-update-available-symbolic");
			goto out;
		}
	}
out:
	return icon;
}

/**
 * gpk_check_update_check_on_battery:
 **/
static gboolean
gpk_check_update_check_on_battery (GpkCheckUpdate *cupdate)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *message;
	NotifyNotification *notification;

	g_return_val_if_fail (GPK_IS_CHECK_UPDATE (cupdate), FALSE);

	ret = g_settings_get_boolean (cupdate->priv->settings, GPK_SETTINGS_UPDATE_BATTERY);
	if (ret) {
		egg_debug ("okay to update due to policy");
		return TRUE;
	}

	ret = gpk_auto_refresh_get_on_battery (cupdate->priv->arefresh);
	if (!ret) {
		egg_debug ("okay to update as on AC");
		return TRUE;
	}

	/* do we do the notification? */
	ret = g_settings_get_boolean (cupdate->priv->settings, GPK_SETTINGS_NOTIFY_UPDATE_NOT_BATTERY);
	if (!ret) {
		egg_debug ("ignoring due to GSettings");
		return FALSE;
	}

	/* TRANSLATORS: policy says update, but we are on battery and so prompt */
	message = _("Automatic updates are not being installed as the computer is running on battery power");
	/* TRANSLATORS: informs user will not install by default */
	notification = notify_notification_new_with_status_icon (_("Updates not installed"),
								 message, "help-browser",
								 cupdate->priv->status_icon);
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
//	notify_notification_add_action (notification, "do-not-show-update-not-battery",
//					/* TRANSLATORS: hide this warning type forever */
//					_("Do not show this warning again"), gpk_check_update_libnotify_cb, cupdate, NULL);
	notify_notification_add_action (notification, "update-all-packages",
					/* TRANSLATORS: to hell with my battery life, just do it */
					_("Install the updates anyway"), gpk_check_update_libnotify_cb, cupdate, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}

	return FALSE;
}

/**
 * gpk_check_update_notify_doing_updates:
 **/
static void
gpk_check_update_notify_doing_updates (GpkCheckUpdate *cupdate)
{
	gboolean ret;
	GError *error = NULL;
	NotifyNotification *notification;

	/* in GSettings? */
	ret = g_settings_get_boolean (cupdate->priv->settings, GPK_SETTINGS_NOTIFY_CRITICAL);
	if (!ret)
		goto out;

	/* TRANSLATORS: title: notification when we scheduled an automatic update */
	notification = notify_notification_new_with_status_icon (_("Updates are being installed"),
						/* TRANSLATORS: tell the user why the hard disk is grinding... */
						_("Updates are being automatically installed on your computer"),
						"software-update-urgent", cupdate->priv->status_icon);
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	/* TRANSLATORS: button: cancel the update system */
	notify_notification_add_action (notification, "cancel",
					_("Cancel update"), gpk_check_update_libnotify_cb, cupdate, NULL);
	/* TRANSLATORS: button: don't show this again */
//	notify_notification_add_action (notification, "do-not-show-update-started",
//					_("Do not show this again"), gpk_check_update_libnotify_cb, cupdate, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
out:
	return;
}

/**
 * gpk_check_update_get_updates_finished_cb:
 **/
static void
gpk_check_update_get_updates_finished_cb (GObject *object, GAsyncResult *res, GpkCheckUpdate *cupdate)
{
	PkClient *client = PK_CLIENT(object);
	PkResults *results;
	GError *error = NULL;
	PkPackage *item;
	guint i;
	gboolean ret;
	GString *status_tooltip = NULL;
	GpkUpdateEnum update;
	GPtrArray *security_array = NULL;
	GIcon *icon = NULL;
	gchar **package_ids;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(client), res, &error);
	if (results == NULL) {
		egg_warning ("failed to get updates: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to get updates: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		gpk_check_update_show_error (cupdate, error_code);
		goto out;
	}

	/* sort by name */
//	pk_package_list_sort (array);

	/* get data */
	array = pk_results_get_package_array (results);

	/* we have no updates */
	if (array->len == 0) {
		egg_debug ("no updates");
		gpk_check_update_set_gicon (cupdate, NULL);
		goto out;
	}

	/* we have updates to process */
	status_tooltip = g_string_new ("");
	security_array = g_ptr_array_new_with_free_func (g_free);

	/* find the security updates first */
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (pk_package_get_info (item) != PK_INFO_ENUM_SECURITY)
			continue;
		g_ptr_array_add (security_array, g_strdup (pk_package_get_id (item)));
	}

	/* work out icon */
	icon = gpk_check_update_get_status_icon (cupdate, array);
	if (icon == NULL) {
		egg_debug ("all updates blocked");
		gpk_check_update_set_gicon (cupdate, NULL);
		goto out;
	}
	gpk_check_update_set_gicon (cupdate, icon);

	/* TRANSLATORS: tooltip: how many updates are waiting to be applied */
	g_string_append_printf (status_tooltip, ngettext ("There is %d update available",
							  "There are %d updates available", array->len), array->len);
	gtk_status_icon_set_tooltip_text (cupdate->priv->status_icon, status_tooltip->str);

	/* if we are just refreshing after a failed update, don't try to do the actions */
	if (FALSE) { //TODO
		egg_debug ("skipping actions");
		goto out;
	}

	/* do we do the automatic updates? */
	update = g_settings_get_enum (cupdate->priv->settings, GPK_SETTINGS_AUTO_UPDATE);
	if (update == GPK_UPDATE_ENUM_UNKNOWN) {
		egg_warning ("policy unknown");
		goto out;
	}

	/* is policy none? */
	if (update == GPK_UPDATE_ENUM_NONE) {
		egg_debug ("not updating as policy NONE");

		/* TODO: use ca_gtk_context_get_for_screen to allow use of GDK_MULTIHEAD_SAFE */

		/* play the sound, using sounds from the naming spec */
		ca_context_play (ca_gtk_context_get (), 0,
				 /* TODO: add a new sound to the spec */
				 CA_PROP_EVENT_ID, "software-update-available",
				 /* TRANSLATORS: this is the application name for libcanberra */
				 CA_PROP_APPLICATION_NAME, _("GNOME PackageKit Update Icon"),
				 /* TRANSLATORS: this is the sound description */
				 CA_PROP_EVENT_DESCRIPTION, _("Update available"), NULL);

		/* do we warn the user? */
		if (security_array->len > 0)
			gpk_check_update_critical_updates_warning (cupdate, security_array);
		goto out;
	}

	/* are we on battery and configured to skip the action */
	ret = gpk_check_update_check_on_battery (cupdate);
	if (!ret &&
	    ((update == GPK_UPDATE_ENUM_SECURITY && security_array->len > 0) ||
	      update == GPK_UPDATE_ENUM_ALL)) {
		egg_debug ("on battery so not doing update");

		/* play the sound, using sounds from the naming spec */
		ca_context_play (ca_gtk_context_get (), 0,
				 /* TODO: add a new sound to the spec */
				 CA_PROP_EVENT_ID, "software-update-available",
				 /* TRANSLATORS: this is the application name for libcanberra */
				 CA_PROP_APPLICATION_NAME, _("GNOME PackageKit Update Icon"),
				 /* TRANSLATORS: this is the sound description */
				 CA_PROP_EVENT_DESCRIPTION, _("Update available (on battery)"), NULL);

		/* do we warn the user? */
		if (security_array->len > 0)
			gpk_check_update_critical_updates_warning (cupdate, security_array);
		goto out;
	}

	/* just do security updates */
	if (update == GPK_UPDATE_ENUM_SECURITY) {
		if (security_array->len == 0) {
			egg_debug ("policy security, but none available");
			goto out;
		}

		/* convert */
		package_ids = pk_ptr_array_to_strv (security_array);
		pk_task_update_packages_async (cupdate->priv->task, package_ids, cupdate->priv->cancellable, NULL, NULL,
					       (GAsyncReadyCallback) gpk_check_update_update_system_finished_cb, cupdate);
		gpk_check_update_notify_doing_updates (cupdate);
		g_strfreev (package_ids);
		goto out;
	}

	/* just do everything */
	if (update == GPK_UPDATE_ENUM_ALL) {
		egg_debug ("we should do the update automatically!");
		pk_task_update_system_async (cupdate->priv->task, cupdate->priv->cancellable, NULL, NULL,
					     (GAsyncReadyCallback) gpk_check_update_update_system_finished_cb, cupdate);
		gpk_check_update_notify_doing_updates (cupdate);
		goto out;
	}

	/* shouldn't happen */
	egg_warning ("unknown update mode");
out:
	if (icon != NULL)
		g_object_unref (icon);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (status_tooltip != NULL)
		g_string_free (status_tooltip, TRUE);
	if (security_array != NULL)
		g_ptr_array_unref (security_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_check_update_get_active_roles_for_tids:
 **/
static PkBitfield
gpk_check_update_get_active_roles_for_tids (GpkCheckUpdate *cupdate, gchar **tids)
{
	PkRoleEnum role;
	PkBitfield roles = 0;
	guint i;
	PkProgress *progress;
	GError *error = NULL;

	for (i=0; tids[i] != NULL; i++) {
		/* get progress */
		progress = pk_client_get_progress (PK_CLIENT(cupdate->priv->task), tids[i], cupdate->priv->cancellable, &error);
		if (progress == NULL) {
			egg_warning ("failed to get progress of %s: %s", tids[i], error->message);
			g_error_free (error);
			goto out;
		}
		/* get data */
		g_object_get (progress,
			      "role", &role,
			      NULL);
		pk_bitfield_add (roles, role);
		g_object_unref (progress);
	}
out:
	return roles;
}

/**
 * gpk_check_update_get_active_roles:
 **/
static PkBitfield
gpk_check_update_get_active_roles (GpkCheckUpdate *cupdate)
{
	PkBitfield roles;
	gchar **tids;

	/* get active transaction id's */
	tids = pk_transaction_list_get_ids (cupdate->priv->tlist);
	roles = gpk_check_update_get_active_roles_for_tids (cupdate, tids);
	g_strfreev (tids);
	return roles;
}

/**
 * gpk_check_update_query_updates:
 **/
static void
gpk_check_update_query_updates (GpkCheckUpdate *cupdate)
{
	PkBitfield roles;
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* No point if we are already updating */
	roles = gpk_check_update_get_active_roles (cupdate);
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_UPDATES) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_PACKAGES) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_SYSTEM)) {
		egg_debug ("Not checking for updates as already in progress");
		goto out;
	}

	/* get new update list */
	pk_client_get_updates_async (PK_CLIENT(cupdate->priv->task),
				     pk_bitfield_value (PK_FILTER_ENUM_NONE),
				     cupdate->priv->cancellable, NULL, NULL,
				     (GAsyncReadyCallback) gpk_check_update_get_updates_finished_cb, cupdate);
out:
	return;
}

/**
 * gpk_check_update_query_updates_changed_cb:
 **/
static gboolean
gpk_check_update_query_updates_changed_cb (GpkCheckUpdate *cupdate)
{
	egg_debug ("getting new update list (after we waited a short delay)");
	cupdate->priv->updates_changed_id = 0;
	gpk_check_update_query_updates (cupdate);
	return FALSE;
}

/**
 * gpk_check_update_updates_changed_cb:
 **/
static void
gpk_check_update_updates_changed_cb (PkControl *control, GpkCheckUpdate *cupdate)
{
	guint thresh;

	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* if we don't want to auto check for updates, don't do this either */
	thresh = g_settings_get_int (cupdate->priv->settings, GPK_SETTINGS_FREQUENCY_GET_UPDATES);
	if (thresh == 0) {
		egg_debug ("not when policy is to never get updates");
		return;
	}

	/* if we get this in the timeout, just remove and start again from now */
	if (cupdate->priv->updates_changed_id > 0)
		g_source_remove (cupdate->priv->updates_changed_id);

	/* now try to get newest update list */
	egg_debug ("updates changed, so getting new update list in %is", GPK_CHECK_UPDATE_UPDATES_CHANGED_TIMEOUT);
	cupdate->priv->updates_changed_id =
		g_timeout_add_seconds (GPK_CHECK_UPDATE_UPDATES_CHANGED_TIMEOUT,
				       (GSourceFunc) gpk_check_update_query_updates_changed_cb, cupdate);
#if GLIB_CHECK_VERSION(2,25,8)
	g_source_set_name_by_id (cupdate->priv->updates_changed_id,
				 "[GpkCheckUpdate] updates-changed");
#endif
}

/**
 * gpk_check_update_restart_schedule_cb:
 **/
static void
gpk_check_update_restart_schedule_cb (PkClient *client, GpkCheckUpdate *cupdate)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *file;

	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* wait for the daemon to quit */
	g_usleep (2*G_USEC_PER_SEC);

	file = BINDIR "/gpk-update-icon";
	egg_debug ("trying to spawn: %s", file);
	ret = g_spawn_command_line_async (file, &error);
	if (!ret) {
		egg_warning ("failed to spawn new instance: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_check_update_transaction_list_changed_cb:
 **/
static void
gpk_check_update_transaction_list_changed_cb (PkControl *control, gchar **transaction_ids, GpkCheckUpdate *cupdate)
{
	PkBitfield roles;
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* inhibit icon if we are updating */
	roles = gpk_check_update_get_active_roles_for_tids (cupdate, transaction_ids);
	cupdate->priv->icon_inhibit_update_in_progress =
		(pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_SYSTEM) ||
		 pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_PACKAGES));
	gpk_check_update_set_icon_visibility (cupdate);
}


/**
 * gpk_check_update_refresh_cache_finished_cb:
 **/
static void
gpk_check_update_refresh_cache_finished_cb (GObject *object, GAsyncResult *res, GpkCheckUpdate *cupdate)
{
	PkClient *client = PK_CLIENT(object);
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(client), res, &error);
	if (results == NULL) {
		egg_warning ("failed to refresh the cache: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to refresh the cache: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		gpk_check_update_show_error (cupdate, error_code);
		goto out;
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_check_update_auto_refresh_cache_cb:
 **/
static void
gpk_check_update_auto_refresh_cache_cb (GpkAutoRefresh *arefresh, GpkCheckUpdate *cupdate)
{
	PkBitfield roles;

	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* No point if we are already updating */
	roles = gpk_check_update_get_active_roles (cupdate);
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REFRESH_CACHE)) {
		egg_debug ("Not refreshing cache as already in progress");
		goto out;
	}

	pk_client_refresh_cache_async (PK_CLIENT(cupdate->priv->task), TRUE, cupdate->priv->cancellable, NULL, NULL,
				       (GAsyncReadyCallback) gpk_check_update_refresh_cache_finished_cb, cupdate);
out:
	return;
}

/**
 * gpk_check_update_auto_get_updates_cb:
 **/
static void
gpk_check_update_auto_get_updates_cb (GpkAutoRefresh *arefresh, GpkCheckUpdate *cupdate)
{
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* show the icon at login time */
	egg_debug ("login cb");
	gpk_check_update_query_updates (cupdate);
}

/**
 * gpk_check_update_get_distro_upgrades_finished_cb:
 **/
static void
gpk_check_update_get_distro_upgrades_finished_cb (GObject *object, GAsyncResult *res, GpkCheckUpdate *cupdate)
{
	PkClient *client = PK_CLIENT(object);
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array = NULL;
	gboolean ret;
	guint i;
	PkDistroUpgrade *item;
	const gchar *title;
	NotifyNotification *notification;
	GString *string = NULL;
	PkError *error_code = NULL;
	gchar *name = NULL;
	PkUpdateStateEnum state;

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(client), res, &error);
	if (results == NULL) {
		egg_warning ("failed to get upgrades: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to get upgrades: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		gpk_check_update_show_error (cupdate, error_code);
		goto out;
	}

	/* process results */
	array = pk_results_get_distro_upgrade_array (results);

	/* any updates? */
	if (array->len == 0) {
		egg_debug ("no upgrades");
		goto out;
	}

	/* do we do the notification? */
	ret = g_settings_get_boolean (cupdate->priv->settings, GPK_SETTINGS_NOTIFY_DISTRO_UPGRADES);
	if (!ret) {
		egg_debug ("ignoring due to GSettings");
		goto out;
	}

	/* find the upgrade string */
	string = g_string_new ("");
	for (i=0; i < array->len; i++) {
		item = (PkDistroUpgrade *) g_ptr_array_index (array, i);
		g_object_get (item,
			      "name", &name,
			      "state", &state,
			      NULL);
		g_string_append_printf (string, "%s (%s)\n", name, pk_distro_upgrade_enum_to_text (state));
		g_free (name);
	}
	if (string->len != 0)
		g_string_set_size (string, string->len-1);

	/* TRANSLATORS: a distro update is available, e.g. Fedora 8 to Fedora 9 */
	title = _("Distribution upgrades available");
	notification = notify_notification_new_with_status_icon (title, string->str, "help-browser", cupdate->priv->status_icon);
	if (notification == NULL) {
		egg_warning ("failed to make bubble");
		goto out;
	}
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_NORMAL);
	notify_notification_add_action (notification, "distro-upgrade-info",
					/* TRANSLATORS: provides more information about the upgrade */
					_("More information"), gpk_check_update_libnotify_cb, cupdate, NULL);
	notify_notification_add_action (notification, "distro-upgrade-do-not-show-available",
					/* TRANSLATORS: hides forever */
					_("Do not show this again"), gpk_check_update_libnotify_cb, cupdate, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (string != NULL)
		g_string_free (string, TRUE);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_check_update_auto_get_upgrades_cb:
 **/
static void
gpk_check_update_auto_get_upgrades_cb (GpkAutoRefresh *arefresh, GpkCheckUpdate *cupdate)
{
	PkBitfield roles;
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* No point if we are already updating */
	roles = gpk_check_update_get_active_roles (cupdate);
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		egg_debug ("Not checking for upgrades as already in progress");
		goto out;
	}

	/* get new distro upgrades list */
	pk_client_get_distro_upgrades_async (PK_CLIENT(cupdate->priv->task), cupdate->priv->cancellable, NULL, NULL,
					     (GAsyncReadyCallback) gpk_check_update_get_distro_upgrades_finished_cb, cupdate);
out:
	return;
}

/**
 * gpk_check_update_notify_network_status_cb:
 **/
static void
gpk_check_update_notify_network_status_cb (PkControl *control, GParamSpec *pspec, GpkCheckUpdate *cupdate)
{
	PkNetworkEnum status;

	g_object_get (control, "network-state", &status, NULL);

	/* inhibit icon when we are offline */
	cupdate->priv->icon_inhibit_network_offline = (status == PK_NETWORK_ENUM_OFFLINE);
	gpk_check_update_set_icon_visibility (cupdate);
}

/**
 * gpk_cupdate_connection_changed_cb:
 **/
static void
gpk_cupdate_connection_changed_cb (EggDbusMonitor *monitor, gboolean connected, GpkCheckUpdate *cupdate)
{
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));
	/* inhibit icon when update viewer open */
	egg_debug ("update viewer on the bus: %i", connected);
	cupdate->priv->icon_inhibit_update_viewer_connected = connected;
	gpk_check_update_set_icon_visibility (cupdate);
}

/**
 * gpk_check_update_get_properties_cb:
 **/
static void
gpk_check_update_get_properties_cb (GObject *object, GAsyncResult *res, GpkCheckUpdate *cupdate)
{
	PkNetworkEnum state;
	GError *error = NULL;
	PkControl *control = PK_CONTROL(object);
	gboolean ret;
	PkBitfield roles;

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	if (!ret) {
		/* TRANSLATORS: backend is broken, and won't tell us what it supports */
		egg_warning ("details could not be retrieved: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get values */
	g_object_get (control,
		      "roles", &roles,
		      "network-state", &state,
		      NULL);

	/* coldplug network state */
	cupdate->priv->icon_inhibit_network_offline = (state == PK_NETWORK_ENUM_OFFLINE);
out:
	return;
}

/**
 * gpk_check_update_file_exist_in_root:
 */
static gboolean
gpk_check_update_file_exist_in_root (const gchar *root, const gchar *filename)
{
	gboolean ret;
	GFile *source;
	gchar *source_path;

	source_path = g_build_filename (root, filename, NULL);
	source = g_file_new_for_path (source_path);

	/* an interesting file exists */
	ret = g_file_query_exists (source, NULL);
	egg_debug ("checking for %s: %s", source_path, ret ? "yes" : "no");
	if (!ret)
		goto out;
out:
	g_free (source_path);
	g_object_unref (source);
	return ret;
}

/**
 * gpk_check_update_mount_added_cb:
 */
static void
gpk_check_update_mount_added_cb (GVolumeMonitor *volume_monitor, GMount *mount, GpkCheckUpdate *cupdate)
{
	gboolean ret = FALSE;
	gchar **filenames = NULL;
	gchar *media_repo_filenames;
	gchar *root_path;
	GFile *root;
	guint i;

	/* check if any installed media is an install disk */
	root = g_mount_get_root (mount);
	root_path = g_file_get_path (root);

	/* use settings */
	media_repo_filenames = g_settings_get_string (cupdate->priv->settings, GPK_SETTINGS_MEDIA_REPO_FILENAMES);
	if (media_repo_filenames == NULL) {
		egg_warning ("failed to get media repo filenames");
		goto out;
	}

	/* search each possible filename */
	filenames = g_strsplit (media_repo_filenames, ",", -1);
	for (i=0; filenames[i] != NULL; i++) {
		ret = gpk_check_update_file_exist_in_root (root_path, filenames[i]);
		if (ret)
			break;
	}

	/* do an updates check with the new media */
	if (ret)
		gpk_check_update_query_updates (cupdate);
out:
	g_strfreev (filenames);
	g_free (media_repo_filenames);
	g_free (root_path);
	g_object_unref (root);
}

/**
 * gpk_check_update_init:
 * @cupdate: This class instance
 **/
static void
gpk_check_update_init (GpkCheckUpdate *cupdate)
{
	gboolean ret;
	PkBitfield roles;

	cupdate->priv = GPK_CHECK_UPDATE_GET_PRIVATE (cupdate);

	cupdate->priv->updates_changed_id = 0;
	cupdate->priv->notification_updates_available = NULL;
	cupdate->priv->notification_error = NULL;
	cupdate->priv->gicon = NULL;
	cupdate->priv->number_updates_critical_last_shown = 0;
	cupdate->priv->status_icon = gtk_status_icon_new ();
	cupdate->priv->cancellable = g_cancellable_new ();
	cupdate->priv->error_code = NULL;
	cupdate->priv->settings = g_settings_new (GPK_SETTINGS_SCHEMA);

	cupdate->priv->arefresh = gpk_auto_refresh_new ();
	g_signal_connect (cupdate->priv->arefresh, "refresh-cache",
			  G_CALLBACK (gpk_check_update_auto_refresh_cache_cb), cupdate);
	g_signal_connect (cupdate->priv->arefresh, "get-updates",
			  G_CALLBACK (gpk_check_update_auto_get_updates_cb), cupdate);
	g_signal_connect (cupdate->priv->arefresh, "get-upgrades",
			  G_CALLBACK (gpk_check_update_auto_get_upgrades_cb), cupdate);

	/* right click actions are common */
	g_signal_connect_object (G_OBJECT (cupdate->priv->status_icon),
				 "popup_menu",
				 G_CALLBACK (gpk_check_update_popup_menu_cb),
				 cupdate, 0);
	g_signal_connect_object (G_OBJECT (cupdate->priv->status_icon),
				 "activate",
				 G_CALLBACK (gpk_check_update_activate_update_cb),
				 cupdate, 0);

	cupdate->priv->dbus_monitor_viewer = egg_dbus_monitor_new ();
	egg_dbus_monitor_assign (cupdate->priv->dbus_monitor_viewer,
				 EGG_DBUS_MONITOR_SESSION,
				 "org.freedesktop.PackageKit.UpdateViewer");
	g_signal_connect (cupdate->priv->dbus_monitor_viewer, "connection-changed",
			  G_CALLBACK (gpk_cupdate_connection_changed_cb), cupdate);

	/* get a volume monitor so we can watch media */
	cupdate->priv->volume_monitor = g_volume_monitor_get ();
	g_signal_connect (cupdate->priv->volume_monitor, "mount-added", G_CALLBACK (gpk_check_update_mount_added_cb), cupdate);

	/* use an asynchronous query object */
	cupdate->priv->task = PK_TASK (gpk_task_new ());
	g_object_set (cupdate->priv->task,
		      "background", TRUE,
		      NULL);

	cupdate->priv->control = pk_control_new ();
	g_signal_connect (cupdate->priv->control, "updates-changed",
			  G_CALLBACK (gpk_check_update_updates_changed_cb), cupdate);
	g_signal_connect (cupdate->priv->control, "transaction-list-changed",
			  G_CALLBACK (gpk_check_update_transaction_list_changed_cb), cupdate);
	g_signal_connect (cupdate->priv->control, "restart-schedule",
			  G_CALLBACK (gpk_check_update_restart_schedule_cb), cupdate);
	g_signal_connect (cupdate->priv->control, "notify::network-state",
			  G_CALLBACK (gpk_check_update_notify_network_status_cb), cupdate);

	/* we need the task list so we can hide the update icon when we are doing the update */
	cupdate->priv->tlist = pk_transaction_list_new ();

	/* coldplug update in progress */
	roles = gpk_check_update_get_active_roles (cupdate);
	cupdate->priv->icon_inhibit_update_in_progress =
		(pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_SYSTEM) ||
		 pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_PACKAGES));

	/* get properties */
	pk_control_get_properties_async (cupdate->priv->control, cupdate->priv->cancellable, (GAsyncReadyCallback) gpk_check_update_get_properties_cb, cupdate);

	/* coldplug update viewer connected */
	ret = egg_dbus_monitor_is_connected (cupdate->priv->dbus_monitor_viewer);
	cupdate->priv->icon_inhibit_update_viewer_connected = ret;
}

/**
 * gpk_check_update_finalize:
 * @object: The object to finalize
 **/
static void
gpk_check_update_finalize (GObject *object)
{
	GpkCheckUpdate *cupdate;

	g_return_if_fail (GPK_IS_CHECK_UPDATE (object));

	cupdate = GPK_CHECK_UPDATE (object);

	g_return_if_fail (cupdate->priv != NULL);

	g_object_unref (cupdate->priv->status_icon);
	g_object_unref (cupdate->priv->tlist);
	g_object_unref (cupdate->priv->arefresh);
	g_object_unref (cupdate->priv->settings);
	g_object_unref (cupdate->priv->control);
	g_object_unref (cupdate->priv->task);
	g_object_unref (cupdate->priv->dbus_monitor_viewer);
	g_object_unref (cupdate->priv->cancellable);
	g_object_unref (cupdate->priv->volume_monitor);
	if (cupdate->priv->gicon != NULL)
		g_object_unref (cupdate->priv->gicon);
	if (cupdate->priv->error_code != NULL)
		g_object_unref (cupdate->priv->error_code);
	if (cupdate->priv->updates_changed_id > 0)
		g_source_remove (cupdate->priv->updates_changed_id);
	if (cupdate->priv->notification_error != NULL)
		g_object_unref (cupdate->priv->notification_error);

	G_OBJECT_CLASS (gpk_check_update_parent_class)->finalize (object);
}

/**
 * gpk_check_update_new:
 *
 * Return value: a new GpkCheckUpdate object.
 **/
GpkCheckUpdate *
gpk_check_update_new (void)
{
	GpkCheckUpdate *cupdate;
	cupdate = g_object_new (GPK_TYPE_CHECK_UPDATE, NULL);
	return GPK_CHECK_UPDATE (cupdate);
}

