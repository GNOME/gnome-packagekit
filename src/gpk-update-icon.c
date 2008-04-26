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

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <gtk/gtk.h>
#include <locale.h>

#include <libgbus.h>

#include <pk-debug.h>
#include <pk-common.h>

#include "gpk-notify.h"
#include "gpk-watch.h"
#include "gpk-firmware.h"
#include "gpk-dbus.h"
#include "gpk-interface.h"

/**
 * gpk_object_register:
 * @connection: What we want to register to
 * @object: The GObject we want to register
 *
 * Return value: success
 **/
static gboolean
gpk_object_register (DBusGConnection *connection, GObject *object)
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
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RequestName failed: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* free the bus_proxy */
	g_object_unref (G_OBJECT (bus_proxy));

	/* already running */
	if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		return FALSE;
	}

	dbus_g_object_type_install_info (GPK_TYPE_DBUS, &dbus_glib_gpk_dbus_object_info);
	dbus_g_connection_register_g_object (connection, PK_DBUS_PATH, object);

	return TRUE;
}

/**
 * pk_dbus_connection_replaced_cb:
 **/
static void
pk_dbus_connection_replaced_cb (LibGBus *libgbus, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	pk_warning ("exiting the mainloop as we have been replaced");
	g_main_loop_quit (loop);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	GpkNotify *notify = NULL;
	GpkWatch *watch = NULL;
	GpkDbus *dbus = NULL;
	GpkFirmware *firmware = NULL;
	GOptionContext *context;
	GError *error = NULL;
	gboolean ret;
	DBusGConnection *connection;
	LibGBus *libgbus;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  N_("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
		  N_("Show the program version and exit"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	g_set_application_name (_("PackageKit Update Applet"));
	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("PackageKit Update Icon"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	pk_debug_init (verbose);
	gtk_init (&argc, &argv);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
                                           PK_DATA G_DIR_SEPARATOR_S "icons");

	/* create new objects */
	dbus = gpk_dbus_new ();
	notify = gpk_notify_new ();
	watch = gpk_watch_new ();
	firmware = gpk_firmware_new ();
	loop = g_main_loop_new (NULL, FALSE);

	/* find out when we are replaced */
	libgbus = libgbus_new ();
	libgbus_assign (libgbus, LIBGBUS_SESSION, PK_DBUS_SERVICE);
	g_signal_connect (libgbus, "connection-replaced",
			  G_CALLBACK (pk_dbus_connection_replaced_cb), loop);

	/* get the bus */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* try to register */
	ret = gpk_object_register (connection, G_OBJECT (dbus));
	if (!ret) {
		pk_warning ("failed to replace running instance.");
		goto out;
	}

	/* wait until loop killed */
	g_main_loop_run (loop);

out:
	g_main_loop_unref (loop);
	g_object_unref (dbus);
	g_object_unref (notify);
	g_object_unref (watch);
	g_object_unref (firmware);
	g_object_unref (libgbus);

	return 0;
}
