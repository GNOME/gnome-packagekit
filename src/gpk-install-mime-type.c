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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>
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
	gboolean ret;
	GError *error = NULL;
	DBusGConnection *connection;
	DBusGProxy *proxy = NULL;
	gchar **types = NULL;

	const GOptionEntry options[] = {
		{ G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_FILENAME_ARRAY, &types,
		/* TRANSLATORS: command line option: a list of catalogs to install */
		  _("Mime types to install"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();
	gtk_init (&argc, &argv);

	/* TRANSLATORS: program name: application to install a package to provide a mime type */
	g_set_application_name (_("Mime Type Installer"));
	context = g_option_context_new ("gpk-install-mime-type");
	g_option_context_set_summary (context, _("Mime Type Installer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, egg_debug_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	/* TRANSLATORS: title to pass to to the user if there are not enough privs */
	ret = gpk_check_privileged_user (_("Mime type installer"), TRUE);
	if (!ret)
		goto out;

	if (types == NULL) {
		/* TRANSLATORS: could not install program supporting this type */
		gpk_error_dialog (_("Failed to install a program to handle this file type"),
				  /* TRANSLATORS: no type given */
				  _("You need to specify a mime-type to install"), NULL);
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
	ret = dbus_g_proxy_call (proxy, "InstallMimeTypes", &error,
				 G_TYPE_UINT, 0, /* xid */
				 G_TYPE_STRV, types, /* data */
				 G_TYPE_STRING, "hide-finished,hide-warnings", /* interaction */
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (!ret && !gpk_ignore_session_error (error)) {
		/* TRANSLATORS: This is when the specified DBus method did not execute successfully */
		gpk_error_dialog (_("The action could not be completed"),
				  /* TRANSLATORS: we don't have anything more useful to translate. sorry. */
				  _("The request failed. More details are available in the detailed report."),
				  error->message);
		egg_warning ("%s", error->message);
		goto out;
	}
out:
	if (error != NULL)
		g_error_free (error);
	if (proxy != NULL)
		g_object_unref (proxy);
	g_strfreev (types);
	return !ret;
}
