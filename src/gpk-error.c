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
#include <unistd.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <pk-common.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-common.h"

/**
 * gpk_error_dialog_modal_with_time:
 * @window: the parent dialog
 * @title: the localised text to put in bold as a title
 * @message: the localised text to put as a message
 * @details: the geeky text to in the expander, or %NULL if nothing
 *
 * Shows a modal error, and blocks until the user clicks close
 **/
gboolean
gpk_error_dialog_modal_with_time (GtkWindow *window, const gchar *title, const gchar *message, const gchar *details, guint timestamp)
{
	GtkWidget *widget;
	GladeXML *glade_xml;
	GtkTextBuffer *buffer = NULL;
	gchar *text;

	g_return_val_if_fail (title != NULL, FALSE);
	g_return_val_if_fail (message != NULL, FALSE);

	glade_xml = glade_xml_new (PK_DATA "/gpk-error.glade", NULL, NULL);

	/* connect up actions */
	widget = glade_xml_get_widget (glade_xml, "window_error");
	g_signal_connect_swapped (widget, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	/* make modal if window set */
	if (window != NULL)
		gtk_window_set_transient_for (GTK_WINDOW (widget), window);

	/* set icon name */
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* close button */
	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);

	/* title */
	widget = glade_xml_get_widget (glade_xml, "label_title");
	text = g_strdup_printf ("<b><big>%s</big></b>", title);
	gtk_label_set_label (GTK_LABEL (widget), text);
	g_free (text);

	/* message */
	widget = glade_xml_get_widget (glade_xml, "label_message");
	gtk_label_set_label (GTK_LABEL (widget), message);

	/* show text in the expander */
	if (egg_strzero (details)) {
		widget = glade_xml_get_widget (glade_xml, "expander_details");
		gtk_widget_hide (widget);
	} else {
		buffer = gtk_text_buffer_new (NULL);
//		text = g_markup_escape_text (details, -1);
		gtk_text_buffer_insert_at_cursor (buffer, details, strlen (details));
		widget = glade_xml_get_widget (glade_xml, "textview_details");
		gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);
	}

	/* show window */
	widget = glade_xml_get_widget (glade_xml, "window_error");
	gtk_window_present_with_time (GTK_WINDOW (widget), timestamp);

	/* wait for button press */
	gtk_main ();

	/* hide window */
	if (GTK_IS_WIDGET (widget))
		gtk_widget_hide (widget);
	g_object_unref (glade_xml);
	if (buffer != NULL)
		g_object_unref (buffer);
	return TRUE;
}

/**
 * gpk_error_dialog_modal:
 * @window: the parent dialog
 * @title: the localised text to put in bold as a title
 * @message: the localised text to put as a message
 * @details: the geeky text to in the expander, or %NULL if nothing
 *
 * Shows a modal error, and blocks until the user clicks close
 **/
gboolean
gpk_error_dialog_modal (GtkWindow *window, const gchar *title, const gchar *message, const gchar *details)
{
	return gpk_error_dialog_modal_with_time (window, title, message, details, 0);
}

/**
 * gpk_error_dialog:
 * @title: the localised text to put in bold as a title
 * @message: the localised text to put as a message
 * @details: the geeky text to in the expander, or %NULL if nothing
 *
 * Shows a modal error, and blocks until the user clicks close
 **/
gboolean
gpk_error_dialog (const gchar *title, const gchar *message, const gchar *details)
{
	return gpk_error_dialog_modal (NULL, title, message, details);
}

