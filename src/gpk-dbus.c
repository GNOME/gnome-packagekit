/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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
#include <gconf/gconf-client.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-dbus.h"
#include "gpk-dbus-task.h"
#include "gpk-x11.h"
#include "gpk-common.h"

static void     gpk_dbus_finalize	(GObject	*object);

#define GPK_DBUS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_DBUS, GpkDbusPrivate))

struct GpkDbusPrivate
{
	GConfClient		*gconf_client;
	gint			 timeout_tmp;
	GpkX11			*x11;
	GPtrArray		*array;
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
			ENUM_ENTRY (GPK_DBUS_ERROR_FAILED, "Failed"),
			ENUM_ENTRY (GPK_DBUS_ERROR_INTERNAL_ERROR, "InternalError"),
			ENUM_ENTRY (GPK_DBUS_ERROR_NO_PACKAGES_FOUND, "NoPackagesFound"),
			ENUM_ENTRY (GPK_DBUS_ERROR_FORBIDDEN, "Forbidden"),
			ENUM_ENTRY (GPK_DBUS_ERROR_CANCELLED, "Cancelled"),
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
 * gpk_dbus_set_interaction_from_text:
 **/
static void
gpk_dbus_set_interaction_from_text (PkBitfield *interact, gint *timeout, const gchar *interaction)
{
	guint i;
	guint len;
	gchar **interactions;
	interactions = g_strsplit (interaction, ",", -1);
	len = g_strv_length (interactions);

	/* do special keys first */
	for (i=0; i<len; i++) {
		if (egg_strequal (interactions[i], "always"))
			*interact = GPK_CLIENT_INTERACT_ALWAYS;
		else if (egg_strequal (interactions[i], "never"))
			*interact = GPK_CLIENT_INTERACT_NEVER;
	}

	/* add or remove from defaults */
	for (i=0; i<len; i++) {
		/* show */
		if (egg_strequal (interactions[i], "show-confirm-search"))
			pk_bitfield_add (*interact, GPK_CLIENT_INTERACT_CONFIRM_SEARCH);
		else if (egg_strequal (interactions[i], "show-confirm-deps"))
			pk_bitfield_add (*interact, GPK_CLIENT_INTERACT_CONFIRM_DEPS);
		else if (egg_strequal (interactions[i], "show-confirm-install"))
			pk_bitfield_add (*interact, GPK_CLIENT_INTERACT_CONFIRM_INSTALL);
		else if (egg_strequal (interactions[i], "show-progress"))
			pk_bitfield_add (*interact, GPK_CLIENT_INTERACT_PROGRESS);
		else if (egg_strequal (interactions[i], "show-finished"))
			pk_bitfield_add (*interact, GPK_CLIENT_INTERACT_FINISHED);
		else if (egg_strequal (interactions[i], "show-warning"))
			pk_bitfield_add (*interact, GPK_CLIENT_INTERACT_WARNING);
		/* hide */
		else if (egg_strequal (interactions[i], "hide-confirm-search"))
			pk_bitfield_remove (*interact, GPK_CLIENT_INTERACT_CONFIRM_SEARCH);
		else if (egg_strequal (interactions[i], "hide-confirm-deps"))
			pk_bitfield_remove (*interact, GPK_CLIENT_INTERACT_CONFIRM_DEPS);
		else if (egg_strequal (interactions[i], "hide-confirm-install"))
			pk_bitfield_remove (*interact, GPK_CLIENT_INTERACT_CONFIRM_INSTALL);
		else if (egg_strequal (interactions[i], "hide-progress"))
			pk_bitfield_remove (*interact, GPK_CLIENT_INTERACT_PROGRESS);
		else if (egg_strequal (interactions[i], "hide-finished"))
			pk_bitfield_remove (*interact, GPK_CLIENT_INTERACT_FINISHED);
		else if (egg_strequal (interactions[i], "hide-warning"))
			pk_bitfield_remove (*interact, GPK_CLIENT_INTERACT_WARNING);
		/* wait */
		else if (g_str_has_prefix (interactions[i], "timeout="))
			*timeout = atoi (&interactions[i][8]);
	}
	g_strfreev (interactions);
}

/**
 * gpk_dbus_parse_interaction:
 **/
static void
gpk_dbus_parse_interaction (GpkDbus *dbus, const gchar *interaction, PkBitfield *interact, gint *timeout)
{
	gchar *policy;

	/* set temp default */
	*interact = 0;
	dbus->priv->timeout_tmp = -1;

	/* get default policy from gconf */
	policy = gconf_client_get_string (dbus->priv->gconf_client, GPK_CONF_DBUS_DEFAULT_INTERACTION, NULL);
	if (policy != NULL) {
		egg_debug ("default is %s", policy);
		gpk_dbus_set_interaction_from_text (interact, &dbus->priv->timeout_tmp, policy);
	}
	g_free (policy);

	/* now override with policy from client */
	gpk_dbus_set_interaction_from_text (interact, &dbus->priv->timeout_tmp, interaction);
	egg_debug ("client is %s", interaction);

	/* now override with enforced policy from gconf */
	policy = gconf_client_get_string (dbus->priv->gconf_client, GPK_CONF_DBUS_ENFORCED_INTERACTION, NULL);
	if (policy != NULL) {
		egg_debug ("enforced is %s", policy);
		gpk_dbus_set_interaction_from_text (interact, &dbus->priv->timeout_tmp, policy);
	}
	g_free (policy);

	/* copy from temp */
	*timeout = dbus->priv->timeout_tmp;
}

/**
 * gpk_dbus_create_task:
 **/
static GpkDbusTask *
gpk_dbus_create_task (GpkDbus *dbus, guint32 xid, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	PkBitfield interact = 0;
	gint timeout = 0;
	gchar *sender;
	gchar *exec;
	guint timestamp = 0;

	task = gpk_dbus_task_new ();

	/* work out what interaction the task should use */
	gpk_dbus_parse_interaction (dbus, interaction, &interact, &timeout);

	/* set interaction mode */
	egg_debug ("interact=%i", (gint) interact);
	gpk_dbus_task_set_interaction (task, interact);

	/* set timeout */
	egg_debug ("timeout=%i", timeout);
	gpk_dbus_task_set_timeout (task, timeout);

	/* set the parent window */
	gpk_dbus_task_set_xid (task, xid);

	/* try to get the user time of the window */
	if (xid != 0) {
		gpk_x11_set_xid (dbus->priv->x11, xid);
		timestamp = gpk_x11_get_user_time (dbus->priv->x11);
	}

	/* set the context for the return values */
	gpk_dbus_task_set_context (task, context);

	/* set the last interaction */
	gpk_dbus_task_set_timestamp (task, timestamp);

	/* set the window for the modal and timestamp */
	gpk_dbus_task_set_xid (task, xid);

	/* get the program name and set */
	sender = dbus_g_method_get_sender (context);
	exec = gpk_dbus_get_exec_for_sender (sender);
	gpk_dbus_task_set_exec (task, exec);

	/* unref on delete */
	//g_signal_connect...

	/* add to array */
	g_ptr_array_add (dbus->priv->array, task);

	g_free (sender);
	g_free (exec);
	return task;
}


/**
 * gpk_dbus_is_installed:
 **/
void
gpk_dbus_is_installed (GpkDbus *dbus, const gchar *package_name, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, 0, interaction, context);
	gpk_dbus_task_is_installed (task, package_name);
}

