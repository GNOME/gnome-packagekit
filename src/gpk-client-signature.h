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

#ifndef __GPK_CLIENT_SIGNATURE_H
#define __GPK_CLIENT_SIGNATURE_H

#include <glib-object.h>
#include <pk-enum.h>

G_BEGIN_DECLS

void		 gpk_client_signature_self_test		(gpointer	 data);
gboolean	 gpk_client_signature_show		(const gchar	*package_id,
							 const gchar	*repository_name,
							 const gchar	*key_url,
							 const gchar	*key_userid,
							 const gchar	*key_id,
							 const gchar	*key_fingerprint,
							 const gchar	*key_timestamp);

G_END_DECLS

#endif	/* __GPK_CLIENT_SIGNATURE_H */
