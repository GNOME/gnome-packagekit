/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __GPK_HELPER_MEDIA_CHANGE_H
#define __GPK_HELPER_MEDIA_CHANGE_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <packagekit-glib/packagekit.h>

#include "gpk-enum.h"

G_BEGIN_DECLS

#define GPK_TYPE_HELPER_MEDIA_CHANGE		(gpk_helper_media_change_get_type ())
#define GPK_HELPER_MEDIA_CHANGE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_HELPER_MEDIA_CHANGE, GpkHelperMediaChange))
#define GPK_HELPER_MEDIA_CHANGE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_HELPER_MEDIA_CHANGE, GpkHelperMediaChangeClass))
#define GPK_IS_HELPER_MEDIA_CHANGE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_HELPER_MEDIA_CHANGE))
#define GPK_IS_HELPER_MEDIA_CHANGE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_HELPER_MEDIA_CHANGE))
#define GPK_HELPER_MEDIA_CHANGE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_HELPER_MEDIA_CHANGE, GpkHelperMediaChangeClass))
#define GPK_HELPER_MEDIA_CHANGE_ERROR		(gpk_helper_media_change_error_quark ())
#define GPK_HELPER_MEDIA_CHANGE_TYPE_ERROR	(gpk_helper_media_change_error_get_type ())

typedef struct GpkHelperMediaChangePrivate GpkHelperMediaChangePrivate;

typedef struct
{
	 GObject			 parent;
	 GpkHelperMediaChangePrivate	*priv;
} GpkHelperMediaChange;

typedef struct
{
	void		(* event)			(GpkHelperMediaChange	*helper,
							 GtkResponseType	 type);
	GObjectClass	parent_class;
} GpkHelperMediaChangeClass;

GType			 gpk_helper_media_change_get_type	(void);
GpkHelperMediaChange	*gpk_helper_media_change_new		(void);
gboolean		 gpk_helper_media_change_set_parent	(GpkHelperMediaChange	*helper,
								 GtkWindow		*window);
gboolean		 gpk_helper_media_change_show		(GpkHelperMediaChange	*helper,
								 PkMediaTypeEnum	 type,
								 const gchar		*media_id,
								 const gchar		*media_text);

G_END_DECLS

#endif /* __GPK_HELPER_MEDIA_CHANGE_H */
