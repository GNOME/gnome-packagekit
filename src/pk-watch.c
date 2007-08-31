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
#include <libnotify/notify.h>
#include <gtk/gtkstatusicon.h>

#include <pk-debug.h>
#include <pk-job-list.h>
#include <pk-task-client.h>
#include <pk-task-common.h>
#include <pk-task-list.h>
#include <pk-connection.h>

#include "pk-common.h"
#include "pk-watch.h"
#include "pk-progress.h"

static void     pk_watch_class_init	(PkWatchClass *klass);
static void     pk_watch_init		(PkWatch      *watch);
static void     pk_watch_finalize	(GObject       *object);

#define PK_WATCH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_WATCH, PkWatchPrivate))

#define PK_WATCH_ICON_STOCK	"system-installer"

struct PkWatchPrivate
{
	GtkStatusIcon		*status_icon;
	PkConnection		*pconnection;
	PkTaskList		*tlist;
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
 * pk_watch_set_icon:
 **/
static gboolean
pk_watch_set_icon (PkWatch *watch, const gchar *icon)
{
	g_return_val_if_fail (watch != NULL, FALSE);
	g_return_val_if_fail (PK_IS_WATCH (watch), FALSE);

	if (icon == NULL) {
		gtk_status_icon_set_visible (GTK_STATUS_ICON (watch->priv->status_icon), FALSE);
		return FALSE;
	}
	gtk_status_icon_set_from_icon_name (GTK_STATUS_ICON (watch->priv->status_icon), icon);
	gtk_status_icon_set_visible (GTK_STATUS_ICON (watch->priv->status_icon), TRUE);
	return TRUE;
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
	GPtrArray *array;
	GString *status;
	const gchar *localised_status;

	g_return_val_if_fail (watch != NULL, FALSE);
	g_return_val_if_fail (PK_IS_WATCH (watch), FALSE);

	array = pk_task_list_get_latest	(watch->priv->tlist);

	length = array->len;
	pk_debug ("refresh tooltip %i", length);
	if (length == 0) {
		gtk_status_icon_set_tooltip (GTK_STATUS_ICON (watch->priv->status_icon), "Doing nothing...");
		return TRUE;
	}
	status = g_string_new ("");
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (array, i);
		localised_status = pk_task_status_to_localised_text (item->status);
		if (item->package_id == NULL || strlen (item->package_id) == 0) {
			g_string_append_printf (status, "%s\n", localised_status);
		} else {
			g_string_append_printf (status, "%s: %s\n", localised_status, item->package_id);
		}
	}
	if (status->len == 0) {
		g_string_append (status, "Doing something...");
	} else {
		g_string_set_size (status, status->len-1);
	}
	gtk_status_icon_set_tooltip (GTK_STATUS_ICON (watch->priv->status_icon), status->str);
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
	PkTaskStatus state;
	guint length;
	GPtrArray *array;
	gboolean state_install = FALSE;
	gboolean state_remove = FALSE;
	gboolean state_setup = FALSE;
	gboolean state_update = FALSE;
	gboolean state_download = FALSE;
	gboolean state_query = FALSE;
	gboolean state_refresh_cache = FALSE;
	const gchar *icon = PK_WATCH_ICON_STOCK;

	g_return_val_if_fail (watch != NULL, FALSE);
	g_return_val_if_fail (PK_IS_WATCH (watch), FALSE);

	array = pk_task_list_get_latest	(watch->priv->tlist);

