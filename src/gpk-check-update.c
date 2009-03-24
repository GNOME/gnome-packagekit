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
#include "egg-dbus-monitor.h"

#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-smart-icon.h"
#include "gpk-auto-refresh.h"
#include "gpk-client.h"
#include "gpk-check-update.h"
#include "gpk-enum.h"

static void     gpk_check_update_finalize	(GObject	     *object);

#define GPK_CHECK_UPDATE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_CHECK_UPDATE, GpkCheckUpdatePrivate))

/* the maximum number of lines of data on the libnotify widget */
#define GPK_CHECK_UPDATE_MAX_NUMBER_SECURITY_ENTRIES	7
#define ACTION_DISTRO_UPGRADE_INFO 			"distro-upgrade-info"
#define ACTION_DISTRO_UPGRADE_DO_NOT_SHOW		"distro-upgrade-do-not-show-available"

struct GpkCheckUpdatePrivate
{
	GpkSmartIcon		*sicon;
	PkConnection		*pconnection;
	PkTaskList		*tlist;
	PkControl		*control;
	GpkAutoRefresh		*arefresh;
	GpkClient		*gclient_refresh_cache;
	GpkClient		*gclient_update_system;
	GpkClient		*gclient_get_updates;
	GpkClient		*gclient_get_distro_upgrades;
	GConfClient		*gconf_client;
	gboolean		 cache_okay;
	gboolean		 cache_update_in_progress;
	gboolean		 get_updates_in_progress;
	guint			 number_updates_critical_last_shown;
	NotifyNotification	*notification_updates_available;
	GPtrArray		*important_updates_array;
	EggDbusMonitor		*dbus_monitor_viewer;
};

G_DEFINE_TYPE (GpkCheckUpdate, gpk_check_update, G_TYPE_OBJECT)

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
 * gpk_check_update_about_dialog_url_cb:
 **/
static void
gpk_check_update_about_dialog_url_cb (GtkAboutDialog *about, const char *address, gpointer data)
{
	GError *error = NULL;
	gboolean ret;
	char *cmdline;
	GdkScreen *gscreen;
	GtkWidget *error_dialog;
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
		error_dialog = gtk_message_dialog_new (GTK_WINDOW (about),
						       GTK_DIALOG_MODAL,
						       GTK_MESSAGE_INFO,
						       GTK_BUTTONS_OK,
						       /* TRANSLATORS: normally a packaging error, cannot launch link */
						       _("Failed to show url"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (error_dialog),
							  "%s", error->message);
		gtk_dialog_run (GTK_DIALOG (error_dialog));
		gtk_widget_destroy (error_dialog);
		g_error_free (error);
	}

out:
	g_free (url);
}

/**
 * gpk_check_update_show_about_cb:
 **/
static void
gpk_check_update_show_about_cb (GtkMenuItem *item, gpointer data)
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

	gtk_about_dialog_set_url_hook (gpk_check_update_about_dialog_url_cb, NULL, NULL);
	gtk_about_dialog_set_email_hook (gpk_check_update_about_dialog_url_cb, (gpointer) "mailto:", NULL);

	gtk_window_set_default_icon_name (GPK_ICON_SOFTWARE_UPDATE);
	gtk_show_about_dialog (NULL,
			       "version", VERSION,
			       "copyright", "Copyright \xc2\xa9 2007-2009 Richard Hughes",
			       "license", license_trans,
				/* TRANSLATORS: website label */
			       "website-label", _("PackageKit Website"),
			       "website", "www.packagekit.org",
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
	egg_debug ("polling check");

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
	ret = gpk_client_update_system (cupdate->priv->gclient_update_system, NULL);

	/* this isn't valid anymore */
	if (ret)
		cupdate->priv->number_updates_critical_last_shown = 0;

	/* we failed, show the icon */
	if (!ret) {
		gpk_smart_icon_set_icon_name (cupdate->priv->sicon, NULL);
		/* we failed, so re-get the update list */
		g_timeout_add_seconds (2, (GSourceFunc) gpk_check_update_get_updates_post_update_cb, cupdate);
	}
	return ret;
}

/**
 * gpk_check_update_menuitem_update_system_cb:
 **/
static void
gpk_check_update_menuitem_update_system_cb (GtkMenuItem *item, gpointer data)
{
	GpkCheckUpdate *cupdate = GPK_CHECK_UPDATE (data);
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));
	gpk_client_set_interaction (cupdate->priv->gclient_update_system, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
	gpk_check_update_update_system (cupdate);
}

