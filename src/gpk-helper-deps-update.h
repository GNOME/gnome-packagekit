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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __GPK_HELPER_DEPS_UPDATE_H
#define __GPK_HELPER_DEPS_UPDATE_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <packagekit-glib/packagekit.h>

G_BEGIN_DECLS

#define GPK_TYPE_HELPER_DEPS_UPDATE		(gpk_helper_deps_update_get_type ())
#define GPK_HELPER_DEPS_UPDATE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_HELPER_DEPS_UPDATE, GpkHelperDepsUpdate))
#define GPK_HELPER_DEPS_UPDATE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_HELPER_DEPS_UPDATE, GpkHelperDepsUpdateClass))
#define GPK_IS_HELPER_DEPS_UPDATE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_HELPER_DEPS_UPDATE))
#define GPK_IS_HELPER_DEPS_UPDATE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_HELPER_DEPS_UPDATE))
#define GPK_HELPER_DEPS_UPDATE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_HELPER_DEPS_UPDATE, GpkHelperDepsUpdateClass))
#define GPK_HELPER_DEPS_UPDATE_ERROR		(gpk_helper_deps_update_error_quark ())
#define GPK_HELPER_DEPS_UPDATE_TYPE_ERROR	(gpk_helper_deps_update_error_get_type ())

typedef struct GpkHelperDepsUpdatePrivate GpkHelperDepsUpdatePrivate;

typedef struct
{
	 GObject			 parent;
	 GpkHelperDepsUpdatePrivate	*priv;
} GpkHelperDepsUpdate;

typedef struct
{
	void		(* event)			(GpkHelperDepsUpdate	*helper,
							 GtkResponseType	 type,
							 PkPackageList		*deps_list);
	GObjectClass	parent_class;
} GpkHelperDepsUpdateClass;

GType		 gpk_helper_deps_update_get_type	(void);
GpkHelperDepsUpdate	*gpk_helper_deps_update_new	(void);
gboolean	 gpk_helper_deps_update_set_parent	(GpkHelperDepsUpdate	*helper,
							 GtkWindow		*window);
gboolean	 gpk_helper_deps_update_show		(GpkHelperDepsUpdate	*helper,
							 PkPackageList		*packages,
							 PkPackageList		*deps_list);

G_END_DECLS

#endif /* __GPK_HELPER_DEPS_UPDATE_H */
