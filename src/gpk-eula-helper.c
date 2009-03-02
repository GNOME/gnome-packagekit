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

#include "gpk-eula-helper.h"
#include "gpk-marshal.h"
#include "gpk-gnome.h"
#include "gpk-common.h"

#include "egg-debug.h"

static void     gpk_eula_helper_finalize	(GObject	  *object);

#define GPK_EULA_HELPER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_EULA_HELPER, GpkEulaHelperPrivate))

struct GpkEulaHelperPrivate
{
	GladeXML		*glade_xml;
	gchar			*eula_id;
};

enum {
	GPK_EULA_HELPER_EVENT,
	GPK_EULA_HELPER_LAST_SIGNAL
};

static guint signals [GPK_EULA_HELPER_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkEulaHelper, gpk_eula_helper, G_TYPE_OBJECT)

/**
 * gpk_eula_helper_button_agree_cb:
 **/
static void
gpk_eula_helper_button_agree_cb (GtkWidget *widget, GpkEulaHelper *eula_helper)
{
	g_signal_emit (eula_helper, signals [GPK_EULA_HELPER_EVENT], 0, GTK_RESPONSE_YES, eula_helper->priv->eula_id);
	widget = glade_xml_get_widget (eula_helper->priv->glade_xml, "dialog_eula");
	gtk_widget_hide (widget);
}

/**
 * gpk_eula_helper_button_cancel_cb:
 **/
static void
gpk_eula_helper_button_cancel_cb (GtkWidget *widget, GpkEulaHelper *eula_helper)
{
	g_signal_emit (eula_helper, signals [GPK_EULA_HELPER_EVENT], 0, GTK_RESPONSE_NO, eula_helper->priv->eula_id);
	widget = glade_xml_get_widget (eula_helper->priv->glade_xml, "dialog_eula");
	gtk_widget_hide (widget);
}

/**
 * gpk_eula_helper_button_help_cb:
 **/
static void
gpk_eula_helper_button_help_cb (GtkWidget *widget, GpkEulaHelper *eula_helper)
{
	/* show the help */
	gpk_gnome_help ("eula");
}

/**
 * gpk_eula_helper_show:
 *
 * Return value: if we agreed
 **/
gboolean
gpk_eula_helper_show (GpkEulaHelper *eula_helper, const gchar *eula_id, const gchar *package_id,
	       const gchar *vendor_name, const gchar *license_agreement)
{
	GtkWidget *widget;
	GtkTextBuffer *buffer;
	gchar *text;
	PkPackageId *ident;

	g_return_val_if_fail (GPK_IS_EULA_HELPER (eula_helper), FALSE);
	g_return_val_if_fail (eula_id != NULL, FALSE);

	/* cache */
	g_free (eula_helper->priv->eula_id);
	eula_helper->priv->eula_id = g_strdup (eula_id);

	/* title */
	widget = glade_xml_get_widget (eula_helper->priv->glade_xml, "label_title");
	ident = pk_package_id_new_from_string (package_id);
	text = g_strdup_printf ("<b><big>License required for %s by %s</big></b>", ident->name, vendor_name);
	gtk_label_set_label (GTK_LABEL (widget), text);
	pk_package_id_free (ident);
	g_free (text);

	buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_insert_at_cursor (buffer, license_agreement, strlen (license_agreement));
	widget = glade_xml_get_widget (eula_helper->priv->glade_xml, "textview_details");
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);

	/* set minimum size a bit bigger */
	gtk_widget_set_size_request (widget, 100, 200);

	/* show window */
	widget = glade_xml_get_widget (eula_helper->priv->glade_xml, "dialog_eula");
	gtk_widget_show (widget);

	g_object_unref (buffer);

	return TRUE;
}

/**
 * gpk_eula_helper_set_parent:
 **/
gboolean
gpk_eula_helper_set_parent (GpkEulaHelper *eula_helper, GtkWindow *window)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_EULA_HELPER (eula_helper), FALSE);
	g_return_val_if_fail (window != NULL, FALSE);

	/* make modal if window set */
	widget = glade_xml_get_widget (eula_helper->priv->glade_xml, "dialog_eula");
	gtk_window_set_transient_for (GTK_WINDOW (widget), window);

	/* this is a modal popup, so don't show a window title */
	gtk_window_set_title (GTK_WINDOW (widget), "");

	return TRUE;
}

/**
 * gpk_eula_helper_class_init:
 * @klass: The GpkEulaHelperClass
 **/
static void
gpk_eula_helper_class_init (GpkEulaHelperClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_eula_helper_finalize;
	g_type_class_add_private (klass, sizeof (GpkEulaHelperPrivate));
	signals [GPK_EULA_HELPER_EVENT] =
		g_signal_new ("event",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkEulaHelperClass, event),
			      NULL, NULL, gpk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
}

/**
 * gpk_eula_helper_init:
 **/
static void
gpk_eula_helper_init (GpkEulaHelper *eula_helper)
{
	GtkWidget *widget;

	eula_helper->priv = GPK_EULA_HELPER_GET_PRIVATE (eula_helper);

	eula_helper->priv->eula_id = NULL;
	eula_helper->priv->glade_xml = glade_xml_new (GPK_DATA "/gpk-eula.glade", NULL, NULL);

	/* connect up default actions */
	widget = glade_xml_get_widget (eula_helper->priv->glade_xml, "dialog_eula");
	g_signal_connect (widget, "delete_event", G_CALLBACK (gpk_eula_helper_button_cancel_cb), eula_helper);

	/* set icon name */
	widget = glade_xml_get_widget (eula_helper->priv->glade_xml, "dialog_eula");
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* connect up buttons */
	widget = glade_xml_get_widget (eula_helper->priv->glade_xml, "button_agree");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_eula_helper_button_agree_cb), eula_helper);
	widget = glade_xml_get_widget (eula_helper->priv->glade_xml, "button_help");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_eula_helper_button_help_cb), eula_helper);
	widget = glade_xml_get_widget (eula_helper->priv->glade_xml, "button_cancel");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_eula_helper_button_cancel_cb), eula_helper);
}

/**
 * gpk_eula_helper_finalize:
 **/
static void
gpk_eula_helper_finalize (GObject *object)
{
	GtkWidget *widget;
	GpkEulaHelper *eula_helper;

	g_return_if_fail (GPK_IS_EULA_HELPER (object));

	eula_helper = GPK_EULA_HELPER (object);

	/* hide window */
	widget = glade_xml_get_widget (eula_helper->priv->glade_xml, "dialog_eula");
	if (GTK_IS_WIDGET (widget))
		gtk_widget_hide (widget);
	g_free (eula_helper->priv->eula_id);
	g_object_unref (eula_helper->priv->glade_xml);

	G_OBJECT_CLASS (gpk_eula_helper_parent_class)->finalize (object);
}

/**
 * gpk_eula_helper_new:
 **/
GpkEulaHelper *
gpk_eula_helper_new (void)
{
	GpkEulaHelper *eula_helper;
	eula_helper = g_object_new (GPK_TYPE_EULA_HELPER, NULL);
	return GPK_EULA_HELPER (eula_helper);
}