/**
 * gpk_check_update_menuitem_show_updates_cb:
 **/
static void
gpk_check_update_menuitem_show_updates_cb (GtkMenuItem *item, gpointer data)
{
	const gchar *command = "gpk-update-viewer";
	if (!g_spawn_command_line_async (command, NULL))
		egg_warning ("Couldn't execute command: %s", command);
}

/**
 * gpk_check_update_activate_update_cb:
 * @button: Which buttons are pressed
 *
 * Callback when the icon is clicked
 **/
static void
gpk_check_update_activate_update_cb (GtkStatusIcon *status_icon, GpkCheckUpdate *icon)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;
	GtkWidget *image;

	egg_debug ("icon left clicked");

	/* TRANSLATORS: context menu to execute the update viewer  */
	item = gtk_image_menu_item_new_with_mnemonic (_("_Show Updates"));
	image = gtk_image_new_from_icon_name (GPK_ICON_SOFTWARE_UPDATE, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (gpk_check_update_menuitem_show_updates_cb), icon);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* TRANSLATORS: context menu to update any pending updates */
	item = gtk_image_menu_item_new_with_mnemonic (_("_Update System Now"));
	image = gtk_image_new_from_icon_name (GPK_ICON_SOFTWARE_UPDATE_AVAILABLE, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (gpk_check_update_menuitem_update_system_cb), icon);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

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
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, GpkCheckUpdate *cupdate)
{
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));
	egg_debug ("connected=%i", connected);
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

	if (egg_strequal (action, "update-all-packages")) {
		gpk_client_set_interaction (cupdate->priv->gclient_update_system, GPK_CLIENT_INTERACT_WARNING);
		gpk_check_update_update_system (cupdate);
	} else if (egg_strequal (action, "update-just-security")) {

		/* just update the important updates */
		package_ids = pk_package_ids_from_array (cupdate->priv->important_updates_array);
		gpk_client_set_interaction (cupdate->priv->gclient_update_system, GPK_CLIENT_INTERACT_WARNING);
		ret = gpk_client_update_packages (cupdate->priv->gclient_update_system, package_ids, &error);
		if (!ret) {
			egg_warning ("Individual updates failed: %s", error->message);
			g_error_free (error);
		}
		/* this isn't valid anymore */
		if (ret)
			cupdate->priv->number_updates_critical_last_shown = 0;
		g_strfreev (package_ids);

	} else if (egg_strequal (action, "do-not-show-notify-critical")) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_CRITICAL);
		gconf_client_set_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_CRITICAL, FALSE, NULL);
	} else if (egg_strequal (action, "do-not-show-update-not-battery")) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_UPDATE_NOT_BATTERY);
		gconf_client_set_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_UPDATE_NOT_BATTERY, FALSE, NULL);
	} else if (egg_strequal (action, ACTION_DISTRO_UPGRADE_INFO)) {
		ret = g_spawn_command_line_async ("/usr/share/PackageKit/pk-upgrade-distro.sh", NULL);
		if (!ret) {
			egg_warning ("Failure launching pk-upgrade-distro.sh");
		}
	} else if (egg_strequal (action, ACTION_DISTRO_UPGRADE_DO_NOT_SHOW)) {
		egg_debug ("set %s to FALSE", GPK_CONF_NOTIFY_DISTRO_UPGRADES);
		gconf_client_set_bool (cupdate->priv->gconf_client, GPK_CONF_NOTIFY_DISTRO_UPGRADES, FALSE, NULL);
	} else {
		egg_warning ("unknown action id: %s", action);
	}
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
	notification = notify_notification_new (title, message, "help-browser", NULL);
	if (notification == NULL) {
		egg_warning ("failed to get bubble");
		return;
	}
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_CRITICAL);
	notify_notification_add_action (notification, "update-just-security",
					/* TRANSLATORS: button: only security updates */
					_("Install only security updates"), gpk_check_update_libnotify_cb, cupdate, NULL);
	notify_notification_add_action (notification, "update-all-packages",
					/* TRANSLATORS: button: all pending updates */
					_("Install all updates"), gpk_check_update_libnotify_cb, cupdate, NULL);
	notify_notification_add_action (notification, "do-not-show-notify-critical",
					/* TRANSLATORS: button: hide forever */
					_("Do not show this again"), gpk_check_update_libnotify_cb, cupdate, NULL);
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
	notification = notify_notification_new (_("Updates not installed"), message, "help-browser", NULL);
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, "do-not-show-update-not-battery",
					/* TRANSLATORS: hide this warning type forever */
					_("Do not show this warning again"), gpk_check_update_libnotify_cb, cupdate, NULL);
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
	PkPackageList *list;
	GError *error = NULL;

	g_return_val_if_fail (GPK_IS_CHECK_UPDATE (cupdate), FALSE);

	/* are we already called */
	if (cupdate->priv->get_updates_in_progress) {
		egg_debug ("GetUpdate already in progress");
		return FALSE;
	}

	/* No point if we are already updating */
	if (pk_task_list_contains_role (cupdate->priv->tlist, PK_ROLE_ENUM_UPDATE_PACKAGES) ||
	    pk_task_list_contains_role (cupdate->priv->tlist, PK_ROLE_ENUM_UPDATE_SYSTEM)) {
		egg_debug ("Not checking for updates as already in progress");
		return FALSE;
	}

	/* get updates */
	gpk_client_set_interaction (cupdate->priv->gclient_get_updates, GPK_CLIENT_INTERACT_NEVER);
	cupdate->priv->get_updates_in_progress = TRUE;
	list = gpk_client_get_updates (cupdate->priv->gclient_get_updates, &error);
	cupdate->priv->get_updates_in_progress = FALSE;
	if (list == NULL) {
		egg_warning ("failed to get updates: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

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
		gpk_smart_icon_set_icon_name (cupdate->priv->sicon, NULL);
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
			/* TRANSLATORS: we have a notification that won't fit, so append on how many other we are not showing */
			g_string_append_printf (status_security, ngettext ("and %d other security update",
									   "and %d other security updates", more), more);
			g_string_append (status_security, "...\n");
			break;
		}
	}

	/* work out icon */
	icon = gpk_check_update_get_best_update_icon (cupdate, list);
	gpk_smart_icon_set_icon_name (cupdate->priv->sicon, icon);
	gpk_smart_icon_pulse (cupdate->priv->sicon);

	/* make tooltip */
	if (status_security->len != 0)
		g_string_set_size (status_security, status_security->len-1);
	/* TRANSLATORS: tooltip: how many updates are waiting to be applied */
	g_string_append_printf (status_tooltip, ngettext ("There is %d update available",
							  "There are %d updates available", length), length);
