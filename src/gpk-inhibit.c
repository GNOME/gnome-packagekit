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
#include <dbus/dbus-glib.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "gpk-inhibit.h"
#include "gpk-common.h"

static void     gpk_inhibit_class_init	(GpkInhibitClass *klass);
static void     gpk_inhibit_init	(GpkInhibit      *inhibit);
static void     gpk_inhibit_finalize	(GObject          *object);

#define GPK_INHIBIT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_INHIBIT, GpkInhibitPrivate))

#define GPM_DBUS_SERVICE			"org.freedesktop.PowerManagement"
#define GPM_DBUS_PATH_INHIBIT			"/org/freedesktop/PowerManagement/Inhibit"
#define GPM_DBUS_INTERFACE_INHIBIT		"org.freedesktop.PowerManagement.Inhibit"

struct GpkInhibitPrivate
{
	DBusGProxy		*proxy_gpm;
	guint			 cookie;
};

G_DEFINE_TYPE (GpkInhibit, gpk_inhibit, G_TYPE_OBJECT)

/**
 * gpk_inhibit_class_init:
 * @klass: The GpkInhibitClass
 **/
static void
gpk_inhibit_class_init (GpkInhibitClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_inhibit_finalize;
	g_type_class_add_private (klass, sizeof (GpkInhibitPrivate));
}

/**
 * gpk_inhibit_create:
 **/
gboolean
gpk_inhibit_create (GpkInhibit *inhibit)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_INHIBIT (inhibit), FALSE);

	if (inhibit->priv->proxy_gpm == NULL) {
		egg_debug ("no connection to g-p-m");
		return FALSE;
	}

	/* check we are not trying to do this twice... */
	if (inhibit->priv->cookie != 0) {
		egg_debug ("cookie already set as %i", inhibit->priv->cookie);
		return FALSE;
	}

	/* coldplug the battery state */
	ret = dbus_g_proxy_call (inhibit->priv->proxy_gpm, "Inhibit", &error,
				 G_TYPE_STRING, _("Software Update Applet"),
				 G_TYPE_STRING, _("A transaction that cannot be interrupted is running"),
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &inhibit->priv->cookie,
				 G_TYPE_INVALID);
	if (error != NULL) {
		printf ("DEBUG: ERROR: %s\n", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * gpk_inhibit_remove:
 **/
gboolean
gpk_inhibit_remove (GpkInhibit *inhibit)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_INHIBIT (inhibit), FALSE);

	if (inhibit->priv->proxy_gpm == NULL) {
		egg_debug ("no connection to g-p-m");
		return FALSE;
	}

	/* check we are not trying to do this twice... */
	if (inhibit->priv->cookie == 0) {
		egg_debug ("cookie not already set");
		return FALSE;
	}

	/* coldplug the battery state */
	ret = dbus_g_proxy_call (inhibit->priv->proxy_gpm, "UnInhibit", &error,
				 G_TYPE_UINT, inhibit->priv->cookie,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		printf ("DEBUG: ERROR: %s\n", error->message);
		g_error_free (error);
	}
	inhibit->priv->cookie = 0;
	return ret;
}

/**
 * gpk_inhibit_init:
 * @inhibit: This class instance
 **/
static void
gpk_inhibit_init (GpkInhibit *inhibit)
{
	DBusGConnection *connection;
	GError *error = NULL;
	inhibit->priv = GPK_INHIBIT_GET_PRIVATE (inhibit);
	inhibit->priv->cookie = 0;

	/* connect to session bus */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (error != NULL) {
		egg_warning ("Cannot connect to session bus: %s", error->message);
		g_error_free (error);
		return;
	}

	/* use gnome-power-manager for the session inhibit stuff */
	inhibit->priv->proxy_gpm = dbus_g_proxy_new_for_name_owner (connection,
				  GPM_DBUS_SERVICE, GPM_DBUS_PATH_INHIBIT,
				  GPM_DBUS_INTERFACE_INHIBIT, &error);
	if (error != NULL) {
		egg_warning ("Cannot connect to gnome-power-manager: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_inhibit_finalize:
 * @object: The object to finalize
 **/
static void
gpk_inhibit_finalize (GObject *object)
{
	GpkInhibit *inhibit;

	g_return_if_fail (PK_IS_INHIBIT (object));

	inhibit = GPK_INHIBIT (object);
	g_return_if_fail (inhibit->priv != NULL);

	g_object_unref (inhibit->priv->proxy_gpm);

	G_OBJECT_CLASS (gpk_inhibit_parent_class)->finalize (object);
}

/**
 * gpk_inhibit_new:
 *
 * Return value: a new GpkInhibit object.
 **/
GpkInhibit *
gpk_inhibit_new (void)
{
	GpkInhibit *inhibit;
	inhibit = g_object_new (GPK_TYPE_INHIBIT, NULL);
	return GPK_INHIBIT (inhibit);
}

