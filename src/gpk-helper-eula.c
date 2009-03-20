/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include "gpk-helper-eula.h"
#include "gpk-marshal.h"
#include "gpk-gnome.h"
#include "gpk-common.h"

#include "egg-debug.h"

static void     gpk_helper_eula_finalize	(GObject	  *object);

#define GPK_HELPER_EULA_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_HELPER_EULA, GpkHelperEulaPrivate))

struct GpkHelperEulaPrivate
{
	GladeXML		*glade_xml;
	gchar			*eula_id;
};

enum {
	GPK_HELPER_EULA_EVENT,
	GPK_HELPER_EULA_LAST_SIGNAL
};

static guint signals [GPK_HELPER_EULA_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkHelperEula, gpk_helper_eula, G_TYPE_OBJECT)

/**
 * gpk_helper_eula_button_agree_cb:
 **/
static void
gpk_helper_eula_button_agree_cb (GtkWidget *widget, GpkHelperEula *helper_eula)
{
	g_signal_emit (helper_eula, signals [GPK_HELPER_EULA_EVENT], 0, GTK_RESPONSE_YES, helper_eula->priv->eula_id);
	widget = glade_xml_get_widget (helper_eula->priv->glade_xml, "dialog_eula");
	gtk_widget_hide (widget);
}

/**
 * gpk_helper_eula_button_cancel_cb:
 **/
static void
gpk_helper_eula_button_cancel_cb (GtkWidget *widget, GpkHelperEula *helper_eula)
{
	g_signal_emit (helper_eula, signals [GPK_HELPER_EULA_EVENT], 0, GTK_RESPONSE_NO, helper_eula->priv->eula_id);
	widget = glade_xml_get_widget (helper_eula->priv->glade_xml, "dialog_eula");
	gtk_widget_hide (widget);
}

/**
 * gpk_helper_eula_button_help_cb:
 **/
static void
gpk_helper_eula_button_help_cb (GtkWidget *widget, GpkHelperEula *helper_eula)
{
	/* show the help */
	gpk_gnome_help ("eula");
}

/**
 * gpk_helper_eula_show:
 *
 * Return value: if we agreed
 **/
gboolean
gpk_helper_eula_show (GpkHelperEula *helper_eula, const gchar *eula_id, const gchar *package_id,
	       const gchar *vendor_name, const gchar *license_agreement)
{
	GtkWidget *widget;
	GtkTextBuffer *buffer;
	gchar *text;
	PkPackageId *ident;

	g_return_val_if_fail (GPK_IS_HELPER_EULA (helper_eula), FALSE);
	g_return_val_if_fail (eula_id != NULL, FALSE);

	/* cache */
	g_free (helper_eula->priv->eula_id);
	helper_eula->priv->eula_id = g_strdup (eula_id);

	/* title */
	widget = glade_xml_get_widget (helper_eula->priv->glade_xml, "label_title");
	ident = pk_package_id_new_from_string (package_id);
	text = g_strdup_printf ("<b><big>License required for %s by %s</big></b>", ident->name, vendor_name);
	gtk_label_set_label (GTK_LABEL (widget), text);
	pk_package_id_free (ident);
	g_free (text);

	buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_insert_at_cursor (buffer, license_agreement, strlen (license_agreement));
	widget = glade_xml_get_widget (helper_eula->priv->glade_xml, "textview_details");
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);

	/* set minimum size a bit bigger */
	gtk_widget_set_size_request (widget, 100, 200);

	/* show window */
	widget = glade_xml_get_widget (helper_eula->priv->glade_xml, "dialog_eula");
	gtk_widget_show (widget);

	g_object_unref (buffer);

	return TRUE;
}

/**
 * gpk_helper_eula_set_parent:
 **/
gboolean
gpk_helper_eula_set_parent (GpkHelperEula *helper_eula, GtkWindow *window)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_HELPER_EULA (helper_eula), FALSE);
	g_return_val_if_fail (window != NULL, FALSE);

	/* make modal if window set */
	widget = glade_xml_get_widget (helper_eula->priv->glade_xml, "dialog_eula");
	gtk_window_set_transient_for (GTK_WINDOW (widget), window);

	/* this is a modal popup, so don't show a window title */
	gtk_window_set_title (GTK_WINDOW (widget), "");

	return TRUE;
}

/**
 * gpk_helper_eula_class_init:
 * @klass: The GpkHelperEulaClass
 **/
static void
gpk_helper_eula_class_init (GpkHelperEulaClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_helper_eula_finalize;
	g_type_class_add_private (klass, sizeof (GpkHelperEulaPrivate));
	signals [GPK_HELPER_EULA_EVENT] =
		g_signal_new ("event",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkHelperEulaClass, event),
			      NULL, NULL, gpk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
}

/**
 * gpk_helper_eula_init:
 **/
static void
gpk_helper_eula_init (GpkHelperEula *helper_eula)
{
	GtkWidget *widget;

	helper_eula->priv = GPK_HELPER_EULA_GET_PRIVATE (helper_eula);

	helper_eula->priv->eula_id = NULL;
	helper_eula->priv->glade_xml = glade_xml_new (GPK_DATA "/gpk-eula.glade", NULL, NULL);

	/* connect up default actions */
	widget = glade_xml_get_widget (helper_eula->priv->glade_xml, "dialog_eula");
	g_signal_connect (widget, "delete_event", G_CALLBACK (gpk_helper_eula_button_cancel_cb), helper_eula);

	/* set icon name */
	widget = glade_xml_get_widget (helper_eula->priv->glade_xml, "dialog_eula");
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* connect up buttons */
	widget = glade_xml_get_widget (helper_eula->priv->glade_xml, "button_agree");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_helper_eula_button_agree_cb), helper_eula);
	widget = glade_xml_get_widget (helper_eula->priv->glade_xml, "button_help");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_helper_eula_button_help_cb), helper_eula);
	widget = glade_xml_get_widget (helper_eula->priv->glade_xml, "button_cancel");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_helper_eula_button_cancel_cb), helper_eula);
}

/**
 * gpk_helper_eula_finalize:
 **/
static void
gpk_helper_eula_finalize (GObject *object)
{
	GtkWidget *widget;
	GpkHelperEula *helper_eula;

	g_return_if_fail (GPK_IS_HELPER_EULA (object));

	helper_eula = GPK_HELPER_EULA (object);

	/* hide window */
	widget = glade_xml_get_widget (helper_eula->priv->glade_xml, "dialog_eula");
	if (GTK_IS_WIDGET (widget))
		gtk_widget_hide (widget);
	g_free (helper_eula->priv->eula_id);
	g_object_unref (helper_eula->priv->glade_xml);

	G_OBJECT_CLASS (gpk_helper_eula_parent_class)->finalize (object);
}

/**
 * gpk_helper_eula_new:
 **/
GpkHelperEula *
gpk_helper_eula_new (void)
{
	GpkHelperEula *helper_eula;
	helper_eula = g_object_new (GPK_TYPE_HELPER_EULA, NULL);
	return GPK_HELPER_EULA (helper_eula);
}

