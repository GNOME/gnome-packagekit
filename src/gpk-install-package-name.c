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

#include <pk-debug.h>

#include "gpk-common.h"
#include "gpk-client.h"

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

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  N_("Show extra debugging information"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	g_type_init ();

	g_set_application_name (_("PackageKit Package Installer"));
	context = g_option_context_new ("gpk-install-package");
	g_option_context_set_summary (context, _("PackageKit Package Installer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	pk_debug_init (verbose);
	gtk_init (&argc, &argv);

	if (argc < 2) {
		gpk_error_modal_dialog (_("Failed to install"), _("You need to specify a package to install"));
		return 1;
	}
	if (argc > 2) {
		gpk_error_modal_dialog (_("Failed to install packages"), _("You can only specify one package name to install"));
		return 1;
	}

	error = NULL;
	gclient = gpk_client_new ();
	ret = gpk_client_install_package_name (gclient, argv[1], NULL);
	g_object_unref (gclient);

	return !ret;
}
