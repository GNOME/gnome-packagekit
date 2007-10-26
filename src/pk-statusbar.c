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

#include <pk-debug.h>
#include <pk-client.h>
#include "pk-statusbar.h"
#include "pk-common-gui.h"

static void     pk_statusbar_class_init	(PkStatusbarClass *klass);
static void     pk_statusbar_init	(PkStatusbar      *sbar);
static void     pk_statusbar_finalize	(GObject          *object);

#define PK_STATUSBAR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_STATUSBAR, PkStatusbarPrivate))

struct PkStatusbarPrivate
{
	guint			 timer_id;
	GtkStatusbar		*statusbar;
	GtkProgressBar		*progressbar;
};

G_DEFINE_TYPE (PkStatusbar, pk_statusbar, G_TYPE_OBJECT)

/**
 * pk_statusbar_class_init:
 * @klass: The PkStatusbarClass
 **/
static void
pk_statusbar_class_init (PkStatusbarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_statusbar_finalize;
	g_type_class_add_private (klass, sizeof (PkStatusbarPrivate));
}

/**
 * pk_statusbar_set_widget:
 **/
gboolean
pk_statusbar_set_widget (PkStatusbar *sbar, GtkWidget *widget)
{
	g_return_val_if_fail (sbar != NULL, FALSE);
	g_return_val_if_fail (PK_IS_STATUSBAR (sbar), FALSE);
	g_return_val_if_fail (sbar->priv->statusbar == NULL, FALSE);
	sbar->priv->statusbar = GTK_STATUSBAR (widget);

	gtk_box_pack_end (GTK_BOX (sbar->priv->statusbar), GTK_WIDGET (sbar->priv->progressbar), TRUE, TRUE, 0);

	return TRUE;
}

/**
 * pk_statusbar_pulse_timeout:
 **/
gboolean
pk_statusbar_pulse_timeout (gpointer data)
{
	PkStatusbar *sbar = (PkStatusbar *) data;
	gtk_progress_bar_pulse (sbar->priv->progressbar);
	return TRUE;
}

/**
 * pk_statusbar_set_percentage:
 **/
gboolean
pk_statusbar_set_percentage (PkStatusbar *sbar, guint percentage)
{
	g_return_val_if_fail (sbar != NULL, FALSE);
	g_return_val_if_fail (PK_IS_STATUSBAR (sbar), FALSE);
	g_return_val_if_fail (sbar->priv->statusbar != NULL, FALSE);

	/* show the progress bar */
	gtk_widget_show (GTK_WIDGET (sbar->priv->progressbar));

	if (percentage == PK_CLIENT_PERCENTAGE_INVALID) {
		/* don't spin twice as fast if more than one signal */
		if (sbar->priv->timer_id != 0) {
			return TRUE;
		}
		sbar->priv->timer_id = g_timeout_add (PK_PROGRESS_BAR_PULSE_DELAY,
							     pk_statusbar_pulse_timeout,
							     sbar);
		return TRUE;
	}

	/* we've gone from unknown -> actual value - cancel the polling */
	if (sbar->priv->timer_id != 0) {
		g_source_remove (sbar->priv->timer_id);
		sbar->priv->timer_id = 0;
	}

	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (sbar->priv->progressbar), (gfloat) percentage / 100.0);
	return TRUE;
}

/**
 * pk_statusbar_set_remaining:
 **/
gboolean
pk_statusbar_set_remaining (PkStatusbar *sbar, guint remaining)
{
	g_return_val_if_fail (sbar != NULL, FALSE);
	g_return_val_if_fail (PK_IS_STATUSBAR (sbar), FALSE);
	g_return_val_if_fail (sbar->priv->statusbar != NULL, FALSE);
	return TRUE;
}

/**
 * pk_statusbar_hide:
 **/
gboolean
pk_statusbar_hide (PkStatusbar *sbar)
{
	g_return_val_if_fail (sbar != NULL, FALSE);
	g_return_val_if_fail (PK_IS_STATUSBAR (sbar), FALSE);
	g_return_val_if_fail (sbar->priv->statusbar != NULL, FALSE);

	/* don't spin anymore if we are not visible */
	if (sbar->priv->timer_id != 0) {
		g_source_remove (sbar->priv->timer_id);
		sbar->priv->timer_id = 0;
	}
	gtk_widget_hide (GTK_WIDGET (sbar->priv->progressbar));
	return TRUE;
}

/**
 * pk_statusbar_init:
 * @statusbar: This class instance
 **/
static void
pk_statusbar_init (PkStatusbar *sbar)
{
	sbar->priv = PK_STATUSBAR_GET_PRIVATE (sbar);
	sbar->priv->timer_id = 0;

	sbar->priv->progressbar = GTK_PROGRESS_BAR (gtk_progress_bar_new ());
	gtk_progress_bar_set_fraction (sbar->priv->progressbar, 0.0);
	gtk_progress_bar_set_pulse_step (sbar->priv->progressbar, PK_PROGRESS_BAR_PULSE_STEP);
}

/**
 * pk_statusbar_finalize:
 * @object: The object to finalize
 **/
static void
pk_statusbar_finalize (GObject *object)
{
	PkStatusbar *sbar;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_STATUSBAR (object));

	sbar = PK_STATUSBAR (object);
	g_return_if_fail (sbar->priv != NULL);

	/* don't spin anymore */
	if (sbar->priv->timer_id != 0) {
		g_source_remove (sbar->priv->timer_id);
	}

	G_OBJECT_CLASS (pk_statusbar_parent_class)->finalize (object);
}

/**
 * pk_statusbar_new:
 *
 * Return value: a new PkStatusbar object.
 **/
PkStatusbar *
pk_statusbar_new (void)
{
	PkStatusbar *sbar;
	sbar = g_object_new (PK_TYPE_STATUSBAR, NULL);
	return PK_STATUSBAR (sbar);
}

