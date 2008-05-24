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

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-common.h>
#include <pk-task-list.h>

#include <gpk-client.h>
#include <gpk-common.h>

#include "gpk-notify.h"
#include "gpk-firmware.h"

static void     gpk_firmware_class_init	(GpkFirmwareClass *klass);
static void     gpk_firmware_init	(GpkFirmware      *firmware);
static void     gpk_firmware_finalize	(GObject	  *object);

#define GPK_FIRMWARE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_FIRMWARE, GpkFirmwarePrivate))
#define GPK_FIRMWARE_STATE_FILE		"/var/run/PackageKit/udev-firmware"
#define GPK_FIRMWARE_LOGIN_DELAY	20 /* seconds */

struct GpkFirmwarePrivate
{
	GpkNotify		*notify;
	gchar			**files;
};

G_DEFINE_TYPE (GpkFirmware, gpk_firmware, G_TYPE_OBJECT)

static gboolean
gpk_firmware_timeout_cb (gpointer data)
{
	const gchar *message;
	GpkFirmware *firmware = (GpkFirmware *) data;

	message = _("Additional firmware is required to make hardware in this computer function correctly.");
	gpk_notify_create (firmware->priv->notify, _("Additional firmware required"), message,
			   "help-browser", GPK_NOTIFY_URGENCY_LOW, GPK_NOTIFY_TIMEOUT_NEVER);
	gpk_notify_button (firmware->priv->notify, GPK_NOTIFY_BUTTON_INSTALL_FIRMWARE, NULL);
	gpk_notify_button (firmware->priv->notify, GPK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN, GPK_CONF_PROMPT_FIRMWARE);
	gpk_notify_show (firmware->priv->notify);

	return FALSE;
}

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
 * gpk_firmware_notify_button_cb:
 **/
static void
gpk_firmware_notify_button_cb (GpkNotify *notify, GpkNotifyButton button,
			       const gchar *data, GpkFirmware *firmware)
{
	pk_debug ("got: %i with data %s", button, data);
	/* find the localised text */
	if (button == GPK_NOTIFY_BUTTON_INSTALL_FIRMWARE) {
		gpk_firmware_install_file (firmware);
	}
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

	firmware->priv->notify = gpk_notify_new ();
	g_signal_connect (firmware->priv->notify, "notification-button",
			  G_CALLBACK (gpk_firmware_notify_button_cb), firmware);

	/* file exists? */
	ret = g_file_test (GPK_FIRMWARE_STATE_FILE, G_FILE_TEST_EXISTS);
	if (!ret) {
		pk_debug ("file '%s' not found", GPK_FIRMWARE_STATE_FILE);
		return;
	}
	/* can we get the contents */
	ret = g_file_get_contents (GPK_FIRMWARE_STATE_FILE, &files, NULL, &error);
	if (!ret) {
		pk_warning ("can't open file %s, %s", GPK_FIRMWARE_STATE_FILE, error->message);
		g_error_free (error);
		return;
	}

	/* split, as we can use multiple lines */
	firmware->priv->files = g_strsplit (files, "\n", 0);

	/* file already exists */
	ret = g_file_test (firmware->priv->files[0], G_FILE_TEST_EXISTS);
	if (ret) {
		pk_debug ("file '%s' already exists", firmware->priv->files[0]);
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
	g_object_unref (firmware->priv->notify);

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

