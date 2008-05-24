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

#ifndef __GPK_NOTIFY_H
#define __GPK_NOTIFY_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GPK_TYPE_NOTIFY		(gpk_notify_get_type ())
#define GPK_NOTIFY(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_NOTIFY, GpkNotify))
#define GPK_NOTIFY_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_NOTIFY, GpkNotifyClass))
#define GPK_IS_NOTIFY(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_NOTIFY))
#define GPK_IS_NOTIFY_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_NOTIFY))
#define GPK_NOTIFY_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_NOTIFY, GpkNotifyClass))
#define GPK_NOTIFY_ERROR	(gpk_notify_error_quark ())
#define GPK_NOTIFY_TYPE_ERROR	(gpk_notify_error_get_type ())

typedef struct GpkNotifyPrivate GpkNotifyPrivate;

typedef struct
{
	 GtkStatusIcon		 parent;
	 GpkNotifyPrivate	*priv;
} GpkNotify;

typedef struct
{
	GtkStatusIconClass	 parent_class;
} GpkNotifyClass;

typedef enum
{
	GPK_NOTIFY_URGENCY_LOW,
	GPK_NOTIFY_URGENCY_NORMAL,
	GPK_NOTIFY_URGENCY_CRITICAL
} GpkNotifyUrgency;

typedef enum
{
	GPK_NOTIFY_TIMEOUT_SHORT,
	GPK_NOTIFY_TIMEOUT_LONG,
	GPK_NOTIFY_TIMEOUT_NEVER
} GpkNotifyTimeout;

typedef enum
{
	GPK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN,
	GPK_NOTIFY_BUTTON_DO_NOT_WARN_AGAIN,
	GPK_NOTIFY_BUTTON_CANCEL_UPDATE,
	GPK_NOTIFY_BUTTON_UPDATE_COMPUTER,
	GPK_NOTIFY_BUTTON_RESTART_COMPUTER,
	GPK_NOTIFY_BUTTON_INSTALL_FIRMWARE,
	GPK_NOTIFY_BUTTON_UNKNOWN
} GpkNotifyButton;

GType		 gpk_notify_get_type			(void) G_GNUC_CONST;
GpkNotify	*gpk_notify_new				(void);
gboolean	 gpk_notify_create			(GpkNotify		*notify,
							 const gchar		*title,
							 const gchar		*message,
							 const gchar		*icon,
							 GpkNotifyUrgency	 urgency,
							 GpkNotifyTimeout	 timeout);
gboolean	 gpk_notify_button			(GpkNotify		*notify,
							 GpkNotifyButton	 button,
							 const gchar		*data);
gboolean	 gpk_notify_show			(GpkNotify		*notify);
gboolean	 gpk_notify_close			(GpkNotify		*notify);


G_END_DECLS

#endif /* __GPK_NOTIFY_H */
