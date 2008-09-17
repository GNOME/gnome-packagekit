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
#include <pk-client.h>
#include <pk-common.h>
#include <pk-package-id.h>
#include <pk-package-ids.h>
#include "gpk-gnome.h"
#include "gpk-error.h"
#include "gpk-common.h"

static PkClient *client = NULL;

/**
 * gpk_client_resolve_show:
 *
 * Return value: if we agreed to remove the deps
 **/
gchar **
gpk_client_resolve_show (GtkWindow *window, gchar **packages)
{
	GError *error = NULL;
	const PkPackageObj *obj;
	PkPackageList *list;
	PkPackageId *id = NULL;
	gchar **package_ids = NULL;
	gchar *text;
	gboolean already_installed = FALSE;
	gboolean ret;
	gchar *title;
	guint len;
	guint i;

	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, TRUE, NULL);

	/* reset */
	ret = pk_client_reset (client, &error);
	if (!ret) {
		gpk_error_dialog_modal (window, _("Failed to reset client"), NULL, error->message);
		g_error_free (error);
		goto out;
	}

	/* find out if we can find a package */
	ret = pk_client_resolve (client, PK_FILTER_ENUM_NONE, packages, &error);
	if (!ret) {
		gpk_error_dialog_modal (window, _("Failed to resolve package"),
					_("Incorrect response from search"), error->message);
		g_error_free (error);
		goto out;
	}

	/* found nothing? */
	list = pk_client_get_package_list (client);
	len = pk_package_list_get_size (list);
	if (len == 0) {
		text = pk_package_ids_to_text (packages, ",");
		title = g_strdup_printf (_("Could not find %s"), text);
		g_free (text);
		gpk_error_dialog_modal (window, title, _("The packages could not be found in any software source"), NULL);
		g_free (title);
		goto out;
	}

	/* see what we've got already */
	for (i=0; i<len; i++) {
		obj = pk_package_list_get_obj (list, i);
		if (obj->info == PK_INFO_ENUM_INSTALLED) {
			already_installed = TRUE;
		} else if (obj->info == PK_INFO_ENUM_AVAILABLE) {
			egg_debug ("package '%s' resolved", obj->id->name);
			id = obj->id;
			//TODO: we need to list these in a gpk-client-chooser
		}
	}

	/* already installed? */
	if (already_installed) {
		text = pk_package_ids_to_text (packages, ",");
		title = g_strdup_printf (_("Failed to install %s"), text);
		g_free (text);
		gpk_error_dialog_modal (window, title, _("The package is already installed"), NULL);
		g_free (title);
		goto out;
	}

	/* got junk? */
	if (id == NULL) {
		gpk_error_dialog_modal (window, _("Failed to find package"), _("Incorrect response from search"), NULL);
		goto out;
	}

	/* convert to data */
	package_ids = pk_package_list_to_strv (list);
	g_object_unref (list);

out:
	g_object_unref (client);
	return package_ids;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpk_client_resolve_self_test (gpointer data)
{
	EggTest *test = (EggTest *) data;

	if (egg_test_start (test, "GpkClientResolve") == FALSE) {
		return;
	}
	egg_test_end (test);
}
#endif

