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

#include <pk-debug.h>
#include <pk-client.h>

static GMainLoop *loop = NULL;

/**
 * pk_monitor_local_install_finished_cb:
 **/
static void
pk_monitor_local_install_finished_cb (PkClient *client, PkExitEnum exit_code, guint runtime, gpointer data)
{
	pk_debug ("unref'ing %p", client);
	g_object_unref (client);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GOptionContext *context;
	gboolean ret;
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;

	const GOptionEntry options[] = {
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  "Show extra debugging information", NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
		  "Show the program version and exit", NULL },
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	g_set_application_name (_("PackageKit File Installer"));
	context = g_option_context_new (_("PackageKit File Installer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version == TRUE) {
		g_print (VERSION "\n");
		return 0;
	}

	pk_debug_init (verbose);
	gtk_init (&argc, &argv);

	if (argc < 2) {
		pk_error ("You need to specify a file to install");
	}
	loop = g_main_loop_new (NULL, FALSE);

	PkClient *client;
	client = pk_client_new ();
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_monitor_local_install_finished_cb), NULL);
	ret = pk_client_install_file (client, argv[1]);
	if (ret == FALSE) {
		pk_debug ("InstallFile not supported!");
	} else {
		g_main_loop_run (loop);
	}
	return 0;
}
