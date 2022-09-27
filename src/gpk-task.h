/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offtask: 8 -*-
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

#ifndef __GPK_TASK_H
#define __GPK_TASK_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

G_BEGIN_DECLS

#define GPK_TYPE_TASK		(gpk_task_get_type ())
#define GPK_TASK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_TASK, GpkTask))
#define GPK_TASK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_TASK, GpkTaskClass))
#define GPK_IS_TASK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_TASK))
#define GPK_IS_TASK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_TASK))
#define GPK_TASK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_TASK, GpkTaskClass))

typedef struct _GpkTaskPrivate	GpkTaskPrivate;
typedef struct _GpkTask		GpkTask;
typedef struct _GpkTaskClass	GpkTaskClass;

struct _GpkTask
{
	 PkTask			 parent;
	 GpkTaskPrivate		*priv;
};

struct _GpkTaskClass
{
	PkTaskClass		 parent_class;
};

GType		 gpk_task_get_type		(void);
GQuark		 gpk_task_error_quark		(void);
GpkTask		*gpk_task_new			(void);
gboolean	 gpk_task_set_parent_window	(GpkTask	*task,
						 GtkWindow	*window);

G_END_DECLS

#endif /* __GPK_TASK_H */

