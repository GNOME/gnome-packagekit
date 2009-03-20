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

#ifndef __GPK_HELPER_EULA_H
#define __GPK_HELPER_EULA_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GPK_TYPE_HELPER_EULA		(gpk_helper_eula_get_type ())
#define GPK_HELPER_EULA(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_HELPER_EULA, GpkHelperEula))
#define GPK_HELPER_EULA_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_HELPER_EULA, GpkHelperEulaClass))
#define GPK_IS_HELPER_EULA(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_HELPER_EULA))
#define GPK_IS_HELPER_EULA_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_HELPER_EULA))
#define GPK_HELPER_EULA_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_HELPER_EULA, GpkHelperEulaClass))
#define GPK_HELPER_EULA_ERROR		(gpk_helper_eula_error_quark ())
#define GPK_HELPER_EULA_TYPE_ERROR	(gpk_helper_eula_error_get_type ())

typedef struct GpkHelperEulaPrivate GpkHelperEulaPrivate;

typedef struct
{
	 GObject			 parent;
	 GpkHelperEulaPrivate	*priv;
} GpkHelperEula;

typedef struct
{
	void		(* event)			(GpkHelperEula		*helper,
							 GtkResponseType	 type,
							 const gchar		*eula_id);
	GObjectClass	parent_class;
} GpkHelperEulaClass;

GType		 gpk_helper_eula_get_type	  	(void);
GpkHelperEula	*gpk_helper_eula_new			(void);
gboolean	 gpk_helper_eula_set_parent		(GpkHelperEula		*helper,
							 GtkWindow		*window);
gboolean	 gpk_helper_eula_show			(GpkHelperEula		*helper,
							 const gchar		*eula_id,
							 const gchar		*package_id,
							 const gchar		*vendor_name,
							 const gchar		*license_agreement);

G_END_DECLS

#endif /* __GPK_HELPER_EULA_H */
