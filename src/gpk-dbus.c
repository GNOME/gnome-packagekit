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
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-dbus.h"
#include "gpk-x11.h"
#include "gpk-client.h"

static void     gpk_dbus_class_init	(GpkDbusClass	*klass);
static void     gpk_dbus_init		(GpkDbus	*dbus);
static void     gpk_dbus_finalize	(GObject	*object);

#define GPK_DBUS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_DBUS, GpkDbusPrivate))

struct GpkDbusPrivate
{
	GpkClient		*gclient;
	PkClient		*client;
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
 * gpk_dbus_set_parent_window:
 **/
void
gpk_dbus_set_parent_window (GpkDbus *dbus, guint32 xid, guint32 timestamp)
{
	GpkX11 *x11;

	/* set the parent window */
	gpk_client_set_parent_xid (dbus->priv->gclient, xid);

	/* try to get the user time of the window if not provided */
	if (timestamp == 0 && xid != 0) {
		x11 = gpk_x11_new ();
		gpk_x11_set_xid (x11, xid);
		timestamp = gpk_x11_get_user_time (x11);
		g_object_unref (x11);
	}

	/* set the last interaction */
	gpk_client_update_timestamp (dbus->priv->gclient, timestamp);
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
	gchar *exec;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallLocalFile method called: %s", full_path);

	/* check sender */
	sender = dbus_g_method_get_sender (context);

	/* just convert from char* to char** */
	full_paths = g_strsplit (full_path, "|", 1);
	gpk_dbus_set_parent_window (dbus, xid, timestamp);

	/* get the program name and set */
	exec = gpk_dbus_get_exec_for_sender (sender);
	gpk_client_set_parent_exec (dbus->priv->gclient, exec);
	g_free (sender);
	g_free (exec);

	/* do the action */
	gpk_client_set_interaction (dbus->priv->gclient, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
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
	gchar *exec;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallProvideFile method called: %s", full_path);

	/* set modality */
	gpk_dbus_set_parent_window (dbus, xid, timestamp);

	/* get the program name and set */
	sender = dbus_g_method_get_sender (context);
	exec = gpk_dbus_get_exec_for_sender (sender);
	gpk_client_set_parent_exec (dbus->priv->gclient, exec);
	g_free (sender);
	g_free (exec);

	/* do the action */
	gpk_client_set_interaction (dbus->priv->gclient, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
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
	gchar *exec;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallPackageName method called: %s", package_name);

	/* check sender */
	sender = dbus_g_method_get_sender (context);

	/* just convert from char* to char** */
	package_names = g_strsplit (package_name, "|", 1);
	gpk_dbus_set_parent_window (dbus, xid, timestamp);

	/* get the program name and set */
	exec = gpk_dbus_get_exec_for_sender (sender);
	gpk_client_set_parent_exec (dbus->priv->gclient, exec);
	g_free (sender);
	g_free (exec);

	/* do the action */
	gpk_client_set_interaction (dbus->priv->gclient, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
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

#if 0
/**
 * gpk_dbus_install_package_names:
 **/
void
gpk_dbus_install_package_names (GpkDbus *dbus, guint32 xid, guint32 timestamp, gchar **package_names, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	gchar *sender;
	gchar *exec;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallPackageNames method called: %s", package_names[0]);

	gpk_dbus_set_parent_window (dbus, xid, timestamp);

	/* get the program name and set */
	sender = dbus_g_method_get_sender (context);
	exec = gpk_dbus_get_exec_for_sender (sender);
	gpk_client_set_parent_exec (dbus->priv->gclient, exec);
	g_free (sender);
	g_free (exec);

	/* do the action */
	gpk_client_set_interaction (dbus->priv->gclient, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
	ret = gpk_client_install_package_names (dbus->priv->gclient, package_names, &error_local);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED,
				     "Method failed: %s", error_local->message);
		g_error_free (error_local);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}
#endif

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
	gchar *exec;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallMimeType method called: %s", mime_type);

	/* set modality */
	gpk_dbus_set_parent_window (dbus, xid, timestamp);

	/* get the program name and set */
	sender = dbus_g_method_get_sender (context);
	exec = gpk_dbus_get_exec_for_sender (sender);
	gpk_client_set_parent_exec (dbus->priv->gclient, exec);
	g_free (sender);
	g_free (exec);

	/* do the action */
	gpk_client_set_interaction (dbus->priv->gclient, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
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
	gchar *exec;

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

	/* set modality */
	gpk_dbus_set_parent_window (dbus, xid, timestamp);

	/* get the program name and set */
	sender = dbus_g_method_get_sender (context);
	exec = gpk_dbus_get_exec_for_sender (sender);
	gpk_client_set_parent_exec (dbus->priv->gclient, exec);
	g_free (sender);
	g_free (exec);

	/* unwrap and turn into a GPtrArray */
	array = g_ptr_array_new ();
	for (i=0; i<codecs->len; i++) {
		varray = (GValueArray *) g_ptr_array_index (codecs, i);
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
	gpk_client_set_interaction (dbus->priv->gclient, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
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
 * gpk_dbus_install_fonts:
 **/
void
gpk_dbus_install_fonts (GpkDbus *dbus, guint32 xid, guint32 timestamp, gchar **fonts, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	gchar *sender;
	gchar *exec;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallFonts method called: %s", fonts[0]);

	gpk_dbus_set_parent_window (dbus, xid, timestamp);

	/* get the program name and set */
	sender = dbus_g_method_get_sender (context);
	exec = gpk_dbus_get_exec_for_sender (sender);
	gpk_client_set_parent_exec (dbus->priv->gclient, exec);
	g_free (sender);
	g_free (exec);

	/* do the action */
	gpk_client_set_interaction (dbus->priv->gclient, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
	ret = gpk_client_install_fonts (dbus->priv->gclient, fonts, &error_local);
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
gpk_dbus_install_font (GpkDbus *dbus, guint32 xid, guint32 timestamp, const gchar *font, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	gchar *sender;
	gchar *exec;
	gchar **fonts;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallFont method called: %s", font);

	/* set modality */
	gpk_dbus_set_parent_window (dbus, xid, timestamp);

	/* get the program name and set */
	sender = dbus_g_method_get_sender (context);
	exec = gpk_dbus_get_exec_for_sender (sender);
	gpk_client_set_parent_exec (dbus->priv->gclient, exec);
	g_free (sender);
	g_free (exec);

	/* do the action */
	fonts = g_strsplit (font, "|", 1);
	gpk_client_set_interaction (dbus->priv->gclient, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
	ret = gpk_client_install_fonts (dbus->priv->gclient, fonts, &error_local);
	g_strfreev (fonts);
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
 * gpk_dbus_install_catalog:
 **/
void
gpk_dbus_install_catalog (GpkDbus *dbus, guint32 xid, guint32 timestamp, const gchar *catalog_file, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	gchar *sender;
	gchar **catalog_files;
	gchar *exec;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallCatalog method called: %s", catalog_file);

	/* check sender */
	sender = dbus_g_method_get_sender (context);

	/* just convert from char* to char** */
	catalog_files = g_strsplit (catalog_file, "|", 1);
	gpk_dbus_set_parent_window (dbus, xid, timestamp);

	/* get the program name and set */
	exec = gpk_dbus_get_exec_for_sender (sender);
	gpk_client_set_parent_exec (dbus->priv->gclient, exec);
	g_free (sender);
	g_free (exec);

	/* do the action */
	gpk_client_set_interaction (dbus->priv->gclient, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
	ret = gpk_client_install_catalogs (dbus->priv->gclient, catalog_files, &error_local);
	g_strfreev (catalog_files);

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
 * gpk_dbus_set_interaction:
 **/
static void
gpk_dbus_set_interaction (GpkDbus *dbus, const gchar *interaction)
{
	guint i;
	guint len;
	gchar **interactions;
	PkBitfield interact;

	/* set the default, for now use never */
	interact = GPK_CLIENT_INTERACT_NEVER;

	interactions = g_strsplit (interaction, ",", -1);
	len = g_strv_length (interactions);
	for (i=0; i<len; i++) {
		/* show */
		if (egg_strequal (interactions[i], "show-confirm-search"))
			pk_bitfield_add (interact, GPK_CLIENT_INTERACT_CONFIRM);	//TODO: need to split
		else if (egg_strequal (interactions[i], "show-confirm-deps"))
			pk_bitfield_add (interact, GPK_CLIENT_INTERACT_CONFIRM);	//TODO: need to split
		else if (egg_strequal (interactions[i], "show-confirm-install"))
			pk_bitfield_add (interact, GPK_CLIENT_INTERACT_CONFIRM);	//TODO: need to split
		else if (egg_strequal (interactions[i], "show-progress"))
			pk_bitfield_add (interact, GPK_CLIENT_INTERACT_PROGRESS);
		else if (egg_strequal (interactions[i], "show-finished"))
			pk_bitfield_add (interact, GPK_CLIENT_INTERACT_FINISHED);
		else if (egg_strequal (interactions[i], "show-warning"))
			pk_bitfield_add (interact, GPK_CLIENT_INTERACT_WARNING);
		/* hide */
		else if (egg_strequal (interactions[i], "hide-confirm-search"))
			pk_bitfield_remove (interact, GPK_CLIENT_INTERACT_CONFIRM);	//TODO: need to split
		else if (egg_strequal (interactions[i], "hide-confirm-deps"))
			pk_bitfield_remove (interact, GPK_CLIENT_INTERACT_CONFIRM);	//TODO: need to split
		else if (egg_strequal (interactions[i], "hide-confirm-install"))
			pk_bitfield_remove (interact, GPK_CLIENT_INTERACT_CONFIRM);	//TODO: need to split
		else if (egg_strequal (interactions[i], "hide-progress"))
			pk_bitfield_remove (interact, GPK_CLIENT_INTERACT_PROGRESS);
		else if (egg_strequal (interactions[i], "hide-finished"))
			pk_bitfield_remove (interact, GPK_CLIENT_INTERACT_FINISHED);
		else if (egg_strequal (interactions[i], "hide-warning"))
			pk_bitfield_remove (interact, GPK_CLIENT_INTERACT_WARNING);
		else
			egg_warning ("failed to get interaction '%s'", interactions[i]);
	}
	g_strfreev (interactions);

	/* set the interaction mode */
	gpk_client_set_interaction (dbus->priv->gclient, interact);
}

/**
 * gpk_dbus_set_context:
 **/
static void
gpk_dbus_set_context (GpkDbus *dbus, DBusGMethodInvocation *context)
{
	gchar *sender;
	gchar *exec;

	/* get the program name and set */
	sender = dbus_g_method_get_sender (context);
	exec = gpk_dbus_get_exec_for_sender (sender);
	gpk_client_set_parent_exec (dbus->priv->gclient, exec);

	g_free (sender);
	g_free (exec);
}

#if 0
/**
 * gpk_dbus_is_package_installed:
 **/
gboolean
gpk_dbus_is_package_installed (GpkDbus *dbus, const gchar *package_name, gboolean *installed, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	PkPackageList *list = NULL;
	gchar **package_names = NULL;

	g_return_val_if_fail (PK_IS_DBUS (dbus), FALSE);

	/* reset */
	ret = pk_client_reset (dbus->priv->client, &error_local);
	if (!ret) {
		*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED, "failed to get installed status: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the package list for the installed packages */
	package_names = g_strsplit (package_name, "|", 1);
	ret = pk_client_resolve (dbus->priv->client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_names, &error_local);
	if (!ret) {
		*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED, "failed to get installed status: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* more than one entry? */
	list = pk_client_get_package_list (dbus->priv->client);
	*installed = (PK_OBJ_LIST(list)->len > 0);
out:
	if (list != NULL)
		g_object_unref (list);
	g_strfreev (package_names);
	return ret;
}
#endif

/**
 * gpk_dbus_is_installed:
 **/
gboolean
gpk_dbus_is_installed (GpkDbus *dbus, const gchar *package_name, gboolean *installed, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	PkPackageList *list = NULL;
	gchar **package_names = NULL;

	g_return_val_if_fail (PK_IS_DBUS (dbus), FALSE);

	/* reset */
	ret = pk_client_reset (dbus->priv->client, &error_local);
	if (!ret) {
		*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED, "failed to get installed status: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the package list for the installed packages */
	package_names = g_strsplit (package_name, "|", 1);
	ret = pk_client_resolve (dbus->priv->client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_names, &error_local);
	if (!ret) {
		*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED, "failed to get installed status: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* more than one entry? */
	list = pk_client_get_package_list (dbus->priv->client);
	*installed = (PK_OBJ_LIST(list)->len > 0);
out:
	if (list != NULL)
		g_object_unref (list);
	g_strfreev (package_names);
	return ret;
}

/**
 * gpk_dbus_search_file:
 **/
gboolean
gpk_dbus_search_file (GpkDbus *dbus, const gchar *file_name, gboolean *installed, gchar **package_name, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	PkPackageList *list = NULL;
	const PkPackageObj *obj;

	g_return_val_if_fail (PK_IS_DBUS (dbus), FALSE);

	/* reset */
	ret = pk_client_reset (dbus->priv->client, &error_local);
	if (!ret) {
		*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED, "failed to get installed status: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the package list for the installed packages */
	ret = pk_client_search_file (dbus->priv->client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), file_name, &error_local);
	if (!ret) {
		*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED, "failed to search for file: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* more than one entry? */
	list = pk_client_get_package_list (dbus->priv->client);
	if (PK_OBJ_LIST(list)->len < 1) {
		*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_DENIED, "could not find package providing file");
		ret = FALSE;
		goto out;
	}

	/* get the package name too */
	*installed = TRUE;
	obj = pk_package_list_get_obj (list, 0);
	*package_name = g_strdup (obj->id->name);

out:
	if (list != NULL)
		g_object_unref (list);
	return ret;
}

/**
 * gpk_dbus_install_provide_files:
 **/
void
gpk_dbus_install_provide_files (GpkDbus *dbus, guint32 xid, gchar **files, const gchar *interaction, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallProvideFiles method called: %s", files[0]);

	/* set common parameters */
	gpk_dbus_set_parent_window (dbus, xid, 0);
	gpk_dbus_set_interaction (dbus, interaction);
	gpk_dbus_set_context (dbus, context);

	/* do the action */
	ret = gpk_client_install_provide_file (dbus->priv->gclient, files[0], &error_local);
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
 * gpk_dbus_install_package_files:
 **/
void
gpk_dbus_install_package_files (GpkDbus *dbus, guint32 xid, gchar **files, const gchar *interaction, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallPackageFiles method called: %s", files[0]);

	/* set common parameters */
	gpk_dbus_set_parent_window (dbus, xid, 0);
	gpk_dbus_set_interaction (dbus, interaction);
	gpk_dbus_set_context (dbus, context);

	/* do the action */
	ret = gpk_client_install_local_files (dbus->priv->gclient, files, &error_local);
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
 * gpk_dbus_install_package_names:
 **/
void
gpk_dbus_install_package_names (GpkDbus *dbus, guint32 xid, gchar **packages, const gchar *interaction, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallPackageNames method called: %s", packages[0]);

	/* set common parameters */
	gpk_dbus_set_parent_window (dbus, xid, 0);
	gpk_dbus_set_interaction (dbus, interaction);
	gpk_dbus_set_context (dbus, context);

	/* do the action */
	ret = gpk_client_install_package_names (dbus->priv->gclient, packages, &error_local);
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
 * gpk_dbus_install_mime_types:
 **/
void
gpk_dbus_install_mime_types (GpkDbus *dbus, guint32 xid, gchar **mime_types, const gchar *interaction, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallMimeTypes method called: %s", mime_types[0]);

	/* set common parameters */
	gpk_dbus_set_parent_window (dbus, xid, 0);
	gpk_dbus_set_interaction (dbus, interaction);
	gpk_dbus_set_context (dbus, context);

	/* do the action */
	ret = gpk_client_install_mime_type (dbus->priv->gclient, mime_types[0], &error_local);
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
 * gpk_dbus_install_fontconfig_resources:
 **/
void
gpk_dbus_install_fontconfig_resources (GpkDbus *dbus, guint32 xid, gchar **resources, const gchar *interaction, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallFontconfigResources method called: %s", resources[0]);

	/* set common parameters */
	gpk_dbus_set_parent_window (dbus, xid, 0);
	gpk_dbus_set_interaction (dbus, interaction);
	gpk_dbus_set_context (dbus, context);

	/* do the action */
	ret = gpk_client_install_fonts (dbus->priv->gclient, resources, &error_local);
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
 * gpk_dbus_install_gstreamer_resources:
 **/
void
gpk_dbus_install_gstreamer_resources (GpkDbus *dbus, guint32 xid, gchar **resources, const gchar *interaction, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;

	g_return_if_fail (PK_IS_DBUS (dbus));

	egg_debug ("InstallGStreamerResources method called: %s", resources[0]);

	/* set common parameters */
	gpk_dbus_set_parent_window (dbus, xid, 0);
	gpk_dbus_set_interaction (dbus, interaction);
	gpk_dbus_set_context (dbus, context);

	/* do the action */
	ret = gpk_client_install_gstreamer_codecs (dbus->priv->gclient, resources, &error_local);
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
	dbus->priv->client = pk_client_new ();
	pk_client_set_use_buffer (dbus->priv->client, TRUE, NULL);
	pk_client_set_synchronous (dbus->priv->client, TRUE, NULL);
	dbus->priv->gclient = gpk_client_new ();
	gpk_client_set_interaction (dbus->priv->gclient, GPK_CLIENT_INTERACT_WARNING_CONFIRM_PROGRESS);
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

