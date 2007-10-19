/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-job-list.h>
#include <pk-client.h>
#include <pk-common.h>
#include <pk-task-list.h>
#include <pk-connection.h>
#include <pk-package-id.h>

#include "pk-common-gui.h"
#include "pk-watch.h"
#include "pk-progress.h"
#include "pk-smart-icon.h"

static void     pk_watch_class_init	(PkWatchClass *klass);
static void     pk_watch_init		(PkWatch      *watch);
static void     pk_watch_finalize	(GObject       *object);

#define PK_WATCH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_WATCH, PkWatchPrivate))

struct PkWatchPrivate
{
	PkClient		*client;
	PkSmartIcon		*sicon;
	PkConnection		*pconnection;
	PkTaskList		*tlist;
	GConfClient		*gconf_client;
	DBusGProxy		*proxy_gpm;
	guint			 cookie;
};

G_DEFINE_TYPE (PkWatch, pk_watch, G_TYPE_OBJECT)

/**
 * pk_watch_class_init:
 * @klass: The PkWatchClass
 **/
static void
pk_watch_class_init (PkWatchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_watch_finalize;
	g_type_class_add_private (klass, sizeof (PkWatchPrivate));
}

/**
 * pk_watch_refresh_tooltip:
 **/
static gboolean
pk_watch_refresh_tooltip (PkWatch *watch)
{
	guint i;
	PkTaskListItem *item;
	guint length;
	GString *status;
	const gchar *localised_status;

	g_return_val_if_fail (watch != NULL, FALSE);
	g_return_val_if_fail (PK_IS_WATCH (watch), FALSE);

	length = pk_task_list_get_size (watch->priv->tlist);
	pk_debug ("refresh tooltip %i", length);
	if (length == 0) {
		pk_smart_icon_set_tooltip (watch->priv->sicon, "Doing nothing...");
		return TRUE;
	}
	status = g_string_new ("");
	for (i=0; i<length; i++) {
		item = pk_task_list_get_item (watch->priv->tlist, i);
		if (item == NULL) {
			pk_warning ("not found item %i", i);
			break;
		}
		localised_status = pk_status_enum_to_localised_text (item->status);

		/* ITS4: ignore, not used for allocation */
		if (strlen (item->package_id) == 0) {
			g_string_append_printf (status, "%s\n", localised_status);
		} else {
			PkPackageId *ident;
			/* display the package name, not the package_id */
			ident = pk_package_id_new_from_string (item->package_id);
			if (ident != NULL) {
				g_string_append_printf (status, "%s: %s\n", localised_status, ident->name);
			} else {
				g_string_append_printf (status, "%s: %s\n", localised_status, item->package_id);
			}
			pk_package_id_free (ident);
		}
	}
	if (status->len == 0) {
		g_string_append (status, "Doing something...");
	} else {
		g_string_set_size (status, status->len-1);
	}
	pk_smart_icon_set_tooltip (watch->priv->sicon, status->str);
	g_string_free (status, TRUE);
	return TRUE;
}

/**
 * pk_watch_refresh_icon:
 **/
