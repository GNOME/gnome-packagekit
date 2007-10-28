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
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-client.h>
#include "pk-inhibit.h"
#include "pk-common-gui.h"

static void     pk_inhibit_class_init	(PkInhibitClass *klass);
static void     pk_inhibit_init	(PkInhibit      *inhibit);
static void     pk_inhibit_finalize	(GObject          *object);

#define PK_INHIBIT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_INHIBIT, PkInhibitPrivate))

struct PkInhibitPrivate
{
	DBusGProxy		*proxy_gpm;
	guint			 cookie;
};

G_DEFINE_TYPE (PkInhibit, pk_inhibit, G_TYPE_OBJECT)

/**
 * pk_inhibit_class_init:
 * @klass: The PkInhibitClass
 **/
static void
pk_inhibit_class_init (PkInhibitClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_inhibit_finalize;
	g_type_class_add_private (klass, sizeof (PkInhibitPrivate));
}

/**
 * pk_inhibit_create:
 **/
gboolean
pk_inhibit_create (PkInhibit *inhibit)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (inhibit != NULL, FALSE);
	g_return_val_if_fail (PK_IS_INHIBIT (inhibit), FALSE);

	if (inhibit->priv->proxy_gpm == NULL) {
		pk_debug ("no connection to g-p-m");
		return FALSE;
	}

	/* check we are not trying to do this twice... */
	if (inhibit->priv->cookie != 0) {
		pk_debug ("cookie already set as %i", inhibit->priv->cookie);
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
 * pk_inhibit_remove:
 **/
gboolean
pk_inhibit_remove (PkInhibit *inhibit)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (inhibit != NULL, FALSE);
	g_return_val_if_fail (PK_IS_INHIBIT (inhibit), FALSE);

	if (inhibit->priv->proxy_gpm == NULL) {
		pk_debug ("no connection to g-p-m");
		return FALSE;
	}

	/* check we are not trying to do this twice... */
	if (inhibit->priv->cookie == 0) {
		pk_debug ("cookie not already set");
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
 * pk_inhibit_init:
 * @inhibit: This class instance
 **/
static void
pk_inhibit_init (PkInhibit *inhibit)
{
	DBusGConnection *connection;
	GError *error = NULL;
	inhibit->priv = PK_INHIBIT_GET_PRIVATE (inhibit);
	inhibit->priv->cookie = 0;

	/* connect to session bus */
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (error != NULL) {
		pk_warning ("Cannot connect to session bus: %s", error->message);
		g_error_free (error);
		return;
	}

	/* use gnome-power-manager for the session inhibit stuff */
	inhibit->priv->proxy_gpm = dbus_g_proxy_new_for_name_owner (connection,
				  GPM_DBUS_SERVICE, GPM_DBUS_PATH_INHIBIT,
				  GPM_DBUS_INTERFACE_INHIBIT, &error);
	if (error != NULL) {
		pk_warning ("Cannot connect to gnome-power-manager: %s", error->message);
		g_error_free (error);
	}
}

/**
 * pk_inhibit_finalize:
 * @object: The object to finalize
 **/
static void
pk_inhibit_finalize (GObject *object)
{
	PkInhibit *inhibit;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_INHIBIT (object));

	inhibit = PK_INHIBIT (object);
	g_return_if_fail (inhibit->priv != NULL);

	g_object_unref (inhibit->priv->proxy_gpm);

	G_OBJECT_CLASS (pk_inhibit_parent_class)->finalize (object);
}

/**
 * pk_inhibit_new:
 *
 * Return value: a new PkInhibit object.
 **/
PkInhibit *
pk_inhibit_new (void)
{
	PkInhibit *inhibit;
	inhibit = g_object_new (PK_TYPE_INHIBIT, NULL);
	return PK_INHIBIT (inhibit);
}

