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

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-package-id.h>
#include <pk-package-list.h>
#include "gpk-gnome.h"
#include "gpk-error.h"
#include "gpk-common.h"

static PkClient *client = NULL;

/**
 * gpk_client_requires_get_package_ids_name:
 **/
gchar *
gpk_client_requires_get_package_ids_name (gchar **package_ids)
{
	guint i;
	guint length;
	gchar *text;
	GString *string;
	PkPackageId *ident;

	string = g_string_new ("");
	length = g_strv_length (package_ids);

	/* for each name */
	for (i=0; i<length; i++) {
		ident = pk_package_id_new_from_string (package_ids[i]);
		g_string_append_printf (string, "%s, ", ident->name);
		pk_package_id_free (ident);
	}

	/* remove last ', ' */
	if (string->len > 2) {
		g_string_set_size (string, string->len - 2);
	}

	text = g_string_free (string, FALSE);
	return text;
}

/**
 * gpk_client_requires_show:
 *
 * Return value: if we agreed to remove the deps
 **/
gboolean
gpk_client_requires_show (GtkWindow *window, gchar **package_ids)
{
	GtkWidget *dialog;
	GtkResponseType button;
	PkPackageList *list;
	gboolean ret;
	guint length;
	guint i;
	GString *string;
	const PkPackageObj *obj;
	gchar *text;
	gchar *name;
	GError *error = NULL;

	list = pk_package_list_new ();

	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, TRUE, NULL);

	/* reset */
	ret = pk_client_reset (client, &error);
	if (!ret) {
		gpk_error_dialog_modal (window, _("Failed to reset client"), NULL, error->message);
		g_error_free (error);
		ret = FALSE;
		goto out;
	}

	/* find out if this would force removal of other packages */
	ret = pk_client_get_requires (client, PK_FILTER_ENUM_INSTALLED, package_ids, TRUE, &error);
	if (!ret) {
		gpk_error_dialog_modal (window, _("Failed to get requires"),
					_("Could not work out what packages would also be removed"),
					error->message);
		g_error_free (error);
		ret = FALSE;
		goto out;
	}

	/* these are the new packages */
	list = pk_client_get_package_list (client);

	/* sort by package_id */
	pk_package_list_sort (list);

	/* process package list */
	string = g_string_new (_("The following packages have to be removed:"));
	g_string_append (string, "\n\n");
	length = pk_package_list_get_size (list);
	/* shortcut */
	if (length == 0) {
		goto out;
	}
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		text = gpk_package_id_format_oneline (obj->id, obj->summary);
		g_string_append_printf (string, "%s\n", text);
		g_free (text);
	}
	/* remove last \n */
	g_string_set_size (string, string->len - 1);

	name = gpk_client_requires_get_package_ids_name (package_ids);
	text = g_strdup_printf (ngettext ("%i other package depends on %s", "%i other packages depend on %s", length), length, name);
	g_free (name);

	/* show UI */
	dialog = gtk_message_dialog_new (window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_QUESTION, GTK_BUTTONS_CANCEL, "%s", text);
	g_free (text);
	/* add a specialist button */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Remove all packages"), GTK_RESPONSE_OK);

	/* display messagebox  */
	text = g_string_free (string, FALSE);
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
	return ret;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
gpk_client_requires_self_test (gpointer data)
{
	LibSelfTest *test = (LibSelfTest *) data;

	if (libst_start (test, "GpkClientRequires", CLASS_AUTO) == FALSE) {
		return;
	}
	libst_end (test);
}
#endif

