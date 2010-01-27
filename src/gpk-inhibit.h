/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __GPK_INHIBIT_H
#define __GPK_INHIBIT_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GPK_TYPE_INHIBIT		(gpk_inhibit_get_type ())
#define GPK_INHIBIT(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_INHIBIT, GpkInhibit))
#define GPK_INHIBIT_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_INHIBIT, GpkInhibitClass))
#define PK_IS_INHIBIT(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_INHIBIT))
#define PK_IS_INHIBIT_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_INHIBIT))
#define GPK_INHIBIT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_INHIBIT, GpkInhibitClass))
#define GPK_INHIBIT_ERROR		(gpk_inhibit_error_quark ())
#define GPK_INHIBIT_TYPE_ERROR		(gpk_inhibit_error_get_type ())

typedef struct GpkInhibitPrivate GpkInhibitPrivate;

typedef struct
{
	 GObject		 parent;
	 GpkInhibitPrivate	*priv;
} GpkInhibit;

typedef struct
{
	GObjectClass	parent_class;
} GpkInhibitClass;

GType		 gpk_inhibit_get_type			(void);
GpkInhibit	*gpk_inhibit_new			(void);
gboolean	 gpk_inhibit_create			(GpkInhibit	*inhibit);
gboolean	 gpk_inhibit_remove			(GpkInhibit	*inhibit);

G_END_DECLS

#endif /* __GPK_INHIBIT_H */
