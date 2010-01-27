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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <libnotify/notify.h>
#include <packagekit-glib2/packagekit.h>
#ifdef GPK_BUILD_GUDEV
#include <gudev/gudev.h>
#endif

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-console-kit.h"

#include "gpk-common.h"
#include "gpk-error.h"
#include "gpk-firmware.h"
#include "gpk-task.h"

static void     gpk_firmware_finalize	(GObject	  *object);

#define GPK_FIRMWARE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_FIRMWARE, GpkFirmwarePrivate))
#define GPK_FIRMWARE_MISSING_DIR		"/dev/.udev/firmware-missing"
#define GPK_FIRMWARE_LOADING_DIR		"/lib/firmware"
#define GPK_FIRMWARE_LOGIN_DELAY		10 /* seconds */
#define GPK_FIRMWARE_PROCESS_DELAY		2 /* seconds */
#define GPK_FIRMWARE_INSERT_DELAY		2 /* seconds */
#define GPK_FIRMWARE_DEVICE_REBIND_PROGRAM	"/usr/sbin/pk-device-rebind"

struct GpkFirmwarePrivate
{
	EggConsoleKit		*consolekit;
	GConfClient		*gconf_client;
	GFileMonitor		*monitor;
	GPtrArray		*array_requested;
	PkTask			*task;
	GPtrArray		*packages_found;
	guint			 timeout_id;
};

typedef enum {
	GPK_FIRMWARE_SUBSYSTEM_USB,
	GPK_FIRMWARE_SUBSYSTEM_PCI,
	GPK_FIRMWARE_SUBSYSTEM_UNKNOWN
} GpkFirmwareSubsystem;

typedef struct {
	gchar			*filename;
	gchar			*sysfs_path;
	gchar			*model;
	gchar			*id;
	GpkFirmwareSubsystem	 subsystem;
} GpkFirmwareRequest;

G_DEFINE_TYPE (GpkFirmware, gpk_firmware, G_TYPE_OBJECT)

static void gpk_firmware_install_file (GpkFirmware *firmware);
static void gpk_firmware_ignore_devices (GpkFirmware *firmware);

/**
 * gpk_firmware_subsystem_can_replug:
 **/
static gboolean
gpk_firmware_subsystem_can_replug (GpkFirmwareSubsystem subsystem)
{
	if (subsystem == GPK_FIRMWARE_SUBSYSTEM_USB)
		return TRUE;
	return FALSE;
}

/**
 * gpk_firmware_request_new:
 **/
static GpkFirmwareRequest *
gpk_firmware_request_new (const gchar *filename, const gchar *sysfs_path)
{
	GpkFirmwareRequest *req;
#ifdef GPK_BUILD_GUDEV
	GUdevDevice *device;
	GUdevClient *client;
	const gchar *subsystem;
	const gchar *model;
	const gchar *id_vendor;
	const gchar *id_product;
#endif

	req = g_new0 (GpkFirmwareRequest, 1);
	req->filename = g_strdup (filename);
	req->sysfs_path = g_strdup (sysfs_path);
	req->subsystem = GPK_FIRMWARE_SUBSYSTEM_UNKNOWN;
#ifdef GPK_BUILD_GUDEV

	/* get all subsystems */
	client = g_udev_client_new (NULL);
	device = g_udev_client_query_by_sysfs_path (client, sysfs_path);
	if (device == NULL)
		goto out;

	/* find subsystem, which will affect if we have to replug, or reboot */
	subsystem = g_udev_device_get_subsystem (device);
	if (g_strcmp0 (subsystem, "usb") == 0) {
		req->subsystem = GPK_FIRMWARE_SUBSYSTEM_USB;
	} else if (g_strcmp0 (subsystem, "pci") == 0) {
		req->subsystem = GPK_FIRMWARE_SUBSYSTEM_PCI;
	} else {
		egg_warning ("subsystem unrecognised: %s", subsystem);
	}

	/* get model, so we can show something sensible */
	model = g_udev_device_get_property (device, "ID_MODEL");
	if (model != NULL && model[0] != '\0') {
		req->model = g_strdup (model);
		/* replace invalid chars */
		g_strdelimit (req->model, "_", ' ');
	}

	/* create ID so we can ignore the specific device */
	id_vendor = g_udev_device_get_property (device, "ID_VENDOR");
	id_product = g_udev_device_get_property (device, "ID_MODEL_ID");
	req->id = g_strdup_printf ("%s_%s", id_vendor, id_product);
out:
	g_object_unref (device);
	g_object_unref (client);
#endif
	return req;
}

