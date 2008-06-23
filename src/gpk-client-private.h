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

#ifndef __GPK_CLIENT_PRIVATE_H
#define __GPK_CLIENT_PRIVATE_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <pk-enum.h>
#include "gpk-client.h"

G_BEGIN_DECLS

GtkWindow	*gpk_client_get_window			(GpkClient	*gclient);
gboolean	 gpk_client_set_title			(GpkClient	*gclient,
							 const gchar	*title);
gboolean	 gpk_client_set_package_label		(GpkClient	*gclient,
							 const gchar	*text);
gboolean	 gpk_client_set_percentage		(GpkClient	*gclient,
							 guint		 percentage);
gboolean	 gpk_client_set_status			(GpkClient	*gclient,
							 PkStatusEnum	 status);

G_END_DECLS

#endif /* __GPK_CLIENT_PRIVATE_H */