	length = array->len;
	if (length == 0) {
		pk_debug ("no activity");
		pk_watch_set_icon (watch, PK_WATCH_ICON_STOCK);
		return TRUE;
	}
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (array, i);
		state = item->status;
		pk_debug ("%i %s", item->job, pk_task_status_to_text (state));
		if (state == PK_TASK_STATUS_SETUP) {
			state_setup = TRUE;
		} else if (state == PK_TASK_STATUS_REFRESH_CACHE) {
			state_refresh_cache = TRUE;
		} else if (state == PK_TASK_STATUS_QUERY) {
			state_query = TRUE;
		} else if (state == PK_TASK_STATUS_REMOVE) {
			state_remove = TRUE;
		} else if (state == PK_TASK_STATUS_DOWNLOAD) {
			state_download = TRUE;
		} else if (state == PK_TASK_STATUS_INSTALL) {
			state_install = TRUE;
		} else if (state == PK_TASK_STATUS_UPDATE) {
			state_update = TRUE;
		}
	}
	/* in order of priority */
	if (state_refresh_cache == TRUE) {
		icon = pk_task_status_to_icon_name (PK_TASK_STATUS_REFRESH_CACHE);
	} else if (state_install == TRUE) {
		icon = pk_task_status_to_icon_name (PK_TASK_STATUS_INSTALL);
	} else if (state_remove == TRUE) {
		icon = pk_task_status_to_icon_name (PK_TASK_STATUS_REMOVE);
	} else if (state_setup == TRUE) {
		icon = pk_task_status_to_icon_name (PK_TASK_STATUS_SETUP);
	} else if (state_update == TRUE) {
		icon = pk_task_status_to_icon_name (PK_TASK_STATUS_UPDATE);
	} else if (state_download == TRUE) {
		icon = pk_task_status_to_icon_name (PK_TASK_STATUS_DOWNLOAD);
	} else if (state_query == TRUE) {
		icon = pk_task_status_to_icon_name (PK_TASK_STATUS_QUERY);
	}
	pk_watch_set_icon (watch, icon);

	return TRUE;
}

/**
 * pk_watch_task_list_changed_cb:
 **/
static void
pk_watch_task_list_changed_cb (PkTaskList *tlist, PkWatch *watch)
{
	pk_watch_refresh_icon (watch);
	pk_watch_refresh_tooltip (watch);
}

/**
 * pk_watch_task_list_finished_cb:
 **/
static void
pk_watch_task_list_finished_cb (PkTaskList *tlist, PkTaskStatus status, const gchar *package, guint runtime, PkWatch *watch)
{
	NotifyNotification *dialog;
	const gchar *title;
	gchar *message = NULL;

	pk_debug ("status=%i, package=%s", status, package);

	/* is it worth showing a UI? */
	if (runtime < 3) {
		pk_debug ("no libwatch, too quick");
		return;
	}

	if (status == PK_TASK_STATUS_REMOVE) {
		message = g_strdup_printf (_("Package '%s' has been removed"), package);
	} else if (status == PK_TASK_STATUS_INSTALL) {
		message = g_strdup_printf (_("Package '%s' has been installed"), package);
	} else if (status == PK_TASK_STATUS_UPDATE) {
		message = g_strdup ("System has been updated");
	}

	/* nothing of interest */
	if (message == NULL) {
		return;
	}
	title = _("Task completed");
	dialog = notify_notification_new_with_status_icon (title, message, "help-browser",
							   watch->priv->status_icon);
	notify_notification_set_timeout (dialog, 5000);
	notify_notification_set_urgency (dialog, NOTIFY_URGENCY_LOW);
	notify_notification_show (dialog, NULL);
	g_free (message);
}

/**
 * pk_watch_task_list_error_code_cb:
 **/
static void
pk_watch_task_list_error_code_cb (PkTaskList *tlist, PkTaskErrorCode error_code, const gchar *details, PkWatch *watch)
{
	NotifyNotification *dialog;
	const gchar *title;

	title = pk_task_error_code_to_localised_text (error_code);
	dialog = notify_notification_new_with_status_icon (title, details, "help-browser",
							   watch->priv->status_icon);
	notify_notification_set_timeout (dialog, 5000);
	notify_notification_set_urgency (dialog, NOTIFY_URGENCY_LOW);
	notify_notification_show (dialog, NULL);
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
			       "website", "www.hughsie.com",
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
			     PkWatch   *icon)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;
	GtkWidget *image;

	pk_debug ("icon right clicked");

	/* About */
	item = gtk_image_menu_item_new_with_mnemonic (_("_About"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (pk_watch_show_about_cb), icon);
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
	NotifyNotification *dialog;
	const gchar *message;

	pk_debug ("not_supported");
	message = _("The action could not be completed due to the backend refusing the command");
	dialog = notify_notification_new_with_status_icon (title, message, "process-stop",
							   watch->priv->status_icon);
	notify_notification_set_timeout (dialog, 5000);
	notify_notification_set_urgency (dialog, NOTIFY_URGENCY_LOW);
	notify_notification_show (dialog, NULL);
}