static gboolean
pk_watch_refresh_icon (PkWatch *watch)
{
	pk_debug ("rescan");
	guint i;
	PkTaskListItem *item;
	PkStatusEnum state;
	guint length;
	gboolean state_install = FALSE;
	gboolean state_remove = FALSE;
	gboolean state_setup = FALSE;
	gboolean state_update = FALSE;
	gboolean state_download = FALSE;
	gboolean state_query = FALSE;
	gboolean state_refresh_cache = FALSE;
	gboolean state_wait = FALSE;
	const gchar *icon = NULL;

	g_return_val_if_fail (watch != NULL, FALSE);
	g_return_val_if_fail (PK_IS_WATCH (watch), FALSE);

	length = pk_task_list_get_size (watch->priv->tlist);
	if (length == 0) {
		pk_debug ("no activity");
		pk_smart_icon_set_icon_name (watch->priv->sicon, NULL);
		return TRUE;
	}
	for (i=0; i<length; i++) {
		item = pk_task_list_get_item (watch->priv->tlist, i);
		if (item == NULL) {
			pk_warning ("not found item %i", i);
			break;
		}
		state = item->status;
		pk_debug ("%s %s", item->tid, pk_status_enum_to_text (state));
		if (state == PK_STATUS_ENUM_SETUP) {
			state_setup = TRUE;
		} else if (state == PK_STATUS_ENUM_REFRESH_CACHE) {
			state_refresh_cache = TRUE;
		} else if (state == PK_STATUS_ENUM_QUERY) {
			state_query = TRUE;
		} else if (state == PK_STATUS_ENUM_REMOVE) {
			state_remove = TRUE;
		} else if (state == PK_STATUS_ENUM_DOWNLOAD) {
			state_download = TRUE;
		} else if (state == PK_STATUS_ENUM_INSTALL) {
			state_install = TRUE;
		} else if (state == PK_STATUS_ENUM_UPDATE) {
			state_update = TRUE;
		} else if (state == PK_STATUS_ENUM_WAIT) {
			state_wait = TRUE;
		}
	}
	/* in order of priority */
	if (state_refresh_cache == TRUE) {
		icon = pk_status_enum_to_icon_name (PK_STATUS_ENUM_REFRESH_CACHE);
	} else if (state_install == TRUE) {
		icon = pk_status_enum_to_icon_name (PK_STATUS_ENUM_INSTALL);
	} else if (state_remove == TRUE) {
		icon = pk_status_enum_to_icon_name (PK_STATUS_ENUM_REMOVE);
	} else if (state_setup == TRUE) {
		icon = pk_status_enum_to_icon_name (PK_STATUS_ENUM_SETUP);
	} else if (state_update == TRUE) {
		icon = pk_status_enum_to_icon_name (PK_STATUS_ENUM_UPDATE);
	} else if (state_download == TRUE) {
		icon = pk_status_enum_to_icon_name (PK_STATUS_ENUM_DOWNLOAD);
	} else if (state_query == TRUE) {
		icon = pk_status_enum_to_icon_name (PK_STATUS_ENUM_QUERY);
	} else if (state_wait == TRUE) {
		icon = pk_status_enum_to_icon_name (PK_STATUS_ENUM_WAIT);
	}
	pk_smart_icon_set_icon_name (watch->priv->sicon, icon);

	return TRUE;
}

/**
 * pk_watch_task_list_changed_cb:
 **/
static void
pk_watch_task_list_changed_cb (PkTaskList *tlist, PkWatch *watch)
{
	g_return_if_fail (watch != NULL);
	g_return_if_fail (PK_IS_WATCH (watch));

	pk_watch_refresh_icon (watch);
	pk_watch_refresh_tooltip (watch);
}

/**
 * pk_watch_task_list_finished_cb:
 **/
static void
pk_watch_task_list_finished_cb (PkTaskList *tlist, PkRoleEnum role, const gchar *package_id, guint runtime, PkWatch *watch)
{
	gboolean value;
	gchar *message = NULL;
	gchar *package;

	g_return_if_fail (watch != NULL);
	g_return_if_fail (PK_IS_WATCH (watch));

	pk_debug ("role=%s, package=%s", pk_role_enum_to_text (role), package_id);

	/* is it worth showing a UI? */
	if (runtime < 3) {
		pk_debug ("no notification, too quick");
		return;
	}

	/* are we accepting notifications */
	value = gconf_client_get_bool (watch->priv->gconf_client, PK_CONF_NOTIFY_COMPLETED, NULL);
	if (value == FALSE) {
		pk_debug ("not showing notification as prevented in gconf");
		return;
	}

	if (role == PK_ROLE_ENUM_REMOVE_PACKAGE) {
		package = pk_package_get_name (package_id);
		message = g_strdup_printf (_("Package '%s' has been removed"), package);
		g_free (package);
	} else if (role == PK_ROLE_ENUM_INSTALL_PACKAGE) {
		package = pk_package_get_name (package_id);
		message = g_strdup_printf (_("Package '%s' has been installed"), package);
		g_free (package);
	} else if (role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		message = g_strdup ("System has been updated");
	}

	/* nothing of interest */
	if (message == NULL) {
		return;
	}

	/* libnotify dialog */
	pk_smart_icon_notify (watch->priv->sicon, _("Task completed"), message,
			      "help-browser", PK_NOTIFY_URGENCY_LOW, 5000);
	g_free (message);
}

/**
 * pk_watch_task_list_error_code_cb:
 **/
static void
pk_watch_task_list_error_code_cb (PkTaskList *tlist, PkErrorCodeEnum error_code, const gchar *details, PkWatch *watch)
{
	const gchar *title;

	g_return_if_fail (watch != NULL);
	g_return_if_fail (PK_IS_WATCH (watch));

	title = pk_error_enum_to_localised_text (error_code);
	pk_smart_icon_notify (watch->priv->sicon, title, details, "help-browser", PK_NOTIFY_URGENCY_LOW, 5000);
}

