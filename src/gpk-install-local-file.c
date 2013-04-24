/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2012-2013 Matthias Klumpp <matthias@tenstral.net>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <locale.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>
#include <dbus/dbus-glib.h>

#include "gpk-common.h"
#include "gpk-debug.h"
#include "gpk-enum.h"
#include "gpk-error.h"
#include "gpk-gnome.h"

typedef struct {
	GCancellable		*cancellable;
	GtkApplication		*application;
	GSettings		*settings_gpk;
	GtkBuilder		*builder;
	GtkWidget		*main_window;
	gchar			**files;
} GpkInstallLocalPrivate;

/**
 * gpk_install_local_help_cb:
 **/
static void
gpk_install_local_run_installation_cb (GtkWidget *widget, GpkInstallLocalPrivate *priv)
{
	gboolean ret;
	GError *error = NULL;
	DBusGConnection *connection;
	DBusGProxy *proxy = NULL;

	/* check dbus connections, exit if not valid */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (connection == NULL) {
		g_warning ("%s", error->message);
		goto out;
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.PackageKit.Modify");
	if (proxy == NULL) {
		g_warning ("Cannot connect to session service");
		goto out;
	}

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (proxy, INT_MAX);

	/* hide the application main window (UI is provided by the session-installer now) */
	gtk_widget_hide (priv->main_window);

	/* do method */
	ret = dbus_g_proxy_call (proxy, "InstallPackageFiles", &error,
				 G_TYPE_UINT, 0, /* xid */
				 G_TYPE_STRV, priv->files, /* data */
				 G_TYPE_STRING, "hide-finished,show-warnings", /* interaction */
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (!ret) {
		g_warning ("%s", error->message);
		goto out;
	}
out:
	if (error != NULL)
		g_error_free (error);
	if (proxy != NULL)
		g_object_unref (proxy);

	/* quit application */
	g_application_release (G_APPLICATION (priv->application));
}

/**
 * gpk_install_local_help_cb:
 **/
static void
gpk_install_local_help_cb (GtkWidget *widget, GpkInstallLocalPrivate *priv)
{
	gpk_gnome_help ("install-files");
}

/**
 * gpk_install_local_close_cb:
 **/
static void
gpk_install_local_close_cb (GtkWidget *widget, gpointer data)
{
	GpkInstallLocalPrivate *priv = (GpkInstallLocalPrivate *) data;
	g_application_release (G_APPLICATION (priv->application));
}

/**
 * gpk_install_local_startup_cb:
 **/
static void
gpk_install_local_startup_cb (GtkApplication *application, GpkInstallLocalPrivate *priv)
{
	GError *error = NULL;

	GtkWidget *widget;
	guint retval;

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* get UI */
	retval = gtk_builder_add_from_file (priv->builder, GPK_DATA "/gpk-install-local.ui", &error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		return;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_install_local_close_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_install_local_help_cb), priv);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder, "button_install"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_install_local_run_installation_cb), priv);

	priv->main_window = GTK_WIDGET (gtk_builder_get_object (priv->builder, "window_install"));
	gtk_application_add_window (application, GTK_WINDOW (priv->main_window));

	gtk_widget_show (priv->main_window);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GOptionContext *context;
	gboolean ret;
	gint status = 0;
	gchar **files = NULL;
	GpkInstallLocalPrivate *priv = NULL;
	const GOptionEntry options[] = {
		{ G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_FILENAME_ARRAY, &files,
		  /* TRANSLATORS: command line option: a list of files to install */
		  _("Files to install"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	/* TRANSLATORS: program name: application to install a package to provide a file */
	g_set_application_name (_("Software Install"));
	context = g_option_context_new ("gpk-install-local-file");
	g_option_context_set_summary (context, _("PackageKit File Installer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gpk_debug_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	/* TRANSLATORS: title to pass to to the user if there are not enough privs */
	ret = gpk_check_privileged_user (_("Local file installer"), TRUE);
	status = !ret;
	if (!ret)
		goto out;

	if (files == NULL) {
		/* TRANSLATORS: could not install a package file */
		gpk_error_dialog (_("Failed to install a package file"),
				  /* TRANSLATORS: nothing selected */
				  _("You need to specify a file to install"), NULL);
		status = 1;
		goto out;
	}

	priv = g_new0 (GpkInstallLocalPrivate, 1);
	priv->cancellable = g_cancellable_new ();
	priv->builder = gtk_builder_new ();
	priv->settings_gpk = g_settings_new (GPK_SETTINGS_SCHEMA);
	priv->files = files;

	/* are we already activated? */
	priv->application = gtk_application_new ("org.freedesktop.PackageKit.LocalInstaller",
						 G_APPLICATION_NON_UNIQUE);
	g_signal_connect (priv->application, "startup",
			  G_CALLBACK (gpk_install_local_startup_cb), priv);

	/* run */
	status = g_application_run (G_APPLICATION (priv->application), argc, argv);

	if (priv != NULL) {
		g_cancellable_cancel (priv->cancellable);
		g_object_unref (priv->cancellable);
		g_object_unref (priv->builder);
		g_object_unref (priv->settings_gpk);
		g_free (priv);
	}

out:
	g_strfreev (files);

	return status;
}
