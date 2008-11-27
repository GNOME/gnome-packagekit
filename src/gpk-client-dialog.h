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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __GPK_CLIENT_DIALOG_H
#define __GPK_CLIENT_DIALOG_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <packagekit-glib/packagekit.h>

G_BEGIN_DECLS

#define GPK_TYPE_CLIENT_DIALOG		(gpk_client_dialog_get_type ())
#define GPK_CLIENT_DIALOG(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_CLIENT_DIALOG, GpkClientDialog))
#define GPK_CLIENT_DIALOG_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_CLIENT_DIALOG, GpkClientDialogClass))
#define GPK_IS_CLIENT_DIALOG(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_CLIENT_DIALOG))
#define GPK_IS_CLIENT_DIALOG_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_CLIENT_DIALOG))
#define GPK_CLIENT_DIALOG_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_CLIENT_DIALOG, GpkClientDialogClass))
#define GPK_CLIENT_DIALOG_ERROR		(gpk_client_dialog_error_quark ())
#define GPK_CLIENT_DIALOG_TYPE_ERROR	(gpk_client_dialog_error_get_type ())

/**
 * GpkClientDialogPage:
 */
typedef enum
{
	GPK_CLIENT_DIALOG_PAGE_CONFIRM,
	GPK_CLIENT_DIALOG_PAGE_PROGRESS,
	GPK_CLIENT_DIALOG_PAGE_FINISHED,
	GPK_CLIENT_DIALOG_PAGE_WARNING,
	GPK_CLIENT_DIALOG_PAGE_CUSTOM,
	GPK_CLIENT_DIALOG_PAGE_UNKNOWN
} GpkClientDialogPage;

/**
 * GpkClientDialogWidgets:
 */
typedef enum
{
	GPK_CLIENT_DIALOG_WIDGET_BUTTON_HELP,
	GPK_CLIENT_DIALOG_WIDGET_BUTTON_CANCEL,
	GPK_CLIENT_DIALOG_WIDGET_BUTTON_CLOSE,
	GPK_CLIENT_DIALOG_WIDGET_BUTTON_ACTION,
	GPK_CLIENT_DIALOG_WIDGET_PADDING,
	GPK_CLIENT_DIALOG_WIDGET_PACKAGE_LIST,
	GPK_CLIENT_DIALOG_WIDGET_PROGRESS_BAR,
	GPK_CLIENT_DIALOG_WIDGET_MESSAGE,
	GPK_CLIENT_DIALOG_WIDGET_UNKNOWN
} GpkClientDialogWidgets;

/* helpers */
#define GPK_CLIENT_DIALOG_PACKAGE_PADDING	pk_bitfield_from_enums (GPK_CLIENT_DIALOG_WIDGET_PADDING, GPK_CLIENT_DIALOG_WIDGET_MESSAGE, -1)
#define GPK_CLIENT_DIALOG_PACKAGE_LIST		pk_bitfield_value (GPK_CLIENT_DIALOG_WIDGET_PACKAGE_LIST)
#define GPK_CLIENT_DIALOG_BUTTON_ACTION		pk_bitfield_value (GPK_CLIENT_DIALOG_WIDGET_BUTTON_ACTION)

typedef struct _GpkClientDialogPrivate	 GpkClientDialogPrivate;
typedef struct _GpkClientDialog		 GpkClientDialog;
typedef struct _GpkClientDialogClass	 GpkClientDialogClass;

struct _GpkClientDialog
{
	GObject				 parent;
	GpkClientDialogPrivate		*priv;
};

struct _GpkClientDialogClass
{
	GObjectClass	parent_class;
};

GQuark		 gpk_client_dialog_error_quark		(void);
GType		 gpk_client_dialog_get_type		(void) G_GNUC_CONST;
GpkClientDialog	*gpk_client_dialog_new			(void);

gboolean	 gpk_client_dialog_present		(GpkClientDialog	*dialog);
gboolean	 gpk_client_dialog_present_with_time	(GpkClientDialog	*dialog,
							 guint32		 timestamp);
gboolean	 gpk_client_dialog_set_package_list	(GpkClientDialog	*dialog,
							 const PkPackageList	*list);
gboolean	 gpk_client_dialog_set_parent		(GpkClientDialog	*dialog,
							 GdkWindow		*window);
gboolean	 gpk_client_dialog_set_window_icon	(GpkClientDialog	*dialog,
							 const gchar		*icon);
gboolean	 gpk_client_dialog_set_title		(GpkClientDialog	*dialog,
							 const gchar		*title);
gboolean	 gpk_client_dialog_set_message		(GpkClientDialog	*dialog,
							 const gchar		*message);
gboolean	 gpk_client_dialog_set_action		(GpkClientDialog	*dialog,
							 const gchar		*action);
gboolean	 gpk_client_dialog_set_percentage	(GpkClientDialog	*dialog,
							 guint			 percentage);
gboolean	 gpk_client_dialog_set_image		(GpkClientDialog	*dialog,
							 const gchar		*image);
gboolean	 gpk_client_dialog_set_image_status	(GpkClientDialog	*dialog,
							 PkStatusEnum		 status);
gboolean	 gpk_client_dialog_set_allow_cancel	(GpkClientDialog	*dialog,
							 gboolean		 can_cancel);
gboolean	 gpk_client_dialog_set_help_id		(GpkClientDialog	*dialog,
							 const gchar		*help_id);
GtkWindow	*gpk_client_dialog_get_window		(GpkClientDialog	*dialog);
GtkResponseType	 gpk_client_dialog_run			(GpkClientDialog	*dialog);
gboolean	 gpk_client_dialog_close		(GpkClientDialog	*dialog);
gboolean	 gpk_client_dialog_setup		(GpkClientDialog	*dialog,
							 GpkClientDialogPage	 page,
							 PkBitfield		 options);

G_END_DECLS

#endif /* __GPK_CLIENT_DIALOG_H */

