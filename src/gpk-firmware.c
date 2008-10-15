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
#include "egg-string-list.h"

#include "gpk-client.h"
#include "gpk-common.h"
#include "gpk-firmware.h"

static void     gpk_firmware_class_init	(GpkFirmwareClass *klass);
static void     gpk_firmware_init	(GpkFirmware      *firmware);
static void     gpk_firmware_finalize	(GObject	  *object);

#define GPK_FIRMWARE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_FIRMWARE, GpkFirmwarePrivate))
#define GPK_FIRMWARE_STATE_DIR		"/var/run/PackageKit/udev"
#define GPK_FIRMWARE_LOGIN_DELAY	60 /* seconds */

struct GpkFirmwarePrivate
{
	EggStrList		*array_found;
	EggStrList		*array_requested;
	GConfClient		*gconf_client;
};

G_DEFINE_TYPE (GpkFirmware, gpk_firmware, G_TYPE_OBJECT)

/**
 * gpk_firmware_install_file:
 **/
static gboolean
gpk_firmware_install_file (GpkFirmware *firmware)
{
	guint i;
	gboolean ret;
	GpkClient *gclient;
	GPtrArray *array;
	GError *error = NULL;
	const gchar *filename;

	gclient = gpk_client_new ();
	array = firmware->priv->array_found;

	/* try to install each firmware file */
	for (i=0; i<array->len; i++) {
		filename = egg_str_list_index (array, i);
		gpk_client_set_interaction (gclient, GPK_CLIENT_INTERACT_WARNING_PROGRESS);
		ret = gpk_client_install_provide_file (gclient, filename, &error);
		if (!ret) {
			egg_warning ("failed to install provide file: %s", error->message);
			g_error_free (error);
			error = NULL;
		}
	}
	g_object_unref (gclient);
	return ret;
}

/**
 * gpk_firmware_libnotify_cb:
 **/
static void
gpk_firmware_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data)
{
	GpkFirmware *firmware = GPK_FIRMWARE (data);

	if (egg_strequal (action, "install-firmware")) {
		gpk_firmware_install_file (firmware);
	} else if (egg_strequal (action, "do-not-show-prompt-firmware")) {
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
static gboolean
gpk_firmware_check_available (GpkFirmware *firmware, const gchar *filename)
{
	gboolean ret;
	guint length = 0;
	PkClient *client = NULL;
	PkPackageList *list = NULL;
	GError *error = NULL;

	/* actually check we can provide the firmware */
	client = pk_client_new ();
	pk_client_set_synchronous (client, TRUE, NULL);
	pk_client_set_use_buffer (client, TRUE, NULL);
	ret = pk_client_search_file (client, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), filename, &error);
	if (!ret) {
		egg_warning ("failed to search file %s: %s", filename, error->message);
		g_error_free (error);
		error = NULL;
		goto out;
	}

	/* make sure we have one package */
	list = pk_client_get_package_list (client);
	length = pk_package_list_get_size (list);
	g_object_unref (list);
	if (length == 0)
		egg_debug ("no package providing %s found", filename);
	else if (length != 1)
		egg_warning ("not one package providing %s found (%i)", filename, length);

out:
	g_object_unref (client);
	return (length == 1);
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

	/* debug so we can catch polling */
	egg_debug ("polling check");

	/* try to find each firmware file in an available package */
	array = firmware->priv->array_requested;
	for (i=0; i<array->len; i++) {
		filename = egg_str_list_index (array, i);
		/* save to new array if we found one package for this file */
		ret = gpk_firmware_check_available (firmware, filename);
		if (ret)
			egg_str_list_add (firmware->priv->array_found, filename);
	}

	/* nothing to do */
	array = firmware->priv->array_found;
	if (array->len == 0) {
		egg_debug ("no packages providing any of the missing firmware");
		goto out;
	}

	/* check we don't want the same package more than once */
	egg_str_list_remove_duplicate (array);

	/* debugging */
	egg_debug ("need to install:");
	egg_str_list_print (array);

	message = _("Additional firmware is required to make hardware in this computer function correctly.");
	notification = notify_notification_new (_("Additional firmware required"), message, "help-browser", NULL);
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, "install-firmware",
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
	EggStrList *banned_list = NULL;

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

	/* add to string list */
	banned_list = egg_str_list_new ();
	egg_str_list_add_strv (banned_list, banned);

	/* remove all entries in banned list from main list */
	egg_str_list_remove_list (array, banned_list);
out:
	g_free (banned_str);
	g_strfreev (banned);
	if (banned_list != NULL)
		g_object_unref (banned_list);
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
	gchar *contents;
	GError *error = NULL;
	GDir *dir;
	const gchar *filename;
	gchar *filename_path;
	gchar **firmware_files;
	guint i;
	guint length;
	GPtrArray *array;

	firmware->priv = GPK_FIRMWARE_GET_PRIVATE (firmware);
	firmware->priv->array_found = egg_str_list_new ();
	firmware->priv->array_requested = egg_str_list_new ();
	firmware->priv->gconf_client = gconf_client_get_default ();

	/* should we check and show the user */
	ret = gconf_client_get_bool (firmware->priv->gconf_client, GPK_CONF_ENABLE_CHECK_FIRMWARE, NULL);
	if (!ret) {
		egg_debug ("not showing thanks to GConf");
		return;
	}

	/* open the directory of requests */
	dir = g_dir_open (GPK_FIRMWARE_STATE_DIR, 0, &error);
	if (dir == NULL) {
		egg_warning ("failed to open directory: %s", error->message);
		g_error_free (error);
		return;
	}

	/* find all the firmware requests */
	filename = g_dir_read_name (dir);
	array = firmware->priv->array_requested;
	while (filename != NULL) {

		/* can we get the contents */
		filename_path = g_build_filename (GPK_FIRMWARE_STATE_DIR, filename, NULL);
		egg_debug ("opening %s", filename_path);
		ret = g_file_get_contents (filename_path, &contents, NULL, &error);
		if (!ret) {
			egg_warning ("can't open file %s, %s", filename, error->message);
			g_error_free (error);
			error = NULL;
			goto skip_file;
		}

		/* split, as we can use multiple requests in one file */
		firmware_files = g_strsplit (contents, "\n", 0);
		length = g_strv_length (firmware_files);
		for (i=0; i<length; i++) {
			if (!egg_strzero (firmware_files[i])) {
				/* file still doesn't exist */
				ret = g_file_test (firmware_files[i], G_FILE_TEST_EXISTS);
				if (!ret)
					egg_str_list_add (array, firmware_files[i]);
			}
		}
		g_free (contents);
		g_strfreev (firmware_files);
skip_file:
		g_free (filename_path);
		/* next file */
		filename = g_dir_read_name (dir);
	}
	g_dir_close (dir);

	/* don't request duplicates */
	egg_str_list_remove_duplicate (array);

	/* debugging */
	for (i=0; i<array->len; i++) {
		filename = egg_str_list_index (array, i);
		egg_debug ("requested: %s", filename);
	}

	/* remove banned files */
	gpk_firmware_remove_banned (firmware, array);

	/* debugging */
	for (i=0; i<array->len; i++) {
		filename = egg_str_list_index (array, i);
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
	egg_str_list_free (firmware->priv->array_found);
	egg_str_list_free (firmware->priv->array_requested);
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

