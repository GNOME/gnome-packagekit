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
#include <gtk/gtk.h>
#include <string.h>

#include "gpk-gnome.h"

/**
 * gpk_gnome_open:
 * @url: a url such as <literal>http://www.hughsie.com</literal>
 **/
gboolean
gpk_gnome_open (const gchar *url)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (url != NULL, FALSE);

	ret = gtk_show_uri (NULL, url, GDK_CURRENT_TIME, &error);

	if (!ret) {
		g_warning ("spawn of '%s' failed", url);
		g_error_free (error);
	}
	return ret;
}

/**
 * gpk_gnome_help:
 * @link_id: Subsection of gnome-packagekit help file, or %NULL.
 **/
gboolean
gpk_gnome_help (const gchar *link_id)
{
	GError *error = NULL;
	gchar *uri;
	gboolean ret = TRUE;

	if (link_id)
		uri = g_strconcat ("help:gnome-packagekit/", link_id, NULL);
	else
		uri = g_strdup ("help:gnome-packagekit");
	g_debug ("opening uri %s", uri);

	gtk_show_uri (NULL, uri, GDK_CURRENT_TIME, &error);

	if (error != NULL) {
		GtkWidget *d;
		d = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					    GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", error->message);
		gtk_dialog_run (GTK_DIALOG(d));
		gtk_widget_destroy (d);
		g_error_free (error);
		ret = FALSE;
	}

	g_free (uri);
	return ret;
}