#if GTK_CHECK_VERSION(2,15,0)
	gtk_status_icon_set_tooltip_text (GTK_STATUS_ICON (cupdate->priv->sicon), status_tooltip->str);
#else
	gtk_status_icon_set_tooltip (GTK_STATUS_ICON (cupdate->priv->sicon), status_tooltip->str);
#endif
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

		/* do we warn the user? */
		if (security_array->len > 0)
			gpk_check_update_critical_updates_warning (cupdate, status_security->str, security_array);
		goto out;
	}

	/* are we on battery and configured to skip the action */
	ret = gpk_check_update_check_on_battery (cupdate);
	if (!ret) {
		egg_debug ("on battery so not doing update");
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

		/* convert */
		package_ids = pk_package_ids_from_array (security_array);
		gpk_client_set_interaction (cupdate->priv->gclient_update_system, GPK_CLIENT_INTERACT_WARNING);
		ret = gpk_client_update_packages (cupdate->priv->gclient_update_system, package_ids, &error);
		if (!ret) {
			egg_warning ("Individual updates failed: %s", error->message);
			g_error_free (error);
		}
		/* this isn't valid anymore */
		if (ret)
			cupdate->priv->number_updates_critical_last_shown = 0;
		g_strfreev (package_ids);
		goto out;
	}

	/* just do everything */
	if (update == GPK_UPDATE_ENUM_ALL) {
		egg_debug ("we should do the update automatically!");
		gpk_client_set_interaction (cupdate->priv->gclient_update_system, GPK_CLIENT_INTERACT_WARNING);
		g_idle_add ((GSourceFunc) gpk_check_update_update_system, cupdate);
		goto out;
	}

	/* shouldn't happen */
	egg_warning ("unknown update mode");
out:
	g_object_unref (list);
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
	gpk_check_update_query_updates (cupdate, TRUE);
	return FALSE;
}

/**
 * gpk_check_update_updates_changed_cb:
 **/
