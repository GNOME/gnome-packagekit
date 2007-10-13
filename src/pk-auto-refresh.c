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
#include <pk-network.h>
#include "pk-common.h"
#include "pk-auto-refresh.h"

//Monitor:
//* online (PkNetwork)
//* idleness (GnomeScreensaver)
//* last update time (PkClient, pk_client_get_time_since_refresh)

static void     pk_auto_refresh_class_init	(PkAutoRefreshClass *klass);
static void     pk_auto_refresh_init		(PkAutoRefresh      *arefresh);
static void     pk_auto_refresh_finalize	(GObject            *object);

#define PK_AUTO_REFRESH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_AUTO_REFRESH, PkAutoRefreshPrivate))
#define PK_AUTO_REFRESH_PERIODIC_CHECK		60*60	/* force check for updates every this much time */
#define PK_AUTO_REFRESH_STARTUP_DELAY		60*2	/* seconds until the first refresh,
							 * and if we failed the first refresh,
							 * check after this much time also */

struct PkAutoRefreshPrivate
{
	gboolean		 session_idle;
	gboolean		 on_battery;
	gboolean		 network_active;
	gboolean		 session_delay;
	gboolean		 sent_get_updates;
	guint			 thresh;
	DBusGProxy		*proxy_gs;
	DBusGProxy		*proxy_gpm;
	PkClient		*client;
	PkNetwork		*network;
};

