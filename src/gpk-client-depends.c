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

static PkClient *client = NULL;
static GConfClient *gconf_client = NULL;

/**
 * gpk_client_checkbutton_show_depends_cb:
 **/
static void
gpk_client_checkbutton_show_depends_cb (GtkWidget *widget, gpointer data)
{
	gboolean checked;
	/* set the policy */
	checked = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	pk_debug ("Changing %s to %i", GPK_CONF_SHOW_DEPENDS, checked);
	gconf_client_set_bool (gconf_client, GPK_CONF_SHOW_DEPENDS, checked, NULL);
}

static gboolean
gpk_client_depends_indervidual (PkPackageList *list, const gchar *package_id)
{
	GError *error = NULL;
	PkPackageItem *item;
	gboolean ret;
	guint len;
	guint i;

	/* reset */
	ret = pk_client_reset (client, &error);
	if (!ret) {
		gpk_error_dialog (_("Failed to reset client"), NULL, error->message);
		g_error_free (error);
		return FALSE;
	}

	/* find out if this would drag in other packages */
	ret = pk_client_get_depends (client, PK_FILTER_ENUM_NOT_INSTALLED, package_id, TRUE, &error);
	if (!ret) {
		gpk_error_dialog (_("Failed to get depends"),
				  _("Could not work out what packages would be also installed"),
				  error->message);
		g_error_free (error);
		return FALSE;
	}

	/* no additional packages? */
	len = pk_client_package_buffer_get_size	(client);
	if (len == 0) {
		pk_debug ("no additional deps");
		return TRUE;
	}

	/* process package list */
	for (i=0; i<len; i++) {
		item = pk_client_package_buffer_get_item (client, i);
		pk_package_list_add (list, item->info, item->package_id, item->summary);
	}
	return TRUE;
}

/**
 * gpk_client_depends_show:
 *
 * Return value: if we agreed to remove the deps
 **/
gboolean
gpk_client_depends_show (gchar **package_ids)
{
	GtkWidget *widget;
	GtkWidget *dialog;
	GtkResponseType button;
	PkPackageList *list;
	gboolean ret;
	guint length;
	guint i;
	GString *string;
	PkPackageItem *item;
	gchar *text;

	list = pk_package_list_new ();
	gconf_client = gconf_client_get_default ();

	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, TRUE, NULL);

	/* have we previously said we don't want to be shown the confirmation */
	ret = gconf_client_get_bool (gconf_client, GPK_CONF_SHOW_DEPENDS, NULL);
	if (!ret) {
		pk_debug ("we've said we don't want the dep dialog");
		ret = TRUE;
		goto out;
	}

	length = g_strv_length (package_ids);
	for (i=0; i<length; i++) {
		ret = gpk_client_depends_indervidual (list, package_ids[i]);
		if (!ret) {
			ret = FALSE;
			goto out;
		}
	}

	/* process package list */
	string = g_string_new (_("The following packages also have to be downloaded:"));
	g_string_append (string, "\n\n");
	length = pk_package_list_get_size (list);
	/* shortcut */
	if (length == 0) {
		goto out;
	}
	for (i=0; i<length; i++) {
		item = pk_package_list_get_item (list, i);
		text = gpk_package_id_format_oneline (item->package_id, item->summary);
		g_string_append_printf (string, "%s\n", text);
		g_free (text);
	}
	/* remove last \n */
	g_string_set_size (string, string->len - 1);

	/* display messagebox  */
	text = g_string_free (string, FALSE);

	/* show UI */
	dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_QUESTION, GTK_BUTTONS_CANCEL,
					 "%s", _("Install additional packages?"));
	/* add a specialist button */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Install"), GTK_RESPONSE_OK);

	/* add a checkbutton for deps screen */
	widget = gtk_check_button_new_with_label (_("Do not show me this again"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_checkbutton_show_depends_cb), NULL);
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), widget);
	gtk_widget_show (widget);

	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", text);
	button = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
	g_free (text);

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

