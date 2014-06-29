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

#ifndef __GPK_MODAL_DIALOG_H
#define __GPK_MODAL_DIALOG_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

G_BEGIN_DECLS

#define GPK_TYPE_CLIENT_DIALOG		(gpk_modal_dialog_get_type ())
#define GPK_MODAL_DIALOG(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_CLIENT_DIALOG, GpkModalDialog))
#define GPK_MODAL_DIALOG_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_CLIENT_DIALOG, GpkModalDialogClass))
#define GPK_IS_CLIENT_DIALOG(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_CLIENT_DIALOG))
#define GPK_IS_CLIENT_DIALOG_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_CLIENT_DIALOG))
#define GPK_MODAL_DIALOG_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_CLIENT_DIALOG, GpkModalDialogClass))
#define GPK_MODAL_DIALOG_ERROR		(gpk_modal_dialog_error_quark ())
#define GPK_MODAL_DIALOG_TYPE_ERROR	(gpk_modal_dialog_error_get_type ())

/**
 * GpkModalDialogPage:
 */
typedef enum
{
	GPK_MODAL_DIALOG_PAGE_CONFIRM,
	GPK_MODAL_DIALOG_PAGE_PROGRESS,
	GPK_MODAL_DIALOG_PAGE_FINISHED,
	GPK_MODAL_DIALOG_PAGE_WARNING,
	GPK_MODAL_DIALOG_PAGE_CUSTOM,
	GPK_MODAL_DIALOG_PAGE_UNKNOWN
} GpkModalDialogPage;

/**
 * GpkModalDialogWidgets:
 */
typedef enum
{
	GPK_MODAL_DIALOG_WIDGET_BUTTON_CANCEL,
	GPK_MODAL_DIALOG_WIDGET_BUTTON_CLOSE,
	GPK_MODAL_DIALOG_WIDGET_BUTTON_ACTION,
	GPK_MODAL_DIALOG_WIDGET_PADDING,
	GPK_MODAL_DIALOG_WIDGET_PACKAGE_LIST,
	GPK_MODAL_DIALOG_WIDGET_PROGRESS_BAR,
	GPK_MODAL_DIALOG_WIDGET_MESSAGE,
	GPK_MODAL_DIALOG_WIDGET_UNKNOWN
} GpkModalDialogWidgets;

/* helpers */
#define GPK_MODAL_DIALOG_PACKAGE_PADDING	pk_bitfield_from_enums (GPK_MODAL_DIALOG_WIDGET_PADDING, GPK_MODAL_DIALOG_WIDGET_MESSAGE, -1)
#define GPK_MODAL_DIALOG_PACKAGE_LIST		pk_bitfield_value (GPK_MODAL_DIALOG_WIDGET_PACKAGE_LIST)
#define GPK_MODAL_DIALOG_BUTTON_ACTION		pk_bitfield_value (GPK_MODAL_DIALOG_WIDGET_BUTTON_ACTION)

typedef struct _GpkModalDialogPrivate	 GpkModalDialogPrivate;
typedef struct _GpkModalDialog		 GpkModalDialog;
typedef struct _GpkModalDialogClass	 GpkModalDialogClass;

struct _GpkModalDialog
{
	GObject				 parent;
	GpkModalDialogPrivate		*priv;
};

struct _GpkModalDialogClass
{
	GObjectClass	parent_class;
};

GQuark		 gpk_modal_dialog_error_quark		(void);
GType		 gpk_modal_dialog_get_type		(void);
GpkModalDialog	*gpk_modal_dialog_new			(void);

gboolean	 gpk_modal_dialog_present		(GpkModalDialog		*dialog);
gboolean	 gpk_modal_dialog_present_with_time	(GpkModalDialog		*dialog,
							 guint32		 timestamp);
gboolean	 gpk_modal_dialog_set_package_list	(GpkModalDialog		*dialog,
							 const GPtrArray	*list);
gboolean	 gpk_modal_dialog_set_parent		(GpkModalDialog		*dialog,
							 GdkWindow		*window);
gboolean	 gpk_modal_dialog_set_window_icon	(GpkModalDialog		*dialog,
							 const gchar		*icon);
gboolean	 gpk_modal_dialog_set_title		(GpkModalDialog		*dialog,
							 const gchar		*title);
gboolean	 gpk_modal_dialog_set_message		(GpkModalDialog		*dialog,
							 const gchar		*message);
gboolean	 gpk_modal_dialog_set_action		(GpkModalDialog		*dialog,
							 const gchar		*action);
gboolean	 gpk_modal_dialog_set_percentage	(GpkModalDialog		*dialog,
							 gint			 percentage);
gboolean	 gpk_modal_dialog_set_remaining		(GpkModalDialog		*dialog,
							 guint			 remaining);
gboolean	 gpk_modal_dialog_set_image		(GpkModalDialog		*dialog,
							 const gchar		*image);
gboolean	 gpk_modal_dialog_set_image_status	(GpkModalDialog		*dialog,
							 PkStatusEnum		 status);
gboolean	 gpk_modal_dialog_set_allow_cancel	(GpkModalDialog		*dialog,
							 gboolean		 can_cancel);
GtkWindow	*gpk_modal_dialog_get_window		(GpkModalDialog		*dialog);
GtkResponseType	 gpk_modal_dialog_run			(GpkModalDialog		*dialog);
gboolean	 gpk_modal_dialog_close			(GpkModalDialog		*dialog);
gboolean	 gpk_modal_dialog_setup			(GpkModalDialog		*dialog,
							 GpkModalDialogPage	 page,
							 PkBitfield		 options);

G_END_DECLS

#endif /* __GPK_MODAL_DIALOG_H */

