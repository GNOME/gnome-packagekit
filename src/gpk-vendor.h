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

#ifndef __GPK_VENDOR_H
#define __GPK_VENDOR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GPK_TYPE_VENDOR			(gpk_vendor_get_type ())
#define GPK_VENDOR(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), GPK_TYPE_VENDOR, GpkVendor))
#define GPK_VENDOR_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GPK_TYPE_VENDOR, GpkVendorClass))
#define PK_IS_VENDOR(o)	 		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPK_TYPE_VENDOR))
#define PK_IS_VENDOR_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), GPK_TYPE_VENDOR))
#define GPK_VENDOR_GET_CLASS(o)		(G_TYPE_INSTANCE_GET_CLASS ((o), GPK_TYPE_VENDOR, GpkVendorClass))
#define GPK_VENDOR_ERROR		(gpk_vendor_error_quark ())
#define GPK_VENDOR_TYPE_ERROR		(gpk_vendor_error_get_type ())

typedef struct GpkVendorPrivate GpkVendorPrivate;

typedef struct
{
	 GObject		 parent;
	 GpkVendorPrivate	*priv;
} GpkVendor;

typedef struct
{
	GObjectClass	parent_class;
} GpkVendorClass;

/**
 * GpkModalDialogWidgets:
 */
typedef enum
{
	GPK_VENDOR_URL_TYPE_CODEC,
	GPK_VENDOR_URL_TYPE_FONT,
	GPK_VENDOR_URL_TYPE_MIME,
	GPK_VENDOR_URL_TYPE_HARDWARE,
	GPK_VENDOR_URL_TYPE_DEFAULT
} GpkVendorUrlType;

GType		 gpk_vendor_get_type			(void);
GpkVendor	*gpk_vendor_new				(void);
gchar		*gpk_vendor_get_not_found_url		(GpkVendor		*vendor,
							 GpkVendorUrlType	 type);

G_END_DECLS

#endif /* __GPK_VENDOR_H */
