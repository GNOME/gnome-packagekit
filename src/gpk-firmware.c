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

#include <pk-client.h>
#include <pk-common.h>
#include <pk-task-list.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-client.h"
#include "gpk-common.h"
#include "gpk-firmware.h"

static void     gpk_firmware_class_init	(GpkFirmwareClass *klass);
static void     gpk_firmware_init	(GpkFirmware      *firmware);
static void     gpk_firmware_finalize	(GObject	  *object);

#define GPK_FIRMWARE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_FIRMWARE, GpkFirmwarePrivate))
#define GPK_FIRMWARE_STATE_FILE		"/var/run/PackageKit/udev-firmware"
#define GPK_FIRMWARE_LOGIN_DELAY	60 /* seconds */

struct GpkFirmwarePrivate
{
	gchar			**files;
	GConfClient		*gconf_client;
};

G_DEFINE_TYPE (GpkFirmware, gpk_firmware, G_TYPE_OBJECT)

/**
 * gpk_firmware_install_file:
 **/
static gboolean
gpk_firmware_install_file (GpkFirmware *firmware)
{
	GpkClient *gclient;
	gboolean ret;

	gclient = gpk_client_new ();
	ret = gpk_client_install_provide_file (gclient, firmware->priv->files[0], NULL);
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
		egg_debug ("set %s to FALSE", GPK_CONF_PROMPT_FIRMWARE);
		gconf_client_set_bool (firmware->priv->gconf_client, GPK_CONF_PROMPT_FIRMWARE, FALSE, NULL);
	} else {
		egg_warning ("unknown action id: %s", action);
	}
}

static gboolean
gpk_firmware_timeout_cb (gpointer data)
{
	gboolean ret;
	PkClient *client = NULL;
	PkPackageList *list = NULL;
	GError *error = NULL;
	guint length;
	const gchar *message;
	GpkFirmware *firmware = GPK_FIRMWARE (data);
	NotifyNotification *notification;

	/* debug so we can catch polling */
	egg_debug ("polling check");

	/* actually check we can provide the firmware */
	client = pk_client_new ();
	pk_client_set_synchronous (client, TRUE, NULL);
	pk_client_set_use_buffer (client, TRUE, NULL);
	ret = pk_client_search_file (client, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED),
				     firmware->priv->files[0], &error);
	if (!ret) {
		egg_warning ("failed to search file %s: %s", firmware->priv->files[0], error->message);
		g_error_free (error);
		goto out;
	}

	/* make sure we have one package */
	list = pk_client_get_package_list (client);
	length = pk_package_list_get_size (list);
	if (length == 0) {
		egg_debug ("no package providing %s found", firmware->priv->files[0]);
		goto out;
	}
	if (length != 1) {
		egg_warning ("not one package providing %s found (%i)", firmware->priv->files[0], length);
		goto out;
	}

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
	if (list != NULL)
		g_object_unref (list);
	if (client != NULL)
		g_object_unref (client);
	/* never repeat */
	return FALSE;
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
	gchar *files;
	GError *error = NULL;

	firmware->priv = GPK_FIRMWARE_GET_PRIVATE (firmware);
	firmware->priv->files = NULL;
	firmware->priv->gconf_client = gconf_client_get_default ();

	/* should we check and show the user */
	ret = gconf_client_get_bool (firmware->priv->gconf_client, GPK_CONF_PROMPT_FIRMWARE, NULL);
	if (!ret) {
		egg_debug ("not showing thanks to GConf");
		return;
	}

	/* file exists? */
	ret = g_file_test (GPK_FIRMWARE_STATE_FILE, G_FILE_TEST_EXISTS);
	if (!ret) {
		egg_debug ("file '%s' not found", GPK_FIRMWARE_STATE_FILE);
		return;
	}
	/* can we get the contents */
	ret = g_file_get_contents (GPK_FIRMWARE_STATE_FILE, &files, NULL, &error);
	if (!ret) {
		egg_warning ("can't open file %s, %s", GPK_FIRMWARE_STATE_FILE, error->message);
		g_error_free (error);
		return;
	}

	/* split, as we can use multiple lines */
	firmware->priv->files = g_strsplit (files, "\n", 0);

	/* file already exists */
	ret = g_file_test (firmware->priv->files[0], G_FILE_TEST_EXISTS);
	if (ret) {
		egg_debug ("file '%s' already exists", firmware->priv->files[0]);
		return;
	}

	/* don't spam the user at startup */
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
	g_strfreev (firmware->priv->files);
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

