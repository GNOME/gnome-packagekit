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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <packagekit-glib2/packagekit.h>
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
	GTimer			*timer;
	guint			 refcount;
	GpkX11			*x11;
	DBusGProxy		*proxy_session_pid;
	DBusGProxy		*proxy_system_pid;
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
 * gpk_dbus_get_idle_time:
 **/
guint
gpk_dbus_get_idle_time (GpkDbus	*dbus)
{
	guint idle = 0;

	/* we need to return 0 if there is a task in progress */
	if (dbus->priv->refcount > 0)
		goto out;

	idle = (guint) g_timer_elapsed (dbus->priv->timer, NULL);
	egg_debug ("we've been idle for %is", idle);
out:
	return idle;
}

/**
 * gpk_dbus_get_pid_session:
 **/
static guint
gpk_dbus_get_pid_session (GpkDbus *dbus, const gchar *sender)
{
	guint pid = G_MAXUINT;
	gboolean ret;
	GError *error = NULL;

	/* get pid from DBus (quite slow) */
	ret = dbus_g_proxy_call (dbus->priv->proxy_session_pid, "GetConnectionUnixProcessID", &error,
				 G_TYPE_STRING, sender,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &pid,
				 G_TYPE_INVALID);
	if (!ret) {
		egg_debug ("failed to get pid from session: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return pid;
}

/**
 * gpk_dbus_get_pid_system:
 **/
static guint
gpk_dbus_get_pid_system (GpkDbus *dbus, const gchar *sender)
{
	guint pid = G_MAXUINT;
	gboolean ret;
	GError *error = NULL;

	/* get pid from DBus (quite slow) */
	ret = dbus_g_proxy_call (dbus->priv->proxy_system_pid, "GetConnectionUnixProcessID", &error,
				 G_TYPE_STRING, sender,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &pid,
				 G_TYPE_INVALID);
	if (!ret) {
		egg_debug ("failed to get pid from system: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return pid;
}

/**
 * gpk_dbus_get_pid:
 **/
static guint
gpk_dbus_get_pid (GpkDbus *dbus, const gchar *sender)
{
	guint pid;

	g_return_val_if_fail (PK_IS_DBUS (dbus), G_MAXUINT);
	g_return_val_if_fail (dbus->priv->proxy_session_pid != NULL, G_MAXUINT);
	g_return_val_if_fail (dbus->priv->proxy_system_pid != NULL, G_MAXUINT);
	g_return_val_if_fail (sender != NULL, G_MAXUINT);

	/* check system bus first */
	pid = gpk_dbus_get_pid_system (dbus, sender);
	if (pid != G_MAXUINT)
		goto out;

	/* and then session bus */
	pid = gpk_dbus_get_pid_session (dbus, sender);
	if (pid != G_MAXUINT)
		goto out;

	/* should be impossible */
	egg_warning ("could not find pid!");
out:
	return pid;
}


/**
 * gpk_dbus_get_exec_for_sender:
 **/
static gchar *
gpk_dbus_get_exec_for_sender (GpkDbus *dbus, const gchar *sender)
{
	gchar *filename = NULL;
	gchar *cmdline = NULL;
	GError *error = NULL;
	guint pid;

	g_return_val_if_fail (PK_IS_DBUS (dbus), NULL);
	g_return_val_if_fail (sender != NULL, NULL);

	/* get pid */
	pid = gpk_dbus_get_pid (dbus, sender);
	if (pid == G_MAXUINT) {
		egg_warning ("failed to get PID");
		goto out;
	}

	/* get command line from proc */
	filename = g_strdup_printf ("/proc/%i/exe", pid);
	cmdline = g_file_read_link (filename, &error);
	if (cmdline == NULL) {
		egg_warning ("failed to find exec: %s", error->message);
		g_error_free (error);
	}
out:
	g_free (filename);
	return cmdline;
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
		if (g_strcmp0 (interactions[i], "always") == 0)
			*interact = GPK_CLIENT_INTERACT_ALWAYS;
		else if (g_strcmp0 (interactions[i], "never") == 0)
			*interact = GPK_CLIENT_INTERACT_NEVER;
	}

	/* add or remove from defaults */
	for (i=0; i<len; i++) {
		/* show */
		if (g_strcmp0 (interactions[i], "show-confirm-search") == 0)
			pk_bitfield_add (*interact, GPK_CLIENT_INTERACT_CONFIRM_SEARCH);
		else if (g_strcmp0 (interactions[i], "show-confirm-deps") == 0)
			pk_bitfield_add (*interact, GPK_CLIENT_INTERACT_CONFIRM_DEPS);
		else if (g_strcmp0 (interactions[i], "show-confirm-install") == 0)
			pk_bitfield_add (*interact, GPK_CLIENT_INTERACT_CONFIRM_INSTALL);
		else if (g_strcmp0 (interactions[i], "show-progress") == 0)
			pk_bitfield_add (*interact, GPK_CLIENT_INTERACT_PROGRESS);
		else if (g_strcmp0 (interactions[i], "show-finished") == 0)
			pk_bitfield_add (*interact, GPK_CLIENT_INTERACT_FINISHED);
		else if (g_strcmp0 (interactions[i], "show-warning") == 0)
			pk_bitfield_add (*interact, GPK_CLIENT_INTERACT_WARNING);
		/* hide */
		else if (g_strcmp0 (interactions[i], "hide-confirm-search") == 0)
			pk_bitfield_remove (*interact, GPK_CLIENT_INTERACT_CONFIRM_SEARCH);
		else if (g_strcmp0 (interactions[i], "hide-confirm-deps") == 0)
			pk_bitfield_remove (*interact, GPK_CLIENT_INTERACT_CONFIRM_DEPS);
		else if (g_strcmp0 (interactions[i], "hide-confirm-install") == 0)
			pk_bitfield_remove (*interact, GPK_CLIENT_INTERACT_CONFIRM_INSTALL);
		else if (g_strcmp0 (interactions[i], "hide-progress") == 0)
			pk_bitfield_remove (*interact, GPK_CLIENT_INTERACT_PROGRESS);
		else if (g_strcmp0 (interactions[i], "hide-finished") == 0)
			pk_bitfield_remove (*interact, GPK_CLIENT_INTERACT_FINISHED);
		else if (g_strcmp0 (interactions[i], "hide-warning") == 0)
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
	exec = gpk_dbus_get_exec_for_sender (dbus, sender);
	if (exec != NULL)
		gpk_dbus_task_set_exec (task, exec);

	/* unref on delete */
	//g_signal_connect...

	/* reset time */
	g_timer_reset (dbus->priv->timer);
	dbus->priv->refcount++;

	g_free (sender);
	g_free (exec);
	return task;
}

/**
 * gpk_dbus_task_finished_cb:
 **/
static void
gpk_dbus_task_finished_cb (GpkDbusTask *task, GpkDbus *dbus)
{
	/* one context has returned */
	if (dbus->priv->refcount > 0)
		dbus->priv->refcount--;

	/* reset time */
	g_timer_reset (dbus->priv->timer);

	g_object_unref (task);
}

/**
 * gpk_dbus_is_installed:
 **/
void
gpk_dbus_is_installed (GpkDbus *dbus, const gchar *package_name, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, 0, interaction, context);
	gpk_dbus_task_is_installed (task, package_name, (GpkDbusTaskFinishedCb) gpk_dbus_task_finished_cb, dbus);
}

/**
 * gpk_dbus_search_file:
 **/
void
gpk_dbus_search_file (GpkDbus *dbus, const gchar *file_name, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, 0, interaction, context);
	gpk_dbus_task_search_file (task, file_name, (GpkDbusTaskFinishedCb) gpk_dbus_task_finished_cb, dbus);
}

/**
 * gpk_dbus_install_package_files:
 **/
void
gpk_dbus_install_package_files (GpkDbus *dbus, guint32 xid, gchar **files, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_package_files (task, files, (GpkDbusTaskFinishedCb) gpk_dbus_task_finished_cb, dbus);
}

/**
 * gpk_dbus_install_provide_files:
 **/
void
gpk_dbus_install_provide_files (GpkDbus *dbus, guint32 xid, gchar **files, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_provide_files (task, files, (GpkDbusTaskFinishedCb) gpk_dbus_task_finished_cb, dbus);
}

/**
 * gpk_dbus_remove_package_by_file:
 **/
void
gpk_dbus_remove_package_by_file (GpkDbus *dbus, guint32 xid, gchar **files, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_remove_package_by_file (task, files, (GpkDbusTaskFinishedCb) gpk_dbus_task_finished_cb, dbus);
}

/**
 * gpk_dbus_install_catalogs:
 **/
void
gpk_dbus_install_catalogs (GpkDbus *dbus, guint32 xid, gchar **files, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_catalogs (task, files, (GpkDbusTaskFinishedCb) gpk_dbus_task_finished_cb, dbus);
}

/**
 * gpk_dbus_install_package_names:
 **/
void
gpk_dbus_install_package_names (GpkDbus *dbus, guint32 xid, gchar **packages, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_package_names (task, packages, (GpkDbusTaskFinishedCb) gpk_dbus_task_finished_cb, dbus);
}

/**
 * gpk_dbus_install_mime_types:
 **/
void
gpk_dbus_install_mime_types (GpkDbus *dbus, guint32 xid, gchar **mime_types, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_mime_types (task, mime_types, (GpkDbusTaskFinishedCb) gpk_dbus_task_finished_cb, dbus);
}

/**
 * gpk_dbus_install_fontconfig_resources:
 **/
void
gpk_dbus_install_fontconfig_resources (GpkDbus *dbus, guint32 xid, gchar **resources, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_fontconfig_resources (task, resources, (GpkDbusTaskFinishedCb) gpk_dbus_task_finished_cb, dbus);
}

/**
 * gpk_dbus_install_gstreamer_resources:
 **/
void
gpk_dbus_install_gstreamer_resources (GpkDbus *dbus, guint32 xid, gchar **resources, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_gstreamer_resources (task, resources, (GpkDbusTaskFinishedCb) gpk_dbus_task_finished_cb, dbus);
}

/**
 * gpk_dbus_install_printer_drivers:
 **/
void
gpk_dbus_install_printer_drivers (GpkDbus *dbus, guint32 xid, gchar **device_ids, const gchar *interaction, DBusGMethodInvocation *context)
{
	GpkDbusTask *task;
	task = gpk_dbus_create_task (dbus, xid, interaction, context);
	gpk_dbus_task_install_printer_drivers (task, device_ids, (GpkDbusTaskFinishedCb) gpk_dbus_task_finished_cb, dbus);
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
	DBusGConnection *connection;

	dbus->priv = GPK_DBUS_GET_PRIVATE (dbus);
	dbus->priv->timeout_tmp = -1;
	dbus->priv->gconf_client = gconf_client_get_default ();
	dbus->priv->x11 = gpk_x11_new ();
	dbus->priv->timer = g_timer_new ();

	/* find out PIDs on the session bus */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);
	dbus->priv->proxy_session_pid = dbus_g_proxy_new_for_name_owner (connection,
								 "org.freedesktop.DBus",
								 "/org/freedesktop/DBus/Bus",
								 "org.freedesktop.DBus", NULL);
	/* find out PIDs on the system bus */
	connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);
	dbus->priv->proxy_system_pid = dbus_g_proxy_new_for_name_owner (connection,
								 "org.freedesktop.DBus",
								 "/org/freedesktop/DBus/Bus",
								 "org.freedesktop.DBus", NULL);
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
	g_timer_destroy (dbus->priv->timer);
	g_object_unref (dbus->priv->gconf_client);
	g_object_unref (dbus->priv->x11);
	g_object_unref (dbus->priv->proxy_session_pid);
	g_object_unref (dbus->priv->proxy_system_pid);

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

