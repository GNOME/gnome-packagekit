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

#include "gpk-helper-untrusted.h"
#include "gpk-marshal.h"
#include "gpk-gnome.h"
#include "gpk-common.h"
#include "gpk-enum.h"

#include "egg-debug.h"

static void     gpk_helper_untrusted_finalize	(GObject	  *object);

#define GPK_HELPER_UNTRUSTED_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_HELPER_UNTRUSTED, GpkHelperUntrustedPrivate))

struct GpkHelperUntrustedPrivate
{
	GtkBuilder		*builder;
};

enum {
	GPK_HELPER_UNTRUSTED_EVENT,
	GPK_HELPER_UNTRUSTED_LAST_SIGNAL
};

static guint signals [GPK_HELPER_UNTRUSTED_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkHelperUntrusted, gpk_helper_untrusted, G_TYPE_OBJECT)

/**
 * gpk_helper_untrusted_button_force_install_cb:
 **/
static void
gpk_helper_untrusted_button_force_install_cb (GtkWidget *widget, GpkHelperUntrusted *helper)
{
	g_signal_emit (helper, signals [GPK_HELPER_UNTRUSTED_EVENT], 0, GTK_RESPONSE_YES);
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_error"));
	gtk_widget_hide (widget);
}

/**
 * gpk_helper_untrusted_button_cancel_cb:
 **/
static void
gpk_helper_untrusted_button_cancel_cb (GtkWidget *widget, GpkHelperUntrusted *helper)
{
	g_signal_emit (helper, signals [GPK_HELPER_UNTRUSTED_EVENT], 0, GTK_RESPONSE_NO);
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_error"));
	gtk_widget_hide (widget);
}

/**
 * gpk_helper_untrusted_show:
 *
 * Return value: if we agreed
 **/
gboolean
gpk_helper_untrusted_show (GpkHelperUntrusted *helper, PkErrorCodeEnum code)
{
	GtkWidget *widget;
	gchar *text;
	const gchar *title;
	gchar *message;

	g_return_val_if_fail (GPK_IS_HELPER_UNTRUSTED (helper), FALSE);

	/* title */
	title = gpk_error_enum_to_localised_text (code);
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "label_title"));
	text = g_strdup_printf ("<b><big>%s</big></b>", title);
	gtk_label_set_label (GTK_LABEL (widget), text);
	g_free (text);

	/* message */
	message = g_strdup_printf ("%s\n%s\n\n%s\n%s",
				   /* TRANSLATORS: is not GPG signed */
				   _("The package is not signed by a trusted provider."),
				   /* TRANSLATORS: user has to trust provider -- I know, this sucks */
				   _("Do not install this package unless you are sure it is safe to do so."),
				   /* TRANSLATORS: warn the user that all bets are off */
				   _("Malicious software can damage your computer or cause other harm."),
				   /* TRANSLATORS: ask if they are absolutely sure they want to do this */
				   _("Are you <b>sure</b> you want to install this package?"));
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "label_message"));
	gtk_label_set_markup (GTK_LABEL (widget), message);
	g_free (message);

	/* show window */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_error"));
	gtk_widget_show (widget);

	return TRUE;
}

/**
 * gpk_helper_untrusted_set_parent:
 **/
gboolean
gpk_helper_untrusted_set_parent (GpkHelperUntrusted *helper, GtkWindow *window)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_HELPER_UNTRUSTED (helper), FALSE);
	g_return_val_if_fail (window != NULL, FALSE);

	/* make modal if window set */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_error"));
	gtk_window_set_transient_for (GTK_WINDOW (widget), window);

	/* this is a modal popup, so don't show a window title */
	gtk_window_set_title (GTK_WINDOW (widget), "");

	return TRUE;
}

/**
 * gpk_helper_untrusted_class_init:
 * @klass: The GpkHelperUntrustedClass
 **/
static void
gpk_helper_untrusted_class_init (GpkHelperUntrustedClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_helper_untrusted_finalize;
	g_type_class_add_private (klass, sizeof (GpkHelperUntrustedPrivate));
	signals [GPK_HELPER_UNTRUSTED_EVENT] =
		g_signal_new ("event",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkHelperUntrustedClass, event),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * gpk_helper_untrusted_init:
 **/
static void
gpk_helper_untrusted_init (GpkHelperUntrusted *helper)
{
	GtkWidget *widget;
	GtkWidget *button;
	guint retval;
	GError *error = NULL;

	helper->priv = GPK_HELPER_UNTRUSTED_GET_PRIVATE (helper);

	/* get UI */
	helper->priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (helper->priv->builder, GPK_DATA "/gpk-error.ui", &error);
	if (error != NULL) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
	}

	/* connect up default actions */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_error"));
	g_signal_connect (widget, "delete_event", G_CALLBACK (gpk_helper_untrusted_button_cancel_cb), helper);

	/* set icon name */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_error"));
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* connect up buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "button_close"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_helper_untrusted_button_cancel_cb), helper);

	/* don't show text in the expander */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "expander_details"));
	gtk_widget_hide (widget);

	/* TRANSLATORS: button label, force the install, even though it's untrusted */
	button = gtk_button_new_with_mnemonic (_("_Force install"));
	g_signal_connect (button, "clicked", G_CALLBACK (gpk_helper_untrusted_button_force_install_cb), helper);

	/* TRANSLATORS: button tooltip */
	gtk_widget_set_tooltip_text (button, _("Force installing package"));

	/* add to box */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_error"));
	widget = gtk_dialog_get_action_area (GTK_DIALOG(widget));
	gtk_box_pack_start (GTK_BOX (widget), button, FALSE, FALSE, 0);
	gtk_widget_show (button);
}

/**
 * gpk_helper_untrusted_finalize:
 **/
static void
gpk_helper_untrusted_finalize (GObject *object)
{
	GtkWidget *widget;
	GpkHelperUntrusted *helper;

	g_return_if_fail (GPK_IS_HELPER_UNTRUSTED (object));

	helper = GPK_HELPER_UNTRUSTED (object);

	/* hide window */
	widget = GTK_WIDGET (gtk_builder_get_object (helper->priv->builder, "dialog_error"));
	if (GTK_IS_WIDGET (widget))
		gtk_widget_hide (widget);
	g_object_unref (helper->priv->builder);

	G_OBJECT_CLASS (gpk_helper_untrusted_parent_class)->finalize (object);
}

/**
 * gpk_helper_untrusted_new:
 **/
GpkHelperUntrusted *
gpk_helper_untrusted_new (void)
{
	GpkHelperUntrusted *helper;
	helper = g_object_new (GPK_TYPE_HELPER_UNTRUSTED, NULL);
	return GPK_HELPER_UNTRUSTED (helper);
}

