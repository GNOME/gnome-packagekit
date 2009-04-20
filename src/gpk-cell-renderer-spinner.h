/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Matthias Clasen <mclasen@redhat.com>
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

#ifndef __GPK_CELL_RENDERER_SPINNER_H__
#define __GPK_CELL_RENDERER_SPINNER_H__

#include <gtk/gtk.h>

#define GPK_TYPE_CELL_RENDERER_SPINNER			(gpk_cell_renderer_spinner_get_type ())
#define GPK_CELL_RENDERER_SPINNER(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GPK_TYPE_CELL_RENDERER_SPINNER, GpkCellRendererSpinner))
#define GPK_CELL_RENDERER_SPINNER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GPK_TYPE_CELL_RENDERER_SPINNER, GpkCellRendererSpinnerClass))
#define GPK_IS_CELL_RENDERER_SPINNER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GPK_TYPE_CELL_RENDERER_SPINNER))
#define GPK_IS_CELL_RENDERER_SPINNER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GPK_TYPE_CELL_RENDERER_SPINNER))
#define GPK_CELL_RENDERER_SPINNER_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GPK_TYPE_CELL_RENDERER_SPINNER, GpkCellRendererSpinnerClass))

typedef struct _GpkCellRendererSpinner		GpkCellRendererSpinner;
typedef struct _GpkCellRendererSpinnerClass	GpkCellRendererSpinnerClass;
typedef struct _GpkCellRendererSpinnerPrivate	GpkCellRendererSpinnerPrivate;

struct _GpkCellRendererSpinner
{
	GtkCellRenderer parent_instance;

	/*< private >*/
	GpkCellRendererSpinnerPrivate *GSEAL (priv);
};

struct _GpkCellRendererSpinnerClass
{
	GtkCellRendererClass parent_class;

	/* Padding for future expansion */
	void (*_gpk_reserved1) (void);
	void (*_gpk_reserved2) (void);
	void (*_gpk_reserved3) (void);
	void (*_gpk_reserved4) (void);
};

GType		 gpk_cell_renderer_spinner_get_type	(void) G_GNUC_CONST;
GtkCellRenderer *gpk_cell_renderer_spinner_new		(void);

#endif /* __GPK_CELL_RENDERER_SPINNER_H__ */

