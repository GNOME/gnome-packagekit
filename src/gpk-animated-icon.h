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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef GPK_ANIMATED_ICON_H
#define GPK_ANIMATED_ICON_H

#include <glib-object.h>
#include <gtk/gtk.h>

#define GPK_TYPE_ANIMATED_ICON			(gpk_animated_icon_get_type())
#define GPK_ANIMATED_ICON(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), GPK_TYPE_ANIMATED_ICON, GpkAnimatedIcon))
#define GPK_ANIMATED_ICON_CLASS(cls)		(G_TYPE_CHECK_CLASS_CAST((cls), GPK_TYPE_ANIMATED_ICON, GpkAnimatedIconClass))
#define GPK_IS_ANIMATED_ICON(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GPK_TYPE_ANIMATED_ICON))
#define GPK_IS_ANIMATED_ICON_CLASS(cls)		(G_TYPE_CHECK_CLASS_TYPE((cls), GPK_TYPE_ANIMATED_ICON))
#define GPK_ANIMATED_ICON_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GPK_TYPE_ANIMATED_ICON, GpkAnimatedIconClass))

G_BEGIN_DECLS

typedef struct _GpkAnimatedIcon			GpkAnimatedIcon;
typedef struct _GpkAnimatedIconClass		GpkAnimatedIconClass;

struct _GpkAnimatedIcon
{
	GtkImage		 parent;
	gchar			*filename;
	guint			 animation_id;
	guint			 frame_counter;
	guint			 number_frames;
	guint			 frame_delay;
	GdkPixbuf		**frames;
};

struct _GpkAnimatedIconClass
{
	GtkImageClass		 parent_class;
};

GType		 gpk_animated_icon_get_type		(void);
GtkWidget	*gpk_animated_icon_new			(void);
gboolean	 gpk_animated_icon_set_filename_tile	(GpkAnimatedIcon	*icon,
							 GtkIconSize		 size,
							 const gchar		*name);
gboolean	 gpk_animated_icon_set_icon_name	(GpkAnimatedIcon	*icon,
							 GtkIconSize		 size,
							 const gchar		*name);
gboolean	 gpk_animated_icon_set_frame_delay	(GpkAnimatedIcon	*icon,
							 guint			 delay_ms);
gboolean	 gpk_animated_icon_enable_animation	(GpkAnimatedIcon	*icon,
							 gboolean		 enabled);

G_END_DECLS

#endif /* GPK_ANIMATED_ICON_H */

