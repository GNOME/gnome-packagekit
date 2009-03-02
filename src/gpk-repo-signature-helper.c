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

#include "gpk-repo-signature-helper.h"
#include "gpk-marshal.h"
#include "gpk-gnome.h"
#include "gpk-common.h"

#include "egg-debug.h"

static void     gpk_repo_signature_helper_finalize	(GObject	  *object);

#define GPK_REPO_SIGNATURE_HELPER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_REPO_SIGNATURE_HELPER, GpkRepoSignatureHelperPrivate))

struct GpkRepoSignatureHelperPrivate
{
	GladeXML		*glade_xml;
	gchar			*key_id;
	gchar			*package_id;
};

enum {
	GPK_REPO_SIGNATURE_HELPER_EVENT,
	GPK_REPO_SIGNATURE_HELPER_LAST_SIGNAL
};

static guint signals [GPK_REPO_SIGNATURE_HELPER_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkRepoSignatureHelper, gpk_repo_signature_helper, G_TYPE_OBJECT)

/**
 * gpk_repo_signature_helper_button_yes_cb:
 **/
static void
gpk_repo_signature_helper_button_yes_cb (GtkWidget *widget, GpkRepoSignatureHelper *repo_signature_helper)
{
	g_signal_emit (repo_signature_helper, signals [GPK_REPO_SIGNATURE_HELPER_EVENT], 0,
		       GTK_RESPONSE_YES, repo_signature_helper->priv->key_id, repo_signature_helper->priv->package_id);
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "dialog_gpg");
	gtk_widget_hide (widget);
}

/**
 * gpk_repo_signature_helper_button_no_cb:
 **/
static void
gpk_repo_signature_helper_button_no_cb (GtkWidget *widget, GpkRepoSignatureHelper *repo_signature_helper)
{
	g_signal_emit (repo_signature_helper, signals [GPK_REPO_SIGNATURE_HELPER_EVENT], 0,
		       GTK_RESPONSE_NO, repo_signature_helper->priv->key_id, repo_signature_helper->priv->package_id);
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "dialog_gpg");
	gtk_widget_hide (widget);
}

/**
 * gpk_repo_signature_helper_button_help_cb:
 **/
static void
gpk_repo_signature_helper_button_help_cb (GtkWidget *widget, GpkRepoSignatureHelper *repo_signature_helper)
{
	/* show the help */
	gpk_gnome_help ("gpg-signature");
}

/**
 * gpk_repo_signature_helper_show:
 *
 * Return value: if we agreed
 **/
gboolean
gpk_repo_signature_helper_show (GpkRepoSignatureHelper *repo_signature_helper, const gchar *package_id, const gchar *repository_name,
				const gchar *key_url, const gchar *key_userid, const gchar *key_id,
				const gchar *key_fingerprint, const gchar *key_timestamp)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_REPO_SIGNATURE_HELPER (repo_signature_helper), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* cache */
	g_free (repo_signature_helper->priv->key_id);
	g_free (repo_signature_helper->priv->package_id);
	repo_signature_helper->priv->key_id = g_strdup (key_id);
	repo_signature_helper->priv->package_id = g_strdup (package_id);

	/* show correct text */
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "label_name");
	gtk_label_set_label (GTK_LABEL (widget), repository_name);
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "label_url");
	gtk_label_set_label (GTK_LABEL (widget), key_url);
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "label_user");
	gtk_label_set_label (GTK_LABEL (widget), key_userid);
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "label_id");
	gtk_label_set_label (GTK_LABEL (widget), key_id);

	/* show window */
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "dialog_gpg");
	gtk_widget_show (widget);

	return TRUE;
}

/**
 * gpk_repo_signature_helper_set_parent:
 **/
gboolean
gpk_repo_signature_helper_set_parent (GpkRepoSignatureHelper *repo_signature_helper, GtkWindow *window)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_REPO_SIGNATURE_HELPER (repo_signature_helper), FALSE);
	g_return_val_if_fail (window != NULL, FALSE);

	/* make modal if window set */
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "dialog_gpg");
	gtk_window_set_transient_for (GTK_WINDOW (widget), window);

	/* this is a modal popup, so don't show a window title */
	gtk_window_set_title (GTK_WINDOW (widget), "");

	return TRUE;
}

/**
 * gpk_repo_signature_helper_class_init:
 * @klass: The GpkRepoSignatureHelperClass
 **/
static void
gpk_repo_signature_helper_class_init (GpkRepoSignatureHelperClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_repo_signature_helper_finalize;
	g_type_class_add_private (klass, sizeof (GpkRepoSignatureHelperPrivate));
	signals [GPK_REPO_SIGNATURE_HELPER_EVENT] =
		g_signal_new ("event",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkRepoSignatureHelperClass, event),
			      NULL, NULL, gpk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
}

/**
 * gpk_repo_signature_helper_init:
 **/
static void
gpk_repo_signature_helper_init (GpkRepoSignatureHelper *repo_signature_helper)
{
	GtkWidget *widget;

	repo_signature_helper->priv = GPK_REPO_SIGNATURE_HELPER_GET_PRIVATE (repo_signature_helper);

	repo_signature_helper->priv->key_id = NULL;
	repo_signature_helper->priv->package_id = NULL;
	repo_signature_helper->priv->glade_xml = glade_xml_new (GPK_DATA "/gpk-signature.glade", NULL, NULL);

	/* connect up default actions */
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "dialog_gpg");
	g_signal_connect (widget, "delete_event", G_CALLBACK (gpk_repo_signature_helper_button_no_cb), repo_signature_helper);

	/* set icon name */
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "dialog_gpg");
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* connect up buttons */
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "button_yes");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_repo_signature_helper_button_yes_cb), repo_signature_helper);
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "button_help");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_repo_signature_helper_button_help_cb), repo_signature_helper);
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "button_no");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_repo_signature_helper_button_no_cb), repo_signature_helper);
}

/**
 * gpk_repo_signature_helper_finalize:
 **/
static void
gpk_repo_signature_helper_finalize (GObject *object)
{
	GtkWidget *widget;
	GpkRepoSignatureHelper *repo_signature_helper;

	g_return_if_fail (GPK_IS_REPO_SIGNATURE_HELPER (object));

	repo_signature_helper = GPK_REPO_SIGNATURE_HELPER (object);

	/* hide window */
	widget = glade_xml_get_widget (repo_signature_helper->priv->glade_xml, "dialog_gpg");
	if (GTK_IS_WIDGET (widget))
		gtk_widget_hide (widget);
	g_free (repo_signature_helper->priv->key_id);
	g_free (repo_signature_helper->priv->package_id);
	g_object_unref (repo_signature_helper->priv->glade_xml);

	G_OBJECT_CLASS (gpk_repo_signature_helper_parent_class)->finalize (object);
}

/**
 * gpk_repo_signature_helper_new:
 **/
GpkRepoSignatureHelper *
gpk_repo_signature_helper_new (void)
{
	GpkRepoSignatureHelper *repo_signature_helper;
	repo_signature_helper = g_object_new (GPK_TYPE_REPO_SIGNATURE_HELPER, NULL);
	return GPK_REPO_SIGNATURE_HELPER (repo_signature_helper);
}

