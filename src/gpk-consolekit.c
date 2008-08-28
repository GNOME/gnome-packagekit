/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>
#include <glib/gi18n.h>
#include <string.h>
#include <unistd.h>
#include <dbus/dbus-glib.h>
#include <polkit-gnome/polkit-gnome.h>

#include "egg-debug.h"
#include <gpk-common.h>
#include <gpk-error.h>

/**
 * gpk_consolekit_try_system_restart:
 **/
static gboolean
gpk_consolekit_try_system_restart (DBusGProxy *proxy, GError **error)
{
	return dbus_g_proxy_call_with_timeout (proxy, "Restart", INT_MAX,
					       error, G_TYPE_INVALID, G_TYPE_INVALID);
}

/**
 * gpk_consolekit_get_action_from_error:
 **/
static PolKitAction *
gpk_consolekit_get_action_from_error (GError *error)
{
	PolKitAction *action;
	gchar *paction, *p;

	action = polkit_action_new ();

	paction = NULL;
	if (g_str_has_prefix (error->message, "Not privileged for action: ")) {
		paction = g_strdup (error->message + strlen ("Not privileged for action: "));
		p = strchr (paction, ' ');
		if (p)
			*p = '\0';
	}
	polkit_action_set_action_id (action, paction);

	g_free (paction);

	return action;
}

/**
 * gpk_consolekit_get_result_from_error:
 **/
static PolKitResult
gpk_consolekit_get_result_from_error (GError *error)
{
	PolKitResult result = POLKIT_RESULT_UNKNOWN;
	const char *p;

	p = strrchr (error->message, ' ');
	if (p) {
		p++;
		polkit_result_from_string_representation (p, &result);
	}

	return result;
}

/**
 * gpk_consolekit_system_restart_auth_cb:
 **/
static void
gpk_consolekit_system_restart_auth_cb (PolKitAction *action, gboolean gained_privilege,
				       GError *error, DBusGProxy *proxy)
{
	GError *local_error;
	gboolean res;

	if (!gained_privilege) {
		if (error != NULL) {
			egg_warning ("Not privileged to restart system: %s", error->message);
		}
		return;
	}

        local_error = NULL;
        res = gpk_consolekit_try_system_restart (proxy, &local_error);
        if (!res) {
                egg_warning ("Unable to restart system: %s", local_error->message);
                g_error_free (local_error);
        }
}

/**
 * gpk_consolekit_request_restart_priv:
 **/
static gboolean
gpk_consolekit_request_restart_priv (DBusGProxy *proxy, PolKitAction *action, GError **error)
{
        guint xid;
        pid_t pid;

        xid = 0;
        pid = getpid ();

	return polkit_gnome_auth_obtain (action, xid, pid,
					 (PolKitGnomeAuthCB) gpk_consolekit_system_restart_auth_cb,
					 proxy, error);
}

/**
 * gpk_restart_system:
 *
 * Return value: if we succeeded
 **/
gboolean
gpk_restart_system (void)
{
	DBusGProxy *proxy;
	DBusGConnection *connection;
	GError *error = NULL;
	gboolean ret;
	PolKitAction *action;
	PolKitAction *action2;
	PolKitResult result;

	/* check dbus connections, exit if not valid */
	connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("cannot acccess the system bus: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (connection,
					   "org.freedesktop.ConsoleKit",
					   "/org/freedesktop/ConsoleKit/Manager",
					   "org.freedesktop.ConsoleKit.Manager");
	if (proxy == NULL) {
		egg_warning ("Cannot connect to ConsoleKit");
		return FALSE;
	}

	/* do the method */
	ret = gpk_consolekit_try_system_restart (proxy, &error);
	if (!ret) {
		if (dbus_g_error_has_name (error, "org.freedesktop.ConsoleKit.Manager.NotPrivileged")) {
			action = gpk_consolekit_get_action_from_error (error);
			result = gpk_consolekit_get_result_from_error (error);

			if (result == POLKIT_RESULT_NO) {
				action2 = polkit_action_new ();
				polkit_action_set_action_id (action2,
							     "org.freedesktop.consolekit.system.restart-multiple-users");
				if (polkit_action_equal (action, action2)) {
					gpk_error_dialog (_("Failed to restart"),
							  _("You are not allowed to restart the computer "
							    "because multiple users are logged in"), NULL);
				}

				g_error_free (error);

				polkit_action_unref (action);
				polkit_action_unref (action2);

				return FALSE;
			}
			g_clear_error (&error);
			ret = gpk_consolekit_request_restart_priv (proxy, action, &error);
			polkit_action_unref (action);
		}
		if (!ret) {
			egg_warning ("Unable to restart system: %s", error->message);
			g_error_free (error);
		}
	}

	g_object_unref (G_OBJECT (proxy));

	return ret;
}

