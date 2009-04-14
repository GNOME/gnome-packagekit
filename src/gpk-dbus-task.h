/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __GPK_DBUS_TASK_H
#define __GPK_DBUS_TASK_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <packagekit-glib/packagekit.h>

G_BEGIN_DECLS

#define GPK_TYPE_DBUS_TASK		(gpk_dbus_task_get_type ())
#define GPK_DBUS_TASK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_DBUS_TASK, GpkDbusTask))
#define GPK_DBUS_TASK_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_DBUS_TASK, GpkDbusTaskClass))
#define GPK_IS_DBUS_TASK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_DBUS_TASK))
#define GPK_IS_DBUS_TASK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_DBUS_TASK))
#define GPK_DBUS_TASK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_DBUS_TASK, GpkDbusTaskClass))

typedef struct _GpkDbusTaskPrivate	 GpkDbusTaskPrivate;
typedef struct _GpkDbusTask		 GpkDbusTask;
typedef struct _GpkDbusTaskClass	 GpkDbusTaskClass;

struct _GpkDbusTask
{
	GObject				 parent;
	GpkDbusTaskPrivate		*priv;
};

struct _GpkDbusTaskClass
{
	GObjectClass	parent_class;
};

/**
 * GpkDbusTaskInteract:
 */
typedef enum
{
	GPK_CLIENT_INTERACT_CONFIRM_SEARCH,
	GPK_CLIENT_INTERACT_CONFIRM_DEPS,
	GPK_CLIENT_INTERACT_CONFIRM_INSTALL,
	GPK_CLIENT_INTERACT_PROGRESS,
	GPK_CLIENT_INTERACT_FINISHED,
	GPK_CLIENT_INTERACT_WARNING,
	GPK_CLIENT_INTERACT_UNKNOWN
} GpkDbusTaskInteract;

#define GPK_CLIENT_INTERACT_NEVER			0
#define GPK_CLIENT_INTERACT_ALWAYS			pk_bitfield_from_enums (GPK_CLIENT_INTERACT_WARNING, \
										GPK_CLIENT_INTERACT_CONFIRM_SEARCH, \
										GPK_CLIENT_INTERACT_CONFIRM_DEPS, \
										GPK_CLIENT_INTERACT_CONFIRM_INSTALL, \
										GPK_CLIENT_INTERACT_PROGRESS, \
										GPK_CLIENT_INTERACT_FINISHED, -1)
#define GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS	pk_bitfield_from_enums (GPK_CLIENT_INTERACT_WARNING, \
										GPK_CLIENT_INTERACT_CONFIRM_SEARCH, \
										GPK_CLIENT_INTERACT_CONFIRM_DEPS, \
										GPK_CLIENT_INTERACT_CONFIRM_INSTALL, \
										GPK_CLIENT_INTERACT_PROGRESS, -1)
#define GPK_CLIENT_INTERACT_WARNING			pk_bitfield_from_enums (GPK_CLIENT_INTERACT_WARNING, -1)
#define GPK_CLIENT_INTERACT_WARNING_PROGRESS		pk_bitfield_from_enums (GPK_CLIENT_INTERACT_WARNING, \
										GPK_CLIENT_INTERACT_PROGRESS, -1)

GQuark		 gpk_dbus_task_error_quark		(void);
GType		 gpk_dbus_task_get_type			(void);
GType		 gpk_dbus_task_error_get_type		(void);
GpkDbusTask	*gpk_dbus_task_new			(void);

/* methods that expect a DBusGMethodInvocation return */
void		 gpk_dbus_task_is_installed		(GpkDbusTask	*task,
							 const gchar	*package_name);
void		 gpk_dbus_task_search_file		(GpkDbusTask	*task,
							 const gchar	*search_file);
void		 gpk_dbus_task_install_package_files	(GpkDbusTask	*task,
							 gchar		**files_rel);
void		 gpk_dbus_task_install_provide_files	(GpkDbusTask	*task,
							 gchar		**full_paths);
void		 gpk_dbus_task_install_mime_types	(GpkDbusTask	*task,
							 gchar		**mime_types);
void		 gpk_dbus_task_install_gstreamer_resources (GpkDbusTask	*task,
							 gchar		**codec_names);
void		 gpk_dbus_task_install_fontconfig_resources (GpkDbusTask *task,
							 gchar		**fonts);
void		 gpk_dbus_task_install_package_names	(GpkDbusTask	*task,
							 gchar		**packages);
void		 gpk_dbus_task_install_catalogs		(GpkDbusTask	*task,
							 gchar		**filenames);

/* set state */
gboolean	 gpk_dbus_task_set_interaction		(GpkDbusTask	*task,
							 PkBitfield	 interact);
gboolean	 gpk_dbus_task_set_timeout		(GpkDbusTask	*task,
							 gint		 timeout);
gboolean	 gpk_dbus_task_set_timestamp		(GpkDbusTask	*task,
							 guint		 timeout);
gboolean	 gpk_dbus_task_set_context		(GpkDbusTask	*task,
							 DBusGMethodInvocation *context);
gboolean	 gpk_dbus_task_set_xid			(GpkDbusTask	*task,
							 guint		 xid);
gboolean	 gpk_dbus_task_set_exec			(GpkDbusTask	*task,
							 const gchar	*exec);

G_END_DECLS

#endif /* __GPK_DBUS_TASK_H */
