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

#include <pk-debug.h>
#include <pk-client.h>

#include "pk-progress.h"
#include "pk-common-gui.h"

static PkProgress *progress = NULL;
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
	gchar *tid;
	GError *error;

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

	g_set_application_name (_("PackageKit File Installer"));
	context = g_option_context_new (_("FILE"));
	g_option_context_set_summary (context, _("PackageKit File Installer"));
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

	if (argc < 2) {
		g_print (_("You need to specify a file to install\n"));
		return 1;
	}

	client = pk_client_new ();
	error = NULL;
	ret = pk_client_install_file (client, argv[1], &error);
	if (ret == FALSE) {
		/* check if we got a permission denied */
		if (g_str_has_prefix (error->message, "org.freedesktop.packagekit.localinstall")) {
			pk_error_modal_dialog (_("Failed to install"),
					       _("You don't have the necessary privileges to install local packages"));
		}
		else {
			pk_error_modal_dialog (_("Failed to install"),
					       error->message);
		}
	} else {
		loop = g_main_loop_new (NULL, FALSE);
		tid = pk_client_get_tid (client);
		/* create a new progress object */
		progress = pk_progress_new ();
		g_signal_connect (progress, "action-unref",
				  G_CALLBACK (pk_monitor_action_unref_cb), loop);
		pk_progress_monitor_tid (progress, tid);
		g_free (tid);
		g_main_loop_run (loop);
	}

	g_object_unref (client);
	return 0;
}
