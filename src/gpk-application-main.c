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

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <gtk/gtk.h>
#include <locale.h>

#include "egg-debug.h"

#include "gpk-application.h"
#include "gpk-common.h"

/**
 * gpk_application_close_cb
 **/
static void
gpk_application_close_cb (GpkApplication *app, GApplication *application)
{
	g_application_quit (application, 0);
}

/**
 * gpk_application_prepare_action_cb:
 **/
static void
gpk_application_prepare_action_cb (GApplication *application, GVariant *arguments,
				   GVariant *platform_data, GpkApplication *app)
{
	gpk_application_show (app);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean program_version = FALSE;
	GpkApplication *app = NULL;
	GOptionContext *context;
	GApplication *application;
	gboolean ret;

	const GOptionEntry options[] = {
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
		  /* TRANSLATORS: show the program version */
		  _("Show the program version and exit"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();
	g_type_init ();
	gtk_init (&argc, &argv);

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("Add/Remove Software"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, egg_debug_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	/* are we running privileged */
	ret = gpk_check_privileged_user (_("Package installer"), TRUE);
	if (!ret)
		return 1;

	/* create a new application object */
	app = gpk_application_new ();

	/* are we already activated? */
	application = g_application_new_and_register ("org.freedesktop.PackageKit.Application", argc, argv);
	g_signal_connect (application, "prepare-activation",
			  G_CALLBACK (gpk_application_prepare_action_cb), app);

	g_signal_connect (app, "action-close",
			  G_CALLBACK (gpk_application_close_cb), application);

	/* run */
	g_application_run (application);

	g_object_unref (app);
	g_object_unref (application);
	return 0;
}