/**
 * pk_watch_show_about_cb:
 **/
static void
pk_watch_show_about_cb (GtkMenuItem *item, gpointer data)
{
	const char *authors[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *documenters[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *artists[] = {
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
  	const char  *translators = _("translator-credits");
	char	    *license_trans;

	/* Translators comment: put your own name here to appear in the about dialog. */
  	if (!strcmp (translators, "translator-credits")) {
		translators = NULL;
	}

	license_trans = g_strconcat (_(license[0]), "\n\n", _(license[1]), "\n\n",
				     _(license[2]), "\n\n", _(license[3]), "\n",  NULL);

	gtk_window_set_default_icon_name ("system-installer");
	gtk_show_about_dialog (NULL,
			       "version", VERSION,
			       "copyright", "Copyright \xc2\xa9 2007 Richard Hughes",
			       "license", license_trans,
			       "website-label", _("PackageKit Website"),
			       "website", "www.packagekit.org",
			       "comments", "PackageKit",
			       "authors", authors,
			       "documenters", documenters,
			       "artists", artists,
			       "translator-credits", translators,
			       "logo-icon-name", "system-installer",
			       NULL);
	g_free (license_trans);
}

/**
 * pk_watch_popup_menu_cb:
 *
 * Display the popup menu.
 **/
static void
pk_watch_popup_menu_cb (GtkStatusIcon *status_icon,
			guint          button,
			guint32        timestamp,
			PkWatch       *watch)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;
	GtkWidget *image;

	g_return_if_fail (watch != NULL);
	g_return_if_fail (PK_IS_WATCH (watch));
	pk_debug ("icon right clicked");

	/* About */
	item = gtk_image_menu_item_new_with_mnemonic (_("_About"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (pk_watch_show_about_cb), watch);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* show the menu */
	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gtk_status_icon_position_menu, status_icon,
			button, timestamp);
	if (button == 0) {
		gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
	}
}

/**
 * pk_watch_not_supported:
 **/
static void
pk_watch_not_supported (PkWatch *watch, const gchar *title)
{
	g_return_if_fail (watch != NULL);
	g_return_if_fail (PK_IS_WATCH (watch));
	pk_debug ("not_supported");
	pk_smart_icon_notify (watch->priv->sicon, title,
			      _("The action could not be completed (the backend refusing the command)"),
			      "process-stop", PK_NOTIFY_URGENCY_LOW, 5000);
}

/**
 * pk_watch_refresh_cache_cb:
 **/
static void
pk_watch_refresh_cache_cb (GtkMenuItem *item, gpointer data)
{
	gboolean ret;
	PkWatch *watch = PK_WATCH (data);

	g_return_if_fail (watch != NULL);
	g_return_if_fail (PK_IS_WATCH (watch));

	pk_debug ("refresh cache");
	ret = pk_client_refresh_cache (watch->priv->client, TRUE);
	if (ret == FALSE) {
		pk_warning ("failed to refresh cache");
		pk_watch_not_supported (watch, _("Failed to refresh cache"));
	}
}

/**
 * pk_watch_manage_packages_cb:
 **/
static void
pk_watch_manage_packages_cb (GtkMenuItem *item, gpointer data)
{
	const gchar *command = "pk-application";
	if (g_spawn_command_line_async (command, NULL) == FALSE) {
		pk_warning ("Couldn't execute command: %s", command);
	}
}

/**
 * pk_monitor_action_unref_cb:
 **/
static void
pk_monitor_action_unref_cb (PkProgress *progress, PkWatch *watch)
{
	g_return_if_fail (watch != NULL);
	g_return_if_fail (PK_IS_WATCH (watch));

	g_object_unref (progress);
}

/**
 * pk_watch_menu_job_status_cb:
 **/
static void
pk_watch_menu_job_status_cb (GtkMenuItem *item, PkWatch *watch)
{
	gchar *tid;
	PkProgress *progress = NULL;

	g_return_if_fail (watch != NULL);
	g_return_if_fail (PK_IS_WATCH (watch));

	/* find the job we should bind to */
	tid = (gchar *) g_object_get_data (G_OBJECT (item), "tid");

	/* launch the UI */
	progress = pk_progress_new ();
	g_signal_connect (progress, "action-unref",
			  G_CALLBACK (pk_monitor_action_unref_cb), watch);
	pk_progress_monitor_tid (progress, tid);
}

/**
 * pk_watch_populate_menu_with_jobs:
 **/
static void
pk_watch_populate_menu_with_jobs (PkWatch *watch, GtkMenu *menu)
{
	guint i;
	PkTaskListItem *item;
	GtkWidget *widget;
	GtkWidget *image;
	const gchar *localised_status;
	const gchar *localised_role;
	const gchar *icon_name;
	gchar *package;
	gchar *text;
	guint length;

	g_return_if_fail (watch != NULL);
	g_return_if_fail (PK_IS_WATCH (watch));

	length = pk_task_list_get_size (watch->priv->tlist);
	if (length == 0) {
		return;
	}

	/* do a menu item for each job */
	for (i=0; i<length; i++) {
		item = pk_task_list_get_item (watch->priv->tlist, i);
		if (item == NULL) {
			pk_warning ("not found item %i", i);
			break;
		}
		localised_role = pk_role_enum_to_localised_present (item->role);
		localised_status = pk_status_enum_to_localised_text (item->status);

		icon_name = pk_status_enum_to_icon_name (item->status);
		if (item->package_id != NULL) {
			package = g_strdup (item->package_id);
			text = g_strdup_printf ("%s %s (%s)", localised_role, package, localised_status);
			g_free (package);
		} else {
			text = g_strdup_printf ("%s (%s)", localised_role, localised_status);
		}

		/* add a job */
		widget = gtk_image_menu_item_new_with_mnemonic (text);

		/* we need the job ID so we know what PkProgress to show */
		g_object_set_data (G_OBJECT (widget), "tid", (gpointer) item->tid);

		image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
		g_signal_connect (G_OBJECT (widget), "activate",
				  G_CALLBACK (pk_watch_menu_job_status_cb), watch);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
		g_free (text);
	}

	/* Separator for HIG? */
	widget = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
}

/**
 * pk_watch_activate_status_cb:
 * @button: Which buttons are pressed
 *
 * Callback when the icon is clicked
 **/
static void
pk_watch_activate_status_cb (GtkStatusIcon *status_icon,
			     PkWatch       *watch)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *widget;
	GtkWidget *image;

	g_return_if_fail (watch != NULL);
	g_return_if_fail (PK_IS_WATCH (watch));

	pk_debug ("icon left clicked");

	/* add jobs as drop down */
	pk_watch_populate_menu_with_jobs (watch, menu);

	/* force a refresh */
	widget = gtk_image_menu_item_new_with_mnemonic (_("_Refresh Software List"));
	image = gtk_image_new_from_icon_name ("view-refresh", GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
	g_signal_connect (G_OBJECT (widget), "activate",
			  G_CALLBACK (pk_watch_refresh_cache_cb), watch);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);

	/* manage packages */
	widget = gtk_image_menu_item_new_with_mnemonic (_("_Manage packages"));
	image = gtk_image_new_from_icon_name ("system-installer", GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
	g_signal_connect (G_OBJECT (widget), "activate",
			  G_CALLBACK (pk_watch_manage_packages_cb), watch);
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
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkWatch *watch)
{
	g_return_if_fail (watch != NULL);
	g_return_if_fail (PK_IS_WATCH (watch));
	pk_debug ("connected=%i", connected);
	if (connected == TRUE) {
		pk_watch_refresh_icon (watch);
		pk_watch_refresh_tooltip (watch);
	} else {
		pk_smart_icon_set_icon_name (watch->priv->sicon, NULL);
	}
}

/**
 * pk_watch_inhibit:
 **/
static gboolean
pk_watch_inhibit (PkWatch *watch)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (watch != NULL, FALSE);
	g_return_val_if_fail (PK_IS_WATCH (watch), FALSE);

	if (watch->priv->proxy_gpm == NULL) {
		pk_debug ("no connection to g-p-m");
		return FALSE;
	}

	/* check we are not trying to do this twice... */
	if (watch->priv->cookie != 0) {
		pk_debug ("cookie already set as %i", watch->priv->cookie);
		return FALSE;
	}

	/* coldplug the battery state */
	ret = dbus_g_proxy_call (watch->priv->proxy_gpm, "Inhibit", &error,
				 G_TYPE_STRING, _("Software Update Applet"),
				 G_TYPE_STRING, _("A transaction that cannot be interrupted is running"),
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &watch->priv->cookie,
				 G_TYPE_INVALID);
	if (error != NULL) {
		printf ("DEBUG: ERROR: %s\n", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_watch_uninhibit:
 **/
static gboolean
pk_watch_uninhibit (PkWatch *watch)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (watch != NULL, FALSE);
	g_return_val_if_fail (PK_IS_WATCH (watch), FALSE);

	if (watch->priv->proxy_gpm == NULL) {
		pk_debug ("no connection to g-p-m");
		return FALSE;
	}

	/* check we are not trying to do this twice... */
	if (watch->priv->cookie == 0) {
		pk_debug ("cookie not already set");
		return FALSE;
	}

	/* coldplug the battery state */
	ret = dbus_g_proxy_call (watch->priv->proxy_gpm, "UnInhibit", &error,
				 G_TYPE_UINT, watch->priv->cookie,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		printf ("DEBUG: ERROR: %s\n", error->message);
		g_error_free (error);
	}
	watch->priv->cookie = 0;
	return ret;
}

/**
 * pk_watch_locked_cb:
 **/
static void
pk_watch_locked_cb (PkClient *client, gboolean is_locked, PkWatch *watch)
{
	g_return_if_fail (watch != NULL);
	g_return_if_fail (PK_IS_WATCH (watch));

	pk_warning ("setting locked %i, doing g-p-m (un)inhibit", is_locked);
	if (is_locked == TRUE) {
		pk_watch_inhibit (watch);
	} else {
		pk_watch_uninhibit (watch);
	}
}

/**
 * pk_watch_init:
 * @watch: This class instance
 **/
static void
pk_watch_init (PkWatch *watch)
{
	DBusGConnection *connection;
	GError *error = NULL;
	GtkStatusIcon *status_icon;
	watch->priv = PK_WATCH_GET_PRIVATE (watch);

	watch->priv->cookie = 0;
	watch->priv->gconf_client = gconf_client_get_default ();
	watch->priv->sicon = pk_smart_icon_new ();

	/* we need to get ::locked */
	watch->priv->client = pk_client_new ();
	g_signal_connect (watch->priv->client, "locked",
			  G_CALLBACK (pk_watch_locked_cb), watch);

	/* connect to session bus */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (error != NULL) {
		pk_warning ("Cannot connect to session bus: %s", error->message);
		g_error_free (error);
		return;
	}

	/* use gnome-power-manager for the session inhibit stuff */
	watch->priv->proxy_gpm = dbus_g_proxy_new_for_name_owner (connection,
				  GPM_DBUS_SERVICE, GPM_DBUS_PATH_INHIBIT,
				  GPM_DBUS_INTERFACE_INHIBIT, &error);
	if (error != NULL) {
		pk_warning ("Cannot connect to gnome-power-manager: %s", error->message);
		g_error_free (error);
	}

	/* right click actions are common */
	status_icon = pk_smart_icon_get_status_icon (watch->priv->sicon);
	g_signal_connect_object (G_OBJECT (status_icon),
				 "popup_menu",
				 G_CALLBACK (pk_watch_popup_menu_cb),
				 watch, 0);
	g_signal_connect_object (G_OBJECT (status_icon),
				 "activate",
				 G_CALLBACK (pk_watch_activate_status_cb),
				 watch, 0);

	watch->priv->tlist = pk_task_list_new ();
	g_signal_connect (watch->priv->tlist, "task-list-changed",
			  G_CALLBACK (pk_watch_task_list_changed_cb), watch);
	g_signal_connect (watch->priv->tlist, "task-list-finished",
			  G_CALLBACK (pk_watch_task_list_finished_cb), watch);
	g_signal_connect (watch->priv->tlist, "error-code",
			  G_CALLBACK (pk_watch_task_list_error_code_cb), watch);

	watch->priv->pconnection = pk_connection_new ();
	g_signal_connect (watch->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), watch);
	if (pk_connection_valid (watch->priv->pconnection)) {
		pk_connection_changed_cb (watch->priv->pconnection, TRUE, watch);
	}
}

/**
 * pk_watch_finalize:
 * @object: The object to finalize
 **/
static void
pk_watch_finalize (GObject *object)
{
	PkWatch *watch;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_WATCH (object));

	watch = PK_WATCH (object);

	g_return_if_fail (watch->priv != NULL);
	g_object_unref (watch->priv->sicon);
	g_object_unref (watch->priv->tlist);
	g_object_unref (watch->priv->client);
	g_object_unref (watch->priv->pconnection);
	g_object_unref (watch->priv->gconf_client);
	g_object_unref (watch->priv->proxy_gpm);

	G_OBJECT_CLASS (pk_watch_parent_class)->finalize (object);
}

/**
 * pk_watch_new:
 *
 * Return value: a new PkWatch object.
 **/
PkWatch *
pk_watch_new (void)
{
	PkWatch *watch;
	watch = g_object_new (PK_TYPE_WATCH, NULL);
	return PK_WATCH (watch);
}

