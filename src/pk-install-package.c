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

#include "pk-progress.h"
#include "pk-common.h"

static PkProgress *progress = NULL;
static gchar *package = NULL;
static GMainLoop *loop = NULL;

/**
 * pk_monitor_action_unref_cb:
 **/
static void
pk_monitor_action_unref_cb (PkProgress *progress, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	g_object_unref (progress);
	g_main_loop_quit (loop);
}

/**
 * pk_monitor_install_finished_cb:
 **/
static void
pk_monitor_install_finished_cb (PkClient *client, PkExitEnum exit_code, guint runtime, gpointer data)
{
	g_object_unref (client);
}

/**
 * pk_monitor_resolve_finished_cb:
 **/
static void
pk_monitor_resolve_finished_cb (PkClient *client, PkExitEnum exit_code, guint runtime, gpointer data)
{
	gchar *tid;
	gboolean ret;

	pk_debug ("unref'ing %p", client);
	g_object_unref (client);

	/* ITS4: ignore, not used for allocation */
	if (strlen (package) == 0) {
		pk_error_modal_dialog (_("Failed to resolve"),
				       _("The package could not be found on the system"));
		g_main_loop_quit (loop);
		return;
	}

	/* create a new instance */
	client = pk_client_new ();
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_monitor_install_finished_cb), NULL);

	pk_warning ("Installing '%s'", package);
	ret = pk_client_install_package (client, package);
	if (ret == FALSE) {
		pk_error_modal_dialog (_("Method not supported"),
				       _("Installing packages is not supported"));
		g_object_unref (client);
		g_main_loop_quit (loop);
		return;
	}

	tid = pk_client_get_tid (client);
	/* create a new progress object */
	progress = pk_progress_new ();
	g_signal_connect (progress, "action-unref",
			  G_CALLBACK (pk_monitor_action_unref_cb), loop);
	pk_progress_monitor_tid (progress, tid);
	g_free (tid);
}

/**
 * pk_monitor_resolve_package_cb:
 **/
static void
pk_monitor_resolve_package_cb (PkClient *client, guint value, const gchar *package_id,
			       const gchar *summary, gboolean data)
{
	/* save */
	pk_error ("package '%s' resolved!", package_id);
	package = g_strdup (package_id);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	PkClient *client;
	GOptionContext *context;
	gboolean ret;
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
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

	g_set_application_name (_("PackageKit Package Installer"));
	context = g_option_context_new (_("PACKAGE_ID"));
	g_option_context_set_summary (context, _("PackageKit Package Installer"));
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
		pk_error ("You need to specify a package to install");
	}
	loop = g_main_loop_new (NULL, FALSE);

	client = pk_client_new ();
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_monitor_resolve_finished_cb), NULL);
	g_signal_connect (client, "package",
			  G_CALLBACK (pk_monitor_resolve_package_cb), NULL);
	ret = pk_client_resolve (client, argv[1]);
	if (ret == FALSE) {
		pk_error_modal_dialog (_("Method not supported"),
				       _("Resolving names to packages is not supported"));
	} else {
		g_main_loop_run (loop);
	}

	g_free (package);
	if (progress != NULL) {
		g_object_unref (progress);
	}

	return 0;
}
