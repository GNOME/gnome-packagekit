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
#include <pk-common.h>

#include "gpk-progress.h"
#include "gpk-common.h"

static GpkProgress *progress = NULL;
static gchar *package = NULL;
static GMainLoop *loop = NULL;
static PkClient *client_resolve = NULL;
static PkClient *client_install = NULL;
static gboolean already_installed = FALSE;

/**
 * gpk_install_package_action_unref_cb:
 **/
static void
gpk_install_package_action_unref_cb (GpkProgress *progress, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	g_main_loop_quit (loop);
}

/**
 * gpk_install_package_resolve_finished_cb:
 **/
static void
gpk_install_package_resolve_finished_cb (PkClient *client, PkExitEnum exit_code, guint runtime, gpointer data)
{
	gchar *tid;
	gboolean ret;

	/* already installed? */
	if (already_installed) {
		gpk_error_modal_dialog (_("Failed to install package"),
				       _("The package is already installed"));
		g_main_loop_quit (loop);
		return;
	}

	/* did we resolve? */
	if (pk_strzero (package)) {
		gpk_error_modal_dialog (_("Failed to find package"),
				       _("The package could not be found on the system"));
		g_main_loop_quit (loop);
		return;
	}

	pk_warning ("Installing '%s'", package);
	ret = pk_client_install_package (client_install, package, NULL);
	if (ret == FALSE) {
		gpk_error_modal_dialog (_("Method not supported"),
				       _("Installing packages is not supported"));
		g_main_loop_quit (loop);
		return;
	}

	tid = pk_client_get_tid (client_install);
	gpk_progress_monitor_tid (progress, tid);
	g_free (tid);
}

/**
 * gpk_install_package_resolve_package_cb:
 **/
static void
gpk_install_package_resolve_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id,
					const gchar *summary, gboolean data)
{
	if (info == PK_INFO_ENUM_INSTALLED) {
		already_installed = TRUE;
	} else if (info == PK_INFO_ENUM_AVAILABLE) {
		pk_debug ("package '%s' resolved!", package_id);
		package = g_strdup (package_id);
	}
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

	g_set_application_name (_("PackageKit Package Installer"));
	context = g_option_context_new ("gpk-install-package");
	g_option_context_set_summary (context, _("PackageKit Package Installer"));
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
		g_print ("%s\n", _("You need to specify a package to install"));
		return 1;
	}
	if (argc > 2) {
		g_print ("%s\n%s\n", _("You can only specify one package to install"),
			 _("This will change in future versions of PackageKit"));
		return 1;
	}
	loop = g_main_loop_new (NULL, FALSE);

	/* create a new progress object */
	progress = gpk_progress_new ();
	g_signal_connect (progress, "action-unref",
			  G_CALLBACK (gpk_install_package_action_unref_cb), loop);

	/* create seporate instances */
	client_install = pk_client_new ();

	/* find the name */
	client_resolve = pk_client_new ();
	g_signal_connect (client_resolve, "finished",
			  G_CALLBACK (gpk_install_package_resolve_finished_cb), NULL);
	g_signal_connect (client_resolve, "package",
			  G_CALLBACK (gpk_install_package_resolve_package_cb), NULL);
	ret = pk_client_resolve (client_resolve, "newest", argv[1], NULL);
	if (ret == FALSE) {
		gpk_error_modal_dialog (_("Method not supported"),
				       _("Resolving names to packages is not supported"));
	} else {
		g_main_loop_run (loop);
	}

	g_free (package);
	g_object_unref (progress);
	g_object_unref (client_resolve);
	g_object_unref (client_install);

	return 0;
}
