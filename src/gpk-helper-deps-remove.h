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

#ifndef __GPK_HELPER_DEPS_REMOVE_H
#define __GPK_HELPER_DEPS_REMOVE_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <packagekit-glib/packagekit.h>

G_BEGIN_DECLS

#define GPK_TYPE_HELPER_DEPS_REMOVE		(gpk_helper_deps_remove_get_type ())
#define GPK_HELPER_DEPS_REMOVE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_HELPER_DEPS_REMOVE, GpkHelperDepsRemove))
#define GPK_HELPER_DEPS_REMOVE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_HELPER_DEPS_REMOVE, GpkHelperDepsRemoveClass))
#define GPK_IS_HELPER_DEPS_REMOVE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_HELPER_DEPS_REMOVE))
#define GPK_IS_HELPER_DEPS_REMOVE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_HELPER_DEPS_REMOVE))
#define GPK_HELPER_DEPS_REMOVE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_HELPER_DEPS_REMOVE, GpkHelperDepsRemoveClass))
#define GPK_HELPER_DEPS_REMOVE_ERROR		(gpk_helper_deps_remove_error_quark ())
#define GPK_HELPER_DEPS_REMOVE_TYPE_ERROR	(gpk_helper_deps_remove_error_get_type ())

typedef struct GpkHelperDepsRemovePrivate GpkHelperDepsRemovePrivate;

typedef struct
{
	 GObject			 parent;
	 GpkHelperDepsRemovePrivate	*priv;
} GpkHelperDepsRemove;

typedef struct
{
	void		(* event)			(GpkHelperDepsRemove	*helper,
							 GtkResponseType	 type);
	GObjectClass	parent_class;
} GpkHelperDepsRemoveClass;

GType		 gpk_helper_deps_remove_get_type	(void);
GpkHelperDepsRemove	*gpk_helper_deps_remove_new	(void);
gboolean	 gpk_helper_deps_remove_set_parent	(GpkHelperDepsRemove	*helper,
							 GtkWindow		*window);
gboolean	 gpk_helper_deps_remove_show		(GpkHelperDepsRemove	*helper,
							 PkPackageList		*packages,
							 PkPackageList		*deps_list);

G_END_DECLS

#endif /* __GPK_HELPER_DEPS_REMOVE_H */
