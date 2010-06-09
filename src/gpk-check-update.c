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
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <libnotify/notify.h>
#include <packagekit-glib/packagekit.h>
#include <canberra-gtk.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-dbus-monitor.h"

#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-auto-refresh.h"
#include "gpk-check-update.h"
#include "gpk-enum.h"
#include "gpk-error.h"
#include "gpk-helper-repo-signature.h"

static void     gpk_check_update_finalize	(GObject	     *object);

#define GPK_CHECK_UPDATE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_CHECK_UPDATE, GpkCheckUpdatePrivate))

/* the maximum number of lines of data on the libnotify widget */
#define GPK_CHECK_UPDATE_MAX_NUMBER_SECURITY_ENTRIES	7

/* the amount of time after ::UpdatesChanged we refresh the update list */
#define GPK_CHECK_UPDATE_UPDATES_CHANGED_TIMEOUT	3 /* seconds */

struct GpkCheckUpdatePrivate
{
	GtkStatusIcon		*status_icon;
	gboolean		 icon_inhibit_update_in_progress;
	gboolean		 icon_inhibit_network_offline;
	gboolean		 icon_inhibit_update_viewer_connected;
	gchar			*icon_name;
	PkTaskList		*tlist;
	PkControl		*control;
	GpkHelperRepoSignature	*helper_repo_signature;
	GpkAutoRefresh		*arefresh;
	PkClient		*client_primary;
	PkClient		*client_secondary;
	GConfClient		*gconf_client;
	guint			 number_updates_critical_last_shown;
	NotifyNotification	*notification_updates_available;
	GPtrArray		*important_updates_array;
	EggDbusMonitor		*dbus_monitor_viewer;
	guint			 updates_changed_id;
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
	if (cupdate->priv->icon_name == NULL) {
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
		gtk_status_icon_set_from_icon_name (cupdate->priv->status_icon, cupdate->priv->icon_name);
		gtk_status_icon_set_visible (cupdate->priv->status_icon, TRUE);
	} else {
		gtk_status_icon_set_visible (cupdate->priv->status_icon, FALSE);
	}
	return ret;
}

/**
 * gpk_check_update_set_icon_name:
 **/
static void
gpk_check_update_set_icon_name (GpkCheckUpdate *cupdate, const gchar *icon_name)
{
	g_free (cupdate->priv->icon_name);
	cupdate->priv->icon_name = g_strdup (icon_name);
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

static gboolean gpk_check_update_query_updates (GpkCheckUpdate *cupdate, gboolean policy_action);

/**
 * gpk_check_update_get_updates_post_update_cb:
 **/
static gboolean
gpk_check_update_get_updates_post_update_cb (GpkCheckUpdate *cupdate)
{
	g_return_val_if_fail (GPK_IS_CHECK_UPDATE (cupdate), FALSE);

	/* debug so we can catch polling */
	egg_debug ("post updates check");

	gpk_check_update_query_updates (cupdate, FALSE);
	return FALSE;
}

/**
 * gpk_check_update_update_system:
 **/
static gboolean
gpk_check_update_update_system (GpkCheckUpdate *cupdate)
{
	gboolean ret;
	GError *error = NULL;

	ret = pk_client_reset (cupdate->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

#if PK_CHECK_VERSION(0,5,0)
	ret = pk_client_update_system (cupdate->priv->client_primary, TRUE, &error);
#else
	ret = pk_client_update_system (cupdate->priv->client_primary, &error);
#endif
	if (!ret) {
		/* we failed, show the icon */
		egg_warning ("cannot update system: %s", error->message);
		g_error_free (error);
		gpk_check_update_set_icon_name (cupdate, NULL);
		/* we failed, so re-get the update list */
		g_timeout_add_seconds (2, (GSourceFunc) gpk_check_update_get_updates_post_update_cb, cupdate);
	}
out:
	return ret;
}

/**
 * gpk_check_update_activate_update_cb:
 *
 * Callback when the icon is left clicked
 **/
static void
gpk_check_update_activate_update_cb (GtkStatusIcon *status_icon, GpkCheckUpdate *icon)
{
	const gchar *command = "gpk-update-viewer";
	if (!g_spawn_command_line_async (command, NULL))
		egg_warning ("Couldn't execute command: %s", command);
}

/**
 * gpk_check_update_libnotify_cb:
 **/
static void
gpk_check_update_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data)
{
	gboolean ret;
	GError *error = NULL;
	gchar **package_ids;
	GpkCheckUpdate *cupdate = GPK_CHECK_UPDATE (data);

	if (g_strcmp0 (action, "do-not-show-complete-restart") == 0) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_UPDATE_COMPLETE_RESTART);
		gconf_client_set_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_UPDATE_COMPLETE_RESTART, FALSE, NULL);
	} else if (g_strcmp0 (action, "do-not-show-complete") == 0) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_UPDATE_COMPLETE);
		gconf_client_set_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_UPDATE_COMPLETE, FALSE, NULL);
	} else if (g_strcmp0 (action, "do-not-show-update-started") == 0) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_UPDATE_STARTED);
		gconf_client_set_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_UPDATE_STARTED, FALSE, NULL);
	} else if (g_strcmp0 (action, "do-not-show-notify-critical") == 0) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_CRITICAL);
		gconf_client_set_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_CRITICAL, FALSE, NULL);
	} else if (g_strcmp0 (action, "do-not-show-update-not-battery") == 0) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_UPDATE_NOT_BATTERY);
		gconf_client_set_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_UPDATE_NOT_BATTERY, FALSE, NULL);
	} else if (g_strcmp0 (action, "distro-upgrade-do-not-show-available") == 0) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_DISTRO_UPGRADES);
		gconf_client_set_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_DISTRO_UPGRADES, FALSE, NULL);
