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

#include "config.h"

#include <glib/gi18n.h>

#include "egg-string.h"
#include "gpk-vendor.h"

static void     gpk_vendor_finalize	(GObject          *object);

#define GPK_VENDOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_VENDOR, GpkVendorPrivate))

struct GpkVendorPrivate
{
	GKeyFile			 *file;
};

G_DEFINE_TYPE (GpkVendor, gpk_vendor, G_TYPE_OBJECT)

/**
 * gpk_vendor_class_init:
 * @klass: The GpkVendorClass
 **/
static void
gpk_vendor_class_init (GpkVendorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_vendor_finalize;
	g_type_class_add_private (klass, sizeof (GpkVendorPrivate));
}

/**
 * gpk_vendor_type_to_string:
 **/
static const gchar *
gpk_vendor_type_to_string (GpkVendorUrlType type)
{
	if (type == GPK_VENDOR_URL_TYPE_CODEC)
		return "CodecUrl";
	if (type == GPK_VENDOR_URL_TYPE_FONT)
		return "FontUrl";
	if (type == GPK_VENDOR_URL_TYPE_MIME)
		return "MimeUrl";
	if (type == GPK_VENDOR_URL_TYPE_HARDWARE)
		return "HardwareUrl";
	return "DefaultUrl";
}

/**
 * gpk_vendor_get_not_found_url:
 **/
gchar *
gpk_vendor_get_not_found_url (GpkVendor *vendor, GpkVendorUrlType type)
{
	const gchar *key;
	gchar *url = NULL;

	/* get data */
	key = gpk_vendor_type_to_string (type);
	url = g_key_file_get_string (vendor->priv->file, "PackagesNotFound", key, NULL);

	/* none is a special value */
	if (g_strcmp0 (url, "none") == 0) {
		g_free (url);
		url = NULL;
	}

	/* got a valid URL */
	if (url != NULL)
		goto out;

	/* default has no fallback */
	if (type == GPK_VENDOR_URL_TYPE_DEFAULT)
		goto out;

	/* get fallback data */
	g_debug ("using fallback");
	key = gpk_vendor_type_to_string (GPK_VENDOR_URL_TYPE_DEFAULT);
	url = g_key_file_get_string (vendor->priv->file, "PackagesNotFound", key, NULL);

	/* none is a special value */
	if (g_strcmp0 (url, "none") == 0) {
		g_free (url);
		url = NULL;
	}
out:
	g_debug ("url=%s", url);
	return url;
}

/**
 * gpk_vendor_init:
 * @vendor: This class instance
 **/
static void
gpk_vendor_init (GpkVendor *vendor)
{
	gboolean ret;

	vendor->priv = GPK_VENDOR_GET_PRIVATE (vendor);

	vendor->priv->file = g_key_file_new ();
	ret = g_key_file_load_from_file (vendor->priv->file, "/etc/PackageKit/Vendor.conf", G_KEY_FILE_NONE, NULL);
	if (!ret)
		g_warning ("file not found");
}

/**
 * gpk_vendor_finalize:
 * @object: The object to finalize
 **/
static void
gpk_vendor_finalize (GObject *object)
{
	GpkVendor *vendor;

	g_return_if_fail (PK_IS_VENDOR (object));

	vendor = GPK_VENDOR (object);
	g_return_if_fail (vendor->priv != NULL);

	g_key_file_free (vendor->priv->file);

	G_OBJECT_CLASS (gpk_vendor_parent_class)->finalize (object);
}

/**
 * gpk_vendor_new:
 *
 * Return value: a new GpkVendor object.
 **/
GpkVendor *
gpk_vendor_new (void)
{
	GpkVendor *vendor;
	vendor = g_object_new (GPK_TYPE_VENDOR, NULL);
	return GPK_VENDOR (vendor);
}

