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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <dbus/dbus-glib.h>

#include "gpk-common.h"
#include "gpk-error.h"
#include "gpk-dbus.h"
#include "gpk-debug.h"

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GOptionContext *context;
	gboolean ret;
	GError *error = NULL;
	gchar **files = NULL;
	gchar *tmp;
	gchar *current_dir;
	guint i;
	DBusGConnection *connection;
	DBusGProxy *proxy = NULL;

	const GOptionEntry options[] = {
		{ G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_FILENAME_ARRAY, &files,
		  /* TRANSLATORS: command line option: a list of files to install */
		  _("Files to install"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	/* TRANSLATORS: program name: application to install a package to provide a file */
	g_set_application_name (_("Software Install"));
	context = g_option_context_new ("gpk-install-local-file");
	g_option_context_set_summary (context, _("PackageKit File Installer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gpk_debug_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	/* TRANSLATORS: title to pass to to the user if there are not enough privs */
	ret = gpk_check_privileged_user (_("Local file installer"), TRUE);
	if (!ret)
		goto out;

	if (files == NULL) {
		/* TRANSLATORS: could not install a package that contained the file we wanted */
		gpk_error_dialog (_("Failed to install a package to provide a file"),
				  /* TRANSLATORS: nothing selected */
				  _("You need to specify a file to install"), NULL);
		goto out;
	}

	/* make sure we don't pass relative paths to the session-interface */
	/* (this is needed if install-local-files is executed from the command-line) */
	current_dir = g_get_current_dir ();
	for (i = 0; files[i] != NULL; i++) {
		if (!g_str_has_prefix (files[i], "/")) {
			tmp = g_build_filename (current_dir, files[i], NULL);
			g_free (files[i]);
			files[i] = tmp;
		}
	}
	g_free (current_dir);

	/* check dbus connections, exit if not valid */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (connection == NULL) {
		g_warning ("%s", error->message);
		goto out;
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit.Modify");
	if (proxy == NULL) {
		g_warning ("Cannot connect to session service");
		goto out;
	}

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (proxy, INT_MAX);

	/* do method */
	ret = dbus_g_proxy_call (proxy, "InstallPackageFiles", &error,
				 G_TYPE_UINT, 0, /* xid */
				 G_TYPE_STRV, files, /* data */
				 G_TYPE_STRING, "hide-finished,show-warnings", /* interaction */
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (!ret) {
		g_warning ("%s", error->message);
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
