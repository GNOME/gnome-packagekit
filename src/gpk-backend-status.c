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

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <gtk/gtk.h>
#include <packagekit-glib/packagekit.h>
#include <locale.h>

#include "egg-debug.h"

#include "gpk-common.h"

/**
 * pk_updates_close_cb:
 **/
static void
pk_updates_close_cb (GtkWidget *widget, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	egg_debug ("emitting action-close");
	g_main_loop_quit (loop);
}

/**
 * pk_updates_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
pk_updates_delete_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	pk_updates_close_cb (widget, data);
	return FALSE;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	GOptionContext *context;
	GtkWidget *widget;
	GtkBuilder *builder;
	gchar *name;
	gchar *author;
	PkRoleEnum roles;
	PkBitfield filters;
	PkControl *control;
	gboolean ret;
	guint retval;
	GError *error = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
		  _("Show the program version and exit"), NULL },
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

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("PackageKit Backend Details Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	egg_debug_init (verbose);
	gtk_init (&argc, &argv);

	loop = g_main_loop_new (NULL, FALSE);

	control = pk_control_new ();
	roles = pk_control_get_actions (control, NULL);
	filters = pk_control_get_filters (control, NULL);

	/* general stuff */
	ret = pk_control_get_backend_detail (control, &name, &author, NULL);
	if (!ret) {
		/* TRANSLATORS: backend is broken, and won't tell us what it supports */
		egg_warning (_("Exiting as backend details could not be retrieved"));
		return 1;
	}

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-backend-status.ui", &error);
	if (error != NULL) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out_build;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_backend"));
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_LOG);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_updates_close_cb), loop);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_backend"));
	g_signal_connect (widget, "delete_event",
			  G_CALLBACK (pk_updates_delete_event_cb), loop);
	gtk_widget_show (GTK_WIDGET (widget));

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_name"));
	gtk_label_set_label (GTK_LABEL (widget), name);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_author"));
	gtk_label_set_label (GTK_LABEL (widget), author);
	g_free (name);
	g_free (author);

	/* actions */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_CANCEL)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_cancel"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DEPENDS)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_get_depends"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_UPDATE_DETAIL)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_get_update_detail"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DETAILS)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_get_description"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_FILES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_get_files"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_REQUIRES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_get_requires"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_UPDATES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_get_updates"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_DETAILS)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_search_details"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_FILE)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_search_file"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_search_group"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_NAME)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_search_name"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REFRESH_CACHE)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_refresh_cache"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REMOVE_PACKAGES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_package_remove"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_INSTALL_PACKAGES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_package_install"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_INSTALL_FILES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_file_install"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_package_update"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_SYSTEM)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_system_update"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_RESOLVE)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_resolve"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_WHAT_PROVIDES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_what_provides"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_PACKAGES)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_get_packages"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}

	/* repos */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_REPO_LIST)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_get_repo_list"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REPO_ENABLE)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_repo_enable"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REPO_SET_DATA)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_repo_set_data"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}

	/* filters */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_installed"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_devel"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_gui"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_FREE)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_free"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_VISIBLE)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_visible"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SUPPORTED)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_supported"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NEWEST)) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "image_newest"));
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}

out_build:
	g_object_unref (builder);
	g_object_unref (control);

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	return 0;
}