/**
 * gpk_firmware_request_free:
 **/
static void
gpk_firmware_request_free (GpkFirmwareRequest *req)
{
	g_free (req->filename);
	g_free (req->model);
	g_free (req->sysfs_path);
	g_free (req->id);
	g_free (req);
}


/**
 * gpk_firmware_rebind:
 **/
static gboolean
gpk_firmware_rebind (GpkFirmware *firmware)
{
	gboolean ret;
	gchar *command;
	gchar *rebind_stderr = NULL;
	gchar *rebind_stdout = NULL;
	GError *error = NULL;
	gint exit_status = 0;
	guint i;
	GPtrArray *array;
	const GpkFirmwareRequest *req;
	GString *string;

	string = g_string_new ("");

	/* make a string array of all the devices to replug */
	array = firmware->priv->array_requested;
	for (i=0; i<array->len; i++) {
		req = g_ptr_array_index (array, i);
		g_string_append_printf (string, "%s ", req->sysfs_path);
	}

	/* remove trailing space */
	if (string->len > 0)
		g_string_set_size (string, string->len-1);

	/* use PolicyKit to do this as root */
	command = g_strdup_printf ("pkexec %s %s", GPK_FIRMWARE_DEVICE_REBIND_PROGRAM, string->str);
	ret = g_spawn_command_line_sync (command, &rebind_stdout, &rebind_stderr, &exit_status, &error);
	if (!ret) {
		egg_warning ("failed to spawn '%s': %s", command, error->message);
		g_error_free (error);
		goto out;
	}

	/* if we failed to rebind the device */
	if (exit_status != 0) {
		egg_warning ("failed to rebind: %s, %s", rebind_stdout, rebind_stderr);
		ret = FALSE;
		goto out;
	}
out:
	g_free (rebind_stdout);
	g_free (rebind_stderr);
	g_free (command);
	g_string_free (string, TRUE);
	return ret;
}

/**
 * gpk_firmware_libnotify_cb:
 **/
static void
gpk_firmware_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data)
{
	GpkFirmware *firmware = GPK_FIRMWARE (data);
	gboolean ret;
	GError *error = NULL;

	if (g_strcmp0 (action, "install-firmware") == 0) {
		gpk_firmware_install_file (firmware);
	} else if (g_strcmp0 (action, "ignore-devices") == 0) {
		gpk_firmware_ignore_devices (firmware);
	} else if (g_strcmp0 (action, "restart-now") == 0) {
		ret = egg_console_kit_restart (firmware->priv->consolekit, &error);
		if (!ret) {
			egg_warning ("failed to reset: %s", error->message);
			g_error_free (error);
		}
	} else {
		egg_warning ("unknown action id: %s", action);
	}
}

/**
 * gpk_firmware_require_restart:
 **/
static void
gpk_firmware_require_restart (GpkFirmware *firmware)
{
	const gchar *message;
	gboolean can_restart = FALSE;
	gboolean ret;
	GError *error = NULL;
	NotifyNotification *notification;

	/* TRANSLATORS: we need to restart so the new hardware can re-request the firmware */
	message = _("You will need to restart this computer before the hardware will work correctly.");

	/* TRANSLATORS: title of libnotify bubble */
	notification = notify_notification_new (_("Additional software was installed"), message, "help-browser", NULL);
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);

	/* only show the restart button if we can restart */
	egg_console_kit_can_restart (firmware->priv->consolekit, &can_restart, NULL);
	if (can_restart) {
		notify_notification_add_action (notification, "restart-now",
						/* TRANSLATORS: button label */
						_("Restart now"), gpk_firmware_libnotify_cb, firmware, NULL);
	}

	/* show the bubble */
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_firmware_require_replug:
 **/
