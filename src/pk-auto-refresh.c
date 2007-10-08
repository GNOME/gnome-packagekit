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
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-connection.h>
#include "pk-common.h"
#include "pk-auto-refresh.h"

#define GS_LISTENER_SERVICE	"org.gnome.ScreenSaver"
#define GS_LISTENER_PATH	"/org/gnome/ScreenSaver"
#define GS_LISTENER_INTERFACE	"org.gnome.ScreenSaver"

//Monitor:
//* online (PkConnection)
//* idleness (GnomeScreensaver)
//* last update time (PkClient, pk_client_get_time_since_refresh)

static void     pk_auto_refresh_class_init	(PkAutoRefreshClass *klass);
static void     pk_auto_refresh_init		(PkAutoRefresh      *arefresh);
static void     pk_auto_refresh_finalize		(GObject       *object);

#define PK_AUTO_REFRESH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_AUTO_REFRESH, PkAutoRefreshPrivate))

struct PkAutoRefreshPrivate
{
	gboolean		 session_idle;
	gboolean		 connection_active;
	guint			 thresh;
	PkClient		*client;
	PkConnection		*connection;
};

enum {
	REFRESH_CACHE,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkAutoRefresh, pk_auto_refresh, G_TYPE_OBJECT)

/**
 * pk_auto_refresh_class_init:
 * @klass: The PkAutoRefreshClass
 **/
