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
#include <unique/unique.h>

#include "egg-debug.h"

#include "gpk-application.h"
#include "gpk-common.h"

/**
 * gpk_application_close_cb
 * @application: This application class instance
 *
 * What to do when we are asked to close for whatever reason
 **/
static void
gpk_application_close_cb (GpkApplication *application)
{
	gtk_main_quit ();
}

/**
 * gpk_application_message_received_cb
 **/
static void
gpk_application_message_received_cb (UniqueApp *app, UniqueCommand command, UniqueMessageData *message_data, guint time_ms, GpkApplication *application)
{
	if (command == UNIQUE_ACTIVATE)
		gpk_application_show (application);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean program_version = FALSE;
	GpkApplication *application = NULL;
	GOptionContext *context;
	UniqueApp *unique_app;
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

	/* are we already activated? */
	unique_app = unique_app_new ("org.freedesktop.PackageKit.Application", NULL);
	if (unique_app_is_running (unique_app)) {
		egg_debug ("You have another instance running. This program will now close");
		unique_app_send_message (unique_app, UNIQUE_ACTIVATE, NULL);
		goto unique_out;
	}

	/* create a new application object */
	application = gpk_application_new ();
	g_signal_connect (unique_app, "message-received",
			  G_CALLBACK (gpk_application_message_received_cb), application);
	g_signal_connect (application, "action-close",
			  G_CALLBACK (gpk_application_close_cb), NULL);

	/* wait */
	gtk_main ();

	g_object_unref (application);
unique_out:
	g_object_unref (unique_app);
	return 0;
}

