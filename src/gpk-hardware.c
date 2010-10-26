/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Scott Reeves <sreeves@novell.com>
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
#include <libnotify/notify.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-common.h"
#include "gpk-hardware.h"
#include "gpk-task.h"

static void     gpk_hardware_finalize	(GObject	  *object);

#define GPK_HARDWARE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_HARDWARE, GpkHardwarePrivate))
#define GPK_HARDWARE_LOGIN_DELAY	50 /* seconds */
#define GPK_HARDWARE_MULTIPLE_HAL_SIGNALS_DELAY	5 /* seconds */
#define GPK_HARDWARE_INSTALL_ACTION  "GpkHardware - install this package"
#define GPK_HARDWARE_DONT_PROMPT_ACTION  "GpkHardware - dont prompt again"

struct GpkHardwarePrivate
{
	PkTask			*task;
	GSettings		*settings;
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
	gchar			**package_ids;
	gchar			*udi;
};

G_DEFINE_TYPE (GpkHardware, gpk_hardware, G_TYPE_OBJECT)

/**
 * gpk_hardware_install_packages_cb:
 **/
static void
gpk_hardware_install_packages_cb (GObject *object, GAsyncResult *res, GpkHardware *hardware)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
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
		goto out;
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_hardware_libnotify_cb:
 **/
static void
gpk_hardware_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data)
{
	GpkHardware *hardware = GPK_HARDWARE (data);

	if (g_strcmp0 (action, GPK_HARDWARE_INSTALL_ACTION) == 0) {
		pk_client_install_packages_async (PK_CLIENT(hardware->priv->task), TRUE, hardware->priv->package_ids, NULL, NULL, NULL,
						  (GAsyncReadyCallback) gpk_hardware_install_packages_cb, hardware);
	} else if (g_strcmp0 (action, GPK_HARDWARE_DONT_PROMPT_ACTION) == 0) {
		egg_debug ("set %s to FALSE", GPK_SETTINGS_ENABLE_CHECK_HARDWARE);
		g_settings_set_boolean (hardware->priv->settings, GPK_SETTINGS_ENABLE_CHECK_HARDWARE, FALSE);
	} else {
		egg_warning ("unknown action id: %s", action);
	}
}

/**
 * gpk_hardware_what_provides_cb:
 **/
static void
gpk_hardware_what_provides_cb (GObject *object, GAsyncResult *res, GpkHardware *hardware)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	gboolean ret;
	gchar *message = NULL;
	gchar *body = NULL;
	NotifyNotification *notification;
	gchar *package = NULL;
	GPtrArray *array = NULL;
	PkPackage *item = NULL;
	PkError *error_code = NULL;
	gchar *package_id = NULL;
	gchar *summary = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to get provides: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		egg_warning ("failed to get provides: %s, %s", pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* If there are no driver packages available just return */
	array = pk_results_get_package_array (results);
	if (array->len == 0) {
		egg_debug ("no drivers available");
		goto out;
	}

	/* only install the first one? */
	item = g_ptr_array_index (array, 0);

	/* get data */
	g_object_get (item,
		      "package-id", &package_id,
		      "summary", &summary,
		      NULL);

	package = gpk_package_id_format_oneline (package_id, summary);

	/* save array */
	if (hardware->priv->package_ids != NULL)
		g_strfreev (hardware->priv->package_ids);
	hardware->priv->package_ids = pk_package_array_to_strv (array);

	/* TODO: tell the user what hardware, NOT JUST A UDI */
	/* TRANSLATORS: we can install an extra package so this hardware works, e.g. firmware */
	message = g_strdup_printf ("%s\n\t%s", _("Additional packages can be installed to support this hardware"), package);
	/* TRANSLATORS: a new bit of hardware has been plugged in */
	body = g_strdup_printf ("%s", _("New hardware attached"));
	notification = notify_notification_new (body, message, NULL);
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, GPK_HARDWARE_INSTALL_ACTION,
	/* TRANSLATORS: button text, install the packages needed for the hardware to work */
			_("Install package"), gpk_hardware_libnotify_cb, hardware, NULL);
	notify_notification_add_action (notification, GPK_HARDWARE_DONT_PROMPT_ACTION,
					/* TRANSLATORS: don't pop-up the same message twice */
					_("Do not show this again"), gpk_hardware_libnotify_cb, hardware, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
out:
	g_free (package);
	g_free (message);
	g_free (body);
	g_free (package_id);
	g_free (summary);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_hardware_check_for_driver_available:
 **/
static void
gpk_hardware_check_for_driver_available (GpkHardware *hardware, const gchar *udi)
{
	gchar **values = NULL;
	values = g_strsplit (udi, "&", -1);
	pk_client_what_provides_async (PK_CLIENT(hardware->priv->task), pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED),
				       PK_PROVIDES_ENUM_HARDWARE_DRIVER, values, NULL, NULL, NULL,
				       (GAsyncReadyCallback) gpk_hardware_what_provides_cb, hardware);
	g_strfreev (values);
}

