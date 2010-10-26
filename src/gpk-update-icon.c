/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2010 Richard Hughes <richard@hughsie.com>
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
#include <libnotify/notify.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-dbus-monitor.h"

#include "gpk-check-update.h"
#include "gpk-watch.h"
#include "gpk-firmware.h"
#include "gpk-hardware.h"
#include "gpk-common.h"
#include "gpk-debug.h"

static GpkCheckUpdate *cupdate = NULL;
static GpkWatch *watch = NULL;
static GpkFirmware *firmware = NULL;
static GpkHardware *hardware = NULL;
static guint timer_id = 0;
static gboolean timed_exit = FALSE;

/**
 * gpk_icon_timed_exit_cb:
 **/
static gboolean
gpk_icon_timed_exit_cb (GtkApplication *application)
{
	g_application_release (G_APPLICATION (application));
	return FALSE;
}

/**
 * gpk_icon_startup_cb:
 **/
static void
gpk_icon_startup_cb (GtkApplication *application, gpointer user_data)
{
	/* create new objects */
	cupdate = gpk_check_update_new ();
	watch = gpk_watch_new ();
	firmware = gpk_firmware_new ();
	hardware = gpk_hardware_new ();

	/* Only timeout if we have specified iton the command line */
	if (timed_exit) {
		timer_id = g_timeout_add_seconds (120, (GSourceFunc) gpk_icon_timed_exit_cb, application);
		g_source_set_name_by_id (timer_id, "[GpkUpdateIcon] timed exit");
	}
}

/**
 * gpm_pack_commandline_cb:
 **/
static int
gpm_pack_commandline_cb (GApplication *application,
			 GApplicationCommandLine *cmdline,
			 gpointer user_data)
{
	gchar **argv;
	gint argc;
	gboolean program_version = FALSE;
	GOptionContext *context;
	gboolean ret;

	const GOptionEntry options[] = {
		{ "timed-exit", '\0', 0, G_OPTION_ARG_NONE, &timed_exit,
		  _("Exit after a small delay"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
		  _("Show the program version and exit"), NULL },
		{ NULL}
	};

	/* get arguments */
	argv = g_application_command_line_get_arguments (cmdline, &argc);

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("Update Applet"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gpk_debug_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	/* TRANSLATORS: title to pass to to the user if there are not enough privs */
	ret = gpk_check_privileged_user (_("Update applet"), FALSE);
	if (!ret) {
		g_warning ("Exit: gpk_check_privileged_user returned FALSE");
		return 1;
	}

	g_strfreev (argv);
	return 0;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GtkApplication *application;
	gint status = 0;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();
	gtk_init (&argc, &argv);
	dbus_g_thread_init ();
	notify_init ("gpk-update-icon");

	/* TRANSLATORS: program name, a session wide daemon to watch for updates and changing system state */
	g_set_application_name (_("Update Applet"));

	/* are we already activated? */
	application = gtk_application_new ("org.freedesktop.PackageKit.UpdateIcon",
					   G_APPLICATION_HANDLES_COMMAND_LINE);
	g_signal_connect (application, "startup",
			  G_CALLBACK (gpk_icon_startup_cb), NULL);
	g_signal_connect (application, "command-line",
			  G_CALLBACK (gpm_pack_commandline_cb), NULL);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* we don't have any windows to assign */
	g_application_hold (G_APPLICATION (application));

	/* run */
	status = g_application_run (G_APPLICATION (application), argc, argv);

	if (cupdate != NULL)
		g_object_unref (cupdate);
	if (watch != NULL)
		g_object_unref (watch);
	if (firmware != NULL)
		g_object_unref (firmware);
	if (hardware != NULL)
		g_object_unref (hardware);
	g_object_unref (application);
	return status;
}

