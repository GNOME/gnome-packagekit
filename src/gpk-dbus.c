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
#include <polkit/polkit.h>
#include <polkit-dbus/polkit-dbus.h>

#include <pk-common.h>
#include <pk-package-id.h>
#include <pk-package-ids.h>
#include <pk-client.h>
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
	PkClient		*client;
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
 * gpk_dbus_get_exec_for_sender:
 **/
static gchar *
gpk_dbus_get_exec_for_sender (const gchar *sender)
{
	pid_t pid;
	gchar exec[128];
	PolKitCaller *caller = NULL;
	DBusError dbus_error;
	gboolean ret = FALSE;
	gint retval;
	DBusConnection *connection;
	gchar *sender_exe = NULL;

	/* get a connection */
	connection = dbus_bus_get (DBUS_BUS_SESSION, NULL);
	if (connection == NULL)
		egg_error ("fatal, no system dbus");

	dbus_error_init (&dbus_error);
	caller = polkit_caller_new_from_dbus_name (connection, sender, &dbus_error);
	if (caller == NULL) {
		egg_warning ("cannot get caller from sender %s: %s", sender, dbus_error.message);
		dbus_error_free (&dbus_error);
		goto out;
	}

	ret = polkit_caller_get_pid (caller, &pid);
	if (!ret) {
		egg_warning ("cannot get pid from sender %p", sender);
		goto out;
	}

	retval = polkit_sysdeps_get_exe_for_pid (pid, exec, 128);
	if (retval == -1) {
		egg_warning ("cannot get exec for pid %i", pid);
		goto out;
	}

	/* make a copy */
	sender_exe = g_strdup (exec);

out:
	if (caller != NULL)
		polkit_caller_unref (caller);
	return sender_exe;
}

/**
 * gpk_dbus_get_application_for_sender:
 **/
