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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>

#include "egg-debug.h"

#include "gpk-common.h"
#include "gpk-client.h"
#include "gpk-error.h"

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GOptionContext *context;
	gboolean ret;
	gboolean verbose = FALSE;
	GError *error;
	GpkClient *gclient;
	gchar **files = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_FILENAME_ARRAY, &files,
		  /* TRANSLATORS: command line option: a list of files to install */
		  _("Files to install"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();

	/* TRANSLATORS: program name: application to install a package to provide a file */
	g_set_application_name (_("PackageKit File Installer"));
	context = g_option_context_new ("gpk-install-file");
	g_option_context_set_summary (context, _("PackageKit File Installer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);
	gtk_init (&argc, &argv);

	/* TRANSLATORS: title to pass to to the user if there are not enough privs */
	ret = gpk_check_privileged_user (_("Local file installer"), TRUE);
	if (!ret)
		return 1;

	if (files == NULL) {
		/* TRANSLATORS: could not install a package that contained the file we wanted */
		gpk_error_dialog (_("Failed to install a package to provide a file"),
				  /* TRANSLATORS: nothing selected */
				  _("You need to specify a file to install"), NULL);
		return 1;
	}

	error = NULL;
	gclient = gpk_client_new ();
	gpk_client_set_interaction (gclient, GPK_CLIENT_INTERACT_ALWAYS);
	ret = gpk_client_install_local_files (gclient, files, NULL);
	g_strfreev (files);
	g_object_unref (gclient);

	return !ret;
}
