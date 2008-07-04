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
#include <gconf/gconf-client.h>

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-package-id.h>
#include <pk-package-list.h>
#include "gpk-gnome.h"
#include "gpk-error.h"
#include "gpk-common.h"
#include "gpk-dialog.h"
#include "gpk-client-private.h"

static PkClient *client = NULL;
static GConfClient *gconf_client = NULL;

/**
 * gpk_client_status_changed_cb:
 **/
static void
gpk_client_status_changed_cb (PkClient *client, PkStatusEnum status, GpkClient *gclient)
{
	gpk_client_set_status (gclient, status);
}

/**
 * gpk_client_depends_show:
 *
 * Return value: if we agreed to remove the deps
 **/
gboolean
gpk_client_depends_show (GpkClient *gclient, gchar **package_ids)
{
	GtkWidget *dialog;
	GtkWindow *window;
	GtkResponseType button;
	PkPackageList *list;
	gboolean ret;
	guint length;
	gchar *name;
	gchar *title;
	gchar *message;
	gchar *button_text;
	GError *error = NULL;

	list = pk_package_list_new ();
	gconf_client = gconf_client_get_default ();

	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, TRUE, NULL);
	g_signal_connect (client, "status-changed",
			  G_CALLBACK (gpk_client_status_changed_cb), gclient);

	/* have we previously said we don't want to be shown the confirmation */
	ret = gconf_client_get_bool (gconf_client, GPK_CONF_SHOW_DEPENDS, NULL);
	if (!ret) {
		pk_debug ("we've said we don't want the dep dialog");
		ret = TRUE;
		goto out;
	}

	/* get the packages we depend on */
	gpk_client_set_title (gclient, _("Finding packages we depend on"));

	/* reset */
	ret = pk_client_reset (client, &error);
	if (!ret) {
		window = gpk_client_get_window (gclient);
		gpk_error_dialog_modal (window, _("Failed to reset client"), NULL, error->message);
		g_error_free (error);
		ret = FALSE;
		goto out;
	}

	/* find out if this would drag in other packages */
	ret = pk_client_get_depends (client, PK_FILTER_ENUM_NOT_INSTALLED, package_ids, TRUE, &error);
	if (!ret) {
		window = gpk_client_get_window (gclient);
		gpk_error_dialog_modal (window, _("Failed to get depends"),
					_("Could not work out what packages would be also installed"),
					error->message);
		g_error_free (error);
		ret = FALSE;
		goto out;
	}

	/* these are the new packages */
	list = pk_client_get_package_list (client);
	length = pk_package_list_get_size (list);
	if (length == 0) {
		ret = TRUE;
		goto out;
	}

	/* title */
	title = g_strdup_printf (ngettext ("%i additional package also has to be installed",
					   "%i additional packages also have to be installed",
					   length), length);

	/* button */
	button_text = g_strdup_printf (ngettext ("Install package",
						 "Install packages", length));

	/* message */
	name = gpk_dialog_package_id_name_join_locale (package_ids);
	message = g_strdup_printf (ngettext ("To install %s, an additional package also has to be downloaded.",
					     "To install %s, additional packages also have to be downloaded.",
					     length), name);
	g_free (name);

	/* show UI */
	window = gpk_client_get_window (gclient);
	dialog = gtk_message_dialog_new (window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_QUESTION, GTK_BUTTONS_CANCEL, "%s", title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gtk_dialog_add_button (GTK_DIALOG (dialog), button_text, GTK_RESPONSE_OK);
	gpk_dialog_embed_package_list_widget (GTK_DIALOG (dialog), list);
	gpk_dialog_embed_do_not_show_widget (GTK_DIALOG (dialog), GPK_CONF_SHOW_DEPENDS);

	g_free (button_text);
	g_free (title);
	g_free (message);

	button = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK) {
		ret = FALSE;
	}

out:
	g_object_unref (list);
	g_object_unref (client);
	g_object_unref (gconf_client);
	return ret;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
gpk_client_depends_self_test (gpointer data)
{
	LibSelfTest *test = (LibSelfTest *) data;

	if (libst_start (test, "GpkClientDepends", CLASS_AUTO) == FALSE) {
		return;
	}
	libst_end (test);
}
#endif

