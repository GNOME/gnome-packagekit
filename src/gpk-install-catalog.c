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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <packagekit-glib/packagekit.h>
#include <dbus/dbus-glib.h>

#include "egg-debug.h"

#include "gpk-common.h"
#include "gpk-error.h"
#include "gpk-dbus.h"

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GOptionContext *context;
	DBusGConnection *connection;
	DBusGProxy *proxy = NULL;
	GError *error = NULL;
	gboolean ret;
	gboolean verbose = FALSE;
	gchar **files = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_FILENAME_ARRAY, &files,
		/* TRANSLATORS: command line option: a list of catalogs to install */
		  _("Catalogs files to install"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();

	/* TRANSLATORS: program name: application to install a catalog of software */
	g_set_application_name (_("Catalog Installer"));
	context = g_option_context_new ("gpk-install-catalog");
	g_option_context_set_summary (context, _("Catalog Installer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);
	gtk_init (&argc, &argv);

	/* are we running privileged */
	ret = gpk_check_privileged_user (_("Catalog installer"), TRUE);
	if (!ret)
		goto out;

	if (files == NULL) {
		gpk_error_dialog (_("Failed to install catalog"),
				  /* TRANSLATORS: no filename was supplied */
				  _("You need to specify a file name to install"), NULL);
		goto out;
	}

	/* check dbus connections, exit if not valid */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (connection == NULL) {
		egg_warning ("%s", error->message);
		goto out;
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit.Modify");
	if (proxy == NULL) {
		egg_warning ("Cannot connect to session service");
		goto out;
	}

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (proxy, INT_MAX);

	/* do method */
	ret = dbus_g_proxy_call (proxy, "InstallCatalogs", &error,
				 G_TYPE_UINT, 0, /* xid */
				 G_TYPE_STRV, files, /* data */
				 G_TYPE_STRING, "hide-finished,hide-warnings", /* interaction */
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (!ret && !gpk_ignore_session_error (error)) {
		/* TRANSLATORS: This is when the specified DBus method did not execute successfully */
		gpk_error_dialog (_("The action could not be completed"),
				  /* TRANSLATORS: we don't have anything more useful to translate. sorry. */
				  _("The request failed. More details are available in the detailed report."),
				  error->message);
		egg_warning ("%i: %s", error->code, error->message);
		goto out;
	}
out:
	if (error != NULL)
		g_error_free (error);
	if (proxy != NULL)
		g_object_unref (proxy);
	g_strfreev (files);
	return !ret;
}

