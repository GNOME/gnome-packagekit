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
