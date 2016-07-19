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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <string.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

#include "gpk-common.h"
#include "gpk-error.h"

static void
gpk_error_dialog_expanded_cb (GObject *object, GParamSpec *param_spec, GtkBuilder *builder)
{
	GtkWindow *window;
	GtkExpander *expander;
	expander = GTK_EXPANDER (object);

	/* only resizable when expanded */
	window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_error"));
	if (gtk_expander_get_expanded (expander))
		gtk_window_set_resizable (window, TRUE);
	else
		gtk_window_set_resizable (window, FALSE);
}

/**
 * gpk_error_dialog_modal_with_time:
 * @window: the parent dialog
 * @title: the localized text to put in bold as a title
 * @message: the localized text to put as a message
 * @details: the geeky text to in the expander, or %NULL if nothing
 *
 * Shows a modal error, and blocks until the user clicks close
 **/
static gboolean
gpk_error_dialog_modal_with_time (GtkWindow *window, const gchar *title, const gchar *message, const gchar *details, guint timestamp)
{
	GtkWidget *widget;
	g_autoptr(GtkBuilder) builder = NULL;
	g_autoptr(GtkTextBuffer) buffer = NULL;
	guint retval;
	g_autoptr(GError) error = NULL;

	g_return_val_if_fail (message != NULL, FALSE);

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_resource (builder,
						"/org/gnome/packagekit/gpk-error.ui",
						&error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		return FALSE;
	}

	/* connect up actions */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_error"));
	gtk_window_set_resizable (GTK_WINDOW (widget), FALSE);
	g_signal_connect_swapped (widget, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	/* never use a title */
	gtk_window_set_title (GTK_WINDOW (widget), "");

	/* make modal if window set */
	if (window != NULL)
		gtk_window_set_transient_for (GTK_WINDOW (widget), window);
	else
		gtk_window_set_modal (GTK_WINDOW (widget), TRUE);

	/* set icon name */
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* close button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);

	/* we become resizable when the expander is expanded */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "expander_details"));
	g_signal_connect (widget, "notify::expanded", G_CALLBACK (gpk_error_dialog_expanded_cb), builder);

	/* title */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_title"));
	gtk_label_set_label (GTK_LABEL (widget), title);

	/* message */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_message"));
	gtk_label_set_markup (GTK_LABEL (widget), message);

	/* show text in the expander */
	if (details == NULL || details[0] == '\0') {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "expander_details"));
		gtk_widget_hide (widget);
	} else {
		buffer = gtk_text_buffer_new (NULL);
//		text = g_markup_escape_text (details, -1);
		gtk_text_buffer_insert_at_cursor (buffer, details, strlen (details));
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "textview_details"));
		gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);
	}

	/* show window */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_error"));
	gtk_window_present_with_time (GTK_WINDOW (widget), timestamp);
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* wait for button press */
	gtk_main ();

	/* hide window */
	if (GTK_IS_WIDGET (widget))
		gtk_widget_hide (widget);
	return TRUE;
}

/**
 * gpk_error_dialog_modal:
 * @window: the parent dialog
 * @title: the localized text to put in bold as a title
 * @message: the localized text to put as a message
 * @details: the geeky text to in the expander, or %NULL if nothing
 *
 * Shows a modal error, and blocks until the user clicks close
 **/
gboolean
gpk_error_dialog_modal (GtkWindow *window, const gchar *title, const gchar *message, const gchar *details)
{
	return gpk_error_dialog_modal_with_time (window, title, message, details, 0);
}
