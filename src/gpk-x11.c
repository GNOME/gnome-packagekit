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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>

#include "egg-string.h"

#include "gpk-x11.h"

static void     gpk_x11_finalize	(GObject	  *object);

#define GPK_X11_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_X11, GpkX11Private))

struct GpkX11Private
{
	GdkDisplay		*gdk_display;
	Display			*display;
	Window			 window;
};

G_DEFINE_TYPE (GpkX11, gpk_x11, G_TYPE_OBJECT)

/**
 * gpk_x11_set_xid:
 **/
gboolean
gpk_x11_set_xid (GpkX11 *x11, guint32 xid)
{
	GdkWindow *window;

	g_return_val_if_fail (GPK_IS_X11 (x11), FALSE);

	window = gdk_x11_window_foreign_new_for_display (x11->priv->gdk_display, xid);
	if (window == NULL)
		return FALSE;

	/* save the x state */
	x11->priv->display = GDK_DISPLAY_XDISPLAY (x11->priv->gdk_display);
	x11->priv->window = GDK_WINDOW_XID (window);

	return TRUE;
}

/**
 * gpk_x11_set_window:
 **/
gboolean
gpk_x11_set_window (GpkX11 *x11, GdkWindow *window)
{
	g_return_val_if_fail (GPK_IS_X11 (x11), FALSE);

	/* save the x state */
	x11->priv->display = GDK_DISPLAY_XDISPLAY (x11->priv->gdk_display);
	x11->priv->window = GDK_WINDOW_XID (window);

	return TRUE;
}

/**
 * gpk_x11_get_user_time:
 **/
guint32
gpk_x11_get_user_time (GpkX11 *x11)
{
	guint32 timestamp = 0;
	Atom atom = None;
	Atom type;
	gint format;
	gulong nitems;
	gulong bytes_after;
	Window *win = NULL;
	int rc;
	int err;

	g_return_val_if_fail (GPK_IS_X11 (x11), 0);

	/* check we have a window */
	if (x11->priv->window == None) {
		g_debug ("no window, so cannot get user_time");
		goto out;
	}

	/* get _NET_WM_USER_TIME_WINDOW which points to a window on which you can find the _NET_WM_USER_TIME property */
	gdk_error_trap_push ();
	atom = gdk_x11_get_xatom_by_name_for_display (x11->priv->gdk_display, "_NET_WM_USER_TIME_WINDOW");
	rc = XGetWindowProperty (x11->priv->display, x11->priv->window, atom, 0, G_MAXLONG, False, XA_WINDOW,
				 &type, &format, &nitems, &bytes_after, (void*) &win);
	err = gdk_error_trap_pop ();
	if (err != Success || rc != Success) {
		g_warning ("couldn't get _NET_WM_USER_TIME_WINDOW");
		goto out;
	}

	/* is not a window */
	if (type != XA_WINDOW) {
		g_warning ("not type XA_WINDOW");
		goto out;
	}

	/* get _NET_WM_USER_TIME so we can get the user time */
	gdk_error_trap_push ();
	atom = gdk_x11_get_xatom_by_name_for_display (x11->priv->gdk_display, "_NET_WM_USER_TIME");
	rc = XGetWindowProperty (x11->priv->display, *win, atom, 0, G_MAXLONG, False, XA_CARDINAL,
				 &type, &format, &nitems, &bytes_after, (void*) &timestamp);
	err = gdk_error_trap_pop ();
	if (err != Success || rc != Success) {
		g_warning ("couldn't get _NET_WM_USER_TIME");
		goto out;
	}

	/* is not a window */
	if (type != XA_CARDINAL) {
		g_warning ("not type XA_CARDINAL");
		goto out;
	}
out:
	if (win != NULL)
		XFree (win);
	return timestamp;
}

/**
 * gpk_x11_get_title:
 **/
gchar *
gpk_x11_get_title (GpkX11 *x11)
{
	gchar *title = NULL;
	Atom atom = None;
	Atom atom_type = None;
	gchar *data = NULL;
	Atom type;
	gint format;
	gulong nitems;
	gulong bytes_after;
	int rc;

	g_return_val_if_fail (GPK_IS_X11 (x11), NULL);

	/* check we have a window */
	if (x11->priv->window == None) {
		g_debug ("no window, so cannot get user_time");
		goto out;
	}

	/* get _NET_WM_NAME */
	gdk_error_trap_push ();
	atom = gdk_x11_get_xatom_by_name_for_display (x11->priv->gdk_display, "_NET_WM_NAME");
	atom_type = gdk_x11_get_xatom_by_name_for_display (x11->priv->gdk_display, "UTF8_STRING");
	rc = XGetWindowProperty (x11->priv->display, x11->priv->window, atom, 0, G_MAXLONG, False, atom_type,
				 &type, &format, &nitems, &bytes_after, (void*) &data);
	gdk_error_trap_pop_ignored ();
	if (rc == Success && nitems > 0) {
		title = g_strdup (data);
		goto out;
	}

	/* we failed to get the UTF8 title, try plain old WM_NAME */
	gdk_error_trap_push ();
	rc = XGetWindowProperty (x11->priv->display, x11->priv->window, XA_WM_NAME, 0, G_MAXLONG, False, XA_STRING,
				 &type, &format, &nitems, &bytes_after, (void*) &data);
	gdk_error_trap_pop_ignored ();
	if (rc == Success && nitems > 0) {
		title = g_strdup (data);
		goto out;
	}
	g_warning ("failed to get X11 name for window %i", (gint) x11->priv->window);
out:
	if (data != NULL)
		XFree (data);
	return title;
}

/**
 * gpk_x11_class_init:
 * @klass: The GpkX11Class
 **/
static void
gpk_x11_class_init (GpkX11Class *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_x11_finalize;
	g_type_class_add_private (klass, sizeof (GpkX11Private));
}

/**
 * gpk_x11_init:
 * @x11: This class instance
 **/
static void
gpk_x11_init (GpkX11 *x11)
{
	x11->priv = GPK_X11_GET_PRIVATE (x11);
	x11->priv->display = NULL;
	x11->priv->window = None;
	x11->priv->gdk_display = gdk_display_get_default ();
}

/**
 * gpk_x11_finalize:
 * @object: The object to finalize
 **/
static void
gpk_x11_finalize (GObject *object)
{
	GpkX11 *x11;
	g_return_if_fail (GPK_IS_X11 (object));
	x11 = GPK_X11 (object);

	g_return_if_fail (x11->priv != NULL);

	G_OBJECT_CLASS (gpk_x11_parent_class)->finalize (object);
}

/**
 * gpk_x11_new:
 *
 * Return value: a new GpkX11 object.
 **/
GpkX11 *
gpk_x11_new (void)
{
	GpkX11 *x11;
	x11 = g_object_new (GPK_TYPE_X11, NULL);
	return GPK_X11 (x11);
}

