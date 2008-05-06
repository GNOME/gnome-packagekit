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
#include <polkit-gnome/polkit-gnome.h>

#include <pk-debug.h>
#include <pk-enum.h>
#include <pk-package-id.h>
#include "gpk-gnome.h"
#include "gpk-common.h"

static gboolean retry_untrusted = FALSE;

/**
 * gpk_client_untrusted_button_cb:
 **/
static void
gpk_client_untrusted_button_cb (PolKitGnomeAction *action, gpointer data)
{
	pk_debug ("need to retry...");
	retry_untrusted = TRUE;
	gtk_main_quit ();
}

/**
 * gpk_client_untrusted_show:
 *
 * Return value: if we agreed
 * TODO: Add in gconf checks to see if we've already agreed
 **/
gboolean
gpk_client_untrusted_show (PkErrorCodeEnum code)
{
	GtkWidget *widget;
	GtkWidget *button;
	PolKitAction *pk_action;
	GladeXML *glade_xml;
	gchar *text;
	const gchar *title;
	gchar *message;
	PolKitGnomeAction *update_system_action;

	glade_xml = glade_xml_new (PK_DATA "/gpk-error.glade", NULL, NULL);

	/* connect up actions */
	widget = glade_xml_get_widget (glade_xml, "window_error");
	g_signal_connect_swapped (widget, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	/* set icon name */
	widget = glade_xml_get_widget (glade_xml, "window_error");
	gtk_window_set_icon_name (GTK_WINDOW (widget), "system-software-installer");

	/* close button */
	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);

	/* title */
	title = gpk_error_enum_to_localised_text (code);
	widget = glade_xml_get_widget (glade_xml, "label_title");
	text = g_strdup_printf ("<b><big>%s</big></b>", title);
	gtk_label_set_label (GTK_LABEL (widget), text);
	g_free (text);

	/* message */
	message = g_strdup_printf ("%s\n%s",
				   _("Malicious software can damage your computer or cause other harm."),
				   _("Are you <b>sure</b> you want to install this package?");
	widget = glade_xml_get_widget (glade_xml, "label_message");
	gtk_label_set_markup (GTK_LABEL (widget), message);
	g_free (message);

	/* don't show text in the expander */
	widget = glade_xml_get_widget (glade_xml, "expander_details");
	gtk_widget_hide (widget);

	/* add the extra button and connect up to a Policykit action */
	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.localinstall-untrusted");
	update_system_action = polkit_gnome_action_new_default ("localinstall-untrusted", pk_action,
								_("_Force install"),
								_("Force installing package"));
	g_object_set (update_system_action,
		      "no-icon-name", GTK_STOCK_APPLY,
		      "auth-icon-name", GTK_STOCK_APPLY,
		      "yes-icon-name", GTK_STOCK_APPLY,
		      "self-blocked-icon-name", GTK_STOCK_APPLY,
		      NULL);
	polkit_action_unref (pk_action);
	g_signal_connect (update_system_action, "activate",
			  G_CALLBACK (gpk_client_untrusted_button_cb), NULL);
	button = polkit_gnome_action_create_button (update_system_action);
	widget = glade_xml_get_widget (glade_xml, "hbuttonbox2");
	gtk_box_pack_start (GTK_BOX (widget), button, FALSE, FALSE, 0);
	gtk_box_reorder_child (GTK_BOX (widget), button, 0);

	/* show window */
	widget = glade_xml_get_widget (glade_xml, "window_error");
	gtk_widget_show (widget);

	/* wait for button press */
	gtk_main ();

	/* hide window */
	if (GTK_IS_WIDGET (widget)) {
		gtk_widget_hide (widget);
	}
	g_object_unref (glade_xml);
	return retry_untrusted;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
gpk_client_untrusted_self_test (gpointer data)
{
	LibSelfTest *test = (LibSelfTest *) data;

	if (libst_start (test, "GpkClientUntrusted", CLASS_AUTO) == FALSE) {
		return;
	}
	libst_end (test);
}
#endif

