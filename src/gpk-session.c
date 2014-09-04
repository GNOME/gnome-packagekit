/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include "gpk-session.h"
#include "gpk-common.h"

static void     gpk_session_finalize   (GObject		*object);

#define GPK_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_SESSION, GpkSessionPrivate))

#define GPK_SESSION_MANAGER_SERVICE			"org.gnome.SessionManager"
#define GPK_SESSION_MANAGER_PATH			"/org/gnome/SessionManager"
#define GPK_SESSION_MANAGER_INTERFACE			"org.gnome.SessionManager"
#define GPK_SESSION_MANAGER_PRESENCE_PATH		"/org/gnome/SessionManager/Presence"
#define GPK_SESSION_MANAGER_PRESENCE_INTERFACE		"org.gnome.SessionManager.Presence"
#define GPK_SESSION_MANAGER_CLIENT_PRIVATE_INTERFACE	"org.gnome.SessionManager.ClientPrivate"
#define GPK_DBUS_PROPERTIES_INTERFACE			"org.freedesktop.DBus.Properties"

typedef enum {
	GPK_SESSION_STATUS_ENUM_AVAILABLE = 0,
	GPK_SESSION_STATUS_ENUM_INVISIBLE,
	GPK_SESSION_STATUS_ENUM_BUSY,
	GPK_SESSION_STATUS_ENUM_IDLE,
	GPK_SESSION_STATUS_ENUM_UNKNOWN
} GpkSessionStatusEnum;

typedef enum {
	GPK_SESSION_INHIBIT_MASK_LOGOUT = 1,
	GPK_SESSION_INHIBIT_MASK_SWITCH = 2,
	GPK_SESSION_INHIBIT_MASK_SUSPEND = 4,
	GPK_SESSION_INHIBIT_MASK_IDLE = 8
} GpkSessionInhibitMask;

struct GpkSessionPrivate
{
	DBusGProxy		*proxy;
	DBusGProxy		*proxy_presence;
	DBusGProxy		*proxy_client_private;
	DBusGProxy		*proxy_prop;
	gboolean		 is_idle_old;
	gboolean		 is_inhibited_old;
};

enum {
	IDLE_CHANGED,
	INHIBITED_CHANGED,
	STOP,
	QUERY_END_SESSION,
	END_SESSION,
	CANCEL_END_SESSION,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };
static gpointer gpk_session_object = NULL;

G_DEFINE_TYPE (GpkSession, gpk_session, G_TYPE_OBJECT)

/**
 * gpk_session_logout:
 **/
gboolean
gpk_session_logout (GpkSession *session)
{
	g_return_val_if_fail (GPK_IS_SESSION (session), FALSE);

	/* no gnome-session */
	if (session->priv->proxy == NULL) {
		g_warning ("no gnome-session");
		return FALSE;
	}

	/* we have to use no reply, as the SM calls into g-p-m to get the can_suspend property */
	dbus_g_proxy_call_no_reply (session->priv->proxy, "Logout",
				    G_TYPE_UINT, 1, /* no confirmation, but use inhibitors */
				    G_TYPE_INVALID);
	return TRUE;
}

/**
 * gpk_session_presence_status_changed_cb:
 **/
static void
gpk_session_presence_status_changed_cb (DBusGProxy *proxy, guint status, GpkSession *session)
{
	gboolean is_idle;
	is_idle = (status == GPK_SESSION_STATUS_ENUM_IDLE);
	if (is_idle != session->priv->is_idle_old) {
		g_debug ("emitting idle-changed : (%i)", is_idle);
		session->priv->is_idle_old = is_idle;
		g_signal_emit (session, signals [IDLE_CHANGED], 0, is_idle);
	}
}

/**
 * gpk_session_is_idle:
 **/
static gboolean
gpk_session_is_idle (GpkSession *session)
{
	gboolean ret;
	gboolean is_idle = FALSE;
	GError *error = NULL;
	GValue *value;

	/* no gnome-session */
	if (session->priv->proxy_prop == NULL) {
		g_warning ("no gnome-session");
		goto out;
	}

	value = g_new0(GValue, 1);
	/* find out if this change altered the inhibited state */
	ret = dbus_g_proxy_call (session->priv->proxy_prop, "Get", &error,
				 G_TYPE_STRING, GPK_SESSION_MANAGER_PRESENCE_INTERFACE,
				 G_TYPE_STRING, "status",
				 G_TYPE_INVALID,
				 G_TYPE_VALUE, value,
				 G_TYPE_INVALID);
	if (!ret) {
		g_warning ("failed to get idle status: %s", error->message);
		g_error_free (error);
		is_idle = FALSE;
		goto out;
	}
	is_idle = (g_value_get_uint (value) == GPK_SESSION_STATUS_ENUM_IDLE);
	g_free (value);
out:
	return is_idle;
}

/**
 * gpk_session_is_inhibited:
 **/
static gboolean
gpk_session_is_inhibited (GpkSession *session)
{
	gboolean ret;
	gboolean is_inhibited = FALSE;
	GError *error = NULL;

	/* no gnome-session */
	if (session->priv->proxy == NULL) {
		g_warning ("no gnome-session");
		goto out;
	}

	/* find out if this change altered the inhibited state */
	ret = dbus_g_proxy_call (session->priv->proxy, "IsInhibited", &error,
				 G_TYPE_UINT, GPK_SESSION_INHIBIT_MASK_IDLE,
				 G_TYPE_INVALID,
				 G_TYPE_BOOLEAN, &is_inhibited,
				 G_TYPE_INVALID);
	if (!ret) {
		g_warning ("failed to get inhibit status: %s", error->message);
		g_error_free (error);
		is_inhibited = FALSE;
	}
out:
	return is_inhibited;
}

