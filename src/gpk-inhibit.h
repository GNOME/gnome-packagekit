/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_INHIBIT_H
#define __PK_INHIBIT_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_INHIBIT			(pk_inhibit_get_type ())
#define PK_INHIBIT(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_INHIBIT, PkInhibit))
#define PK_INHIBIT_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_INHIBIT, PkInhibitClass))
#define PK_IS_INHIBIT(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_INHIBIT))
#define PK_IS_INHIBIT_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_INHIBIT))
#define PK_INHIBIT_GET_CLASS(o)		(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_INHIBIT, PkInhibitClass))
#define PK_INHIBIT_ERROR		(pk_inhibit_error_quark ())
#define PK_INHIBIT_TYPE_ERROR		(pk_inhibit_error_get_type ()) 

typedef struct PkInhibitPrivate PkInhibitPrivate;

typedef struct
{
	 GObject		 parent;
	 PkInhibitPrivate	*priv;
} PkInhibit;

typedef struct
{
	GObjectClass	parent_class;
} PkInhibitClass;

GType		 pk_inhibit_get_type			(void);
PkInhibit	*pk_inhibit_new				(void);
gboolean	 pk_inhibit_create			(PkInhibit	*inhibit);
gboolean	 pk_inhibit_remove			(PkInhibit	*inhibit);

G_END_DECLS

#endif /* __PK_INHIBIT_H */
