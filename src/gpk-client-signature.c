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

static gboolean has_imported_signature = FALSE;

/**
 * gpk_client_signature_button_yes_cb:
 **/
static void
gpk_client_signature_button_yes_cb (GtkWidget *widget_button, gpointer data)
{
	has_imported_signature = TRUE;
	gtk_main_quit ();
}

/**
 * gpk_client_signature_button_help_cb:
 **/
static void
gpk_client_signature_button_help_cb (GtkWidget *widget, gpointer data)
{
	/* show the help */
	gpk_gnome_help ("gpg-signature");
}

/**
 * gpk_client_signature_show:
 *
 * Return value: if we agreed
 **/
gboolean
gpk_client_signature_show (const gchar *package_id, const gchar *repository_name,
			   const gchar *key_url, const gchar *key_userid, const gchar *key_id,
			   const gchar *key_fingerprint, const gchar *key_timestamp)
{
	GtkWidget *widget;
	GladeXML *glade_xml;

	g_return_val_if_fail (package_id != NULL, FALSE);

	glade_xml = glade_xml_new (PK_DATA "/gpk-signature.glade", NULL, NULL);

	/* connect up default actions */
	widget = glade_xml_get_widget (glade_xml, "window_gpg");
	g_signal_connect_swapped (widget, "delete_event", G_CALLBACK (gtk_main_quit), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_no");
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);

	/* set icon name */
	widget = glade_xml_get_widget (glade_xml, "window_gpg");
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* connect up buttons */
	widget = glade_xml_get_widget (glade_xml, "button_yes");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_signature_button_yes_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_signature_button_help_cb), NULL);

	/* show correct text */
	widget = glade_xml_get_widget (glade_xml, "label_name");
	gtk_label_set_label (GTK_LABEL (widget), repository_name);
	widget = glade_xml_get_widget (glade_xml, "label_url");
	gtk_label_set_label (GTK_LABEL (widget), key_url);
	widget = glade_xml_get_widget (glade_xml, "label_user");
	gtk_label_set_label (GTK_LABEL (widget), key_userid);
	widget = glade_xml_get_widget (glade_xml, "label_id");
	gtk_label_set_label (GTK_LABEL (widget), key_id);

	/* show window */
	widget = glade_xml_get_widget (glade_xml, "window_gpg");
	gtk_widget_show (widget);

	/* wait for button press */
	has_imported_signature = FALSE;
	gtk_main ();

	/* hide window */
	if (GTK_IS_WIDGET (widget)) {
		gtk_widget_hide (widget);
	}
	g_object_unref (glade_xml);

	return has_imported_signature;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
gpk_client_signature_self_test (gpointer data)
{
	LibSelfTest *test = (LibSelfTest *) data;

	if (libst_start (test, "GpkClientEula", CLASS_AUTO) == FALSE) {
		return;
	}
	libst_end (test);
}
#endif

