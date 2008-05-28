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
#include <pk-common.h>
#include <pk-package-id.h>
#include "gpk-gnome.h"
#include "gpk-error.h"
#include "gpk-common.h"

static PkClient *client = NULL;

static gchar *
gpk_client_resolve_indervidual (const gchar *package)
{
	GError *error = NULL;
	PkPackageItem *item;
	PkPackageList *list;
	gchar *package_id = NULL;
	gboolean already_installed = FALSE;
	gboolean ret;
	gchar *title;
	guint len;
	guint i;

	/* reset */
	ret = pk_client_reset (client, &error);
	if (!ret) {
		gpk_error_dialog (_("Failed to reset client"), NULL, error->message);
		g_error_free (error);
		goto out;
	}

	/* find out if we can find a package */
	ret = pk_client_resolve (client, PK_FILTER_ENUM_NONE, package, &error);
	if (!ret) {
		gpk_error_dialog (_("Failed to resolve package"),
				  _("Incorrect response from search"),
				  error->message);
		g_error_free (error);
		goto out;
	}

	/* found nothing? */
	list = pk_client_get_package_list (client);
	len = pk_package_list_get_size (list);
	if (len == 0) {
		title = g_strdup_printf (_("Could not find %s"), package);
		gpk_error_dialog (title, _("The package could not be found in any software sources"), NULL);
		g_free (title);
		goto out;
	}

	/* see what we've got already */
	for (i=0; i<len; i++) {
		item = pk_package_list_get_item (list, i);
		if (item->info == PK_INFO_ENUM_INSTALLED) {
			already_installed = TRUE;
		} else if (item->info == PK_INFO_ENUM_AVAILABLE) {
			pk_debug ("package '%s' resolved", item->package_id);
			//TODO: we need to list these in a gpk-client-chooser
			if (package_id != NULL) {
				pk_warning ("throwing away %s", package_id);
				g_free (package_id);
			}
			package_id = g_strdup (item->package_id);
		}
	}
	g_object_unref (list);

	/* already installed? */
	if (already_installed) {
		title = g_strdup_printf (_("Failed to install %s"), package);
		gpk_error_dialog (title, _("The package is already installed"), NULL);
		g_free (title);
		goto out;
	}

	/* got junk? */
	if (package_id == NULL) {
		gpk_error_dialog (_("Failed to find package"), _("Incorrect response from search"), NULL);
		goto out;
	}
out:
	return package_id;
}

/**
 * gpk_client_resolve_show:
 *
 * Return value: if we agreed to remove the deps
 **/
gchar **
gpk_client_resolve_show (gchar **packages)
{
	guint i;
	guint length;
	gchar *package_id;
	gchar **package_ids = NULL;
	GPtrArray *array;

	array = g_ptr_array_new ();
	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, TRUE, NULL);

	length = g_strv_length (packages);
	for (i=0; i<length; i++) {
		package_id = gpk_client_resolve_indervidual (packages[i]);
		if (package_id == NULL) {
			goto out;
		}
		g_ptr_array_add (array, package_id);
	}
	package_ids = pk_ptr_array_to_argv (array);

out:
	g_ptr_array_free (array, TRUE);
	g_object_unref (client);
	return package_ids;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
gpk_client_resolve_self_test (gpointer data)
{
	LibSelfTest *test = (LibSelfTest *) data;

	if (libst_start (test, "GpkClientResolve", CLASS_AUTO) == FALSE) {
		return;
	}
	libst_end (test);
}
#endif

