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
#include <gtk/gtkstatusicon.h>
#include <libnotify/notify.h>
#include <gconf/gconf-client.h>

#include <pk-debug.h>
#include <pk-enum.h>
#include "gpk-marshal.h"
#include "gpk-common.h"
#include "gpk-notify.h"
#include "gpk-smart-icon.h"

static void     gpk_notify_class_init	(GpkNotifyClass *klass);
static void     gpk_notify_init		(GpkNotify      *notify);
static void     gpk_notify_finalize	(GObject        *object);

#define GPK_NOTIFY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_NOTIFY, GpkNotifyPrivate))
#define GPK_NOTIFY_PERSIST_TIMEOUT	100

struct GpkNotifyPrivate
{
	NotifyNotification	*notification;
	GConfClient		*gconf_client;
	gchar			*notify_data;
	gboolean		 has_gconf_check;
};

enum {
	NOTIFICATION_BUTTON,
	LAST_SIGNAL
};

G_DEFINE_TYPE (GpkNotify, gpk_notify, GTK_TYPE_STATUS_ICON)

static guint signals [LAST_SIGNAL] = { 0 };

static PkEnumMatch enum_button_ids[] = {
	{GPK_NOTIFY_BUTTON_UNKNOWN,		"unknown"},	/* fall though value */
	{GPK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN,	"do-not-show-again"},
	{GPK_NOTIFY_BUTTON_DO_NOT_WARN_AGAIN,	"do-not-warn-again"},
	{GPK_NOTIFY_BUTTON_CANCEL_UPDATE,	"cancel-update"},
	{GPK_NOTIFY_BUTTON_UPDATE_COMPUTER,	"update-computer"},
	{GPK_NOTIFY_BUTTON_RESTART_COMPUTER,	"restart-computer"},
	{GPK_NOTIFY_BUTTON_INSTALL_FIRMWARE,	"install-firmware"},
	{0, NULL}
};

/**
 * gpk_notify_class_init:
 * @klass: The GpkNotifyClass
 **/
