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
#include <gtk/gtk.h>
#include <string.h>

#include "egg-debug.h"

/**
 * gpk_gnome_open:
 * @url: a url such as <literal>http://www.hughsie.com</literal>
 **/
gboolean
gpk_gnome_open (const gchar *url)
{
	gchar *data;
	gboolean ret;

	g_return_val_if_fail (url != NULL, FALSE);

	data = g_strconcat ("gnome-open ", url, NULL);
	ret = g_spawn_command_line_async (data, NULL);
	if (!ret)
		egg_warning ("spawn of '%s' failed", data);
	g_free (data);
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
	gchar *command;
	const gchar *lang;
	gchar *uri = NULL;
	GdkScreen *gscreen;
	gint i;
	gboolean ret = TRUE;
	const gchar *const *langs = g_get_language_names ();

	for (i = 0; langs[i]; i++) {
		lang = langs[i];
		if (strchr (lang, '.'))
			continue;
		uri = g_build_filename (DATADIR, "/gnome/help/gnome-packagekit/",
					lang, "/gnome-packagekit.xml", NULL);
		if (g_file_test (uri, G_FILE_TEST_EXISTS))
			break;
		g_free (uri);
	}
	if (link_id)
		command = g_strconcat ("gnome-open ghelp://", uri, "?", link_id, NULL);
	else
		command = g_strconcat ("gnome-open ghelp://", uri, NULL);
	egg_debug ("using command %s", command);

	gscreen = gdk_screen_get_default ();
	gdk_spawn_command_line_on_screen (gscreen, command, &error);
	if (error != NULL) {
		GtkWidget *d;
		d = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
					    GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", error->message);
		gtk_dialog_run (GTK_DIALOG(d));
		gtk_widget_destroy (d);
		g_error_free (error);
		ret = FALSE;
	}

	g_free (command);
	g_free (uri);
	return ret;
}

