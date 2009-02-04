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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __GPK_APPLICATION_H
#define __GPK_APPLICATION_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GPK_TYPE_APPLICATION		(gpk_application_get_type ())
#define GPK_APPLICATION(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_APPLICATION, GpkApplication))
#define GPK_APPLICATION_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_APPLICATION, GpkApplicationClass))
#define PK_IS_APPLICATION(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_APPLICATION))
#define PK_IS_APPLICATION_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_APPLICATION))
#define GPK_APPLICATION_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_APPLICATION, GpkApplicationClass))

typedef struct GpkApplicationPrivate GpkApplicationPrivate;

typedef struct
{
	GObject		 parent;
	GpkApplicationPrivate *priv;
} GpkApplication;

typedef struct
{
	GObjectClass	parent_class;
	void		(* action_help)			(GpkApplication	*application);
	void		(* action_close)		(GpkApplication	*application);
} GpkApplicationClass;

GType		 gpk_application_get_type		(void);
GpkApplication	*gpk_application_new			(void);
void		 gpk_application_show			(GpkApplication *application);

G_END_DECLS

#endif	/* __GPK_APPLICATION_H */
