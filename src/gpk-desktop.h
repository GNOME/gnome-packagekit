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

#ifndef __GPK_DESKTOP_H
#define __GPK_DESKTOP_H

#include <glib.h>
#include <packagekit-glib2/packagekit.h>

G_BEGIN_DECLS

gchar		*gpk_desktop_guess_best_file		(PkDesktop	*desktop,
							 const gchar	*package);
gchar		*gpk_desktop_guess_icon_name		(PkDesktop	*desktop,
							 const gchar	*package);
gchar		*gpk_desktop_guess_localised_name	(PkDesktop	*desktop,
							 const gchar	*package);
gint		 gpk_desktop_get_file_weight		(const gchar	*filename);
gboolean	 gpk_desktop_check_icon_valid		(const gchar	*icon);

G_END_DECLS

#endif	/* __GPK_DESKTOP_H */
