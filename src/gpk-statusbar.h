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

#ifndef __GPK_STATUSBAR_H
#define __GPK_STATUSBAR_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GPK_TYPE_STATUSBAR		(gpk_statusbar_get_type ())
#define GPK_STATUSBAR(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_STATUSBAR, GpkStatusbar))
#define GPK_STATUSBAR_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_STATUSBAR, GpkStatusbarClass))
#define PK_IS_STATUSBAR(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_STATUSBAR))
#define PK_IS_STATUSBAR_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_STATUSBAR))
#define GPK_STATUSBAR_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_STATUSBAR, GpkStatusbarClass))
#define GPK_STATUSBAR_ERROR		(gpk_statusbar_error_quark ())
#define GPK_STATUSBAR_TYPE_ERROR	(gpk_statusbar_error_get_type ())

typedef struct GpkStatusbarPrivate GpkStatusbarPrivate;

typedef struct
{
	 GObject		 parent;
	 GpkStatusbarPrivate	*priv;
} GpkStatusbar;

typedef struct
{
	GObjectClass	parent_class;
} GpkStatusbarClass;

GType		 gpk_statusbar_get_type			(void);
GpkStatusbar	*gpk_statusbar_new			(void);
gboolean	 gpk_statusbar_set_widget		(GpkStatusbar	*arefresh,
							 GtkWidget	*widget);
gboolean	 gpk_statusbar_set_percentage		(GpkStatusbar	*arefresh,
							 guint		 percentage);
gboolean	 gpk_statusbar_set_status		(GpkStatusbar	*arefresh,
							 PkStatusEnum	 status);
gboolean	 gpk_statusbar_hide			(GpkStatusbar	*arefresh);
gboolean	 gpk_statusbar_set_remaining		(GpkStatusbar	*arefresh,
							 guint		 remaining);

G_END_DECLS

#endif /* __GPK_STATUSBAR_H */
