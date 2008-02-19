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
#include <libnotify/notify.h>

#include <pk-debug.h>
#include <pk-enum.h>
#include "pk-marshal.h"
#include "pk-common-gui.h"
#include "pk-smart-icon.h"

static void     pk_smart_icon_class_init	(PkSmartIconClass *klass);
static void     pk_smart_icon_init		(PkSmartIcon      *sicon);
static void     pk_smart_icon_finalize		(GObject       *object);

#define PK_SMART_ICON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_SMART_ICON, PkSmartIconPrivate))
#define PK_SMART_ICON_PERSIST_TIMEOUT	100

struct PkSmartIconPrivate
{
	GtkStatusIcon		*status_icon;
	NotifyNotification	*dialog;
	gchar			*current;
	gchar			*new;
	gchar			*notify_data;
	guint			 event_source;
};

enum {
	NOTIFICATION_BUTTON,
	LAST_SIGNAL
};

G_DEFINE_TYPE (PkSmartIcon, pk_smart_icon, G_TYPE_OBJECT)

static guint signals [LAST_SIGNAL] = { 0, };

static PkEnumMatch enum_button_ids[] = {
	{PK_NOTIFY_BUTTON_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN,	"do-not-show-again"},
	{PK_NOTIFY_BUTTON_DO_NOT_WARN_AGAIN,	"do-not-warn-again"},
	{PK_NOTIFY_BUTTON_CANCEL_UPDATE,	"cancel-update"},
	{PK_NOTIFY_BUTTON_UPDATE_COMPUTER,	"update-computer"},
	{PK_NOTIFY_BUTTON_RESTART_COMPUTER,	"restart-computer"},
	{0, NULL},
};

/**
 * pk_smart_icon_class_init:
 * @klass: The PkSmartIconClass
 **/
