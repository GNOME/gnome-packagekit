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
#include <glade/glade.h>
#include <pk-enum-list.h>
#include <pk-client.h>
#include <locale.h>

#include <pk-debug.h>

/**
 * pk_updates_close_cb:
 **/
static void
pk_updates_close_cb (GtkWidget *widget, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	pk_debug ("emitting action-close");
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
	GladeXML *glade_xml;
	gchar *name;
	gchar *author;
	PkEnumList *role_list;
	PkEnumList *filter_list;
	PkClient *client;
	gboolean retval;

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

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("PackageKit Backend Details Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	pk_debug_init (verbose);
	gtk_init (&argc, &argv);

	loop = g_main_loop_new (NULL, FALSE);

	client = pk_client_new ();
	role_list = pk_client_get_actions (client);
	filter_list = pk_client_get_filters (client);

	/* general stuff */
	retval = pk_client_get_backend_detail (client, &name, &author, NULL);
	if (FALSE == retval) {
		pk_warning (_("Exiting as backend details could not be retrieved\n"));
		return 1;
	}

	glade_xml = glade_xml_new (PK_DATA "/gpk-backend-status.glade", NULL, NULL);

	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_updates_close_cb), loop);

	widget = glade_xml_get_widget (glade_xml, "window_backend");
	g_signal_connect (widget, "delete_event",
			  G_CALLBACK (pk_updates_delete_event_cb), loop);
	gtk_widget_show (GTK_WIDGET (widget));

	widget = glade_xml_get_widget (glade_xml, "label_name");
	gtk_label_set_label (GTK_LABEL (widget), name);
	widget = glade_xml_get_widget (glade_xml, "label_author");
	gtk_label_set_label (GTK_LABEL (widget), author);
	g_free (name);
	g_free (author);

	/* actions */
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_CANCEL)) {
		widget = glade_xml_get_widget (glade_xml, "image_cancel");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_DEPENDS)) {
		widget = glade_xml_get_widget (glade_xml, "image_get_depends");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_UPDATE_DETAIL)) {
		widget = glade_xml_get_widget (glade_xml, "image_get_update_detail");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_DESCRIPTION)) {
		widget = glade_xml_get_widget (glade_xml, "image_get_description");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_FILES)) {
		widget = glade_xml_get_widget (glade_xml, "image_get_files");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_REQUIRES)) {
		widget = glade_xml_get_widget (glade_xml, "image_get_requires");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_UPDATES)) {
		widget = glade_xml_get_widget (glade_xml, "image_get_updates");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_SEARCH_DETAILS)) {
		widget = glade_xml_get_widget (glade_xml, "image_search_details");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_SEARCH_FILE)) {
		widget = glade_xml_get_widget (glade_xml, "image_search_file");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_SEARCH_GROUP)) {
		widget = glade_xml_get_widget (glade_xml, "image_search_group");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_SEARCH_NAME)) {
		widget = glade_xml_get_widget (glade_xml, "image_search_name");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_REFRESH_CACHE)) {
		widget = glade_xml_get_widget (glade_xml, "image_refresh_cache");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_REMOVE_PACKAGE)) {
		widget = glade_xml_get_widget (glade_xml, "image_package_remove");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_INSTALL_PACKAGE)) {
		widget = glade_xml_get_widget (glade_xml, "image_package_install");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_INSTALL_FILE)) {
		widget = glade_xml_get_widget (glade_xml, "image_file_install");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		widget = glade_xml_get_widget (glade_xml, "image_package_update");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_UPDATE_SYSTEM)) {
		widget = glade_xml_get_widget (glade_xml, "image_system_update");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_RESOLVE)) {
		widget = glade_xml_get_widget (glade_xml, "image_resolve");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_WHAT_PROVIDES)) {
		widget = glade_xml_get_widget (glade_xml, "image_what_provides");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}

	/* repos */
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_REPO_LIST)) {
		widget = glade_xml_get_widget (glade_xml, "image_get_repo_list");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_REPO_ENABLE)) {
		widget = glade_xml_get_widget (glade_xml, "image_repo_enable");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_REPO_SET_DATA)) {
		widget = glade_xml_get_widget (glade_xml, "image_repo_set_data");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}

	/* filters */
	if (pk_enum_list_contains (filter_list, PK_FILTER_ENUM_INSTALLED)) {
		widget = glade_xml_get_widget (glade_xml, "image_installed");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (filter_list, PK_FILTER_ENUM_DEVELOPMENT)) {
		widget = glade_xml_get_widget (glade_xml, "image_devel");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (filter_list, PK_FILTER_ENUM_GUI)) {
		widget = glade_xml_get_widget (glade_xml, "image_gui");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (filter_list, PK_FILTER_ENUM_FREE)) {
		widget = glade_xml_get_widget (glade_xml, "image_free");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (filter_list, PK_FILTER_ENUM_VISIBLE)) {
		widget = glade_xml_get_widget (glade_xml, "image_visible");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (filter_list, PK_FILTER_ENUM_SUPPORTED)) {
		widget = glade_xml_get_widget (glade_xml, "image_supported");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (filter_list, PK_FILTER_ENUM_NEWEST)) {
		widget = glade_xml_get_widget (glade_xml, "image_newest");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}

	g_object_unref (glade_xml);
	g_object_unref (client);
	g_object_unref (role_list);
	g_object_unref (filter_list);

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	return 0;
}
