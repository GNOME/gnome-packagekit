/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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
#include <gtk/gtkstatusicon.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "gpk-marshal.h"
#include "gpk-common.h"
#include "gpk-smart-icon.h"

static void     gpk_smart_icon_finalize		(GObject           *object);

#define GPK_SMART_ICON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_SMART_ICON, GpkSmartIconPrivate))
#define GPK_SMART_ICON_PERSIST_TIMEOUT	100

struct GpkSmartIconPrivate
{
	gchar			*current;
	gchar			*new;
	guint			 event_source;
};

G_DEFINE_TYPE (GpkSmartIcon, gpk_smart_icon, GTK_TYPE_STATUS_ICON)

/**
 * gpk_smart_icon_class_init:
 * @klass: The GpkSmartIconClass
 **/
static void
gpk_smart_icon_class_init (GpkSmartIconClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_smart_icon_finalize;
	g_type_class_add_private (klass, sizeof (GpkSmartIconPrivate));
}

/**
 * gpk_smart_icon_set_icon_name_cb:
 **/
static gboolean
gpk_smart_icon_set_icon_name_cb (gpointer data)
{
	GpkSmartIcon *sicon = (GpkSmartIcon *) data;

	/* no point setting the same */
	if (g_strcmp0 (sicon->priv->new, sicon->priv->current) == 0) {
		egg_debug ("setting the same: %s", sicon->priv->new);
		return FALSE;
	}

	/* save new version of what we have */
	g_free (sicon->priv->current);
	sicon->priv->current = g_strdup (sicon->priv->new);
	egg_debug ("setting new: %s", sicon->priv->new);

	/* set the correct thing */
	if (sicon->priv->new == NULL) {
		gtk_status_icon_set_visible (GTK_STATUS_ICON (sicon), FALSE);
	} else {
		gtk_status_icon_set_from_icon_name (GTK_STATUS_ICON (sicon), sicon->priv->new);
		gtk_status_icon_set_visible (GTK_STATUS_ICON (sicon), TRUE);
	}
	return FALSE;
}

/**
 * gpk_smart_icon_set_icon:
 **/
gboolean
gpk_smart_icon_set_icon_name (GpkSmartIcon *sicon, const gchar *icon_name)
{
	g_return_val_if_fail (GPK_IS_SMART_ICON (sicon), FALSE);

	/* if we have a request pending, then cancel it in preference to this one */
	if (sicon->priv->event_source != 0) {
		g_source_remove (sicon->priv->event_source);
		sicon->priv->event_source = 0;
	}

	/* tell us what we -want- */
	g_free (sicon->priv->new);
	egg_debug ("setting icon name %s", icon_name);
	sicon->priv->new = g_strdup (icon_name);

	/* wait a little while to see if it's worth displaying the icon */
	sicon->priv->event_source = g_timeout_add (GPK_SMART_ICON_PERSIST_TIMEOUT, gpk_smart_icon_set_icon_name_cb, sicon);
	return TRUE;
}

/**
 * gpk_smart_icon_set_priority:
 **/
gboolean
gpk_smart_icon_set_priority (GpkSmartIcon *sicon, guint number)
{
	g_return_val_if_fail (GPK_IS_SMART_ICON (sicon), FALSE);
	egg_debug ("set priority %i", number);
	return TRUE;
}

/**
 * gpk_smart_icon_init:
 * @smart_icon: This class instance
 **/
static void
gpk_smart_icon_init (GpkSmartIcon *sicon)
{
	sicon->priv = GPK_SMART_ICON_GET_PRIVATE (sicon);
	sicon->priv->new = NULL;
	sicon->priv->current = NULL;
	sicon->priv->event_source = 0;
	gtk_status_icon_set_visible (GTK_STATUS_ICON (sicon), FALSE);
}

/**
 * gpk_smart_icon_finalize:
 * @object: The object to finalize
 **/
static void
gpk_smart_icon_finalize (GObject *object)
{
	GpkSmartIcon *sicon;

	g_return_if_fail (GPK_IS_SMART_ICON (object));

	sicon = GPK_SMART_ICON (object);
	g_return_if_fail (sicon->priv != NULL);

	/* remove any timers that may be pending */
	if (sicon->priv->event_source != 0)
		g_source_remove (sicon->priv->event_source);

	g_free (sicon->priv->new);
	g_free (sicon->priv->current);

	G_OBJECT_CLASS (gpk_smart_icon_parent_class)->finalize (object);
}

/**
 * gpk_smart_icon_new:
 *
 * Return value: a new GpkSmartIcon object.
 **/
GpkSmartIcon *
gpk_smart_icon_new (void)
{
	GpkSmartIcon *sicon;
	sicon = g_object_new (GPK_TYPE_SMART_ICON, NULL);
	return GPK_SMART_ICON (sicon);
}