static gboolean
gpk_hardware_device_added_timeout (gpointer data)
{
	GpkHardware *hardware = GPK_HARDWARE (data);
	egg_debug ("multiple signal timeout callback");
	gpk_hardware_check_for_driver_available (hardware, hardware->priv->udi);

	g_free (hardware->priv->udi);
	hardware->priv->udi = NULL;
	return FALSE;
}

/**
 * gpk_hardware_device_added_cb:
 **/
static void
gpk_hardware_device_added_cb (DBusGProxy *proxy, const gchar *udi, GpkHardware *hardware)
{
	guint added_id;

	egg_debug ("hardware added. udi=%s", udi);
	/* we get multiple hal signals for one device plugin. Ignore all but first one.
	   TODO: should we act on a different one ?
	*/
	if (hardware->priv->udi == NULL) {
		hardware->priv->udi = g_strdup (udi);
		added_id = g_timeout_add_seconds (GPK_HARDWARE_MULTIPLE_HAL_SIGNALS_DELAY,
				       gpk_hardware_device_added_timeout, hardware);
		g_source_set_name_by_id (added_id, "[GpkHardware] added");
	}
}

/**
 * gpk_hardware_timeout_cb:
 * @data: This class instance
 **/
static gboolean
gpk_hardware_timeout_cb (gpointer data)
{
	egg_debug ("hardware timout callback");
	/* TODO: need to coldplug for any hardware without drivers */
	gpk_hardware_check_for_driver_available (GPK_HARDWARE (data), "unavailable");
	return FALSE;
}

/**
 * gpk_hardware_init:
 * @hardware: This class instance
 **/
static void
gpk_hardware_init (GpkHardware *hardware)
{
	gboolean ret;
	guint login_id = 0;
	GError *error = NULL;

	hardware->priv = GPK_HARDWARE_GET_PRIVATE (hardware);
	hardware->priv->settings = g_settings_new (GPK_SETTINGS_SCHEMA);
	hardware->priv->package_ids = NULL;
	hardware->priv->udi = NULL;

	/* should we check and show the user */
	ret = g_settings_get_boolean (hardware->priv->settings, GPK_SETTINGS_ENABLE_CHECK_HARDWARE);
	if (!ret) {
		egg_debug ("hardware driver checking disabled in GSettings");
		return;
	}
	hardware->priv->task = PK_TASK(gpk_task_new ());
	g_object_set (hardware->priv->task,
		      "background", TRUE,
		      NULL);

	hardware->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("Cannot connect to bus: %s", error->message);
		g_error_free (error);
		return;
	}
	hardware->priv->proxy = dbus_g_proxy_new_for_name (hardware->priv->connection,
							   "org.freedesktop.Hal",
							   "/org/freedesktop/Hal/Manager",
							   "org.freedesktop.Hal.Manager");
	dbus_g_proxy_add_signal (hardware->priv->proxy, "DeviceAdded",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (hardware->priv->proxy, "DeviceAdded",
				     G_CALLBACK (gpk_hardware_device_added_cb), hardware, NULL);

	/* check at startup (plus delay) and see if there is cold plugged hardware needing drivers */
	login_id = g_timeout_add_seconds (GPK_HARDWARE_LOGIN_DELAY, gpk_hardware_timeout_cb, hardware);
	g_source_set_name_by_id (login_id, "[GpkHardware] login");
}

/**
 * gpk_hardware_class_init:
 * @klass: The GpkHardwareClass
 **/
static void
gpk_hardware_class_init (GpkHardwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_hardware_finalize;
	g_type_class_add_private (klass, sizeof (GpkHardwarePrivate));
}

/**
 * gpk_hardware_finalize:
 * @object: The object to finalize
 **/
static void
gpk_hardware_finalize (GObject *object)
{
	GpkHardware *hardware;

	g_return_if_fail (GPK_IS_HARDWARE (object));

	hardware = GPK_HARDWARE (object);

	g_return_if_fail (hardware->priv != NULL);
	g_object_unref (hardware->priv->settings);
	if (hardware->priv->task != NULL)
		g_object_unref (hardware->priv->task);
	g_strfreev (hardware->priv->package_ids);

	G_OBJECT_CLASS (gpk_hardware_parent_class)->finalize (object);
}

/**
 * gpk_hardware_new:
 *
 * Return value: a new GpkHardware object.
 **/
GpkHardware *
gpk_hardware_new (void)
{
	GpkHardware *hardware;
	hardware = g_object_new (GPK_TYPE_HARDWARE, NULL);
	return GPK_HARDWARE (hardware);
}

