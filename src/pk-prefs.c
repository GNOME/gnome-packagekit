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

#include <glib.h>
#include <glib/gi18n.h>

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-enum-list.h>
#include "pk-common.h"

static GladeXML *glade_xml = NULL;

/**
 * pk_button_help_cb:
 **/
static void
pk_button_help_cb (GtkWidget *widget,
		   gboolean  data)
{
	pk_debug ("emitting action-help");
}

/**
 * pk_button_close_cb:
 **/
static void
pk_button_close_cb (GtkWidget	*widget,
		     gboolean	data)
{
	GMainLoop *loop = (GMainLoop *) data;
	g_main_loop_quit (loop);
	pk_debug ("emitting action-close");
}

/**
 * pk_window_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
pk_window_delete_event_cb (GtkWidget	*widget,
			    GdkEvent	*event,
			    gboolean	 data)
{
	GMainLoop *loop = (GMainLoop *) data;
	g_main_loop_quit (loop);
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
	GOptionContext *context;
	GtkWidget *main_window;
	GtkWidget *widget;
	PkEnumList *role_list;
	PkClient *client;

	const GOptionEntry options[] = {
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  "Show extra debugging information", NULL },
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (_("Software Update Preferences"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);
	pk_debug_init (verbose);
	gtk_init (&argc, &argv);

	loop = g_main_loop_new (NULL, FALSE);

	client = pk_client_new ();

	/* get actions */
	role_list = pk_client_get_actions (client);

	glade_xml = glade_xml_new (PK_DATA "/pk-prefs.glade", NULL, NULL);
	main_window = glade_xml_get_widget (glade_xml, "window_prefs");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), "system-installer");

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (pk_window_delete_event_cb), loop);

	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_close_cb), loop);
	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_help_cb), NULL);

	gtk_widget_show (main_window);

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	g_object_unref (client);
	g_object_unref (role_list);

	return 0;
}
