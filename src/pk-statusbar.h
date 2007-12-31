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

#ifndef __PK_STATUSBAR_H
#define __PK_STATUSBAR_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PK_TYPE_STATUSBAR		(pk_statusbar_get_type ())
#define PK_STATUSBAR(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_STATUSBAR, PkStatusbar))
#define PK_STATUSBAR_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_STATUSBAR, PkStatusbarClass))
#define PK_IS_STATUSBAR(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_STATUSBAR))
#define PK_IS_STATUSBAR_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_STATUSBAR))
#define PK_STATUSBAR_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_STATUSBAR, PkStatusbarClass))
#define PK_STATUSBAR_ERROR		(pk_statusbar_error_quark ())
#define PK_STATUSBAR_TYPE_ERROR		(pk_statusbar_error_get_type ()) 

typedef struct PkStatusbarPrivate PkStatusbarPrivate;

typedef struct
{
	 GObject		 parent;
	 PkStatusbarPrivate	*priv;
} PkStatusbar;

typedef struct
{
	GObjectClass	parent_class;
} PkStatusbarClass;

GType		 pk_statusbar_get_type			(void);
PkStatusbar	*pk_statusbar_new			(void);
gboolean	 pk_statusbar_set_widget		(PkStatusbar	*arefresh,
							 GtkWidget	*widget);
gboolean	 pk_statusbar_set_percentage		(PkStatusbar	*arefresh,
							 guint		 percentage);
gboolean	 pk_statusbar_set_status		(PkStatusbar	*arefresh,
							 PkStatusEnum	 status);
gboolean	 pk_statusbar_hide			(PkStatusbar	*arefresh);
gboolean	 pk_statusbar_set_remaining		(PkStatusbar	*arefresh,
							 guint		 remaining);

G_END_DECLS

#endif /* __PK_STATUSBAR_H */