/**
 * pk_watch_refresh_cache_cb:
 **/
static void
pk_watch_refresh_cache_cb (GtkMenuItem *item, gpointer data)
{
	gboolean ret;
	PkTaskClient *tclient;
	PkWatch *watch = PK_WATCH (data);
	pk_debug ("refresh cache");

	tclient = pk_task_client_new ();
	ret = pk_task_client_refresh_cache (tclient, TRUE);
	if (ret == FALSE) {
		g_object_unref (tclient);
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
	g_object_unref (progress);
}

/**
 * pk_watch_menu_job_status_cb:
 **/
static void
pk_watch_menu_job_status_cb (GtkMenuItem *item, PkWatch *watch)
{
	guint job;
	PkProgress *progress = NULL;

	/* find the job we should bind to */
	job = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (item), "job"));

	/* launch the UI */
	progress = pk_progress_new ();
	g_signal_connect (progress, "action-unref",
			  G_CALLBACK (pk_monitor_action_unref_cb), watch);
	pk_progress_monitor_job (progress, job);
}

/**
 * pk_watch_populate_menu_with_jobs:
 **/
static void
pk_watch_populate_menu_with_jobs (PkWatch *watch, GtkMenu *menu)
{
	guint i;
	PkTaskListItem *item;
	GPtrArray *array;
	GtkWidget *widget;
	GtkWidget *image;
	const gchar *localised_status;
	const gchar *localised_role;
	const gchar *icon_name;
	gchar *package;
	gchar *text;

	array = pk_task_list_get_latest	(watch->priv->tlist);
	if (array->len == 0) {
		return;
	}

	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		localised_role = pk_task_role_to_localised_text (item->role);
		localised_status = pk_task_status_to_localised_text (item->status);

		icon_name = pk_task_status_to_icon_name (item->status);
		if (item->package_id != NULL) {
			package = g_strdup (item->package_id);
			text = g_strdup_printf ("%s %s (%s) [%i]", localised_role, package, localised_status, item->job);
			g_free (package);
		} else {
			text = g_strdup_printf ("%s (%s) [%i]", localised_role, localised_status, item->job);
		}

		/* add a job */
		widget = gtk_image_menu_item_new_with_mnemonic (text);

		/* we need the job ID so we know what PkProgress to show */
		g_object_set_data (G_OBJECT (widget), "job", GUINT_TO_POINTER (item->job));

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
			   PkWatch   *watch)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *widget;
	GtkWidget *image;

	pk_debug ("icon left clicked");

	/* add jobs as drop down */
	pk_watch_populate_menu_with_jobs (watch, menu);

	/* force a refresh */
	widget = gtk_image_menu_item_new_with_mnemonic (_("_Refresh cache"));
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
	pk_debug ("connected=%i", connected);
	if (connected == TRUE) {
		pk_watch_refresh_icon (watch);
		pk_watch_refresh_tooltip (watch);
	} else {
		pk_watch_set_icon (watch, NULL);
	}
}

/**
 * pk_watch_init:
 * @watch: This class instance
 **/
static void
pk_watch_init (PkWatch *watch)
{
	watch->priv = PK_WATCH_GET_PRIVATE (watch);

	watch->priv->status_icon = gtk_status_icon_new ();
	gtk_status_icon_set_visible (GTK_STATUS_ICON (watch->priv->status_icon), FALSE);

	/* right click actions are common */
	g_signal_connect_object (G_OBJECT (watch->priv->status_icon),
				 "popup_menu",
				 G_CALLBACK (pk_watch_popup_menu_cb),
				 watch, 0);
	g_signal_connect_object (G_OBJECT (watch->priv->status_icon),
				 "activate",
				 G_CALLBACK (pk_watch_activate_status_cb),
				 watch, 0);

	notify_init ("packagekit-update-applet");
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
	g_object_unref (watch->priv->status_icon);
	g_object_unref (watch->priv->tlist);
	g_object_unref (watch->priv->pconnection);

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