/**
 * gpk_dbus_search_file:
 **/
void
gpk_dbus_search_file (GpkDbus *dbus, const gchar *file_name, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, 0, interaction, context);
	gpk_dbus_task_search_file (task, file_name);
}

/**
 * gpk_dbus_install_package_files:
 **/
void
gpk_dbus_install_package_files (GpkDbus *dbus, guint32 xid, gchar **files, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_package_files (task, files);
}

/**
 * gpk_dbus_install_provide_files:
 **/
void
gpk_dbus_install_provide_files (GpkDbus *dbus, guint32 xid, gchar **files, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_provide_files (task, files);
}

/**
 * gpk_dbus_install_package_names:
 **/
void
gpk_dbus_install_package_names (GpkDbus *dbus, guint32 xid, gchar **packages, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_package_names (task, packages);
}

/**
 * gpk_dbus_install_mime_types:
 **/
void
gpk_dbus_install_mime_types (GpkDbus *dbus, guint32 xid, gchar **mime_types, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_mime_types (task, mime_types);
}

/**
 * gpk_dbus_install_fontconfig_resources:
 **/
void
gpk_dbus_install_fontconfig_resources (GpkDbus *dbus, guint32 xid, gchar **resources, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_fontconfig_resources (task, resources);
}

/**
 * gpk_dbus_install_gstreamer_resources:
 **/
void
gpk_dbus_install_gstreamer_resources (GpkDbus *dbus, guint32 xid, gchar **resources, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_gstreamer_resources (task, resources);
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
	dbus->priv->timeout_tmp = -1;
	dbus->priv->gconf_client = gconf_client_get_default ();
	dbus->priv->array = g_ptr_array_new ();
	dbus->priv->x11 = gpk_x11_new ();
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
	g_ptr_array_foreach (dbus->priv->array, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (dbus->priv->array, TRUE);
	g_object_unref (dbus->priv->gconf_client);
	g_object_unref (dbus->priv->x11);

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

