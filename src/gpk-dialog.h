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

#ifndef __GPK_DIALOG_H
#define __GPK_DIALOG_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

G_BEGIN_DECLS

gboolean	 gpk_dialog_embed_package_list_widget	(GtkDialog	*dialog,
							 GPtrArray	*array);
gboolean	 gpk_dialog_embed_file_list_widget	(GtkDialog	*dialog,
							 GPtrArray	*files);
gboolean	 gpk_dialog_embed_do_not_show_widget	(GtkDialog	*dialog,
							 const gchar	*key);
gchar		*gpk_dialog_package_id_name_join_locale	(gchar		**package_ids);
gboolean	 gpk_dialog_embed_tabbed_widget		(GtkDialog      *dialog,
							 GtkNotebook	*tabbed_widget);
gboolean	 gpk_dialog_tabbed_package_list_widget	(GtkWidget	*tab_page,
							 GPtrArray	*array);
gboolean	 gpk_dialog_tabbed_download_size_widget	(GtkWidget	*tab_page,
							 const gchar	*title,
							 guint64	 size);

G_END_DECLS

#endif	/* __GPK_DIALOG_H */