static void
pk_auto_refresh_class_init (PkAutoRefreshClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_auto_refresh_finalize;
	g_type_class_add_private (klass, sizeof (PkAutoRefreshPrivate));
	signals [REFRESH_CACHE] =
		g_signal_new ("refresh-cache",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * pk_auto_refresh_do_action:
 **/
static gboolean
pk_auto_refresh_do_action (PkAutoRefresh *arefresh)
{
	g_return_val_if_fail (arefresh != NULL, FALSE);
	g_return_val_if_fail (PK_IS_AUTO_REFRESH (arefresh), FALSE);

	pk_debug ("probably should refresh");
	g_signal_emit (arefresh, signals [REFRESH_CACHE], 0);
	return TRUE;
}

/**
 * pk_auto_refresh_change_state:
 **/
static gboolean
pk_auto_refresh_change_state (PkAutoRefresh *arefresh)
{
	guint time;

	g_return_val_if_fail (arefresh != NULL, FALSE);
	g_return_val_if_fail (PK_IS_AUTO_REFRESH (arefresh), FALSE);

	/* no point continuing if we have no network */
	if (arefresh->priv->connection_active == FALSE) {
		pk_debug ("not when no network");
		return FALSE;
	}

	/* get the time since the last refresh */
//	time = pk_client_get_time_since_refresh (arefresh->priv->client);
	time = 60*60;

	/* if we've waited a whole day to become idle then just force it
	 * we don't want a machine running at 100% to never get updates... */
	if (time > 60*60*24) {
		pk_debug ("been a long time since we refreshed");
		pk_auto_refresh_do_action (arefresh);
		return TRUE;
	}

	/* only do the refresh cache when the user is idle */
	if (arefresh->priv->session_idle == FALSE) {
		pk_debug ("not when session active");
		return FALSE;
	}

	/* if the user is bandwidth poor, we may want to wait longer */
	if (time < arefresh->priv->thresh) {
		pk_debug ("not when time less than refresh (%i<%i)", time, arefresh->priv->thresh);
		return FALSE;
	}

	/* we have satisfied all preconditions */
	pk_auto_refresh_do_action (arefresh);
	return TRUE;
}

/**
 * pk_auto_refresh_gnome_screensaver_idle_cb:
 **/
static void
pk_auto_refresh_gnome_screensaver_idle_cb (DBusGProxy *proxy, gboolean is_idle, PkAutoRefresh *arefresh)
{
	g_return_if_fail (arefresh != NULL);
	g_return_if_fail (PK_IS_AUTO_REFRESH (arefresh));

	arefresh->priv->session_idle = is_idle;
	pk_auto_refresh_change_state (arefresh);
}

/**
 * pk_auto_refresh_connection_changed_cb:
 **/
static void
pk_auto_refresh_connection_changed_cb (PkConnection *connection, gboolean connected, PkAutoRefresh *arefresh)
{
	g_return_if_fail (arefresh != NULL);
	g_return_if_fail (PK_IS_AUTO_REFRESH (arefresh));

	arefresh->priv->connection_active = connected;
	pk_auto_refresh_change_state (arefresh);
}

/**
 * pk_auto_refresh_timeout_cb:
 **/
static gboolean
pk_auto_refresh_timeout_cb (gpointer user_data)
{
	PkAutoRefresh *arefresh = PK_AUTO_REFRESH (user_data);

	g_return_val_if_fail (arefresh != NULL, FALSE);
	g_return_val_if_fail (PK_IS_AUTO_REFRESH (arefresh), FALSE);

	pk_auto_refresh_change_state (arefresh);

	/* always return */
	return TRUE;
}

/**
 * pk_auto_refresh_init:
 * @auto_refresh: This class instance
 **/
static void
pk_auto_refresh_init (PkAutoRefresh *arefresh)
{
	GError *error = NULL;
	DBusGConnection *connection;
	DBusGProxy *proxy;

	arefresh->priv = PK_AUTO_REFRESH_GET_PRIVATE (arefresh);
	arefresh->priv->session_idle = FALSE;
	arefresh->priv->connection_active = FALSE;
	arefresh->priv->thresh = 60;
	arefresh->priv->client = pk_client_new ();

	/* connect to system manager */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (error != NULL) {
		pk_warning ("Cannot connect to system bus: %s", error->message);
		g_error_free (error);
		return;
	}
	proxy = dbus_g_proxy_new_for_name_owner (connection,
				  GS_LISTENER_SERVICE, GS_LISTENER_PATH,
				  GS_LISTENER_INTERFACE, &error);
	if (error != NULL) {
		pk_warning ("Cannot connect to system manager: %s", error->message);
		g_error_free (error);
		return;
	}

	/* get SessionIdleChanged */
	dbus_g_proxy_add_signal (proxy, "SessionIdleChanged", G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "SessionIdleChanged",
				     G_CALLBACK (pk_auto_refresh_gnome_screensaver_idle_cb),
				     arefresh, NULL);

	/* we don't start the daemon for this, it's just a wrapper for
	 * NetworkManager or alternative */
	arefresh->priv->connection = pk_connection_new ();
	g_signal_connect (arefresh->priv->connection, "connection-changed",
			  G_CALLBACK (pk_auto_refresh_connection_changed_cb), arefresh);
	if (pk_connection_valid (arefresh->priv->connection) == TRUE) {
		arefresh->priv->connection_active = TRUE;
	}

	/* we check this in case we miss one of the async signals */
	g_timeout_add_seconds (60*60, pk_auto_refresh_timeout_cb, arefresh);

	/* do we do it now? */
	pk_auto_refresh_change_state (arefresh);
}

/**
 * pk_auto_refresh_finalize:
 * @object: The object to finalize
 **/
static void
pk_auto_refresh_finalize (GObject *object)
{
	PkAutoRefresh *arefresh;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_AUTO_REFRESH (object));

	arefresh = PK_AUTO_REFRESH (object);
	g_return_if_fail (arefresh->priv != NULL);

	g_object_unref (arefresh->priv->client);
	g_object_unref (arefresh->priv->connection);

	G_OBJECT_CLASS (pk_auto_refresh_parent_class)->finalize (object);
}

/**
 * pk_auto_refresh_new:
 *
 * Return value: a new PkAutoRefresh object.
 **/
PkAutoRefresh *
pk_auto_refresh_new (void)
{
	PkAutoRefresh *arefresh;
	arefresh = g_object_new (PK_TYPE_AUTO_REFRESH, NULL);
	return PK_AUTO_REFRESH (arefresh);
}