static void
gpk_notify_class_init (GpkNotifyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_notify_finalize;
	g_type_class_add_private (klass, sizeof (GpkNotifyPrivate));
	signals [NOTIFICATION_BUTTON] =
		g_signal_new ("notification-button",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, gpk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
}

/**
 * gpk_notify_create:
 **/
gboolean
gpk_notify_create (GpkNotify *notify, const gchar *title, const gchar *message,
		   const gchar *icon, GpkNotifyUrgency urgency, GpkNotifyTimeout timeout)
{
	guint timeout_val = 0;

	g_return_val_if_fail (GPK_IS_NOTIFY (notify), FALSE);

	pk_debug ("Doing notification: %s, %s, %s", title, message, icon);

	/* no gconf to check */
	notify->priv->has_gconf_check = FALSE;

	/* default values */
	if (timeout == GPK_NOTIFY_TIMEOUT_SHORT) {
		timeout_val = 5000;
	} else if (timeout == GPK_NOTIFY_TIMEOUT_LONG) {
		timeout_val = 15000;
	}

	/* TODO: use the single icon */
	if (FALSE && gtk_status_icon_get_visible (GTK_STATUS_ICON (NULL))) {
		notify->priv->notification = notify_notification_new_with_status_icon (title, message, icon, GTK_STATUS_ICON (NULL));
	} else {
		notify->priv->notification = notify_notification_new (title, message, icon, NULL);
	}
	notify_notification_set_timeout (notify->priv->notification, timeout_val);
	notify_notification_set_urgency (notify->priv->notification, (NotifyUrgency) urgency);
	return TRUE;
}

/**
 * gpk_notify_libnotify_cb:
 **/
static void
gpk_notify_libnotify_cb (NotifyNotification *notification, gchar *action, GpkNotify *notify)
{
	GpkNotifyButton button;

	g_return_if_fail (GPK_IS_NOTIFY (notify));

	/* get the value */
	button = pk_enum_find_value (enum_button_ids, action);

	/* send a signal with the type and data */
	pk_debug ("emit: %s with data %s", action, notify->priv->notify_data);
	g_signal_emit (notify, signals [NOTIFICATION_BUTTON], 0, button, notify->priv->notify_data);
}

/**
 * gpk_notify_button:
 **/
gboolean
gpk_notify_button (GpkNotify *notify, GpkNotifyButton button, const gchar *data)
{
	const gchar *text = NULL;
	const gchar *id = NULL;

	g_return_val_if_fail (GPK_IS_NOTIFY (notify), FALSE);

	/* get the id */
	id = pk_enum_find_string (enum_button_ids, button);

	/* find the localised text */
	if (button == GPK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN) {
		text = _("Do not show this notification again");
		notify->priv->has_gconf_check = TRUE;
	} else if (button == GPK_NOTIFY_BUTTON_DO_NOT_WARN_AGAIN) {
		text = _("Do not warn me again");
		notify->priv->has_gconf_check = TRUE;
	} else if (button == GPK_NOTIFY_BUTTON_CANCEL_UPDATE) {
		text = _("Cancel system update");
	} else if (button == GPK_NOTIFY_BUTTON_UPDATE_COMPUTER) {
		text = _("Update computer now");
	} else if (button == GPK_NOTIFY_BUTTON_RESTART_COMPUTER) {
		text = _("Restart computer now");
	} else if (button == GPK_NOTIFY_BUTTON_INSTALL_FIRMWARE) {
		text = _("Install firmware");
	}

	/* save data privately, TODO: this really needs to be in a hashtable */
	notify->priv->notify_data = g_strdup (data);

	/* add a button to the UI */
	notify_notification_add_action (notify->priv->notification, id, text, (NotifyActionCallback) gpk_notify_libnotify_cb, notify, NULL);
	return FALSE;
}

/**
 * gpk_notify_show:
 * Return value: if the notification is being displayed
 *
 * This will show the notification previously setup with gpk_notify_create() and
 * gpk_notify_button().
 *
 * If you set a key using %GPK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN or
 * %GPK_NOTIFY_BUTTON_DO_NOT_WARN_AGAIN then this key will be checked before the notification is
 * shown.
 **/
gboolean
gpk_notify_show (GpkNotify *notify)
{
	GError *error = NULL;
	gboolean value;

	g_return_val_if_fail (GPK_IS_NOTIFY (notify), FALSE);
	g_return_val_if_fail (notify->priv->notification != NULL, FALSE);

	/* check the gconf key isn't set to ignore */
	if (notify->priv->has_gconf_check) {
		pk_debug ("key is %s", notify->priv->notify_data);
		/* are we accepting notifications */
		value = gconf_client_get_bool (notify->priv->gconf_client, notify->priv->notify_data, NULL);
		if (!value) {
			pk_debug ("not showing notification as prevented in gconf with %s", notify->priv->notify_data);
			return FALSE;
		}
	}

	notify_notification_close (notify->priv->notification, NULL);
	notify_notification_show (notify->priv->notification, &error);
	if (error != NULL) {
		pk_warning ("error: %s", error->message);
		g_error_free (error);
		return FALSE;
	}
	return TRUE;
}

/**
 * gpk_notify_close:
 **/
gboolean
gpk_notify_close (GpkNotify *notify)
{
	g_return_val_if_fail (GPK_IS_NOTIFY (notify), FALSE);
	notify_notification_close (notify->priv->notification, NULL);
	return TRUE;
}

/**
 * gpk_notify_init:
 * @smart_icon: This class instance
 **/
static void
gpk_notify_init (GpkNotify *notify)
{
	notify->priv = GPK_NOTIFY_GET_PRIVATE (notify);
	notify->priv->notification = NULL;
	notify->priv->notify_data = NULL;
	notify->priv->has_gconf_check = FALSE;
	notify->priv->gconf_client = gconf_client_get_default ();

	/* signal we are here... */
	notify_init ("packagekit");
}

/**
 * gpk_notify_finalize:
 * @object: The object to finalize
 **/
static void
gpk_notify_finalize (GObject *object)
{
	GpkNotify *notify;

	g_return_if_fail (GPK_IS_NOTIFY (object));

	notify = GPK_NOTIFY (object);
	g_return_if_fail (notify->priv != NULL);

	g_object_unref (notify->priv->gconf_client);

	if (notify->priv->notification != NULL) {
		notify_notification_close (notify->priv->notification, NULL);
		g_object_unref (notify->priv->notification);
	}
	if (notify->priv->notify_data != NULL) {
		g_free (notify->priv->notify_data);
	}

	G_OBJECT_CLASS (gpk_notify_parent_class)->finalize (object);
}

/**
 * gpk_notify_new:
 *
 * Return value: a new GpkNotify object.
 **/
GpkNotify *
gpk_notify_new (void)
{
	GpkNotify *notify;
	notify = g_object_new (GPK_TYPE_NOTIFY, NULL);
	return GPK_NOTIFY (notify);
}

