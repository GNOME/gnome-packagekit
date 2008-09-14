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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <pk-common.h>
#include <pk-package-id.h>
#include <pk-package-ids.h>
#include <pk-enum.h>
#include "egg-debug.h"
#include <pk-package-list.h>

#include "gpk-dbus.h"
#include "gpk-client.h"

static void     gpk_dbus_class_init	(GpkDbusClass	*klass);
static void     gpk_dbus_init		(GpkDbus	*dbus);
static void     gpk_dbus_finalize	(GObject	*object);

#define GPK_DBUS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_DBUS, GpkDbusPrivate))

struct GpkDbusPrivate
{
	GpkClient		*gclient;
};

G_DEFINE_TYPE (GpkDbus, gpk_dbus, G_TYPE_OBJECT)

/**
 * gpk_dbus_error_quark:
 * Return value: Our personal error quark.
 **/
GQuark
gpk_dbus_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("gpk_dbus_error");
	return quark;
}

/**
 * gpk_dbus_error_get_type:
 **/
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
gpk_dbus_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (GPK_DBUS_ERROR_DENIED, "PermissionDenied"),
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GpkDbusError", values);
	}
	return etype;
}

/**
 * gpk_dbus_install_local_file:
 **/
void
gpk_dbus_install_local_file (GpkDbus *dbus, guint32 xid, guint32 timestamp, const gchar *full_path, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	gchar *sender;
	gchar **full_paths;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallLocalFile method called: %s", full_path);

	/* check sender */
	sender = dbus_g_method_get_sender (context);
	egg_debug ("sender=%s", sender);

	/* just convert from char* to char** */
	full_paths = g_strsplit (full_path, "|", 1);
	gpk_client_set_parent_xid (dbus->priv->gclient, xid);
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);
	ret = gpk_client_install_local_files (dbus->priv->gclient, full_paths, &error_local);
	g_strfreev (full_paths);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED,
				     "Method failed: %s", error_local->message);
		g_error_free (error_local);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * gpk_dbus_install_provide_file:
 **/
void
gpk_dbus_install_provide_file (GpkDbus *dbus, guint32 xid, guint32 timestamp, const gchar *full_path, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	gchar *sender;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallProvideFile method called: %s", full_path);

	/* check sender */
	sender = dbus_g_method_get_sender (context);
	egg_debug ("sender=%s", sender);

	gpk_client_set_parent_xid (dbus->priv->gclient, xid);
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);
	ret = gpk_client_install_provide_file (dbus->priv->gclient, full_path, &error_local);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED,
				     "Method failed: %s", error_local->message);
		g_error_free (error_local);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * gpk_dbus_install_package_name:
 **/
void
gpk_dbus_install_package_name (GpkDbus *dbus, guint32 xid, guint32 timestamp, const gchar *package_name, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	gchar *sender;
	gchar **package_names;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallPackageName method called: %s", package_name);

	/* check sender */
	sender = dbus_g_method_get_sender (context);
	egg_debug ("sender=%s", sender);

	/* just convert from char* to char** */
	package_names = g_strsplit (package_name, "|", 1);
	gpk_client_set_parent_xid (dbus->priv->gclient, xid);
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);
	ret = gpk_client_install_package_names (dbus->priv->gclient, package_names, &error_local);
	g_strfreev (package_names);

	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED,
				     "Method failed: %s", error_local->message);
		g_error_free (error_local);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * gpk_dbus_install_mime_type:
 **/
void
gpk_dbus_install_mime_type (GpkDbus *dbus, guint32 xid, guint32 timestamp, const gchar *mime_type, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	gchar *sender;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallMimeType method called: %s", mime_type);

	/* check sender */
	sender = dbus_g_method_get_sender (context);
	egg_debug ("sender=%s", sender);

	gpk_client_set_parent_xid (dbus->priv->gclient, xid);
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);
	ret = gpk_client_install_mime_type (dbus->priv->gclient, mime_type, &error_local);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED,
				     "Method failed: %s", error_local->message);
		g_error_free (error_local);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * gpk_dbus_install_gstreamer_codecs:
 **/
void
gpk_dbus_install_gstreamer_codecs (GpkDbus *dbus, guint32 xid, guint32 timestamp, gchar **codec_name_strings, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	gchar *sender;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallGStreamerCodecs method called: %s", codec_name_strings[0]);

	/* check sender */
	sender = dbus_g_method_get_sender (context);
	egg_debug ("sender=%s", sender);

	gpk_client_set_parent_xid (dbus->priv->gclient, xid);
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);
	ret = gpk_client_install_gstreamer_codecs (dbus->priv->gclient, codec_name_strings, &error_local);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED,
				     "Method failed: %s", error_local->message);
		g_error_free (error_local);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * gpk_dbus_install_font:
 **/
void
gpk_dbus_install_font (GpkDbus *dbus, guint32 xid, guint32 timestamp, const gchar *font_desc, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	gchar *sender;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallFont method called: %s", font_desc);

	/* check sender */
	sender = dbus_g_method_get_sender (context);
	egg_debug ("sender=%s", sender);

	gpk_client_set_parent_xid (dbus->priv->gclient, xid);
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);
	ret = gpk_client_install_font (dbus->priv->gclient, font_desc, &error_local);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED,
				     "Method failed: %s", error_local->message);
		g_error_free (error_local);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * gpk_dbus_class_init:
 * @klass: The GpkDbusClass
 **/
static void
gpk_dbus_class_init (GpkDbusClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_dbus_finalize;
	g_type_class_add_private (klass, sizeof (GpkDbusPrivate));
}

/**
 * gpk_dbus_init:
 * @dbus: This class instance
 **/
static void
gpk_dbus_init (GpkDbus *dbus)
{
	dbus->priv = GPK_DBUS_GET_PRIVATE (dbus);
	dbus->priv->gclient = gpk_client_new ();
}

/**
 * gpk_dbus_finalize:
 * @object: The object to finalize
 **/
static void
gpk_dbus_finalize (GObject *object)
{
	GpkDbus *dbus;
	g_return_if_fail (PK_IS_DBUS (object));

	dbus = GPK_DBUS (object);
	g_return_if_fail (dbus->priv != NULL);
	g_object_unref (dbus->priv->gclient);

	G_OBJECT_CLASS (gpk_dbus_parent_class)->finalize (object);
}

/**
 * gpk_dbus_new:
 *
 * Return value: a new GpkDbus object.
 **/
GpkDbus *
gpk_dbus_new (void)
{
	GpkDbus *dbus;
	dbus = g_object_new (GPK_TYPE_DBUS, NULL);
	return GPK_DBUS (dbus);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_dbus (EggTest *test)
{
	GpkDbus *dbus = NULL;
	gboolean ret;
	const gchar *temp;
	GError *error = NULL;

	if (egg_test_start (test, "GpkDbus") == FALSE) {
		return;
	}

	/************************************************************/
	egg_test_title (test, "get GpkDbus object");
	dbus = gpk_dbus_new ();
	if (dbus != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	g_object_unref (dbus);

	egg_test_end (test);
}
#endif

