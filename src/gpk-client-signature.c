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
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"

#include "gpk-gnome.h"
#include "gpk-common.h"
#include "gpk-client-signature.h"

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
gpk_client_signature_show (GtkWindow *window, const gchar *package_id, const gchar *repository_name,
			   const gchar *key_url, const gchar *key_userid, const gchar *key_id,
			   const gchar *key_fingerprint, const gchar *key_timestamp)
{
	GtkWidget *widget;
	GtkBuilder *builder;
	guint retval;
	GError *error = NULL;

	g_return_val_if_fail (package_id != NULL, FALSE);

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-signature.ui", &error);
	if (error != NULL) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out_build;
	}

	/* connect up default actions */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_gpg"));
	g_signal_connect_swapped (widget, "delete_event", G_CALLBACK (gtk_main_quit), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_no"));
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);

	/* set icon name */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_gpg"));
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* connect up buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_yes"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_signature_button_yes_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_help"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_signature_button_help_cb), NULL);

	/* show correct text */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_name"));
	gtk_label_set_label (GTK_LABEL (widget), repository_name);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_url"));
	gtk_label_set_label (GTK_LABEL (widget), key_url);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_user"));
	gtk_label_set_label (GTK_LABEL (widget), key_userid);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_id"));
	gtk_label_set_label (GTK_LABEL (widget), key_id);

	/* make modal if window set */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_gpg"));
	if (window != NULL)
		gtk_window_set_transient_for (GTK_WINDOW (widget), window);

	/* show window */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_gpg"));
	gtk_widget_show (widget);

	/* wait for button press */
	has_imported_signature = FALSE;
	gtk_main ();

	/* hide window */
	if (GTK_IS_WIDGET (widget))
		gtk_widget_hide (widget);
out_build:
	g_object_unref (builder);
	return has_imported_signature;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpk_client_signature_self_test (gpointer data)
{
	EggTest *test = (EggTest *) data;

	if (egg_test_start (test, "GpkClientEula") == FALSE)
		return;
	egg_test_end (test);
}
#endif

