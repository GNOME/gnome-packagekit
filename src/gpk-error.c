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
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-common.h"
#include "gpk-error.h"

/**
 * gpk_error_dialog_expanded_cb:
 **/
static void
gpk_error_dialog_expanded_cb (GObject *object, GParamSpec *param_spec, GladeXML *glade_xml)
{
	GtkWidget *widget;
	GtkExpander *expander;
	expander = GTK_EXPANDER (object);

	/* only resizable when expanded */
	widget = glade_xml_get_widget (glade_xml, "dialog_error");
	if (gtk_expander_get_expanded (expander))
		gtk_window_set_resizable (GTK_WINDOW (widget), TRUE);
	else
		gtk_window_set_resizable (GTK_WINDOW (widget), FALSE);
}

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

	glade_xml = glade_xml_new (GPK_DATA "/gpk-error.glade", NULL, NULL);

	/* connect up actions */
	widget = glade_xml_get_widget (glade_xml, "dialog_error");
	gtk_window_set_resizable (GTK_WINDOW (widget), FALSE);
	g_signal_connect_swapped (widget, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	/* make modal if window set */
	if (window != NULL) {
		gtk_window_set_transient_for (GTK_WINDOW (widget), window);
		gtk_window_set_title (GTK_WINDOW (widget), "");
	} else {
		gtk_window_set_modal (GTK_WINDOW (widget), TRUE);
		gtk_window_set_title (GTK_WINDOW (widget), title);
	}

	/* set icon name */
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* close button */
	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);

	/* we become resizable when the expander is expanded */
	widget = glade_xml_get_widget (glade_xml, "expander_details");
	g_signal_connect (widget, "notify::expanded", G_CALLBACK (gpk_error_dialog_expanded_cb), glade_xml);

	/* title */
	widget = glade_xml_get_widget (glade_xml, "label_title");
	text = g_strdup_printf ("<b><big>%s</big></b>", title);
	gtk_label_set_label (GTK_LABEL (widget), text);
	g_free (text);

	/* message */
	widget = glade_xml_get_widget (glade_xml, "label_message");
	gtk_label_set_markup (GTK_LABEL (widget), message);

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
	widget = glade_xml_get_widget (glade_xml, "dialog_error");
	gtk_window_present_with_time (GTK_WINDOW (widget), timestamp);
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

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

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpk_error_test (EggTest *test)
{
	gboolean ret;

	if (!egg_test_start (test, "GpkError"))
		return;

	/************************************************************/
	egg_test_title (test, "do dialog");
	ret = gpk_error_dialog ("No space is left on the disk",
				"There is insufficient space on the device.\n"
				"Free some space on the system disk to perform this operation.",
				"[Errno 28] No space left on device");
	egg_test_assert (test, ret);

	egg_test_end (test);
}
#endif

