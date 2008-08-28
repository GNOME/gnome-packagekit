/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
#include <string.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include "egg-debug.h"
#include <pk-package-id.h>
#include "gpk-gnome.h"
#include "gpk-common.h"

static gboolean has_agreed_eula = FALSE;

/**
 * gpk_client_eula_button_agree_cb:
 **/
static void
gpk_client_eula_button_agree_cb (GtkWidget *widget_button, gpointer data)
{
	has_agreed_eula = TRUE;
	gtk_main_quit ();
}

/**
 * gpk_client_eula_button_help_cb:
 **/
static void
gpk_client_eula_button_help_cb (GtkWidget *widget, gpointer data)
{
	/* show the help */
	gpk_gnome_help ("eula");
}

/**
 * gpk_client_eula_show:
 *
 * Return value: if we agreed
 * TODO: Add in gconf checks to see if we've already agreed
 **/
gboolean
gpk_client_eula_show (GtkWindow *window, const gchar *eula_id, const gchar *package_id,
		      const gchar *vendor_name, const gchar *license_agreement)
{
	GladeXML *glade_xml;
	GtkWidget *widget;
	GtkTextBuffer *buffer;
	gchar *text;
	PkPackageId *ident;

	g_return_val_if_fail (eula_id != NULL, FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (vendor_name != NULL, FALSE);
	g_return_val_if_fail (license_agreement != NULL, FALSE);

	glade_xml = glade_xml_new (PK_DATA "/gpk-eula.glade", NULL, NULL);

	/* connect up default actions */
	widget = glade_xml_get_widget (glade_xml, "window_eula");
	g_signal_connect_swapped (widget, "delete_event", G_CALLBACK (gtk_main_quit), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_cancel");
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);

	/* set icon name */
	widget = glade_xml_get_widget (glade_xml, "window_eula");
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* connect up buttons */
	widget = glade_xml_get_widget (glade_xml, "button_agree");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_eula_button_agree_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_eula_button_help_cb), NULL);

	/* title */
	widget = glade_xml_get_widget (glade_xml, "label_title");
	ident = pk_package_id_new_from_string (package_id);
	text = g_strdup_printf ("<b><big>License required for %s by %s</big></b>", ident->name, vendor_name);
	gtk_label_set_label (GTK_LABEL (widget), text);
	pk_package_id_free (ident);
	g_free (text);

	buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_insert_at_cursor (buffer, license_agreement, strlen (license_agreement));
	widget = glade_xml_get_widget (glade_xml, "textview_details");
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);

	/* set minimum size a bit bigger */
	gtk_widget_set_size_request (widget, 100, 200);

	/* make modal if window set */
	widget = glade_xml_get_widget (glade_xml, "window_eula");
	if (window != NULL) {
		gtk_window_set_transient_for (GTK_WINDOW (widget), window);
	}

	/* show window */
	gtk_widget_show (widget);

	/* wait for button press */
	has_agreed_eula = FALSE;
	gtk_main ();

	/* hide window */
	if (GTK_IS_WIDGET (widget)) {
		gtk_widget_hide (widget);
	}
	g_object_unref (glade_xml);
	g_object_unref (buffer);

	return has_agreed_eula;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
gpk_client_eula_self_test (gpointer data)
{
	LibSelfTest *test = (LibSelfTest *) data;

	if (libst_start (test, "GpkClientEula", CLASS_AUTO) == FALSE) {
		return;
	}
	libst_end (test);
}
#endif

