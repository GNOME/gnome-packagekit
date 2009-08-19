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

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <libnotify/notify.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-common.h"
#include "gpk-firmware.h"

static void     gpk_firmware_finalize	(GObject	  *object);

#define GPK_FIRMWARE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_FIRMWARE, GpkFirmwarePrivate))
#define GPK_FIRMWARE_MISSING_DIR		"/dev/.udev/firmware-missing"
#define GPK_FIRMWARE_LOADING_DIR		"/lib/firmware"
#define GPK_FIRMWARE_LOGIN_DELAY		60 /* seconds */

struct GpkFirmwarePrivate
{
	PkClient		*client_primary;
	PkPackageList		*packages_found;
	GPtrArray		*array_requested;
	GConfClient		*gconf_client;
};

G_DEFINE_TYPE (GpkFirmware, gpk_firmware, G_TYPE_OBJECT)

/**
 * gpk_firmware_install_file:
 **/
static gboolean
gpk_firmware_install_file (GpkFirmware *firmware)
{
	gboolean ret;
	GError *error = NULL;
	gchar **package_ids;

	/* install all of the firmware files */
	package_ids = pk_package_list_to_strv (firmware->priv->packages_found);
	ret = pk_client_reset (firmware->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("failed to reset: %s", error->message);
		g_error_free (error);
		goto out;
	}
#if PK_CHECK_VERSION(0,5,0)
	ret = pk_client_install_packages (firmware->priv->client_primary, TRUE, package_ids, &error);
#else
	ret = pk_client_install_packages (firmware->priv->client_primary, package_ids, &error);
#endif
	if (!ret) {
		egg_warning ("failed to install provide file: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * gpk_firmware_libnotify_cb:
 **/
static void
gpk_firmware_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data)
{
	GpkFirmware *firmware = GPK_FIRMWARE (data);

	if (g_strcmp0 (action, "install-firmware") == 0) {
		gpk_firmware_install_file (firmware);
	} else if (g_strcmp0 (action, "do-not-show-prompt-firmware") == 0) {
		egg_debug ("set %s to FALSE", GPK_CONF_ENABLE_CHECK_FIRMWARE);
		gconf_client_set_bool (firmware->priv->gconf_client, GPK_CONF_ENABLE_CHECK_FIRMWARE, FALSE, NULL);
	} else {
		egg_warning ("unknown action id: %s", action);
	}
}

/**
 * gpk_firmware_check_available:
 * @firmware: This class instance
 * @filename: Firmware to search for
 **/
static PkPackageObj *
gpk_firmware_check_available (GpkFirmware *firmware, const gchar *filename)
{
	gboolean ret;
	guint length = 0;
	PkPackageList *list = NULL;
	GError *error = NULL;
	PkPackageObj *obj = NULL;
	PkBitfield filter;

	/* actually check we can provide the firmware */
	ret = pk_client_reset (firmware->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("failed to reset: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* search for newest not installed package */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED, PK_FILTER_ENUM_NEWEST, -1);
	ret = pk_client_search_file (firmware->priv->client_primary, filter, filename, &error);
	if (!ret) {
		egg_warning ("failed to search file %s: %s", filename, error->message);
		g_error_free (error);
		goto out;
	}

	/* make sure we have one package */
	list = pk_client_get_package_list (firmware->priv->client_primary);
	length = pk_package_list_get_size (list);
	if (length == 0)
		egg_debug ("no package providing %s found", filename);
	else if (length != 1)
		egg_warning ("not one package providing %s found (%i)", filename, length);
	else
		obj = pk_package_obj_copy (pk_package_list_get_obj (list, 0));
out:
	if (list != NULL)
		g_object_unref (list);
	return obj;
}

/**
 * gpk_firmware_timeout_cb:
 * @data: This class instance
 **/
static gboolean
gpk_firmware_timeout_cb (gpointer data)
{
	guint i;
	gboolean ret;
	const gchar *filename;
	const gchar *message;
	GpkFirmware *firmware = GPK_FIRMWARE (data);
	NotifyNotification *notification;
	GPtrArray *array;
	GError *error = NULL;
	PkPackageObj *obj = NULL;

	/* debug so we can catch polling */
	egg_debug ("polling check");

	/* try to find each firmware file in an available package */
	array = firmware->priv->array_requested;
	for (i=0; i<array->len; i++) {
		filename = g_ptr_array_index (array, i);
		/* save to new array if we found one package for this file */
		obj = gpk_firmware_check_available (firmware, filename);
		if (obj != NULL) {
			pk_obj_list_add (PK_OBJ_LIST (firmware->priv->packages_found), obj);
			pk_package_obj_free (obj);
		}
	}

	/* nothing to do */
	if (pk_package_list_get_size (firmware->priv->packages_found) == 0) {
		egg_debug ("no packages providing any of the missing firmware");
		goto out;
	}

	/* check we don't want the same package more than once */
	pk_obj_list_remove_duplicate (PK_OBJ_LIST (firmware->priv->packages_found));

	/* TRANSLATORS: we need another package to keep udev quiet */
	message = _("Additional firmware is required to make hardware in this computer function correctly.");
	/* TRANSLATORS: title of libnotify bubble */
	notification = notify_notification_new (_("Additional firmware required"), message, "help-browser", NULL);
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, "install-firmware",
					/* TRANSLATORS: button label */
					_("Install firmware"), gpk_firmware_libnotify_cb, firmware, NULL);
	notify_notification_add_action (notification, "do-not-show-prompt-firmware",
					_("Do not show this again"), gpk_firmware_libnotify_cb, firmware, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}

out:
	/* never repeat */
	return FALSE;
}

/**
 * gpk_firmware_remove_banned:
 * @data: This class instance
 **/
static void
gpk_firmware_remove_banned (GpkFirmware *firmware, GPtrArray *array)
{
	gchar *banned_str;
	gchar **banned = NULL;
	gchar *filename;
	guint i, j;
	gboolean ret;

	/* get from gconf */
	banned_str = gconf_client_get_string (firmware->priv->gconf_client, GPK_CONF_BANNED_FIRMWARE, NULL);
	if (banned_str == NULL) {
		egg_warning ("could not read banned list");
		goto out;
	}

	/* nothing in list, common case */
	if (egg_strzero (banned_str)) {
		egg_debug ("nothing in banned list");
		goto out;
	}

	/* split using "," */
	banned = g_strsplit (banned_str, ",", 0);

	/* remove any banned pattern matches */
	for (i=0; i<array->len; i++) {
		filename = g_ptr_array_index (array, i);
		for (j=0; banned[j] != NULL; j++) {
			ret = g_pattern_match_simple (banned[j], filename);
			if (ret) {
				egg_debug ("match %s for %s, removing", banned[j], filename);
				g_free (filename);
				g_ptr_array_remove_index_fast (array, i);
			}
		}
	}
out:
	g_free (banned_str);
	g_strfreev (banned);
}

/**
 * gpk_firmware_udev_text_decode:
 **/
static gchar *
gpk_firmware_udev_text_decode (const gchar *data)
{
	guint i;
	guint j;
	gchar *decode;

	decode = g_strdup (data);
	for (i = 0, j = 0; data[i] != '\0'; j++) {
		if (memcmp (&data[i], "\\x2f", 4) == 0) {
			decode[j] = '/';
			i += 4;
		}else if (memcmp (&data[i], "\\x5c", 4) == 0) {
			decode[j] = '\\';
			i += 4;
		} else {
			decode[j] = data[i];
			i++;
		}
	}
	decode[j] = '\0';
	return decode;
}

/**
 * gpk_firmware_class_init:
 * @klass: The GpkFirmwareClass
 **/
static void
gpk_firmware_class_init (GpkFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_firmware_finalize;
	g_type_class_add_private (klass, sizeof (GpkFirmwarePrivate));
}

/**
 * gpk_firmware_init:
 * @firmware: This class instance
 **/
static void
gpk_firmware_init (GpkFirmware *firmware)
{
	gboolean ret;
	GError *error = NULL;
	GDir *dir;
	const gchar *filename;
	gchar *filename_decoded;
	gchar *filename_path;
	guint i;
	GPtrArray *array;

	firmware->priv = GPK_FIRMWARE_GET_PRIVATE (firmware);
	firmware->priv->packages_found = pk_package_list_new ();
	firmware->priv->array_requested = g_ptr_array_new ();
	firmware->priv->gconf_client = gconf_client_get_default ();
	firmware->priv->client_primary = pk_client_new ();
	pk_client_set_synchronous (firmware->priv->client_primary, TRUE, NULL);
	pk_client_set_use_buffer (firmware->priv->client_primary, TRUE, NULL);

	/* should we check and show the user */
	ret = gconf_client_get_bool (firmware->priv->gconf_client, GPK_CONF_ENABLE_CHECK_FIRMWARE, NULL);
	if (!ret) {
		egg_debug ("not showing thanks to GConf");
		return;
	}

	/* open the directory of requests */
	dir = g_dir_open (GPK_FIRMWARE_MISSING_DIR, 0, &error);
	if (dir == NULL) {
		egg_warning ("failed to open directory: %s", error->message);
		g_error_free (error);
		return;
	}

	/* find all the firmware requests */
	filename = g_dir_read_name (dir);
	array = firmware->priv->array_requested;
	while (filename != NULL) {

		/* decode udev text */
		filename_decoded = gpk_firmware_udev_text_decode (filename);
		filename_path = g_build_filename (GPK_FIRMWARE_LOADING_DIR, filename_decoded, NULL);
		egg_debug ("filename=%s -> %s", filename, filename_path);

		/* file already exists */
		ret = g_file_test (filename_path, G_FILE_TEST_EXISTS);
		if (!ret)
			g_ptr_array_add (array, g_strdup (filename_path));

		g_free (filename_decoded);
		g_free (filename_path);
		/* next file */
		filename = g_dir_read_name (dir);
	}
	g_dir_close (dir);

	/* debugging */
	for (i=0; i<array->len; i++) {
		filename = g_ptr_array_index (array, i);
		egg_debug ("requested: %s", filename);
	}

	/* remove banned files */
	gpk_firmware_remove_banned (firmware, array);

	/* debugging */
	for (i=0; i<array->len; i++) {
		filename = g_ptr_array_index (array, i);
		egg_debug ("searching for: %s", filename);
	}

	/* don't spam the user at startup, so wait a little delay */
	if (array->len > 0)
		g_timeout_add_seconds (GPK_FIRMWARE_LOGIN_DELAY, gpk_firmware_timeout_cb, firmware);
}

/**
 * gpk_firmware_finalize:
 * @object: The object to finalize
 **/
static void
gpk_firmware_finalize (GObject *object)
{
	GpkFirmware *firmware;

	g_return_if_fail (GPK_IS_FIRMWARE (object));

	firmware = GPK_FIRMWARE (object);

	g_return_if_fail (firmware->priv != NULL);
	g_ptr_array_foreach (firmware->priv->array_requested, (GFunc) g_free, NULL);
	g_ptr_array_free (firmware->priv->array_requested, TRUE);
	g_object_unref (firmware->priv->packages_found);
	g_object_unref (firmware->priv->client_primary);
	g_object_unref (firmware->priv->gconf_client);

	G_OBJECT_CLASS (gpk_firmware_parent_class)->finalize (object);
}

/**
 * gpk_firmware_new:
 *
 * Return value: a new GpkFirmware object.
 **/
GpkFirmware *
gpk_firmware_new (void)
{
	GpkFirmware *firmware;
	firmware = g_object_new (GPK_TYPE_FIRMWARE, NULL);
	return GPK_FIRMWARE (firmware);
}

