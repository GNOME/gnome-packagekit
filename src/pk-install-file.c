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
#include <glade/glade.h>
#include <dbus/dbus-glib.h>
#include <gtk/gtk.h>
#include <locale.h>

#include <pk-debug.h>
#include <pk-client.h>

#include "pk-progress.h"
#include "pk-common-gui.h"

static GladeXML *glade_xml = NULL;
static PkClient *client = NULL;

typedef enum {
        PAGE_PROGRESS,
        PAGE_CONFIRM,
        PAGE_ERROR,
        PAGE_LAST
} PkPageEnum;

static void
pk_updates_set_page (PkPageEnum page)
{
        GList *list, *l;
        GtkWidget *widget;
        guint i;

        widget = glade_xml_get_widget (glade_xml, "hbox_hidden");
        list = gtk_container_get_children (GTK_CONTAINER (widget));
        for (l=list, i=0; l; l=l->next, i++) {
                if (i == page) {
                        gtk_widget_show (l->data);
                } else {
                        gtk_widget_hide (l->data);
                }
        }
}

static gboolean
finished_timeout (gpointer data)
{
	gtk_main_quit ();

	return FALSE;
}

static void
pk_install_file_finished_cb (PkClient   *client,
			     PkExitEnum  exit,
			     guint       runtime,
			     gpointer    data)
{
	if (exit == PK_EXIT_ENUM_SUCCESS) {
                pk_updates_set_page (PAGE_CONFIRM);

		g_timeout_add_seconds (30, finished_timeout, NULL);
	}

}

static gint pulse_timeout = 0;

static void
pk_install_file_progress_changed_cb (PkClient *client,
				     guint     percentage,
				     guint     subpercentage,
				     guint     elapsed,
				     guint     remaining,
				     gpointer  data)
{
        GtkWidget *widget;

        widget = glade_xml_get_widget (glade_xml, "progressbar_percent");

	if (pulse_timeout != 0) {
		g_source_remove (pulse_timeout);
		pulse_timeout = 0;
	}

        if (percentage != PK_CLIENT_PERCENTAGE_INVALID) {
                gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);
        }
}

static gboolean
pulse_progress (gpointer data)
{
        GtkWidget *widget;

        widget = glade_xml_get_widget (glade_xml, "progressbar_percent");

	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (widget));

	return TRUE;
}

static void
pk_install_file_status_changed_cb (PkClient     *client,
				   PkStatusEnum  status,
				   gpointer      data)
{
        GtkWidget *widget;
        gchar *text;

        widget = glade_xml_get_widget (glade_xml, "progress_part_label");
        text = g_strdup_printf ("<b>%s</b>", pk_status_enum_to_localised_text (status));
        gtk_label_set_markup (GTK_LABEL (widget), text);
        g_free (text);

	if (status == PK_STATUS_ENUM_WAIT) {
		if (pulse_timeout == 0) {
			widget = glade_xml_get_widget (glade_xml, "progressbar_percent");

			gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (widget ), 0.04);
			pulse_timeout = g_timeout_add (75, pulse_progress, NULL);
		}
	}
}

static void
pk_install_file_error_code_cb (PkClient        *client,
			       PkErrorCodeEnum  code,
			       const gchar     *details,
			       gpointer         data)
{
        GtkWidget *widget;
        const gchar *title;
        gchar *title_bold;
        gchar *details_safe;

        pk_updates_set_page (PAGE_ERROR);

        /* set bold title */
        widget = glade_xml_get_widget (glade_xml, "label_error_title");
        title = pk_error_enum_to_localised_text (code);
        title_bold = g_strdup_printf ("<b>%s</b>", title);
        gtk_label_set_label (GTK_LABEL (widget), title_bold);
        g_free (title_bold);

        widget = glade_xml_get_widget (glade_xml, "label_error_message");
        gtk_label_set_label (GTK_LABEL (widget), pk_error_enum_to_localised_message (code));

        widget = glade_xml_get_widget (glade_xml, "label_error_details");
        details_safe = g_markup_escape_text (details, -1);
        gtk_label_set_label (GTK_LABEL (widget), details_safe);
}

static gboolean
pk_window_delete_event_cb (GtkWidget    *widget,
                            GdkEvent    *event,
                            gpointer    data)
{
        gtk_main_quit ();

        return FALSE;
}

static void
pk_button_close_cb (GtkWidget *widget, gpointer data)
{
        gtk_main_quit ();
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
	GError *error;
	GtkWidget *main_window;
	GtkWidget *widget;

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

	g_set_application_name (_("PackageKit File Installer"));
	context = g_option_context_new (_("FILE"));
	g_option_context_set_summary (context, _("PackageKit File Installer"));
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
		g_print (_("You need to specify a file to install\n"));
		return 1;
	}

	client = pk_client_new ();

	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_install_file_finished_cb), NULL);
	g_signal_connect (client, "progress-changed",
			  G_CALLBACK (pk_install_file_progress_changed_cb), NULL);
	g_signal_connect (client, "status-changed",
			  G_CALLBACK (pk_install_file_status_changed_cb), NULL);
	g_signal_connect (client, "error-code",
			  G_CALLBACK (pk_install_file_error_code_cb), NULL);

	glade_xml = glade_xml_new (PK_DATA "/pk-install-file.glade", NULL, NULL);
	main_window = glade_xml_get_widget (glade_xml, "window_updates");

        /* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (pk_window_delete_event_cb), NULL);

	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_close_cb), NULL);

	widget = glade_xml_get_widget (glade_xml, "button_close2");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_close_cb), NULL);

	widget = glade_xml_get_widget (glade_xml, "button_close3");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_close_cb), NULL);

        /* set the label blank initially */
        widget = glade_xml_get_widget (glade_xml, "progress_part_label");
        gtk_label_set_label (GTK_LABEL (widget), "");

	pk_updates_set_page (PAGE_PROGRESS);

	error = NULL;
	ret = pk_client_install_file (client, argv[1], &error);
	if (ret == FALSE) {
		gtk_widget_hide (main_window);

		/* check if we got a permission denied */
		if (g_str_has_prefix (error->message, "org.freedesktop.packagekit.localinstall")) {
			pk_error_modal_dialog (_("Failed to install"),
					       _("You don't have the necessary privileges to install local packages"));
		}
		else {
			pk_error_modal_dialog (_("Failed to install"),
					       error->message);
		}
	} else {
		gtk_main ();
	}

	g_object_unref (client);

	return 0;
}