enum {
	REFRESH_CACHE,
	GET_UPDATES,
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
	signals [GET_UPDATES] =
		g_signal_new ("get-updates",
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

	pk_debug ("emitting refresh-cache");
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

	/* we shouldn't do this early in the session startup */
	if (arefresh->priv->session_delay == FALSE) {
		pk_debug ("not when this early in the session");
		return FALSE;
	}

	/* no point continuing if we have no network */
	if (arefresh->priv->network_active == FALSE) {
		pk_debug ("not when no network");
		return FALSE;
	}

	/* we do this to get an icon at startup */
	if (arefresh->priv->sent_get_updates == FALSE) {
		pk_debug ("emitting get-updates");
		g_signal_emit (arefresh, signals [GET_UPDATES], 0);
		arefresh->priv->sent_get_updates = TRUE;
		return TRUE;
	}

	/* no point continuing if we are on battery */
	if (arefresh->priv->on_battery == TRUE) {
		pk_debug ("not when on battery");
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
 * pk_auto_refresh_idle_cb:
 **/
static void
pk_auto_refresh_idle_cb (DBusGProxy *proxy, gboolean is_idle, PkAutoRefresh *arefresh)
{
	g_return_if_fail (arefresh != NULL);
	g_return_if_fail (PK_IS_AUTO_REFRESH (arefresh));

	pk_debug ("setting is_idle %i", is_idle);
	arefresh->priv->session_idle = is_idle;
	pk_auto_refresh_change_state (arefresh);
}

/**
 * pk_auto_refresh_on_battery_cb:
 **/
static void
pk_auto_refresh_on_battery_cb (DBusGProxy *proxy, gboolean on_battery, PkAutoRefresh *arefresh)
{
	g_return_if_fail (arefresh != NULL);
	g_return_if_fail (PK_IS_AUTO_REFRESH (arefresh));

	pk_debug ("setting on_battery %i", on_battery);
	arefresh->priv->on_battery = on_battery;
	pk_auto_refresh_change_state (arefresh);
}

/**
 * pk_auto_refresh_get_on_battery:
 **/
gboolean
pk_auto_refresh_get_on_battery (PkAutoRefresh *arefresh)
{
	g_return_val_if_fail (arefresh != NULL, FALSE);
	g_return_val_if_fail (PK_IS_AUTO_REFRESH (arefresh), FALSE);
	return arefresh->priv->on_battery;
}

/**
 * pk_auto_refresh_network_changed_cb:
 **/
static void
pk_auto_refresh_network_changed_cb (PkNetwork *network, gboolean online, PkAutoRefresh *arefresh)
{
	g_return_if_fail (arefresh != NULL);
	g_return_if_fail (PK_IS_AUTO_REFRESH (arefresh));

	pk_debug ("setting online %i", online);
	arefresh->priv->network_active = online;
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

	//FIXME: need to get the client state for this to work, for now, bodge
	//pk_auto_refresh_change_state (arefresh);
	pk_auto_refresh_do_action (arefresh); //FIXME: remove!

	/* always return */
	return TRUE;
}

/**
 * pk_auto_refresh_check_delay_cb:
 **/
static gboolean
pk_auto_refresh_check_delay_cb (gpointer user_data)
{
	gboolean ret;
	PkAutoRefresh *arefresh = PK_AUTO_REFRESH (user_data);

	g_return_val_if_fail (arefresh != NULL, FALSE);
	g_return_val_if_fail (PK_IS_AUTO_REFRESH (arefresh), FALSE);

	/* we have waited enough */
	if (arefresh->priv->session_delay == FALSE) {
		pk_debug ("setting session delay TRUE");
		arefresh->priv->session_delay = TRUE;
	}

	ret = pk_auto_refresh_change_state (arefresh);
	/* we failed to do the refresh cache at first boot. Keep trying... */
	if (ret == FALSE) {
		return TRUE;
	}

	/* we don't want to do this timer again as we sent the signal */
	return FALSE;
}

/**
 * pk_auto_refresh_init:
 * @auto_refresh: This class instance
 **/
static void
pk_auto_refresh_init (PkAutoRefresh *arefresh)
{
	DBusGConnection *network;
	GError *error = NULL;
	gboolean on_battery;
	gboolean ret;

	arefresh->priv = PK_AUTO_REFRESH_GET_PRIVATE (arefresh);
	arefresh->priv->on_battery = FALSE;
	arefresh->priv->session_idle = FALSE;
	arefresh->priv->network_active = FALSE;
	arefresh->priv->session_delay = FALSE;
	arefresh->priv->sent_get_updates = FALSE;

	/* need to get from gconf */
	arefresh->priv->thresh = 15*60;

	/* we need to query the last cache refresh time */
	arefresh->priv->client = pk_client_new ();

	/* connect to session bus */
	network = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (error != NULL) {
		pk_warning ("Cannot connect to session bus: %s", error->message);
		g_error_free (error);
		return;
	}

	/* use gnome-screensaver for the idle detection */
	arefresh->priv->proxy_gs = dbus_g_proxy_new_for_name_owner (network,
				  GS_DBUS_SERVICE, GS_DBUS_PATH,
				  GS_DBUS_INTERFACE, &error);
	if (error != NULL) {
		pk_warning ("Cannot connect to gnome-screensaver: %s", error->message);
		g_error_free (error);
		return;
	}
	/* get SessionIdleChanged */
	dbus_g_proxy_add_signal (arefresh->priv->proxy_gs, "SessionIdleChanged",
				 G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (arefresh->priv->proxy_gs, "SessionIdleChanged",
				     G_CALLBACK (pk_auto_refresh_idle_cb),
				     arefresh, NULL);

	/* use gnome-power-manager for the battery detection */
	arefresh->priv->proxy_gpm = dbus_g_proxy_new_for_name_owner (network,
				  GPM_DBUS_SERVICE, GPM_DBUS_PATH,
				  GPM_DBUS_INTERFACE, &error);
	if (error != NULL) {
		pk_warning ("Cannot connect to gnome-power-manager: %s", error->message);
		g_error_free (error);
		return;
	}
	/* get GetOnBattery */
	dbus_g_proxy_add_signal (arefresh->priv->proxy_gpm, "OnBatteryChanged",
				 G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (arefresh->priv->proxy_gpm, "OnBatteryChanged",
				     G_CALLBACK (pk_auto_refresh_on_battery_cb),
				     arefresh, NULL);

	/* coldplug the battery state */
	ret = dbus_g_proxy_call (arefresh->priv->proxy_gpm, "GetOnBattery", &error,
				 G_TYPE_INVALID,
				 G_TYPE_BOOLEAN, &on_battery,
				 G_TYPE_INVALID);
	if (error != NULL) {
		printf ("DEBUG: ERROR: %s\n", error->message);
		g_error_free (error);
	}
	if (ret == TRUE) {
		arefresh->priv->on_battery = on_battery;
		pk_debug ("setting on battery %i", on_battery);
	}

	/* we don't start the daemon for this, it's just a wrapper for
	 * NetworkManager or alternative */
	arefresh->priv->network = pk_network_new ();
	g_signal_connect (arefresh->priv->network, "online",
			  G_CALLBACK (pk_auto_refresh_network_changed_cb), arefresh);
	if (pk_network_is_online (arefresh->priv->network) == TRUE) {
		arefresh->priv->network_active = TRUE;
	}

	/* we check this in case we miss one of the async signals */
	g_timeout_add_seconds (PK_AUTO_REFRESH_PERIODIC_CHECK, pk_auto_refresh_timeout_cb, arefresh);

	/* wait a little bit for login to quiece, even if everything is okay */
	g_timeout_add_seconds (PK_AUTO_REFRESH_STARTUP_DELAY, pk_auto_refresh_check_delay_cb, arefresh);
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
	g_object_unref (arefresh->priv->network);
	g_object_unref (arefresh->priv->proxy_gs);
	g_object_unref (arefresh->priv->proxy_gpm);

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

