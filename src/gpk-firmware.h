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

#ifndef __GPK_FIRMWARE_H
#define __GPK_FIRMWARE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GPK_TYPE_FIRMWARE		(gpk_firmware_get_type ())
#define GPK_FIRMWARE(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_FIRMWARE, GpkFirmware))
#define GPK_FIRMWARE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_FIRMWARE, GpkFirmwareClass))
#define GPK_IS_FIRMWARE(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_FIRMWARE))
#define GPK_IS_FIRMWARE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_FIRMWARE))
#define GPK_FIRMWARE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_FIRMWARE, GpkFirmwareClass))
#define GPK_FIRMWARE_ERROR		(gpk_firmware_error_quark ())
#define GPK_FIRMWARE_TYPE_ERROR		(gpk_firmware_error_get_type ())

typedef struct GpkFirmwarePrivate GpkFirmwarePrivate;

typedef struct
{
	 GObject		 parent;
	 GpkFirmwarePrivate	*priv;
} GpkFirmware;

typedef struct
{
	GObjectClass	parent_class;
} GpkFirmwareClass;

GType		 gpk_firmware_get_type		  	(void) G_GNUC_CONST;
GpkFirmware	*gpk_firmware_new			(void);

G_END_DECLS

#endif /* __GPK_FIRMWARE_H */
