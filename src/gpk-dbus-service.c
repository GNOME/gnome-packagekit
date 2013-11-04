/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <libnotify/notify.h>
#include <packagekit-glib2/packagekit.h>

#include "gpk-common.h"
#include "gpk-dbus.h"
#include "gpk-debug.h"

#include "org.freedesktop.PackageKit.h"

static GMainLoop *loop = NULL;

#define GPK_SESSION_IDLE_EXIT	60 /* seconds */

/**
 * gpk_dbus_service_object_register:
 * @connection: What we want to register to
 * @object: The GObject we want to register
 *
 * Return value: success
 **/
static gboolean
gpk_dbus_service_object_register (DBusGConnection *connection, GObject *object)
{
	DBusGProxy *bus_proxy = NULL;
	GError *error = NULL;
	guint request_name_result;
	gboolean ret;

	/* connect to the bus */
	bus_proxy = dbus_g_proxy_new_for_name (connection, DBUS_SERVICE_DBUS,
					       DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS);

	/* get our name */
	ret = dbus_g_proxy_call (bus_proxy, "RequestName", &error,
				 G_TYPE_STRING, PK_DBUS_SERVICE,
				 G_TYPE_UINT, DBUS_NAME_FLAG_ALLOW_REPLACEMENT |
					      DBUS_NAME_FLAG_REPLACE_EXISTING |
					      DBUS_NAME_FLAG_DO_NOT_QUEUE,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &request_name_result,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the D-Bus method failed */
		g_warning ("RequestName failed: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* free the bus_proxy */
	g_object_unref (G_OBJECT (bus_proxy));

	/* already running */
	if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
		return FALSE;

	dbus_g_object_type_install_info (GPK_TYPE_DBUS, &dbus_glib_gpk_dbus_object_info);
	dbus_g_error_domain_register (GPK_DBUS_ERROR, NULL, GPK_DBUS_TYPE_ERROR);
	dbus_g_connection_register_g_object (connection, PK_DBUS_PATH, object);

	return TRUE;
}

/**
 * gpk_dbus_service_check_idle_cb:
 **/
static gboolean
gpk_dbus_service_check_idle_cb (GpkDbus *dbus)
{
	guint idle;

	/* get the idle time */
	idle = gpk_dbus_get_idle_time (dbus);
	if (idle > GPK_SESSION_IDLE_EXIT) {
		g_debug ("exiting loop as idle");
		g_main_loop_quit (loop);
		return FALSE;
	}
	/* continue to poll */
	return TRUE;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean no_timed_exit = FALSE;
	GpkDbus *dbus = NULL;
	GOptionContext *context;
	GError *error = NULL;
	gboolean ret;
	guint retval = 0;
	DBusGConnection *connection;
	guint timer_id = 0;

	const GOptionEntry options[] = {
		{ "no-timed-exit", '\0', 0, G_OPTION_ARG_NONE, &no_timed_exit,
		  _("Do not exit after the request has been processed"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	dbus_g_thread_init ();
	notify_init (_("Software Install"));

	/* TRANSLATORS: program name, a session wide daemon to watch for updates and changing system state */
	g_set_application_name (_("Software Install"));
	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("Session D-Bus service for PackageKit"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gpk_debug_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	gtk_init (&argc, &argv);

	/* create new objects */
	dbus = gpk_dbus_new ();
	loop = g_main_loop_new (NULL, FALSE);

	/* get the bus */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (error) {
		g_warning ("%s", error->message);
		g_error_free (error);
		retval = 1;
		goto out;
	}

	/* try to register */
	ret = gpk_dbus_service_object_register (connection, G_OBJECT (dbus));
	if (!ret) {
		g_warning ("failed to replace running instance.");
		retval = 1;
		goto out;
	}

	/* only timeout if we have specified iton the command line */
	if (!no_timed_exit) {
		timer_id = g_timeout_add_seconds (5, (GSourceFunc) gpk_dbus_service_check_idle_cb, dbus);
		g_source_set_name_by_id (timer_id, "[GpkDbusService] timed exit");
	}

	/* wait */
	g_main_loop_run (loop);
out:
	g_main_loop_unref (loop);
	g_object_unref (dbus);
	return retval;
}
