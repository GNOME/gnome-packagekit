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
#include <packagekit-glib/packagekit.h>

#include "gpk-helper-repo-signature.h"
#include "gpk-marshal.h"
#include "gpk-gnome.h"
#include "gpk-common.h"

#include "egg-debug.h"

static void     gpk_helper_repo_signature_finalize	(GObject	  *object);

#define GPK_HELPER_REPO_SIGNATURE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_HELPER_REPO_SIGNATURE, GpkHelperRepoSignaturePrivate))

struct GpkHelperRepoSignaturePrivate
{
	GtkBuilder		*builder;
	gchar			*key_id;
	gchar			*package_id;
};

enum {
	GPK_HELPER_REPO_SIGNATURE_EVENT,
	GPK_HELPER_REPO_SIGNATURE_LAST_SIGNAL
};

static guint signals [GPK_HELPER_REPO_SIGNATURE_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkHelperRepoSignature, gpk_helper_repo_signature, G_TYPE_OBJECT)

/**
 * gpk_helper_repo_signature_button_yes_cb:
 **/
static void
gpk_helper_repo_signature_button_yes_cb (GtkWidget *widget, GpkHelperRepoSignature *helper)
{
	g_signal_emit (helper, signals [GPK_HELPER_REPO_SIGNATURE_EVENT], 0,
		       GTK_RESPONSE_YES, helper->priv->key_id, helper->priv->package_id);
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_gpg"));
	gtk_widget_hide (widget);
}

/**
 * gpk_helper_repo_signature_button_no_cb:
 **/
static void
gpk_helper_repo_signature_button_no_cb (GtkWidget *widget, GpkHelperRepoSignature *helper)
{
	g_signal_emit (helper, signals [GPK_HELPER_REPO_SIGNATURE_EVENT], 0,
		       GTK_RESPONSE_NO, helper->priv->key_id, helper->priv->package_id);
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_gpg"));
	gtk_widget_hide (widget);
}

/**
 * gpk_helper_repo_signature_button_help_cb:
 **/
static void
gpk_helper_repo_signature_button_help_cb (GtkWidget *widget, GpkHelperRepoSignature *helper)
{
	/* show the help */
	gpk_gnome_help ("gpg-signature");
}

/**
 * gpk_helper_repo_signature_show:
 *
 * Return value: if we agreed
 **/
gboolean
gpk_helper_repo_signature_show (GpkHelperRepoSignature *helper, const gchar *package_id, const gchar *repository_name,
				const gchar *key_url, const gchar *key_userid, const gchar *key_id,
				const gchar *key_fingerprint, const gchar *key_timestamp)
{
	GtkWidget *widget;
	PkPackageId *id;
	gchar *text;

	g_return_val_if_fail (GPK_IS_HELPER_REPO_SIGNATURE (helper), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* cache */
	g_free (helper->priv->key_id);
	g_free (helper->priv->package_id);
	helper->priv->key_id = g_strdup (key_id);
	helper->priv->package_id = g_strdup (package_id);

	/* show correct text */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "label_name"));
	gtk_label_set_label (GTK_LABEL (widget), repository_name);
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "label_url"));
	gtk_label_set_label (GTK_LABEL (widget), key_url);
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "label_user"));
	gtk_label_set_label (GTK_LABEL (widget), key_userid);
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "label_id"));
	gtk_label_set_label (GTK_LABEL (widget), key_id);

	/* TODO: pass this as a struct from the PkClient signal */
	id = pk_package_id_new_from_string (package_id);
	text = gpk_package_id_name_version (id);

	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "label_package"));
	gtk_label_set_label (GTK_LABEL (widget), text);

	g_free (text);
	pk_package_id_free (id);

	/* show window */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_gpg"));
	gtk_widget_show (widget);

	return TRUE;
}

/**
 * gpk_helper_repo_signature_set_parent:
 **/
gboolean
gpk_helper_repo_signature_set_parent (GpkHelperRepoSignature *helper, GtkWindow *window)
{
	GtkWindow *widget;

	g_return_val_if_fail (GPK_IS_HELPER_REPO_SIGNATURE (helper), FALSE);
	g_return_val_if_fail (window != NULL, FALSE);

	/* make modal if window set */
	widget = GTK_WINDOW (gtk_builder_get_object (helper->priv->builder, "dialog_gpg"));
	gtk_window_set_transient_for (widget, window);
	gtk_window_set_modal (widget, TRUE);

	/* this is a modal popup, so don't show a window title */
	gtk_window_set_title (widget, "");

	return TRUE;
}

/**
 * gpk_helper_repo_signature_class_init:
 * @klass: The GpkHelperRepoSignatureClass
 **/
static void
gpk_helper_repo_signature_class_init (GpkHelperRepoSignatureClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_helper_repo_signature_finalize;
	g_type_class_add_private (klass, sizeof (GpkHelperRepoSignaturePrivate));
	signals [GPK_HELPER_REPO_SIGNATURE_EVENT] =
		g_signal_new ("event",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkHelperRepoSignatureClass, event),
			      NULL, NULL, gpk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
}

/**
 * gpk_helper_repo_signature_init:
 **/
static void
gpk_helper_repo_signature_init (GpkHelperRepoSignature *helper)
{
	GtkWidget *widget;
	guint retval;
	GError *error = NULL;

	helper->priv = GPK_HELPER_REPO_SIGNATURE_GET_PRIVATE (helper);

	helper->priv->key_id = NULL;
	helper->priv->package_id = NULL;

	/* get UI */
	helper->priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (helper->priv->builder, GPK_DATA "/gpk-signature.ui", &error);
	if (error != NULL) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
	}

	/* connect up default actions */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_gpg"));
	g_signal_connect (widget, "delete_event", G_CALLBACK (gpk_helper_repo_signature_button_no_cb), helper);

	/* set icon name */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_gpg"));
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* connect up buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "button_yes"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_helper_repo_signature_button_yes_cb), helper);
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "button_help"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_helper_repo_signature_button_help_cb), helper);
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "button_no"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_helper_repo_signature_button_no_cb), helper);
}

/**
 * gpk_helper_repo_signature_finalize:
 **/
static void
gpk_helper_repo_signature_finalize (GObject *object)
{
	GtkWidget *widget;
	GpkHelperRepoSignature *helper;

	g_return_if_fail (GPK_IS_HELPER_REPO_SIGNATURE (object));

	helper = GPK_HELPER_REPO_SIGNATURE (object);

	/* hide window */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_gpg"));
	if (GTK_IS_WIDGET (widget))
		gtk_widget_hide (widget);
	g_free (helper->priv->key_id);
	g_free (helper->priv->package_id);
	g_object_unref (helper->priv->builder);

	G_OBJECT_CLASS (gpk_helper_repo_signature_parent_class)->finalize (object);
}

/**
 * gpk_helper_repo_signature_new:
 **/
GpkHelperRepoSignature *
gpk_helper_repo_signature_new (void)
{
	GpkHelperRepoSignature *helper;
	helper = g_object_new (GPK_TYPE_HELPER_REPO_SIGNATURE, NULL);
	return GPK_HELPER_REPO_SIGNATURE (helper);
}