static void
gpk_check_update_updates_changed_cb (PkControl *control, GpkCheckUpdate *cupdate)
{
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* now try to get newest update list */
	egg_warning ("updates changed");

	/* ignore our own updates */
	if (!cupdate->priv->get_updates_in_progress) {
		g_idle_add ((GSourceFunc) gpk_check_update_query_updates_idle_cb, cupdate);
		egg_warning ("not own updates");
	} else
		egg_warning ("own updates");
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
	/* hide icon if we are updating */
	if (pk_task_list_contains_role (tlist, PK_ROLE_ENUM_UPDATE_SYSTEM) ||
	    pk_task_list_contains_role (tlist, PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		gpk_smart_icon_set_icon_name (cupdate->priv->sicon, NULL);
	}
}

/**
 * gpk_check_update_auto_refresh_cache_cb:
 **/
static void
gpk_check_update_auto_refresh_cache_cb (GpkAutoRefresh *arefresh, GpkCheckUpdate *cupdate)
{
	gboolean ret;
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* got a cache, no need to poll */
	if (cupdate->priv->cache_okay)
		return;

	/* already in progress, but not yet certified okay */
	if (cupdate->priv->cache_update_in_progress)
		return;

	cupdate->priv->cache_update_in_progress = TRUE;
	cupdate->priv->cache_okay = TRUE;

	/* use the gnome helper to refresh the cache */
	gpk_client_set_interaction (cupdate->priv->gclient_refresh_cache, GPK_CLIENT_INTERACT_NEVER);
	ret = gpk_client_refresh_cache (cupdate->priv->gclient_refresh_cache, NULL);
	if (!ret) {
		/* we failed to get the cache */
		egg_warning ("failed to refresh cache");

		/* try again in a few minutes */
		cupdate->priv->cache_okay = FALSE;
	} else {
		/* stop the polling */
		cupdate->priv->cache_okay = TRUE;

		/* now try to get updates */
		egg_debug ("get updates");
		gpk_check_update_query_updates (cupdate, TRUE);
	}
	cupdate->priv->cache_update_in_progress = FALSE;
}

/**
 * gpk_check_update_auto_get_updates_cb:
 **/
static void
gpk_check_update_auto_get_updates_cb (GpkAutoRefresh *arefresh, GpkCheckUpdate *cupdate)
{
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* show the icon at login time */
	g_idle_add ((GSourceFunc) gpk_check_update_query_updates_idle_cb, cupdate);
}

/**
 * gpk_check_update_auto_get_upgrades_cb:
 **/
static void
gpk_check_update_auto_get_upgrades_cb (GpkAutoRefresh *arefresh, GpkCheckUpdate *cupdate)
{
	GError *error = NULL;
	const GPtrArray	*array;
	gboolean ret;
	guint i;
	PkDistroUpgradeObj *obj;
	const gchar *title;
	NotifyNotification *notification;
	GString *string = NULL;
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));

	/* get updates */
	gpk_client_set_interaction (cupdate->priv->gclient_get_distro_upgrades, GPK_CLIENT_INTERACT_NEVER);
	array = gpk_client_get_distro_upgrades (cupdate->priv->gclient_get_distro_upgrades, &error);
	if (array == NULL) {
		egg_warning ("failed to get upgrades: %s", error->message);
		g_error_free (error);
		goto out;
	}

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
		obj = (PkDistroUpgradeObj *) g_ptr_array_index (array, i);
		g_string_append_printf (string, "%s (%s)\n", obj->name, pk_distro_upgrade_enum_to_text (obj->state));
	}
	if (string->len != 0)
		g_string_set_size (string, string->len-1);

	/* TRANSLATORS: a distro update is available, e.g. Fedora 8 to Fedora 9 */
	title = _("Distribution upgrades available");
	notification = notify_notification_new (title, string->str, "help-browser", NULL);
	if (notification == NULL) {
		egg_warning ("failed to get bubble");
		return;
	}
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_NORMAL);
	notify_notification_add_action (notification, ACTION_DISTRO_UPGRADE_INFO,
					/* TRANSLATORS: provides more information about the upgrade */
					_("More information"), gpk_check_update_libnotify_cb, cupdate, NULL);
	notify_notification_add_action (notification, ACTION_DISTRO_UPGRADE_DO_NOT_SHOW,
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
	//TODO: check that set_visible (TRUE) on a unset icon doesn't cause an icon to show
	if (state == PK_NETWORK_ENUM_OFFLINE)
		gtk_status_icon_set_visible (GTK_STATUS_ICON(cupdate->priv->sicon), FALSE);
	else
		gtk_status_icon_set_visible (GTK_STATUS_ICON(cupdate->priv->sicon), TRUE);
}