static gchar *
gpk_dbus_get_application_for_sender (GpkDbus *dbus, const gchar *sender)
{
	gchar *exec;
	gchar *application = NULL;
	gboolean ret;
	GError *error = NULL;
	guint length;
	PkPackageList *list = NULL;
	const PkPackageObj *obj;

	exec = gpk_dbus_get_exec_for_sender (sender);
	if (exec == NULL) {
		egg_warning ("could not get exec name for %s", sender);
		goto out;
	}
	egg_debug ("got application path %s", exec);

	/* reset client */
	ret = pk_client_reset (dbus->priv->client, &error);
	if (!ret) {
		egg_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find the package name */
	ret = pk_client_search_file (dbus->priv->client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), exec, &error);
	if (!ret) {
		egg_warning ("failed to search file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get the list of packages */
	list = pk_client_get_package_list (dbus->priv->client);
	length = pk_package_list_get_size (list);

	/* nothing found */
	if (length == 0) {
		egg_debug ("cannot find installed package that provides : %s", exec);
		goto out;
	}

	/* check we have one */
	if (length != 1)
		egg_warning ("not one return, using first");

	/* copy name */
	obj = pk_package_list_get_obj (list, 0);
	application = g_strdup (obj->id->name);
	egg_debug ("got application package %s", application);

out:
	/* use the exec name if we can't find an installed package */
	if (application == NULL && exec != NULL)
		application = g_strdup (exec);
	if (list != NULL)
		g_object_unref (list);
	g_free (exec);
	return application;
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
	gchar *application;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallLocalFile method called: %s", full_path);

	/* check sender */
	sender = dbus_g_method_get_sender (context);
	egg_debug ("sender=%s", sender);

	/* just convert from char* to char** */
	full_paths = g_strsplit (full_path, "|", 1);
	gpk_client_set_parent_xid (dbus->priv->gclient, xid);
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);

	/* get the program name and set */
	application = gpk_dbus_get_application_for_sender (dbus, sender);
	gpk_client_set_application (dbus->priv->gclient, application);
	g_free (sender);
	g_free (application);

	/* do the action */
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
	gchar *application;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallProvideFile method called: %s", full_path);

	/* check sender */
	sender = dbus_g_method_get_sender (context);
	egg_debug ("sender=%s", sender);

	gpk_client_set_parent_xid (dbus->priv->gclient, xid);
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);

	/* get the program name and set */
	application = gpk_dbus_get_application_for_sender (dbus, sender);
	gpk_client_set_application (dbus->priv->gclient, application);
	g_free (sender);
	g_free (application);

	/* do the action */
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
	gchar *application;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallPackageName method called: %s", package_name);

	/* check sender */
	sender = dbus_g_method_get_sender (context);
	egg_debug ("sender=%s", sender);

	/* just convert from char* to char** */
	package_names = g_strsplit (package_name, "|", 1);
	gpk_client_set_parent_xid (dbus->priv->gclient, xid);
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);

	/* get the program name and set */
	application = gpk_dbus_get_application_for_sender (dbus, sender);
	gpk_client_set_application (dbus->priv->gclient, application);
	g_free (sender);
	g_free (application);

	/* do the action */
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
	gchar *application;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallMimeType method called: %s", mime_type);

	/* check sender */
	sender = dbus_g_method_get_sender (context);
	egg_debug ("sender=%s", sender);

	gpk_client_set_parent_xid (dbus->priv->gclient, xid);
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);

	/* get the program name and set */
	application = gpk_dbus_get_application_for_sender (dbus, sender);
	gpk_client_set_application (dbus->priv->gclient, application);
	g_free (sender);
	g_free (application);

	/* do the action */
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
gpk_dbus_install_gstreamer_codecs (GpkDbus *dbus, guint32 xid, guint32 timestamp, GPtrArray *codecs, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	gchar *sender;
	gchar *application;

	g_return_if_fail (PK_IS_DBUS (dbus));

	guint i;
	GValue *value;
	gchar *description;
	gchar *detail;
	gchar *encoded;
	GPtrArray *array;
	GValueArray *varray;
	gchar **codec_strings;

	egg_debug ("InstallGStreamerCodecs method called");

	/* check sender */
	sender = dbus_g_method_get_sender (context);
	egg_debug ("sender=%s", sender);

	gpk_client_set_parent_xid (dbus->priv->gclient, xid);
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);

	/* get the program name and set */
	application = gpk_dbus_get_application_for_sender (dbus, sender);
	gpk_client_set_application (dbus->priv->gclient, application);
	g_free (sender);
	g_free (application);

	/* unwrap and turn into a GPtrArray */
	array = g_ptr_array_new ();
	for (i=0; i<codecs->len; i++) {
		varray = (GValueArray *) g_ptr_array_index (codecs, 0);
		value = g_value_array_get_nth (varray, 0);
		description = g_value_dup_string (value);
		value = g_value_array_get_nth (varray, 1);
		detail = g_value_dup_string (value);
		encoded = g_strdup_printf ("%s|%s", description, detail);
		g_ptr_array_add (array, encoded);
		g_free (description);
		g_free (detail);
	}

	/* convert to an strv */
	codec_strings = pk_ptr_array_to_strv (array);
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);

	/* do the action */
	ret = gpk_client_install_gstreamer_codecs (dbus->priv->gclient, codec_strings, &error_local);
	g_strfreev (codec_strings);

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
	gchar *application;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallFont method called: %s", font_desc);

	/* check sender */
	sender = dbus_g_method_get_sender (context);
	egg_debug ("sender=%s", sender);

	gpk_client_set_parent_xid (dbus->priv->gclient, xid);
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);

	/* get the program name and set */
	application = gpk_dbus_get_application_for_sender (dbus, sender);
	gpk_client_set_application (dbus->priv->gclient, application);
	g_free (sender);
	g_free (application);

	/* do the action */
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
	dbus->priv->client = pk_client_new ();
	pk_client_set_synchronous (dbus->priv->client, TRUE, NULL);
	pk_client_set_use_buffer (dbus->priv->client, TRUE, NULL);

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
	g_object_unref (dbus->priv->client);
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
gpk_dbus_test (EggTest *test)
{
	GpkDbus *dbus = NULL;

	if (!egg_test_start (test, "GpkDbus"))
		return;

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