static void
pk_smart_icon_class_init (PkSmartIconClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_smart_icon_finalize;
	g_type_class_add_private (klass, sizeof (PkSmartIconPrivate));
	signals [NOTIFICATION_BUTTON] =
		g_signal_new ("notification-button",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
}

static gboolean
pk_smart_icon_set_icon_name_cb (gpointer data)
{
	PkSmartIcon *sicon = (PkSmartIcon *) data;

	/* no point setting the same */
	if (sicon->priv->new != NULL &&
	    sicon->priv->current != NULL &&
	    strcmp (sicon->priv->new, sicon->priv->current) == 0) {
		return FALSE;
	}
	if (sicon->priv->new == NULL &&
	    sicon->priv->current == NULL) {
		return FALSE;
	}

	/* save what we have */
	g_free (sicon->priv->current);
	sicon->priv->current = g_strdup (sicon->priv->new);

	/* set the correct thing */
	if (sicon->priv->new == NULL) {
		gtk_status_icon_set_visible (GTK_STATUS_ICON (sicon->priv->status_icon), FALSE);
	} else {
		gtk_status_icon_set_from_icon_name (GTK_STATUS_ICON (sicon->priv->status_icon), sicon->priv->new);
		gtk_status_icon_set_visible (GTK_STATUS_ICON (sicon->priv->status_icon), TRUE);
	}
	return FALSE;
}

/**
 * pk_smart_icon_set_icon:
 **/
gboolean
pk_smart_icon_set_icon_name (PkSmartIcon *sicon, const gchar *icon_name)
{
	g_return_val_if_fail (sicon != NULL, FALSE);
	g_return_val_if_fail (PK_IS_SMART_ICON (sicon), FALSE);

	/* if we have a request pending, then cancel it in preference to this one */
	if (sicon->priv->event_source != 0) {
		g_source_remove (sicon->priv->event_source);
		sicon->priv->event_source = 0;
	}
	/* tell us what we -want- */
	g_free (sicon->priv->new);
	pk_debug ("setting icon name %s", icon_name);
	sicon->priv->new = g_strdup (icon_name);

	/* wait a little while to see if it's worth displaying the icon */
	sicon->priv->event_source = g_timeout_add (PK_SMART_ICON_PERSIST_TIMEOUT, pk_smart_icon_set_icon_name_cb, sicon);
	return TRUE;
}

/**
 * pk_smart_icon_sync:
 **/
gboolean
pk_smart_icon_sync (PkSmartIcon *sicon)
{
	g_return_val_if_fail (sicon != NULL, FALSE);
	g_return_val_if_fail (PK_IS_SMART_ICON (sicon), FALSE);

	/* if we have a request pending, then cancel it in preference to this one */
	if (sicon->priv->event_source != 0) {
		g_source_remove (sicon->priv->event_source);
		sicon->priv->event_source = 0;
	}

	/* sync the icon now */
	pk_smart_icon_set_icon_name_cb (sicon);

	/* wait until we are in the panel.
	 * We should probably use gtk_status_icon_is_embedded if it worked... */
	g_usleep (50000);

	return TRUE;
}

/**
 * pk_smart_icon_get_status_icon:
 **/
GtkStatusIcon *
pk_smart_icon_get_status_icon (PkSmartIcon *sicon)
{
	g_return_val_if_fail (sicon != NULL, NULL);
	g_return_val_if_fail (PK_IS_SMART_ICON (sicon), NULL);
	return sicon->priv->status_icon;
}

/**
 * pk_smart_icon_set_tooltip:
 **/
gboolean
pk_smart_icon_set_tooltip (PkSmartIcon *sicon, const gchar *tooltip)
{
	g_return_val_if_fail (sicon != NULL, FALSE);
	g_return_val_if_fail (PK_IS_SMART_ICON (sicon), FALSE);
	gtk_status_icon_set_tooltip (GTK_STATUS_ICON (sicon->priv->status_icon), tooltip);
	return TRUE;
}

/**
 * pk_smart_icon_notify:
 **/
gboolean
pk_smart_icon_notify_new (PkSmartIcon *sicon, const gchar *title, const gchar *message,
		      const gchar *icon, PkNotifyUrgency urgency, PkNotifyTimeout timeout)
{
	guint timeout_val = 0;

	g_return_val_if_fail (sicon != NULL, FALSE);
	g_return_val_if_fail (PK_IS_SMART_ICON (sicon), FALSE);

	/* default values */
	if (timeout == PK_NOTIFY_TIMEOUT_SHORT) {
		timeout_val = 5000;
	} else if (timeout == PK_NOTIFY_TIMEOUT_LONG) {
		timeout_val = 15000;
	}

	if (gtk_status_icon_get_visible (sicon->priv->status_icon) == TRUE) {
		sicon->priv->dialog = notify_notification_new_with_status_icon (title, message, icon, sicon->priv->status_icon);
	} else {
		sicon->priv->dialog = notify_notification_new (title, message, icon, NULL);
	}
	notify_notification_set_timeout (sicon->priv->dialog, timeout_val);
	notify_notification_set_urgency (sicon->priv->dialog, urgency);
	return TRUE;
}

/**
 * pk_smart_icon_libnotify_cb:
 **/
static void
pk_smart_icon_libnotify_cb (NotifyNotification *dialog, gchar *action, PkSmartIcon *sicon)
{
	PkNotifyButton button;

	g_return_if_fail (sicon != NULL);
	g_return_if_fail (PK_IS_SMART_ICON (sicon));

	/* get the value */
	button = pk_enum_find_value (enum_button_ids, action);

	/* send a signal with the type and data */
	pk_debug ("emit: %s with data %s", action, sicon->priv->notify_data);
	g_signal_emit (sicon, signals [NOTIFICATION_BUTTON], 0, button, sicon->priv->notify_data);
}

/**
 * pk_smart_icon_notify_button:
 **/
gboolean
pk_smart_icon_notify_button (PkSmartIcon *sicon, PkNotifyButton button, const gchar *data)
{
	const gchar *text = NULL;
	const gchar *id = NULL;

	g_return_val_if_fail (sicon != NULL, FALSE);
	g_return_val_if_fail (PK_IS_SMART_ICON (sicon), FALSE);

	/* get the id */
	id = pk_enum_find_string (enum_button_ids, button);

	/* find the localised text */
	if (button == PK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN) {
		text = _("Do not show this notification again");
	} else if (button == PK_NOTIFY_BUTTON_DO_NOT_WARN_AGAIN) {
		text = _("Do not warn me again");
	} else if (button == PK_NOTIFY_BUTTON_CANCEL_UPDATE) {
		text = _("Cancel system update");
	} else if (button == PK_NOTIFY_BUTTON_UPDATE_COMPUTER) {
		text = _("Update computer now");
	} else if (button == PK_NOTIFY_BUTTON_RESTART_COMPUTER) {
		text = _("Restart computer now");
	}

	/* save data privately, TODO: this really needs to be in a hashtable */
	sicon->priv->notify_data = g_strdup (data);

	/* add a button to the UI */
	notify_notification_add_action (sicon->priv->dialog, id, text, (NotifyActionCallback) pk_smart_icon_libnotify_cb, sicon, NULL);
	return FALSE;
}

/**
 * pk_smart_icon_notify_show:
 **/
gboolean
pk_smart_icon_notify_show (PkSmartIcon *sicon)
{
	GError *error = NULL;

	g_return_val_if_fail (sicon != NULL, FALSE);
	g_return_val_if_fail (PK_IS_SMART_ICON (sicon), FALSE);
	g_return_val_if_fail (sicon->priv->dialog != NULL, FALSE);

	notify_notification_close (sicon->priv->dialog, NULL);
	notify_notification_show (sicon->priv->dialog, &error);
	if (error != NULL) {
		pk_warning ("error: %s", error->message);
		g_error_free (error);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_smart_icon_notify_close:
 **/
gboolean
pk_smart_icon_notify_close (PkSmartIcon *sicon)
{
	g_return_val_if_fail (sicon != NULL, FALSE);
	g_return_val_if_fail (PK_IS_SMART_ICON (sicon), FALSE);
	notify_notification_close (sicon->priv->dialog, NULL);
	return TRUE;
}

/**
 * pk_smart_icon_init:
 * @smart_icon: This class instance
 **/
static void
pk_smart_icon_init (PkSmartIcon *sicon)
{
	sicon->priv = PK_SMART_ICON_GET_PRIVATE (sicon);
	sicon->priv->status_icon = gtk_status_icon_new ();
	sicon->priv->new = NULL;
	sicon->priv->current = NULL;
	sicon->priv->dialog = NULL;
	sicon->priv->notify_data = NULL;
	sicon->priv->event_source = 0;

	/* signal we are here... */
	notify_init ("packagekit");

	gtk_status_icon_set_visible (GTK_STATUS_ICON (sicon->priv->status_icon), FALSE);
}

/**
 * pk_smart_icon_finalize:
 * @object: The object to finalize
 **/
static void
pk_smart_icon_finalize (GObject *object)
{
	PkSmartIcon *sicon;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_SMART_ICON (object));

	sicon = PK_SMART_ICON (object);
	g_return_if_fail (sicon->priv != NULL);
	g_object_unref (sicon->priv->status_icon);
	g_object_unref (sicon->priv->new);
	g_object_unref (sicon->priv->current);
	if (sicon->priv->dialog != NULL) {
		notify_notification_close (sicon->priv->dialog, NULL);
		g_object_unref (sicon->priv->dialog);
	}
	if (sicon->priv->notify_data != NULL) {
		g_free (sicon->priv->notify_data);
	}

	G_OBJECT_CLASS (pk_smart_icon_parent_class)->finalize (object);
}

/**
 * pk_smart_icon_new:
 *
 * Return value: a new PkSmartIcon object.
 **/
PkSmartIcon *
pk_smart_icon_new (void)
{
	PkSmartIcon *sicon;
	sicon = g_object_new (PK_TYPE_SMART_ICON, NULL);
	return PK_SMART_ICON (sicon);
}

