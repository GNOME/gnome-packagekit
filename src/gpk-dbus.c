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
#include <libgbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <pk-common.h>
#include <pk-package-id.h>
#include <pk-package-ids.h>
#include <pk-enum.h>
#include <pk-debug.h>
#include <pk-package-list.h>

#include "gpk-dbus.h"

static void     gpk_dbus_class_init	(GpkDbusClass *klass);
static void     gpk_dbus_init		(GpkDbus      *dbus);
static void     gpk_dbus_finalize		(GObject	   *object);

#define GPK_DBUS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_DBUS, GpkDbusPrivate))

struct GpkDbusPrivate
{
	guint			 signal_update_detail;
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
	if (!quark) {
		quark = g_quark_from_static_string ("gpk_dbus_error");
	}
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
gpk_dbus_install_local_file (GpkDbus *dbus, const gchar *full_path, DBusGMethodInvocation *context)
{
	GError *error;
	gchar *sender;

	g_return_if_fail (PK_IS_DBUS (dbus));

	pk_debug ("InstallFile method called: %s", full_path);

	if (FALSE) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this dbus - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	pk_warning ("sender=%s", sender);

	dbus_g_method_return (context);
}

/**
 * gpk_dbus_install_provide_file:
 **/
void
gpk_dbus_install_provide_file (GpkDbus *dbus, const gchar *full_path, DBusGMethodInvocation *context)
{
	dbus_g_method_return (context);
}

/**
 * gpk_dbus_install_package_name:
 **/
void
gpk_dbus_install_package_name (GpkDbus *dbus, const gchar *package_name, DBusGMethodInvocation *context)
{
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
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_dbus (LibSelfTest *test)
{
	GpkDbus *dbus = NULL;
	gboolean ret;
	const gchar *temp;
	GError *error = NULL;

	if (libst_start (test, "GpkDbus", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get GpkDbus object");
	dbus = gpk_dbus_new ();
	if (dbus != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	g_object_unref (dbus);

	libst_end (test);
}
#endif

