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
#include <dbus/dbus-glib.h>
#include <gconf/gconf-client.h>
#include <packagekit-glib/packagekit.h>
#include <devkit-power-gobject/devicekit-power.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-dbus-monitor.h"

#include "gpk-common.h"
#include "gpk-auto-refresh.h"
#include "gpk-enum.h"

static void     gpk_auto_refresh_finalize	(GObject            *object);

#define GPK_AUTO_REFRESH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_AUTO_REFRESH, GpkAutoRefreshPrivate))
#define GPK_AUTO_REFRESH_PERIODIC_CHECK		60*60	/* force check for updates every this much time */
#define GPK_UPDATES_LOGIN_TIMEOUT		3	/* seconds */

#define GS_DBUS_SERVICE				"org.gnome.ScreenSaver"
#define GS_DBUS_PATH				"/org/gnome/ScreenSaver"
#define GS_DBUS_INTERFACE			"org.gnome.ScreenSaver"

/*
 * at startup, after a small delay, force a GetUpdates call
 * every hour (or any event) check:
   - if we are online, idle and on AC power, it's been more than a day since we refreshed then RefreshCache
   - if we are online and it's been longer than the timeout since getting the updates period then GetUpdates
*/

struct GpkAutoRefreshPrivate
{
	gboolean		 session_idle;
	gboolean		 on_battery;
	gboolean		 network_active;
	gboolean		 force_get_updates_login;
	guint			 force_get_updates_login_timeout_id;
	guint			 timeout_id;
	EggDbusMonitor		*monitor_gs;
	DkpClient		*client;
	GConfClient		*gconf_client;
	DBusGProxy		*proxy_gs;
	DBusGConnection		*connection;
	PkControl		*control;
};

enum {
	REFRESH_CACHE,
	GET_UPDATES,
	GET_UPGRADES,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GpkAutoRefresh, gpk_auto_refresh, G_TYPE_OBJECT)

/**
 * gpk_auto_refresh_class_init:
 * @klass: The GpkAutoRefreshClass
 **/