//	} else if (egg_strequal (action, "show-error-details")) {
//		/* TRANSLATORS: detailed text about the error */
//		gpk_error_dialog (_("Error details"), _("Package Manager error details"), cupdate->priv->error_details);
	} else if (g_strcmp0 (action, "cancel") == 0) {
		/* try to cancel */
		ret = pk_client_cancel (cupdate->priv->client_primary, &error);
		if (!ret) {
			egg_warning ("failed to cancel client: %s", error->message);
			g_error_free (error);
		}
	} else if (g_strcmp0 (action, "update-all-packages") == 0) {
		gpk_check_update_update_system (cupdate);
	} else if (g_strcmp0 (action, "show-update-viewer") == 0) {
		ret = g_spawn_command_line_async (BINDIR "/gpk-update-viewer", &error);
		if (!ret) {
			egg_warning ("Failure launching update viewer: %s", error->message);
			g_error_free (error);
		}
	} else if (g_strcmp0 (action, "update-just-security") == 0) {

		ret = pk_client_reset (cupdate->priv->client_primary, &error);
		if (!ret) {
			egg_warning ("cannot reset client: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* just update the important updates */
		package_ids = pk_package_ids_from_array (cupdate->priv->important_updates_array);
#if PK_CHECK_VERSION(0,5,0)
		ret = pk_client_update_packages (cupdate->priv->client_primary, TRUE, package_ids, &error);
#else
		ret = pk_client_update_packages (cupdate->priv->client_primary, package_ids, &error);
#endif
		if (!ret) {
			egg_warning ("Individual updates failed: %s", error->message);
			g_error_free (error);
		}
		g_strfreev (package_ids);

	} else if (g_strcmp0 (action, "distro-upgrade-info") == 0) {
		ret = g_spawn_command_line_async (DATADIR "/PackageKit/pk-upgrade-distro.sh", &error);
		if (!ret) {
			egg_warning ("Failure launching pk-upgrade-distro.sh: %s", error->message);
			g_error_free (error);
		}
	} else {
		egg_warning ("unknown action id: %s", action);
	}
out:
	return;
}

/**
 * gpk_check_update_critical_updates_warning:
 **/
static void
gpk_check_update_critical_updates_warning (GpkCheckUpdate *cupdate, const gchar *details, GPtrArray *array)
{
	guint i;
	const gchar *package_id;
	const gchar *title;
	gchar *message;
	GString *string;
	gboolean ret;
	GError *error = NULL;
	NotifyNotification *notification;

	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* do we do the notification? */
	ret = gconf_client_get_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_CRITICAL, NULL);
	if (!ret) {
		egg_debug ("ignoring due to GConf");
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

	/* save for later */
	if (cupdate->priv->important_updates_array != NULL) {
		g_ptr_array_foreach (cupdate->priv->important_updates_array, (GFunc) g_free, NULL);
		g_ptr_array_free (cupdate->priv->important_updates_array, TRUE);
	}
	cupdate->priv->important_updates_array = g_ptr_array_new ();
	for (i=0; i<array->len; i++) {
		package_id = g_ptr_array_index (array, i);
		g_ptr_array_add (cupdate->priv->important_updates_array, g_strdup (package_id));
	}

	/* TRANSLATORS: title in the libnotify popup */
	title = ngettext ("Security update available", "Security updates available", array->len);

	/* format message text */
	string = g_string_new ("");
	/* TRANSLATORS: message when there are security updates */
	g_string_append (string, ngettext ("The following important update is available for your computer:",
					   "The following important updates are available for your computer:", array->len));
	g_string_append (string, "\n\n");
	g_string_append (string, details);
	message = g_string_free (string, FALSE);

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
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_CRITICAL);
	notify_notification_add_action (notification, "update-just-security",
					/* TRANSLATORS: button: only security updates */
					_("Install only security updates"), gpk_check_update_libnotify_cb, cupdate, NULL);
//	notify_notification_add_action (notification, "update-all-packages",
//					/* TRANSLATORS: button: all pending updates */
//					_("Install all updates"), gpk_check_update_libnotify_cb, cupdate, NULL);
	notify_notification_add_action (notification, "show-update-viewer",
					/* TRANSLATORS: button: open the update viewer */
					_("Show all software updates"), gpk_check_update_libnotify_cb, cupdate, NULL);
//	notify_notification_add_action (notification, "do-not-show-notify-critical",
//					/* TRANSLATORS: button: hide forever */
//					_("Do not show this again"), gpk_check_update_libnotify_cb, cupdate, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
	/* track so we can prevent doubled notifications */
	cupdate->priv->notification_updates_available = notification;

	g_free (message);
}