/**
 * gpk_cupdate_connection_changed_cb:
 **/
static void
gpk_cupdate_connection_changed_cb (EggDbusMonitor *monitor, gboolean connected, GpkCheckUpdate *cupdate)
{
	g_return_if_fail (GPK_IS_CHECK_UPDATE (cupdate));
	if (connected) {
		egg_debug ("update viewer on the bus, so hiding icon");
		gpk_smart_icon_set_icon_name (cupdate->priv->sicon, NULL);
	}
}

/**
 * gpk_check_update_init:
 * @cupdate: This class instance
 **/
static void
gpk_check_update_init (GpkCheckUpdate *cupdate)
{
	GtkStatusIcon *status_icon;
	cupdate->priv = GPK_CHECK_UPDATE_GET_PRIVATE (cupdate);

	cupdate->priv->notification_updates_available = NULL;
	cupdate->priv->important_updates_array = NULL;
	cupdate->priv->number_updates_critical_last_shown = 0;
	cupdate->priv->sicon = gpk_smart_icon_new ();

	cupdate->priv->gconf_client = gconf_client_get_default ();
	cupdate->priv->arefresh = gpk_auto_refresh_new ();
	g_signal_connect (cupdate->priv->arefresh, "refresh-cache",
			  G_CALLBACK (gpk_check_update_auto_refresh_cache_cb), cupdate);
	g_signal_connect (cupdate->priv->arefresh, "get-updates",
			  G_CALLBACK (gpk_check_update_auto_get_updates_cb), cupdate);
	g_signal_connect (cupdate->priv->arefresh, "get-upgrades",
			  G_CALLBACK (gpk_check_update_auto_get_upgrades_cb), cupdate);

	/* right click actions are common */
	status_icon = GTK_STATUS_ICON (cupdate->priv->sicon);
	g_signal_connect_object (G_OBJECT (status_icon),
				 "popup_menu",
				 G_CALLBACK (gpk_check_update_popup_menu_cb),
				 cupdate, 0);
	g_signal_connect_object (G_OBJECT (status_icon),
				 "activate",
				 G_CALLBACK (gpk_check_update_activate_update_cb),
				 cupdate, 0);

	cupdate->priv->dbus_monitor_viewer = egg_dbus_monitor_new ();
	egg_dbus_monitor_assign (cupdate->priv->dbus_monitor_viewer,
				 EGG_DBUS_MONITOR_SESSION,
				 "org.freedesktop.PackageKit.UpdateViewer2");
	g_signal_connect (cupdate->priv->dbus_monitor_viewer, "connection-changed",
			  G_CALLBACK (gpk_cupdate_connection_changed_cb), cupdate);

	/* install stuff using the gnome helpers */
	cupdate->priv->gclient_refresh_cache = gpk_client_new ();
	cupdate->priv->gclient_update_system = gpk_client_new ();
	cupdate->priv->gclient_get_updates = gpk_client_new ();
	cupdate->priv->gclient_get_distro_upgrades = gpk_client_new ();

	cupdate->priv->pconnection = pk_connection_new ();
	g_signal_connect (cupdate->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), cupdate);
	if (pk_connection_valid (cupdate->priv->pconnection)) {
		pk_connection_changed_cb (cupdate->priv->pconnection, TRUE, cupdate);
	}

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

	/* refresh the cache, and poll until we get a good refresh */
	cupdate->priv->cache_okay = FALSE;
	cupdate->priv->cache_update_in_progress = FALSE;
	cupdate->priv->get_updates_in_progress = FALSE;
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

	g_object_unref (cupdate->priv->sicon);
	g_object_unref (cupdate->priv->pconnection);
	g_object_unref (cupdate->priv->tlist);
	g_object_unref (cupdate->priv->arefresh);
	g_object_unref (cupdate->priv->gconf_client);
	g_object_unref (cupdate->priv->control);
	g_object_unref (cupdate->priv->gclient_refresh_cache);
	g_object_unref (cupdate->priv->gclient_update_system);
	g_object_unref (cupdate->priv->gclient_get_updates);
	g_object_unref (cupdate->priv->gclient_get_distro_upgrades);
	g_object_unref (cupdate->priv->dbus_monitor_viewer);
	if (cupdate->priv->important_updates_array != NULL) {
		g_ptr_array_foreach (cupdate->priv->important_updates_array, (GFunc) g_free, NULL);
		g_ptr_array_free (cupdate->priv->important_updates_array, TRUE);
	}

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