static void
gpk_auto_refresh_class_init (GpkAutoRefreshClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_auto_refresh_finalize;
	g_type_class_add_private (klass, sizeof (GpkAutoRefreshPrivate));
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
	signals [GET_UPGRADES] =
		g_signal_new ("get-upgrades",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * gpk_auto_refresh_signal_refresh_cache:
 **/
static gboolean
gpk_auto_refresh_signal_refresh_cache (GpkAutoRefresh *arefresh)
{
	g_return_val_if_fail (GPK_IS_AUTO_REFRESH (arefresh), FALSE);

	egg_debug ("emitting refresh-cache");
	g_signal_emit (arefresh, signals [REFRESH_CACHE], 0);
	return TRUE;
}

/**
 * gpk_auto_refresh_signal_get_updates:
 **/
static gboolean
gpk_auto_refresh_signal_get_updates (GpkAutoRefresh *arefresh)
{
	g_return_val_if_fail (GPK_IS_AUTO_REFRESH (arefresh), FALSE);

	egg_debug ("emitting get-updates");
	g_signal_emit (arefresh, signals [GET_UPDATES], 0);
	return TRUE;
}

/**
 * gpk_auto_refresh_signal_get_upgrades:
 **/
static gboolean
gpk_auto_refresh_signal_get_upgrades (GpkAutoRefresh *arefresh)
{
	g_return_val_if_fail (GPK_IS_AUTO_REFRESH (arefresh), FALSE);

	egg_debug ("emitting get-upgrades");
	g_signal_emit (arefresh, signals [GET_UPGRADES], 0);
	return TRUE;
}

/**
 * gpk_auto_refresh_get_frequency_prefs:
 **/
static guint
gpk_auto_refresh_get_frequency_prefs (GpkAutoRefresh *arefresh, const gchar *key)
{
	guint value;

	g_return_val_if_fail (GPK_IS_AUTO_REFRESH (arefresh), 0);

	/* get from gconf */
	value = gconf_client_get_int (arefresh->priv->gconf_client, key, NULL);
	if (value == 0) {
		egg_warning ("no schema for %s, using daily", key);
		value = 86400;
	}

	/* convert to enum and get seconds */
	return value;
}

/**
 * gpk_auto_refresh_maybe_refresh_cache:
 **/
static gboolean
gpk_auto_refresh_maybe_refresh_cache (GpkAutoRefresh *arefresh)
{
	guint time_s;
	guint thresh;
	gboolean ret;

	g_return_val_if_fail (GPK_IS_AUTO_REFRESH (arefresh), FALSE);

	/* if we don't want to auto check for updates, don't do this either */
	thresh = gpk_auto_refresh_get_frequency_prefs (arefresh, GPK_CONF_FREQUENCY_GET_UPDATES);
	if (thresh == 0) {
		egg_debug ("not when policy is to never refresh");
		return FALSE;
	}

	/* not on battery */
	if (arefresh->priv->on_battery) {
		egg_debug ("not when on battery");
		return FALSE;
	}

	/* only do the refresh cache when the user is idle */
	if (!arefresh->priv->session_idle) {
		egg_debug ("not when session active");
		return FALSE;
	}

	/* get this each time, as it may have changed behind out back */
	thresh = gpk_auto_refresh_get_frequency_prefs (arefresh, GPK_CONF_FREQUENCY_REFRESH_CACHE);
	if (thresh == 0) {
		egg_debug ("not when policy is to never refresh");
		return FALSE;
	}

	/* get the time since the last refresh */
	ret = pk_control_get_time_since_action (arefresh->priv->control,
						PK_ROLE_ENUM_REFRESH_CACHE, &time_s, NULL);
	if (!ret) {
		egg_warning ("failed to get last time");
		return FALSE;
	}

	/* have we passed the timout? */
	if (time_s < thresh) {
		egg_debug ("not before timeout, thresh=%u, now=%u", thresh, time_s);
		return FALSE;
	}

	gpk_auto_refresh_signal_refresh_cache (arefresh);
	return TRUE;
}

/**
 * gpk_auto_refresh_maybe_get_updates:
 **/
static gboolean
gpk_auto_refresh_maybe_get_updates (GpkAutoRefresh *arefresh)
{
	guint time_s;
	guint thresh;
	gboolean ret;

	g_return_val_if_fail (GPK_IS_AUTO_REFRESH (arefresh), FALSE);

	if (!arefresh->priv->force_get_updates_login) {
		arefresh->priv->force_get_updates_login = TRUE;
		if (gconf_client_get_bool (arefresh->priv->gconf_client, GPK_CONF_FORCE_GET_UPDATES_LOGIN, NULL)) {
			egg_debug ("forcing get update due to GConf");
			gpk_auto_refresh_signal_get_updates (arefresh);
			return TRUE;
		}
	}

	/* get this each time, as it may have changed behind out back */
	thresh = gpk_auto_refresh_get_frequency_prefs (arefresh, GPK_CONF_FREQUENCY_GET_UPDATES);
	if (thresh == 0) {
		egg_debug ("not when policy is set to never get updates");
		return FALSE;
	}

	/* get the time since the last refresh */
	ret = pk_control_get_time_since_action (arefresh->priv->control,
						PK_ROLE_ENUM_GET_UPDATES, &time_s, NULL);
	if (!ret) {
		egg_warning ("failed to get last time");
		return FALSE;
	}

	/* have we passed the timout? */
	if (time_s < thresh) {
		egg_debug ("not before timeout, thresh=%u, now=%u", thresh, time_s);
		return FALSE;
	}

	gpk_auto_refresh_signal_get_updates (arefresh);
	return TRUE;
}

/**
 * gpk_auto_refresh_maybe_get_upgrades:
 **/
static gboolean
gpk_auto_refresh_maybe_get_upgrades (GpkAutoRefresh *arefresh)
{
	guint time_s;
	guint thresh;
	gboolean ret;

	g_return_val_if_fail (GPK_IS_AUTO_REFRESH (arefresh), FALSE);

	/* get this each time, as it may have changed behind out back */
	thresh = gpk_auto_refresh_get_frequency_prefs (arefresh, GPK_CONF_FREQUENCY_GET_UPGRADES);
	if (thresh == 0) {
		egg_debug ("not when policy is set to never check for upgrades");
		return FALSE;
	}

	/* get the time since the last refresh */
	ret = pk_control_get_time_since_action (arefresh->priv->control,
						PK_ROLE_ENUM_GET_DISTRO_UPGRADES, &time_s, NULL);
	if (!ret) {
		egg_debug ("failed to get last time");
		return FALSE;
	}

	/* have we passed the timout? */
	if (time_s < thresh) {
		egg_debug ("not before timeout, thresh=%u, now=%u", thresh, time_s);
		return FALSE;
	}

	gpk_auto_refresh_signal_get_upgrades (arefresh);
	return TRUE;
}

/**
 * gpk_auto_refresh_change_state_cb:
 **/
static gboolean
gpk_auto_refresh_change_state_cb (GpkAutoRefresh *arefresh)
{
	/* check all actions */
	gpk_auto_refresh_maybe_refresh_cache (arefresh);
	gpk_auto_refresh_maybe_get_updates (arefresh);
	gpk_auto_refresh_maybe_get_upgrades (arefresh);
	return FALSE;
}

/**
 * gpk_auto_refresh_maybe_get_updates_logon_cb:
 **/
static gboolean
gpk_auto_refresh_maybe_get_updates_logon_cb (GpkAutoRefresh *arefresh)
{
	gpk_auto_refresh_maybe_get_updates (arefresh);
	/* never repeat, even if failure */
	return FALSE;
}

/**
 * gpk_auto_refresh_change_state:
 **/
static gboolean
gpk_auto_refresh_change_state (GpkAutoRefresh *arefresh)
{
	gboolean force;
	guint value;

	g_return_val_if_fail (GPK_IS_AUTO_REFRESH (arefresh), FALSE);

	/* no point continuing if we have no network */
	if (!arefresh->priv->network_active) {
		egg_debug ("not when no network");
		return FALSE;
	}

	/* only force a check if the user REALLY, REALLY wants to break
	 * set policy and have an update at startup */
	if (!arefresh->priv->force_get_updates_login) {
		force = gconf_client_get_bool (arefresh->priv->gconf_client, GPK_CONF_FORCE_GET_UPDATES_LOGIN, NULL);
		if (force) {
			/* don't immediately send the signal, if we are called during object initialization
			 * we need to wait until upper layers  finish hooking up to the signal first. */
			if (arefresh->priv->force_get_updates_login_timeout_id == 0)
				arefresh->priv->force_get_updates_login_timeout_id =
					g_timeout_add_seconds (GPK_UPDATES_LOGIN_TIMEOUT, (GSourceFunc) gpk_auto_refresh_maybe_get_updates_logon_cb, arefresh);
		}
	}

	/* wait a little time for things to settle down */
	if (arefresh->priv->timeout_id != 0)
		g_source_remove (arefresh->priv->timeout_id);
	value = gconf_client_get_int (arefresh->priv->gconf_client, GPK_CONF_SESSION_STARTUP_TIMEOUT, NULL);
	egg_debug ("defering action for %i seconds", value);
	arefresh->priv->timeout_id = g_timeout_add_seconds (value, (GSourceFunc) gpk_auto_refresh_change_state_cb, arefresh);

	return TRUE;
}

/**
 * gpk_auto_refresh_gconf_key_changed_cb:
 *
 * We might have to do things when the gconf keys change; do them here.
 **/
static void
gpk_auto_refresh_gconf_key_changed_cb (GConfClient *client, guint cnxn_id, GConfEntry *entry, GpkAutoRefresh *arefresh)
{
	g_return_if_fail (GPK_IS_AUTO_REFRESH (arefresh));
	if (egg_strequal (entry->key, GPK_CONF_SESSION_STARTUP_TIMEOUT) ||
	    egg_strequal (entry->key, GPK_CONF_FORCE_GET_UPDATES_LOGIN) ||
	    egg_strequal (entry->key, GPK_CONF_FREQUENCY_GET_UPDATES) ||
	    egg_strequal (entry->key, GPK_CONF_FREQUENCY_GET_UPGRADES) ||
	    egg_strequal (entry->key, GPK_CONF_FREQUENCY_REFRESH_CACHE) ||
	    egg_strequal (entry->key, GPK_CONF_AUTO_UPDATE) ||
	    egg_strequal (entry->key, GPK_CONF_UPDATE_BATTERY))
		gpk_auto_refresh_change_state (arefresh);
}

/**
 * gpk_auto_refresh_idle_cb:
 **/
static void
gpk_auto_refresh_idle_cb (DBusGProxy *proxy, gboolean is_idle, GpkAutoRefresh *arefresh)
{
	g_return_if_fail (GPK_IS_AUTO_REFRESH (arefresh));

	egg_debug ("setting is_idle %i", is_idle);
	arefresh->priv->session_idle = is_idle;
	if (arefresh->priv->session_idle)
		gpk_auto_refresh_change_state (arefresh);
}

/**
 * gpk_auto_refresh_get_on_battery:
 **/
gboolean
gpk_auto_refresh_get_on_battery (GpkAutoRefresh *arefresh)
{
	g_return_val_if_fail (GPK_IS_AUTO_REFRESH (arefresh), FALSE);
	return arefresh->priv->on_battery;
}

/**
 * gpk_auto_refresh_convert_network_state:
 **/
static gboolean
gpk_auto_refresh_convert_network_state (GpkAutoRefresh *arefresh, PkNetworkEnum state)
{
	/* offline */
	if (state == PK_NETWORK_ENUM_OFFLINE)
		return FALSE;

	/* online */
	if (state == PK_NETWORK_ENUM_ONLINE ||
	    state == PK_NETWORK_ENUM_WIRED)
		return TRUE;

	/* check policy */
	if (state == PK_NETWORK_ENUM_MOBILE)
		return gconf_client_get_bool (arefresh->priv->gconf_client, GPK_CONF_CONNECTION_USE_MOBILE, NULL);

	/* check policy */
	if (state == PK_NETWORK_ENUM_WIFI)
		return gconf_client_get_bool (arefresh->priv->gconf_client, GPK_CONF_CONNECTION_USE_WIFI, NULL);

	/* not recognised */
	egg_warning ("state unknown: %i", state);
	return TRUE;
}

/**
 * gpk_auto_refresh_network_status_changed_cb:
 **/
static void
gpk_auto_refresh_network_status_changed_cb (PkControl *control, PkNetworkEnum state, GpkAutoRefresh *arefresh)
{
	g_return_if_fail (GPK_IS_AUTO_REFRESH (arefresh));

	arefresh->priv->network_active = gpk_auto_refresh_convert_network_state (arefresh, state);
	egg_debug ("setting online %i", arefresh->priv->network_active);
	if (arefresh->priv->network_active)
		gpk_auto_refresh_change_state (arefresh);
}

/**
 * gpk_auto_refresh_timeout_cb:
 **/
static gboolean
gpk_auto_refresh_timeout_cb (gpointer user_data)
{
	GpkAutoRefresh *arefresh = GPK_AUTO_REFRESH (user_data);

	g_return_val_if_fail (GPK_IS_AUTO_REFRESH (arefresh), FALSE);

	/* debug so we can catch polling */
	egg_debug ("polling check");

	/* triggered once an hour */
	gpk_auto_refresh_change_state (arefresh);

	/* always return */
	return TRUE;
}

/**
 * pk_connection_gs_changed_cb:
 **/
static void
pk_connection_gs_changed_cb (EggDbusMonitor *egg_dbus_monitor, gboolean connected, GpkAutoRefresh *arefresh)
{
	GError *error = NULL;

	g_return_if_fail (GPK_IS_AUTO_REFRESH (arefresh));

	egg_debug ("gnome-screensaver connection-changed: %i", connected);

	/* is this valid? */
	if (!connected) {
		if (arefresh->priv->proxy_gs != NULL) {
			g_object_unref (arefresh->priv->proxy_gs);
			arefresh->priv->proxy_gs = NULL;
		}
		return;
	}

	/* use gnome-screensaver for the idle detection */
	arefresh->priv->proxy_gs = dbus_g_proxy_new_for_name_owner (arefresh->priv->connection,
					  GS_DBUS_SERVICE, GS_DBUS_PATH, GS_DBUS_INTERFACE, &error);
	if (error != NULL) {
		egg_warning ("Cannot connect to gnome-screensaver: %s", error->message);
		g_error_free (error);
		return;
	}
	/* get SessionIdleChanged */
	dbus_g_proxy_add_signal (arefresh->priv->proxy_gs, "SessionIdleChanged",
				 G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (arefresh->priv->proxy_gs, "SessionIdleChanged",
				     G_CALLBACK (gpk_auto_refresh_idle_cb),
				     arefresh, NULL);

}

/**
 * gpk_auto_refresh_client_changed_cb:
 **/
static void
gpk_auto_refresh_client_changed_cb (DkpClient *client, GpkAutoRefresh *arefresh)
{
	gboolean on_battery;

	g_return_if_fail (GPK_IS_AUTO_REFRESH (arefresh));

	/* get the on-battery state */
	on_battery = dkp_client_on_battery (arefresh->priv->client);
	if (on_battery == arefresh->priv->on_battery) {
		egg_debug ("same state as before, ignoring");
		return;
	}

	/* save in local cache */
	egg_debug ("setting on_battery %i", on_battery);
	arefresh->priv->on_battery = on_battery;
	if (!on_battery)
		gpk_auto_refresh_change_state (arefresh);
}

/**
 * gpk_auto_refresh_init:
 * @auto_refresh: This class instance
 **/
static void
gpk_auto_refresh_init (GpkAutoRefresh *arefresh)
{
	GError *error = NULL;
	PkNetworkEnum state;

	arefresh->priv = GPK_AUTO_REFRESH_GET_PRIVATE (arefresh);
	arefresh->priv->on_battery = FALSE;
	arefresh->priv->session_idle = FALSE;
	arefresh->priv->network_active = FALSE;
	arefresh->priv->force_get_updates_login = FALSE;
	arefresh->priv->timeout_id = 0;
	arefresh->priv->force_get_updates_login_timeout_id = 0;
	arefresh->priv->proxy_gs = NULL;

	/* we need to know the updates frequency */
	arefresh->priv->gconf_client = gconf_client_get_default ();

	/* watch gnome-packagekit keys */
	gconf_client_add_dir (arefresh->priv->gconf_client, GPK_CONF_DIR,
			      GCONF_CLIENT_PRELOAD_NONE, NULL);
	gconf_client_notify_add (arefresh->priv->gconf_client, GPK_CONF_DIR,
				 (GConfClientNotifyFunc) gpk_auto_refresh_gconf_key_changed_cb,
				 arefresh, NULL, NULL);

	/* we need to query the last cache refresh time */
	arefresh->priv->control = pk_control_new ();
	g_signal_connect (arefresh->priv->control, "network-state-changed",
			  G_CALLBACK (gpk_auto_refresh_network_status_changed_cb), arefresh);
	state = pk_control_get_network_state (arefresh->priv->control, NULL);
	arefresh->priv->network_active = gpk_auto_refresh_convert_network_state (arefresh, state);

	/* connect to session bus */
	arefresh->priv->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (error != NULL) {
		egg_warning ("Cannot connect to session bus: %s", error->message);
		g_error_free (error);
		return;
	}

	/* use a DkpClient */
	arefresh->priv->client = dkp_client_new ();
	g_signal_connect (arefresh->priv->client, "changed",
			  G_CALLBACK (gpk_auto_refresh_client_changed_cb), arefresh);

	/* get the battery state */
	arefresh->priv->on_battery = dkp_client_on_battery (arefresh->priv->client);
	egg_debug ("setting on battery %i", arefresh->priv->on_battery);

	/* watch gnome-screensaver */
	arefresh->priv->monitor_gs = egg_dbus_monitor_new ();
	g_signal_connect (arefresh->priv->monitor_gs, "connection-changed",
			  G_CALLBACK (pk_connection_gs_changed_cb), arefresh);
	egg_dbus_monitor_assign (arefresh->priv->monitor_gs, EGG_DBUS_MONITOR_SESSION, GS_DBUS_SERVICE);

	/* we check this in case we miss one of the async signals */
	g_timeout_add_seconds (GPK_AUTO_REFRESH_PERIODIC_CHECK, gpk_auto_refresh_timeout_cb, arefresh);

	/* check system state */
	gpk_auto_refresh_change_state (arefresh);
}

/**
 * gpk_auto_refresh_finalize:
 * @object: The object to finalize
 **/
static void
gpk_auto_refresh_finalize (GObject *object)
{
	GpkAutoRefresh *arefresh;

	g_return_if_fail (GPK_IS_AUTO_REFRESH (object));

	arefresh = GPK_AUTO_REFRESH (object);
	g_return_if_fail (arefresh->priv != NULL);

	if (arefresh->priv->timeout_id != 0)
		g_source_remove (arefresh->priv->timeout_id);
	if (arefresh->priv->force_get_updates_login_timeout_id != 0)
		g_source_remove (arefresh->priv->force_get_updates_login_timeout_id);

	g_object_unref (arefresh->priv->control);
	g_object_unref (arefresh->priv->monitor_gs);
	g_object_unref (arefresh->priv->gconf_client);
	g_object_unref (arefresh->priv->client);

	/* only unref the proxies if they were ever set */
	if (arefresh->priv->proxy_gs != NULL)
		g_object_unref (arefresh->priv->proxy_gs);

	G_OBJECT_CLASS (gpk_auto_refresh_parent_class)->finalize (object);
}

/**
 * gpk_auto_refresh_new:
 *
 * Return value: a new GpkAutoRefresh object.
 **/
GpkAutoRefresh *
gpk_auto_refresh_new (void)
{
	GpkAutoRefresh *arefresh;
	arefresh = g_object_new (GPK_TYPE_AUTO_REFRESH, NULL);
	return GPK_AUTO_REFRESH (arefresh);
}