static void
gpk_firmware_require_replug (GpkFirmware *firmware)
{
	const gchar *message;
	gboolean ret;
	GError *error = NULL;
	NotifyNotification *notification;

	/* TRANSLATORS: we need to remove an replug so the new hardware can re-request the firmware */
	message = _("You will need to remove and then reinsert the hardware before it will work correctly.");

	/* TRANSLATORS: title of libnotify bubble */
	notification = notify_notification_new (_("Additional software was installed"), message, "help-browser", NULL);
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);

	/* show the bubble */
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_firmware_require_nothing:
 **/
static void
gpk_firmware_require_nothing (GpkFirmware *firmware)
{
	const gchar *message;
	gboolean ret;
	GError *error = NULL;
	NotifyNotification *notification;

	/* TRANSLATORS: we need to remove an replug so the new hardware can re-request the firmware */
	message = _("Your hardware has been set up and is now ready to use.");

	/* TRANSLATORS: title of libnotify bubble */
	notification = notify_notification_new (_("Additional software was installed"), message, "help-browser", NULL);
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);

	/* show the bubble */
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_firmware_install_packages_cb:
 **/
static void
gpk_firmware_install_packages_cb (GObject *object, GAsyncResult *res, GpkFirmware *firmware)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	GPtrArray *array = NULL;
	gboolean restart = FALSE;
	const GpkFirmwareRequest *req;
	gboolean ret;
	guint i;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to install file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to install file: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));

		/* ignore some errors */
		if (pk_error_get_code (error_code) != PK_ERROR_ENUM_PROCESS_KILL &&
		    pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED &&
		    pk_error_get_code (error_code) != PK_ERROR_ENUM_NOT_AUTHORIZED) {
			gpk_error_dialog (gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
					  gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}
		goto out;
	}

	/* go through all the requests, and find the worst type */
	array = firmware->priv->array_requested;
	for (i=0; i<array->len; i++) {
		req = g_ptr_array_index (array, i);
		ret = gpk_firmware_subsystem_can_replug (req->subsystem);
		if (!ret) {
			restart = TRUE;
			break;
		}
	}

	/* can we just rebind the device */
	ret = g_file_test (GPK_FIRMWARE_DEVICE_REBIND_PROGRAM, G_FILE_TEST_EXISTS);
	if (ret) {
		ret = gpk_firmware_rebind (firmware);
		if (ret) {
			gpk_firmware_require_nothing (firmware);
			goto out;
		}
	} else {
		/* give the user the correct message */
		if (restart)
			gpk_firmware_require_restart (firmware);
		else
			gpk_firmware_require_replug (firmware);
	}

	/* clear array */
	g_ptr_array_set_size (firmware->priv->array_requested, 0);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_firmware_install_file:
 **/
static void
gpk_firmware_install_file (GpkFirmware *firmware)
{
	gchar **package_ids;

	/* install all of the firmware files */
	package_ids = pk_package_array_to_strv (firmware->priv->packages_found);
	pk_client_install_packages_async (PK_CLIENT(firmware->priv->task), TRUE, package_ids, NULL, NULL, NULL,
					  (GAsyncReadyCallback) gpk_firmware_install_packages_cb, firmware);
	g_strfreev (package_ids);
}

/**
 * gpk_firmware_ignore_devices:
 **/
