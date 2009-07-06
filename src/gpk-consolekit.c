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

#include "egg-debug.h"
#include "gpk-common.h"
#include "gpk-consolekit.h"
#include "gpk-error.h"

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
	ret = dbus_g_proxy_call_with_timeout (proxy, "Restart", INT_MAX,
					      &error, G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("Unable to restart system: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (G_OBJECT (proxy));

	return ret;
}