/**
 * gpk_session_inhibit_changed_cb:
 **/
static void
gpk_session_inhibit_changed_cb (DBusGProxy *proxy, const gchar *id, GpkSession *session)
{
	gboolean is_inhibited;

	is_inhibited = gpk_session_is_inhibited (session);
	if (is_inhibited != session->priv->is_inhibited_old) {
		g_debug ("emitting inhibited-changed : (%i)", is_inhibited);
		session->priv->is_inhibited_old = is_inhibited;
		g_signal_emit (session, signals [INHIBITED_CHANGED], 0, is_inhibited);
	}
}

/**
 * gpk_session_class_init:
 * @klass: This class instance
 **/
static void
gpk_session_class_init (GpkSessionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_session_finalize;
	g_type_class_add_private (klass, sizeof (GpkSessionPrivate));

	signals [IDLE_CHANGED] =
		g_signal_new ("idle-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkSessionClass, idle_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [INHIBITED_CHANGED] =
		g_signal_new ("inhibited-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkSessionClass, inhibited_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [STOP] =
		g_signal_new ("stop",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkSessionClass, stop),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [QUERY_END_SESSION] =
		g_signal_new ("query-end-session",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkSessionClass, query_end_session),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [END_SESSION] =
		g_signal_new ("end-session",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkSessionClass, end_session),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [CANCEL_END_SESSION] =
		g_signal_new ("cancel-end-session",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkSessionClass, cancel_end_session),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * gpk_session_init:
 * @session: This class instance
 **/
static void
gpk_session_init (GpkSession *session)
{
	DBusGConnection *connection;
	GError *error = NULL;

	session->priv = GPK_SESSION_GET_PRIVATE (session);
	session->priv->is_idle_old = FALSE;
	session->priv->is_inhibited_old = FALSE;
	session->priv->proxy_client_private = NULL;

	connection = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);

	/* get org.gnome.Session interface */
	session->priv->proxy = dbus_g_proxy_new_for_name_owner (connection, GPK_SESSION_MANAGER_SERVICE,
								GPK_SESSION_MANAGER_PATH,
								GPK_SESSION_MANAGER_INTERFACE, &error);
	if (session->priv->proxy == NULL) {
		g_warning ("DBUS error: %s", error->message);
		g_error_free (error);
		return;
	}

	/* get org.gnome.Session.Presence interface */
	session->priv->proxy_presence = dbus_g_proxy_new_for_name_owner (connection, GPK_SESSION_MANAGER_SERVICE,
									 GPK_SESSION_MANAGER_PRESENCE_PATH,
									 GPK_SESSION_MANAGER_PRESENCE_INTERFACE, &error);
	if (session->priv->proxy_presence == NULL) {
		g_warning ("DBUS error: %s", error->message);
		g_error_free (error);
		return;
	}

	/* get properties interface */
	session->priv->proxy_prop = dbus_g_proxy_new_for_name_owner (connection, GPK_SESSION_MANAGER_SERVICE,
								     GPK_SESSION_MANAGER_PRESENCE_PATH,
								     GPK_DBUS_PROPERTIES_INTERFACE, &error);
	if (session->priv->proxy_prop == NULL) {
		g_warning ("DBUS error: %s", error->message);
		g_error_free (error);
		return;
	}

	/* get StatusChanged */
	dbus_g_proxy_add_signal (session->priv->proxy_presence, "StatusChanged", G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (session->priv->proxy_presence, "StatusChanged", G_CALLBACK (gpk_session_presence_status_changed_cb), session, NULL);

	/* get InhibitorAdded */
	dbus_g_proxy_add_signal (session->priv->proxy, "InhibitorAdded", DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (session->priv->proxy, "InhibitorAdded", G_CALLBACK (gpk_session_inhibit_changed_cb), session, NULL);

	/* get InhibitorRemoved */
	dbus_g_proxy_add_signal (session->priv->proxy, "InhibitorRemoved", DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (session->priv->proxy, "InhibitorRemoved", G_CALLBACK (gpk_session_inhibit_changed_cb), session, NULL);

	/* coldplug */
	session->priv->is_inhibited_old = gpk_session_is_inhibited (session);
	session->priv->is_idle_old = gpk_session_is_idle (session);
	g_debug ("idle: %i, inhibited: %i", session->priv->is_idle_old, session->priv->is_inhibited_old);
}

/**
 * gpk_session_finalize:
 * @object: This class instance
 **/
static void
gpk_session_finalize (GObject *object)
{
	GpkSession *session;
	g_return_if_fail (object != NULL);
	g_return_if_fail (GPK_IS_SESSION (object));

	session = GPK_SESSION (object);
	session->priv = GPK_SESSION_GET_PRIVATE (session);

	g_object_unref (session->priv->proxy);
	g_object_unref (session->priv->proxy_presence);
	if (session->priv->proxy_client_private != NULL)
		g_object_unref (session->priv->proxy_client_private);
	g_object_unref (session->priv->proxy_prop);

	G_OBJECT_CLASS (gpk_session_parent_class)->finalize (object);
}

/**
 * gpk_session_new:
 * Return value: new GpkSession instance.
 **/
GpkSession *
gpk_session_new (void)
{
	if (gpk_session_object != NULL) {
		g_object_ref (gpk_session_object);
	} else {
		gpk_session_object = g_object_new (GPK_TYPE_SESSION, NULL);
		g_object_add_weak_pointer (gpk_session_object, &gpk_session_object);
	}
	return GPK_SESSION (gpk_session_object);
}