static void
gpk_firmware_ignore_devices (GpkFirmware *firmware)
{
	gboolean ret;
	gchar *existing = NULL;
	GError *error = NULL;
	GpkFirmwareRequest *req;
	GPtrArray *array;
	GString *string = NULL;
	guint i;

	/* get from gconf */
	existing = gconf_client_get_string (firmware->priv->gconf_client, GPK_CONF_IGNORED_DEVICES, &error);
	if (error != NULL) {
		egg_warning ("failed to get ignored devices: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get existing string */
	string = g_string_new (existing);
	if (string->len > 0)
		g_string_append (string, ",");

	/* add all listed devices */
	array = firmware->priv->array_requested;
	for (i=0; i<array->len; i++) {
		req = g_ptr_array_index (array, i);
		g_string_append_printf (string, "%s,", req->id);
	}

	/* remove final ',' */
	if (string->len > 2)
		g_string_set_size (string, string->len - 1);

	/* set new string to gconf */
	ret = gconf_client_set_string (firmware->priv->gconf_client, GPK_CONF_IGNORED_DEVICES, string->str, &error);
	if (!ret) {
		egg_warning ("failed to set new string %s: %s", string->str, error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (existing);
	if (string != NULL)
		g_string_free (string, TRUE);
}

/**
 * gpk_firmware_check_available:
 * @firmware: This class instance
 * @filename: Firmware to search for
 **/
static PkPackage *
gpk_firmware_check_available (GpkFirmware *firmware, const gchar *filename)
{
	guint length = 0;
	GPtrArray *array = NULL;
	GError *error = NULL;
	PkPackage *item = NULL;
	PkBitfield filter;
	PkResults *results;
	gchar **values = NULL;
	PkError *error_code = NULL;

	/* search for newest not installed package */
	filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED, PK_FILTER_ENUM_NEWEST, -1);
	values = g_strsplit (filename, "&", -1);
	results = pk_client_search_files (PK_CLIENT(firmware->priv->task), filter, values, NULL, NULL, NULL, &error);
	if (results == NULL) {
		egg_warning ("failed to search file %s: %s", filename, error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to search file: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* make sure we have one package */
	array = pk_results_get_package_array (results);
	if (array->len == 0)
		egg_debug ("no package providing %s found", filename);
	else if (array->len != 1)
		egg_warning ("not one package providing %s found (%i)", filename, length);
	else
		item = g_object_ref (g_ptr_array_index (array, 0));
out:
	g_strfreev (values);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_object_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return item;
}

/**
 * gpk_firmware_remove_duplicate:
 **/
static void
gpk_firmware_remove_duplicate (GPtrArray *array)
{
	guint i, j;
	const gchar *val;
	const gchar *val_tmp;

	/* remove each duplicate entry */
	for (i=0; i<array->len; i++) {
		val = g_ptr_array_index (array, i);
		for (j=i+1; j<array->len; j++) {
			val_tmp = g_ptr_array_index (array, j);
			if (g_strcmp0 (val_tmp, val) == 0)
				g_ptr_array_remove_index_fast (array, j);
		}
	}
}

/**
 * gpk_firmware_timeout_cb:
 **/
static gboolean
gpk_firmware_timeout_cb (gpointer data)
{
	guint i;
	gboolean ret;
	GString *string;
	GpkFirmware *firmware = GPK_FIRMWARE (data);
	NotifyNotification *notification;
	GPtrArray *array;
	GError *error = NULL;
	PkPackage *item = NULL;
	const GpkFirmwareRequest *req;
	gboolean has_data = FALSE;

	/* message string */
	string = g_string_new ("");

	/* try to find each firmware file in an available package */
	array = firmware->priv->array_requested;
	for (i=0; i<array->len; i++) {
		req = g_ptr_array_index (array, i);
		/* save to new array if we found one package for this file */
		item = gpk_firmware_check_available (firmware, req->filename);
		if (item != NULL) {
			g_ptr_array_add (firmware->priv->packages_found, item);
			g_object_unref (item);
		}
	}

	/* nothing to do */
	if (firmware->priv->packages_found->len == 0) {
		egg_debug ("no packages providing any of the missing firmware");
		goto out;
	}

	/* check we don't want the same package more than once */
	gpk_firmware_remove_duplicate (firmware->priv->packages_found);

	/* have we got any models to array */
	for (i=0; i<array->len; i++) {
		req = g_ptr_array_index (array, i);
		if (req->model != NULL) {
			has_data = TRUE;
			break;
		}
	}

	/* TRANSLATORS: we need another package to keep udev quiet */
	g_string_append (string, _("Additional firmware is required to make hardware in this computer function correctly."));

	/* sdd what information we have */
	if (has_data) {
		g_string_append (string, "\n");
		for (i=0; i<array->len; i++) {
			req = g_ptr_array_index (array, i);
			if (req->model != NULL)
				g_string_append_printf (string, "\n• %s", req->model);
		}
		g_string_append (string, "\n");
	}

	/* TRANSLATORS: title of libnotify bubble */
	notification = notify_notification_new (_("Additional firmware required"), string->str, "help-browser", NULL);
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, "install-firmware",
					/* TRANSLATORS: button label */
					_("Install firmware"), gpk_firmware_libnotify_cb, firmware, NULL);
	notify_notification_add_action (notification, "ignore-devices",
					/* TRANSLATORS: we should ignore this device and not ask anymore */
					_("Ignore devices"), gpk_firmware_libnotify_cb, firmware, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}

out:
	g_string_free (string, TRUE);
	/* never repeat */
	return FALSE;
}

/**
 * gpk_firmware_remove_banned:
 **/
static void
gpk_firmware_remove_banned (GpkFirmware *firmware, GPtrArray *array)
{
	gboolean ret;
	gchar **banned = NULL;
	gchar *banned_str;
	GpkFirmwareRequest *req;
	guint i, j;

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
		req = g_ptr_array_index (array, i);
		for (j=0; banned[j] != NULL; j++) {
			ret = g_pattern_match_simple (banned[j], req->filename);
			if (ret) {
				egg_debug ("match %s for %s, removing", banned[j], req->filename);
				gpk_firmware_request_free (req);
				g_ptr_array_remove_index_fast (array, i);
				break;
			}
		}
	}
out:
	g_free (banned_str);
	g_strfreev (banned);
}

/**
 * gpk_firmware_remove_ignored:
 **/
static void
gpk_firmware_remove_ignored (GpkFirmware *firmware, GPtrArray *array)
{
	gboolean ret;
	gchar **ignored = NULL;
	gchar *ignored_str;
	GpkFirmwareRequest *req;
	guint i, j;

	/* get from gconf */
	ignored_str = gconf_client_get_string (firmware->priv->gconf_client, GPK_CONF_IGNORED_DEVICES, NULL);
	if (ignored_str == NULL) {
		egg_warning ("could not read ignored list");
		goto out;
	}

	/* nothing in list, common case */
	if (egg_strzero (ignored_str)) {
		egg_debug ("nothing in ignored list");
		goto out;
	}

	/* split using "," */
	ignored = g_strsplit (ignored_str, ",", 0);

	/* remove any ignored pattern matches */
	for (i=0; i<array->len; i++) {
		req = g_ptr_array_index (array, i);
		for (j=0; ignored[j] != NULL; j++) {
			ret = g_pattern_match_simple (ignored[j], req->id);
			if (ret) {
				egg_debug ("match %s for %s, removing", ignored[j], req->id);
				gpk_firmware_request_free (req);
				g_ptr_array_remove_index_fast (array, i);
				break;
			}
		}
	}
out:
	g_free (ignored_str);
	g_strfreev (ignored);
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
		} else if (memcmp (&data[i], "\\x5c", 4) == 0) {
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
 * gpk_firmware_get_device:
 **/
static gchar *
gpk_firmware_get_device (GpkFirmware *firmware, const gchar *filename)
{
	GFile *file;
	GFileInfo *info;
	const gchar *symlink_path;
	guint len;
	gchar *syspath = NULL;
	gchar **split = NULL;
	GError *error = NULL;
	gchar *target = NULL;
	guint i;

	/* get the file data */
	file = g_file_new_for_path (filename);
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET, G_FILE_QUERY_INFO_NONE, NULL, &error);
	if (info == NULL) {
		egg_warning ("Failed to get symlink: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* /devices/pci0000:00/0000:00:1d.0/usb5/5-2/firmware/5-2 */
	symlink_path = g_file_info_get_symlink_target (info);
	if (symlink_path == NULL) {
		egg_warning ("failed to get symlink target");
		goto out;
	}

	/* prepend sys to make '/sys/devices/pci0000:00/0000:00:1d.0/usb5/5-2/firmware/5-2' */
	syspath = g_strjoin (NULL, "/sys", symlink_path, NULL);

	/* now find device without the junk */
	split = g_strsplit (syspath, "/", -1);
	len = g_strv_length (split);

	/* start with the longest, and try to find a path that exists */
	for (i=len; i>1; i--) {
		split[i] = NULL;
		target = g_strjoinv ("/", split);
		egg_debug ("testing %s", target);
		if (g_file_test (target, G_FILE_TEST_EXISTS))
			goto out;
		g_free (target);
	}

	/* ensure we return error if nothing found */
	target = NULL;
out:
	if (info != NULL)
		g_object_unref (info);
	g_object_unref (file);
	g_free (syspath);
	g_strfreev (split);
	return target;
}

/**
 * gpk_firmware_add_file:
 **/
static void
gpk_firmware_add_file (GpkFirmware *firmware, const gchar *filename_no_path)
{
	gboolean ret;
	gchar *filename_path = NULL;
	gchar *missing_path = NULL;
	gchar *sysfs_path = NULL;
	GpkFirmwareRequest *req;
	GPtrArray *array;
	guint i;

	/* this is the file we want to load */
	filename_path = g_build_filename (GPK_FIRMWARE_LOADING_DIR, filename_no_path, NULL);

	/* file already exists */
	ret = g_file_test (filename_path, G_FILE_TEST_EXISTS);
	if (ret)
		goto out;

	/* this is the file that udev created for us */
	missing_path = g_build_filename (GPK_FIRMWARE_MISSING_DIR, filename_no_path, NULL);
	egg_debug ("filename=%s -> %s", missing_path, filename_path);

	/* get symlink target */
	sysfs_path = gpk_firmware_get_device (firmware, missing_path);
	if (sysfs_path == NULL)
		goto out;

	/* find any previous requests with this path or firmware */
	array = firmware->priv->array_requested;
	for (i=0; i<array->len; i++) {
		req = g_ptr_array_index (array, i);
		if (g_strcmp0 (sysfs_path, req->sysfs_path) == 0) {
			egg_debug ("ignoring previous sysfs request for %s", sysfs_path);
			goto out;
		}
		if (g_strcmp0 (filename_path, req->filename) == 0) {
			egg_debug ("ignoring previous filename request for %s", filename_path);
			goto out;
		}
	}

	/* create new request object */
	req = gpk_firmware_request_new (filename_path, sysfs_path);
	g_ptr_array_add (firmware->priv->array_requested, req);
out:
	g_free (missing_path);
	g_free (filename_path);
	g_free (sysfs_path);
}

/**
 * gpk_firmware_scan_directory:
 **/
static void
gpk_firmware_scan_directory (GpkFirmware *firmware)
{
	gboolean ret;
	GError *error = NULL;
	GDir *dir;
	const gchar *filename;
	gchar *filename_decoded;
	guint i;
	GPtrArray *array;
	const GpkFirmwareRequest *req;

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
	while (filename != NULL) {

		filename_decoded = gpk_firmware_udev_text_decode (filename);
		gpk_firmware_add_file (firmware, filename_decoded);
		g_free (filename_decoded);

		/* next file */
		filename = g_dir_read_name (dir);
	}
	g_dir_close (dir);

	/* debugging */
	array = firmware->priv->array_requested;
	for (i=0; i<array->len; i++) {
		req = g_ptr_array_index (array, i);
		egg_debug ("requested: %s", req->filename);
	}

	/* remove banned files */
	gpk_firmware_remove_banned (firmware, array);

	/* remove ignored devices */
	gpk_firmware_remove_ignored (firmware, array);

	/* debugging */
	array = firmware->priv->array_requested;
	for (i=0; i<array->len; i++) {
		req = g_ptr_array_index (array, i);
		egg_debug ("searching for: %s", req->filename);
	}

	/* don't spam the user at startup, so wait a little delay */
	if (array->len > 0)
		g_timeout_add_seconds (GPK_FIRMWARE_PROCESS_DELAY, gpk_firmware_timeout_cb, firmware);
}

/**
 * gpk_firmware_scan_directory_cb:
 **/
static gboolean
gpk_firmware_scan_directory_cb (GpkFirmware *firmware)
{
	gpk_firmware_scan_directory (firmware);
	firmware->priv->timeout_id = 0;
	return FALSE;
}

/**
 * gpk_firmware_monitor_changed_cb:
 **/
static void
gpk_firmware_monitor_changed_cb (GFileMonitor *monitor, GFile *file, GFile *other_file,
				 GFileMonitorEvent event_type, GpkFirmware *firmware)
{
	if (firmware->priv->timeout_id > 0) {
		egg_debug ("clearing timeout as device changed");
		g_source_remove (firmware->priv->timeout_id);
	}

	/* wait for the device to settle */
	firmware->priv->timeout_id =
		g_timeout_add_seconds (GPK_FIRMWARE_INSERT_DELAY, (GSourceFunc) gpk_firmware_scan_directory_cb, firmware);
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
	GFile *file;
	GError *error = NULL;

	firmware->priv = GPK_FIRMWARE_GET_PRIVATE (firmware);
	firmware->priv->timeout_id = 0;
	firmware->priv->packages_found = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	firmware->priv->array_requested = g_ptr_array_new_with_free_func ((GDestroyNotify) gpk_firmware_request_free);
	firmware->priv->gconf_client = gconf_client_get_default ();
	firmware->priv->consolekit = egg_console_kit_new ();
	firmware->priv->task = PK_TASK(gpk_task_new ());
	g_object_set (firmware->priv->task,
		      "background", TRUE,
		      NULL);

	/* setup watch for new hardware */
	file = g_file_new_for_path (GPK_FIRMWARE_MISSING_DIR);
	firmware->priv->monitor = g_file_monitor (file, G_FILE_MONITOR_NONE, NULL, &error);
	if (firmware->priv->monitor == NULL) {
		egg_warning ("failed to setup monitor: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* limit to one per second */
	g_file_monitor_set_rate_limit (firmware->priv->monitor, 1000);

	/* get notified of changes */
	g_signal_connect (firmware->priv->monitor, "changed",
			  G_CALLBACK (gpk_firmware_monitor_changed_cb), firmware);
out:
	g_object_unref (file);
	firmware->priv->timeout_id =
		g_timeout_add_seconds (GPK_FIRMWARE_LOGIN_DELAY, (GSourceFunc) gpk_firmware_scan_directory_cb, firmware);
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
	g_ptr_array_unref (firmware->priv->array_requested);
	g_ptr_array_unref (firmware->priv->packages_found);
	g_object_unref (PK_CLIENT(firmware->priv->task));
	g_object_unref (firmware->priv->gconf_client);
	g_object_unref (firmware->priv->consolekit);
	if (firmware->priv->monitor != NULL)
		g_object_unref (firmware->priv->monitor);
	if (firmware->priv->timeout_id > 0)
		g_source_remove (firmware->priv->timeout_id);

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

