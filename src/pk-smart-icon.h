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

#ifndef __PK_SMART_ICON_H
#define __PK_SMART_ICON_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_SMART_ICON		(pk_smart_icon_get_type ())
#define PK_SMART_ICON(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_SMART_ICON, PkSmartIcon))
#define PK_SMART_ICON_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_SMART_ICON, PkSmartIconClass))
#define PK_IS_SMART_ICON(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_SMART_ICON))
#define PK_IS_SMART_ICON_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_SMART_ICON))
#define PK_SMART_ICON_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_SMART_ICON, PkSmartIconClass))
#define PK_SMART_ICON_ERROR		(pk_smart_icon_error_quark ())
#define PK_SMART_ICON_TYPE_ERROR	(pk_smart_icon_error_get_type ()) 

typedef struct PkSmartIconPrivate PkSmartIconPrivate;

typedef struct
{
	 GObject		 parent;
	 PkSmartIconPrivate	*priv;
} PkSmartIcon;

typedef struct
{
	GObjectClass	parent_class;
} PkSmartIconClass;

typedef enum
{
	PK_NOTIFY_URGENCY_LOW,
	PK_NOTIFY_URGENCY_NORMAL,
	PK_NOTIFY_URGENCY_CRITICAL
} PkNotifyUrgency;

typedef enum
{
	PK_NOTIFY_TIMEOUT_SHORT,
	PK_NOTIFY_TIMEOUT_LONG,
	PK_NOTIFY_TIMEOUT_NEVER
} PkNotifyTimeout;

typedef enum
{
	PK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN,
	PK_NOTIFY_BUTTON_DO_NOT_WARN_AGAIN,
	PK_NOTIFY_BUTTON_CANCEL_UPDATE,
	PK_NOTIFY_BUTTON_UPDATE_COMPUTER,
	PK_NOTIFY_BUTTON_RESTART_COMPUTER,
	PK_NOTIFY_BUTTON_UNKNOWN
} PkNotifyButton;

GType		 pk_smart_icon_get_type		  	(void);
PkSmartIcon	*pk_smart_icon_new			(void);
GtkStatusIcon	*pk_smart_icon_get_status_icon		(PkSmartIcon	*sicon);
gboolean	 pk_smart_icon_sync			(PkSmartIcon	*sicon);
gboolean	 pk_smart_icon_set_icon_name		(PkSmartIcon	*sicon,
							 const gchar	*icon_name);
gboolean	 pk_smart_icon_set_tooltip		(PkSmartIcon	*sicon,
							 const gchar	*tooltip);
gboolean	 pk_smart_icon_notify_new		(PkSmartIcon	*sicon,
							 const gchar	*title,
							 const gchar	*message,
							 const gchar	*icon,
							 PkNotifyUrgency urgency,
							 PkNotifyTimeout timeout);
gboolean	 pk_smart_icon_notify_button		(PkSmartIcon	*sicon,
							 PkNotifyButton	 button,
							 const gchar	*data);
gboolean	 pk_smart_icon_notify_show		(PkSmartIcon	*sicon);


G_END_DECLS

#endif /* __PK_SMART_ICON_H */