/**
 * gpk_check_update_client_info_to_bitfield:
 **/
static PkBitfield
gpk_check_update_client_info_to_bitfield (GpkCheckUpdate *cupdate, PkPackageList *list)
{
	guint i;
	guint length;
	PkBitfield infos = 0;
	const PkPackageObj *obj;

	g_return_val_if_fail (GPK_IS_CHECK_UPDATE (cupdate), PK_INFO_ENUM_UNKNOWN);

	/* shortcut */
	length = pk_package_list_get_size (list);
	if (length == 0)
		return PK_INFO_ENUM_UNKNOWN;

	/* add each status to a list */
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		if (obj == NULL) {
			egg_warning ("not found obj %i", i);
			break;
		}
		egg_debug ("%s %s", obj->id->name, pk_info_enum_to_text (obj->info));
		pk_bitfield_add (infos, obj->info);
	}
	return infos;
}

/**
 * gpk_check_update_get_best_update_icon:
 **/
static const gchar *
gpk_check_update_get_best_update_icon (GpkCheckUpdate *cupdate, PkPackageList *list)
{
	gint value;
	PkBitfield infos;
	const gchar *icon;

	g_return_val_if_fail (GPK_IS_CHECK_UPDATE (cupdate), NULL);

	/* get an enumerated list with all the update types */
	infos = gpk_check_update_client_info_to_bitfield (cupdate, list);

	/* get the most important icon */
	value = pk_bitfield_contain_priority (infos,
					      PK_INFO_ENUM_SECURITY,
					      PK_INFO_ENUM_IMPORTANT,
					      PK_INFO_ENUM_BUGFIX,
					      PK_INFO_ENUM_NORMAL,
					      PK_INFO_ENUM_ENHANCEMENT,
					      PK_INFO_ENUM_LOW, -1);
	if (value == -1) {
		egg_warning ("should not be possible!");
		value = PK_INFO_ENUM_LOW;
	}

	/* get the icon */
	icon = gpk_info_enum_to_icon_name (value);
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

	ret = gconf_client_get_bool (cupdate->priv->gconf_client, GPK_CONF_UPDATE_BATTERY, NULL);
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
	ret = gconf_client_get_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_UPDATE_NOT_BATTERY, NULL);
	if (!ret) {
		egg_debug ("ignoring due to GConf");
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
 * gpk_check_update_get_update_policy:
 **/
static GpkUpdateEnum
gpk_check_update_get_update_policy (GpkCheckUpdate *cupdate)
{
	GpkUpdateEnum update;
	gchar *updates;

	g_return_val_if_fail (GPK_IS_CHECK_UPDATE (cupdate), FALSE);

	updates = gconf_client_get_string (cupdate->priv->gconf_client, GPK_CONF_AUTO_UPDATE, NULL);
	if (updates == NULL) {
		egg_warning ("'%s' gconf key is null!", GPK_CONF_AUTO_UPDATE);
		return GPK_UPDATE_ENUM_UNKNOWN;
	}
	update = gpk_update_enum_from_text (updates);
	g_free (updates);
	return update;
}

/**
 * gpk_check_update_query_updates:
 **/
static gboolean
gpk_check_update_query_updates (GpkCheckUpdate *cupdate, gboolean policy_action)
{
	gboolean ret = FALSE;
	GError *error = NULL;

	g_return_val_if_fail (GPK_IS_CHECK_UPDATE (cupdate), FALSE);

	/* No point if we are already updating */
	if (pk_task_list_contains_role (cupdate->priv->tlist, PK_ROLE_ENUM_UPDATE_PACKAGES) ||
	    pk_task_list_contains_role (cupdate->priv->tlist, PK_ROLE_ENUM_UPDATE_SYSTEM)) {
		egg_debug ("Not checking for updates as already in progress");
		goto out;
	}

	ret = pk_client_reset (cupdate->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	ret = pk_client_get_updates (cupdate->priv->client_primary, PK_FILTER_ENUM_NONE, &error);
	if (!ret) {
		egg_warning ("cannot get updates: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return ret;
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

	/* in GConf? */
	ret = gconf_client_get_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_CRITICAL, NULL);
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
 * gpk_check_update_policy_all_idle_cb:
 **/
static gboolean
gpk_check_update_policy_all_idle_cb (GpkCheckUpdate *cupdate)
{
	gboolean ret;
	ret = gpk_check_update_update_system (cupdate);
	if (ret)
		gpk_check_update_notify_doing_updates (cupdate);

	/* never repeat */
	return FALSE;
}

/**
 * gpk_check_update_process_updates:
 **/
static gboolean
gpk_check_update_process_updates (GpkCheckUpdate *cupdate, PkPackageList *list, gboolean policy_action)
{
	const PkPackageObj *obj;
	guint length;
	guint i;
	guint more;
	guint showing = 0;
	gboolean ret = FALSE;
	GString *status_security;
	GString *status_tooltip;
	GpkUpdateEnum update;
	GPtrArray *security_array;
	const gchar *icon;
	gchar *package_id;
	gchar **package_ids;
	GError *error = NULL;

	g_return_val_if_fail (GPK_IS_CHECK_UPDATE (cupdate), FALSE);

	/* sort by name */
	pk_package_list_sort (list);

	/* filter out the same package with multiple architectures */
	pk_package_list_set_fuzzy_arch (list, TRUE);
	pk_obj_list_remove_duplicate (PK_OBJ_LIST(list));

	/* we have updates to process */
	status_security = g_string_new ("");
	status_tooltip = g_string_new ("");
	security_array = g_ptr_array_new ();

	/* find packages */
	length = pk_package_list_get_size (list);
	egg_debug ("length=%i", length);

	/* we have no updates */
	if (length == 0) {
		egg_debug ("no updates");
		gpk_check_update_set_icon_name (cupdate, NULL);
		goto out;
	}

	/* find the security updates first */
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		if (obj->info == PK_INFO_ENUM_SECURITY) {
			/* add to array */
			package_id = pk_package_id_to_string (obj->id);
			g_ptr_array_add (security_array, package_id);
		}
	}

	/* get the security update text */
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		if (obj->info != PK_INFO_ENUM_SECURITY)
			continue;

		/* don't use a huge notification that won't fit on the screen */
		g_string_append_printf (status_security, "<b>%s</b> - %s\n",
					obj->id->name, obj->summary);
		if (++showing == GPK_CHECK_UPDATE_MAX_NUMBER_SECURITY_ENTRIES) {
			more = security_array->len - showing;
			if (more > 0) {
				/* TRANSLATORS: we have a notification that won't fit, so append on how many other we are not showing */
				g_string_append_printf (status_security, ngettext ("and %d other security update",
										   "and %d other security updates", more), more);
				g_string_append (status_security, "...\n");
			}
			break;
		}
	}

	/* work out icon (cannot be NULL) */
	icon = gpk_check_update_get_best_update_icon (cupdate, list);
	gpk_check_update_set_icon_name (cupdate, icon);

	/* make tooltip */
	if (status_security->len != 0)
		g_string_set_size (status_security, status_security->len-1);
	/* TRANSLATORS: tooltip: how many updates are waiting to be applied */
	g_string_append_printf (status_tooltip, ngettext ("There is %d update available",
							  "There are %d updates available", length), length);
	gtk_status_icon_set_tooltip_text (cupdate->priv->status_icon, status_tooltip->str);

	/* if we are just refreshing after a failed update, don't try to do the actions */
	if (!policy_action) {
		egg_debug ("skipping actions");
		ret = TRUE;
		goto out;
	}

	/* do we do the automatic updates? */
	update = gpk_check_update_get_update_policy (cupdate);
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
			gpk_check_update_critical_updates_warning (cupdate, status_security->str, security_array);
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
			gpk_check_update_critical_updates_warning (cupdate, status_security->str, security_array);
		goto out;
	}

	/* just do security updates */
	if (update == GPK_UPDATE_ENUM_SECURITY) {
		if (security_array->len == 0) {
			egg_debug ("policy security, but none available");
			goto out;
		}

		ret = pk_client_reset (cupdate->priv->client_primary, &error);
		if (!ret) {
			egg_warning ("cannot reset client: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* convert */
		package_ids = pk_package_ids_from_array (security_array);
#if PK_CHECK_VERSION(0,5,0)
		ret = pk_client_update_packages (cupdate->priv->client_primary, TRUE, package_ids, &error);
#else
		ret = pk_client_update_packages (cupdate->priv->client_primary, package_ids, &error);
#endif
		if (!ret) {
			egg_warning ("Individual updates failed: %s", error->message);
			g_error_free (error);
		} else {
			gpk_check_update_notify_doing_updates (cupdate);
		}
		g_strfreev (package_ids);
		goto out;
	}

	/* just do everything */
	if (update == GPK_UPDATE_ENUM_ALL) {
		egg_debug ("we should do the update automatically!");
		g_idle_add ((GSourceFunc) gpk_check_update_policy_all_idle_cb, cupdate);
		goto out;
	}

	/* shouldn't happen */
	egg_warning ("unknown update mode");
out:
	g_string_free (status_security, TRUE);
	g_string_free (status_tooltip, TRUE);
	g_ptr_array_foreach (security_array, (GFunc) g_free, NULL);
	g_ptr_array_free (security_array, TRUE);
	return ret;
}

/**
 * gpk_check_update_query_updates_idle_cb:
 **/
static gboolean
gpk_check_update_query_updates_idle_cb (GpkCheckUpdate *cupdate)
{
	egg_debug ("idle cb");
	gpk_check_update_query_updates (cupdate, TRUE);
	return FALSE;
}

/**
 * gpk_check_update_query_updates_changed_cb:
 **/
static gboolean
gpk_check_update_query_updates_changed_cb (GpkCheckUpdate *cupdate)
{
	egg_debug ("getting new update list (after we waited a short delay)");
	cupdate->priv->updates_changed_id = 0;
	gpk_check_update_query_updates (cupdate, TRUE);
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
	thresh = gconf_client_get_int (cupdate->priv->gconf_client, GPK_CONF_FREQUENCY_GET_UPDATES, NULL);
	if (thresh == 0) {
		egg_debug ("not when policy is to never get updates");
		return;
	}

	/* if we get this in the timeout, just remove and start again from now */
	if (cupdate->priv->updates_changed_id > 0)
		g_source_remove (cupdate->priv->updates_changed_id);

	/* now try to get newest update list */
	egg_debug ("updates changed, so getting new update list soon");
	cupdate->priv->updates_changed_id =
		g_timeout_add_seconds (GPK_CHECK_UPDATE_UPDATES_CHANGED_TIMEOUT,
				       (GSourceFunc) gpk_check_update_query_updates_changed_cb, cupdate);
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
 * gpk_check_update_task_list_changed_cb:
 **/
static void
gpk_check_update_task_list_changed_cb (PkTaskList *tlist, GpkCheckUpdate *cupdate)
{
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* inhibit icon if we are updating */
	cupdate->priv->icon_inhibit_update_in_progress =
		(pk_task_list_contains_role (tlist, PK_ROLE_ENUM_UPDATE_SYSTEM) ||
		 pk_task_list_contains_role (tlist, PK_ROLE_ENUM_UPDATE_PACKAGES));
	gpk_check_update_set_icon_visibility (cupdate);
}

/**
 * gpk_check_update_auto_refresh_cache_cb:
 **/
static void
gpk_check_update_auto_refresh_cache_cb (GpkAutoRefresh *arefresh, GpkCheckUpdate *cupdate)
{
	gboolean ret;
	GError *error = NULL;

	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	ret = pk_client_reset (cupdate->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	ret = pk_client_refresh_cache (cupdate->priv->client_primary, TRUE, &error);
	if (!ret) {
		egg_warning ("cannot refresh cache: %s", error->message);
		g_error_free (error);
		goto out;
	}
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
	g_idle_add ((GSourceFunc) gpk_check_update_query_updates_idle_cb, cupdate);
}

/**
 * gpk_check_update_auto_get_upgrades_cb:
 **/
static void
gpk_check_update_auto_get_upgrades_cb (GpkAutoRefresh *arefresh, GpkCheckUpdate *cupdate)
{
	gboolean ret;
	GError *error = NULL;

	ret = pk_client_reset (cupdate->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	ret = pk_client_get_distro_upgrades (cupdate->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("cannot get updates: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return;
}

/**
 * gpk_check_update_process_distro_upgrades:
 **/
static void
gpk_check_update_process_distro_upgrades (GpkCheckUpdate *cupdate, PkObjList *array)
{
	gboolean ret;
	GError *error = NULL;
	guint i;
	PkDistroUpgradeObj *obj;
	const gchar *title;
	NotifyNotification *notification;
	GString *string = NULL;
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* any updates? */
	if (array->len == 0) {
		egg_debug ("no upgrades");
		goto out;
	}

	/* do we do the notification? */
	ret = gconf_client_get_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_DISTRO_UPGRADES, NULL);
	if (!ret) {
		egg_debug ("ignoring due to GConf");
		goto out;
	}

	/* find the upgrade string */
	string = g_string_new ("");
	for (i=0; i < array->len; i++) {
		obj = (PkDistroUpgradeObj *) pk_obj_list_index (array, i);
		g_string_append_printf (string, "%s (%s)\n", obj->name, pk_distro_upgrade_enum_to_text (obj->state));
	}
	if (string->len != 0)
		g_string_set_size (string, string->len-1);

	/* TRANSLATORS: a distro update is available, e.g. Fedora 8 to Fedora 9 */
	title = _("Distribution upgrades available");
	notification = notify_notification_new_with_status_icon (title, string->str, "help-browser", cupdate->priv->status_icon);
	if (notification == NULL) {
		egg_warning ("failed to get bubble");
		return;
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
	if (string != NULL)
		g_string_free (string, TRUE);
}

/**
 * gpk_check_update_network_status_changed_cb:
 **/
static void
gpk_check_update_network_status_changed_cb (PkControl *control, PkNetworkEnum state, GpkCheckUpdate *cupdate)
{
	/* inhibit icon when we are offline */
	cupdate->priv->icon_inhibit_network_offline = (state == PK_NETWORK_ENUM_OFFLINE);
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
 * gpk_check_update_error_code_cb:
 **/
static void
gpk_check_update_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, GpkCheckUpdate *cupdate)
{
	PkRoleEnum role;
	gboolean ret;
	GError *error = NULL;

	/* ignore some errors */
	if (code == PK_ERROR_ENUM_PROCESS_KILL ||
	    code == PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
		egg_debug ("error ignored %s\n%s", pk_error_enum_to_text (code), details);
		goto out;
	}

	/* ignore the ones we can handle */
	if (pk_error_code_is_need_untrusted (code)) {
		egg_debug ("error ignored as we're handling %s\n%s", pk_error_enum_to_text (code), details);
		goto out;
	}

	/* get the role */
	ret = pk_client_get_role (client, &role, NULL, &error);
	if (!ret) {
		egg_warning ("failed to get role: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* if we're doing queries automatically for the user, don't spam them
	   with locking failure messages */
	if (code == PK_ERROR_ENUM_CANNOT_GET_LOCK) {
		if (role == PK_ROLE_ENUM_GET_UPDATES ||
		    role == PK_ROLE_ENUM_GET_DISTRO_UPGRADES) {
			egg_debug ("cannot get lock for automatic action, ignoring");
			goto out;
		}
	}

	/* not modal as we are a status icon */
	gpk_error_dialog (gpk_error_enum_to_localised_text (code),
			  gpk_error_enum_to_localised_message (code), details);
out:
	return;
}

/**
 * gpk_check_update_repo_signature_event_cb:
 **/
static void
gpk_check_update_repo_signature_event_cb (GpkHelperRepoSignature *helper_repo_signature, GtkResponseType type, const gchar *key_id, const gchar *package_id, GpkCheckUpdate *cupdate)
{
	gboolean ret;
	GError *error = NULL;

	if (type != GTK_RESPONSE_YES) {
		goto out;
	}

	/* reset client */
	ret = pk_client_reset (cupdate->priv->client_secondary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* install signature */
	ret = pk_client_install_signature (cupdate->priv->client_secondary, PK_SIGTYPE_ENUM_GPG, key_id, package_id, &error);
	if (!ret) {
		egg_warning ("cannot install signature: %s", error->message);
		g_error_free (error);
		goto out;
	}
	/* set state */
	egg_debug ("repo sig cb");
	gpk_check_update_query_updates (cupdate, TRUE);
out:
	return;
}

/**
 * gpk_check_update_repo_signature_required_cb:
 **/
static void
gpk_check_update_repo_signature_required_cb (PkClient *client, const gchar *package_id, const gchar *repository_name,
					      const gchar *key_url, const gchar *key_userid, const gchar *key_id,
					      const gchar *key_fingerprint, const gchar *key_timestamp,
					      PkSigTypeEnum type, GpkCheckUpdate *cupdate)
{
	/* use the helper */
	gpk_helper_repo_signature_show (cupdate->priv->helper_repo_signature, package_id,
					repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp);
}

/**
 * gpk_check_update_primary_requeue:
 **/
static gboolean
gpk_check_update_primary_requeue (GpkCheckUpdate *cupdate)
{
	gboolean ret;
	GError *error = NULL;

	/* retry new action */
	ret = pk_client_requeue (cupdate->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("Failed to requeue: %s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * gpk_check_update_finished_notify:
 **/
static void
gpk_check_update_finished_notify (GpkCheckUpdate *cupdate, PkClient *client)
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
		/* TRANSLATORS: we did the update, but some updates were skipped and not applied */
		message = ngettext ("One package was skipped:",
				    "Some packages were skipped:", skipped_number);
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
	ret = gconf_client_get_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_UPDATE_COMPLETE, NULL);
	if (!ret) {
		egg_debug ("ignoring due to GConf");
		return;
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
	g_string_free (message_text, TRUE);
}

/**
 * gpk_check_update_finished_cb:
 **/
static void
gpk_check_update_finished_cb (PkClient *client, PkExitEnum exit_enum, guint runtime, GpkCheckUpdate *cupdate)
{
	PkRoleEnum role;
	PkPackageList *list;
	PkObjList *array;

	pk_client_get_role (client, &role, NULL, NULL);
	egg_debug ("role: %s, exit: %s", pk_role_enum_to_text (role), pk_exit_enum_to_text (exit_enum));

	/* need to handle retry with only_trusted=FALSE */
	if (client == cupdate->priv->client_primary &&
	    exit_enum == PK_EXIT_ENUM_NEED_UNTRUSTED) {
		egg_debug ("need to handle untrusted");
		pk_client_set_only_trusted (client, FALSE);
		gpk_check_update_primary_requeue (cupdate);
		return;
	}

	/* we've just agreed to auth */
	if (role == PK_ROLE_ENUM_INSTALL_SIGNATURE) {
		if (exit_enum == PK_EXIT_ENUM_SUCCESS)
			gpk_check_update_primary_requeue (cupdate);
	}

	/* get-updates */
	if (role == PK_ROLE_ENUM_GET_UPDATES &&
	    exit_enum == PK_EXIT_ENUM_SUCCESS) {
		list = pk_client_get_package_list (client);
		gpk_check_update_process_updates (cupdate, list, TRUE);
		g_object_unref (list);
	}

	/* get-upgrades */
	if (role == PK_ROLE_ENUM_GET_DISTRO_UPGRADES &&
	    exit_enum == PK_EXIT_ENUM_SUCCESS) {
#if PK_CHECK_VERSION(0,5,0)
		array = pk_client_get_distro_upgrade_list (client);
#else
		array = pk_client_get_cached_objects (client); /* removed in 0.5.x */
#endif
		gpk_check_update_process_distro_upgrades (cupdate, array);
		g_object_unref (array);
	}

	/* refresh-cache */
	if (role == PK_ROLE_ENUM_REFRESH_CACHE &&
	    exit_enum == PK_EXIT_ENUM_SUCCESS) {
		egg_debug ("finished refresh cb");
		gpk_check_update_query_updates (cupdate, TRUE);
	}

	/* updates */
	if ((role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	     role == PK_ROLE_ENUM_UPDATE_SYSTEM) &&
	    exit_enum == PK_EXIT_ENUM_SUCCESS) {

		/* play the sound, using sounds from the naming spec */
		ca_context_play (ca_gtk_context_get (), 0,
				 /* TODO: add a new sound to the spec */
				 CA_PROP_EVENT_ID, "complete-download",
				 /* TRANSLATORS: this is the application name for libcanberra */
				 CA_PROP_APPLICATION_NAME, _("GNOME PackageKit Update Icon"),
				 /* TRANSLATORS: this is the sound description */
				 CA_PROP_EVENT_DESCRIPTION, _("Updated successfully"), NULL);

		gpk_check_update_finished_notify (cupdate, client);
		cupdate->priv->number_updates_critical_last_shown = 0;
	}
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
	const gchar *filenames[] = { "media.repo", ".discinfo", NULL };
	gchar *root_path;
	GFile *root;
	guint i;

	/* check if any installed media is an install disk */
	root = g_mount_get_root (mount);
	root_path = g_file_get_path (root);

	/* search each possible filename */
	for (i=0; filenames[i] != NULL; i++) {
		ret = gpk_check_update_file_exist_in_root (root_path, filenames[i]);
		if (ret)
			break;
	}

	/* do an updates check with the new media */
	if (ret)
		gpk_check_update_query_updates (cupdate, FALSE);

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
	PkNetworkEnum state;

	cupdate->priv = GPK_CHECK_UPDATE_GET_PRIVATE (cupdate);

	cupdate->priv->updates_changed_id = 0;
	cupdate->priv->notification_updates_available = NULL;
	cupdate->priv->important_updates_array = NULL;
	cupdate->priv->icon_name = NULL;
	cupdate->priv->number_updates_critical_last_shown = 0;
	cupdate->priv->status_icon = gtk_status_icon_new ();

	/* preload all the common GConf keys */
	cupdate->priv->gconf_client = gconf_client_get_default ();
	gconf_client_add_dir (cupdate->priv->gconf_client, GPK_CONF_DIR,
			      GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

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

	/* use an asynchronous query object */
	cupdate->priv->client_primary = pk_client_new ();
	pk_client_set_use_buffer (cupdate->priv->client_primary, TRUE, NULL);
	g_signal_connect (cupdate->priv->client_primary, "finished",
			  G_CALLBACK (gpk_check_update_finished_cb), cupdate);
	g_signal_connect (cupdate->priv->client_primary, "error-code",
			  G_CALLBACK (gpk_check_update_error_code_cb), cupdate);
	g_signal_connect (cupdate->priv->client_primary, "repo-signature-required",
			  G_CALLBACK (gpk_check_update_repo_signature_required_cb), cupdate);

	/* this is for the auth callback */
	cupdate->priv->client_secondary = pk_client_new ();
	g_signal_connect (cupdate->priv->client_secondary, "error-code",
			  G_CALLBACK (gpk_check_update_error_code_cb), cupdate);
	g_signal_connect (cupdate->priv->client_secondary, "finished",
			  G_CALLBACK (gpk_check_update_finished_cb), cupdate);

	/* helpers */
	cupdate->priv->helper_repo_signature = gpk_helper_repo_signature_new ();
	g_signal_connect (cupdate->priv->helper_repo_signature, "event", G_CALLBACK (gpk_check_update_repo_signature_event_cb), cupdate);

	cupdate->priv->control = pk_control_new ();
	g_signal_connect (cupdate->priv->control, "updates-changed",
			  G_CALLBACK (gpk_check_update_updates_changed_cb), cupdate);
	g_signal_connect (cupdate->priv->control, "restart-schedule",
			  G_CALLBACK (gpk_check_update_restart_schedule_cb), cupdate);
	g_signal_connect (cupdate->priv->control, "network-state-changed",
			  G_CALLBACK (gpk_check_update_network_status_changed_cb), cupdate);

	/* we need the task list so we can hide the update icon when we are doing the update */
	cupdate->priv->tlist = pk_task_list_new ();
	g_signal_connect (cupdate->priv->tlist, "changed",
			  G_CALLBACK (gpk_check_update_task_list_changed_cb), cupdate);

	/* get a volume monitor so we can watch media */
	cupdate->priv->volume_monitor = g_volume_monitor_get ();
	g_signal_connect (cupdate->priv->volume_monitor, "mount-added", G_CALLBACK (gpk_check_update_mount_added_cb), cupdate);

	/* coldplug update in progress */
	cupdate->priv->icon_inhibit_update_in_progress =
		(pk_task_list_contains_role (cupdate->priv->tlist, PK_ROLE_ENUM_UPDATE_SYSTEM) ||
		 pk_task_list_contains_role (cupdate->priv->tlist, PK_ROLE_ENUM_UPDATE_PACKAGES));

	/* coldplug network state */
	state = pk_control_get_network_state (cupdate->priv->control, NULL);
	cupdate->priv->icon_inhibit_network_offline = (state == PK_NETWORK_ENUM_OFFLINE);

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
	g_object_unref (cupdate->priv->gconf_client);
	g_object_unref (cupdate->priv->control);
	g_object_unref (cupdate->priv->client_primary);
	g_object_unref (cupdate->priv->client_secondary);
	g_object_unref (cupdate->priv->dbus_monitor_viewer);
	g_object_unref (cupdate->priv->helper_repo_signature);
	g_object_unref (cupdate->priv->volume_monitor);
	if (cupdate->priv->important_updates_array != NULL) {
		g_ptr_array_foreach (cupdate->priv->important_updates_array, (GFunc) g_free, NULL);
		g_ptr_array_free (cupdate->priv->important_updates_array, TRUE);
	}
	g_free (cupdate->priv->icon_name);
	if (cupdate->priv->updates_changed_id > 0)
		g_source_remove (cupdate->priv->updates_changed_id);

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

