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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <pk-debug.h>
#include <pk-client.h>

#include "gpk-client.h"
#include "gpk-common.h"

static void     gpk_client_class_init	(GpkClientClass *klass);
static void     gpk_client_init		(GpkClient      *gclient);
static void     gpk_client_finalize	(GObject	*object);

#define GPK_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_CLIENT, GpkClientPrivate))

struct GpkClientPrivate
{
	PkClient		*client_action;
	PkClient		*client_resolve;
	GladeXML		*glade_xml;
	gint			 pulse_timeout;
};

typedef enum {
	GPK_CLIENT_PAGE_PROGRESS,
	GPK_CLIENT_PAGE_CONFIRM,
	GPK_CLIENT_PAGE_ERROR,
	GPK_CLIENT_PAGE_LAST
} GpkClientPageEnum;

G_DEFINE_TYPE (GpkClient, gpk_client, G_TYPE_OBJECT)

/**
 * gpk_client_set_page:
 **/
static void
gpk_client_set_page (GpkClient *gclient, GpkClientPageEnum page)
{
	GList *list, *l;
	GtkWidget *widget;
	guint i;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	widget = glade_xml_get_widget (gclient->priv->glade_xml, "hbox_hidden");
	list = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l=list, i=0; l; l=l->next, i++) {
		if (i == page) {
			gtk_widget_show (l->data);
		} else {
			gtk_widget_hide (l->data);
		}
	}
}

/**
 * gpk_install_finished_timeout:
 **/
static gboolean
gpk_install_finished_timeout (gpointer data)
{
	gtk_main_quit ();
	return FALSE;
}

/**
 * gpk_client_finished_cb:
 **/
static void
gpk_client_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, GpkClient *gclient)
{
	GtkWidget *widget;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	if (exit == PK_EXIT_ENUM_SUCCESS) {
		gpk_client_set_page (gclient, GPK_CLIENT_PAGE_CONFIRM);
		g_timeout_add_seconds (30, gpk_install_finished_timeout, gclient);
	}
	/* make insensitive */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_cancel");
	gtk_widget_set_sensitive (widget, FALSE);

	/* set to 100% */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "progressbar_percent");
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), 1.0f);
}

/**
 * gpk_client_progress_changed_cb:
 **/
static void
gpk_client_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				guint elapsed, guint remaining, GpkClient *gclient)
{
	GtkWidget *widget;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	widget = glade_xml_get_widget (gclient->priv->glade_xml, "progressbar_percent");
	if (gclient->priv->pulse_timeout != 0) {
		g_source_remove (gclient->priv->pulse_timeout);
		gclient->priv->pulse_timeout = 0;
	}

	if (percentage != PK_CLIENT_PERCENTAGE_INVALID) {
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);
	}
}

/**
 * gpk_client_pulse_progress:
 **/
static gboolean
gpk_client_pulse_progress (GpkClient *gclient)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	widget = glade_xml_get_widget (gclient->priv->glade_xml, "progressbar_percent");
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (widget));
	return TRUE;
}

/**
 * gpk_client_status_changed_cb:
 **/
static void
gpk_client_status_changed_cb (PkClient *client, PkStatusEnum status, GpkClient *gclient)
{
	GtkWidget *widget;
	gchar *text;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	widget = glade_xml_get_widget (gclient->priv->glade_xml, "progress_part_label");
	text = g_strdup_printf ("<b>%s</b>", gpk_status_enum_to_localised_text (status));
	gtk_label_set_markup (GTK_LABEL (widget), text);
	g_free (text);

	if (status == PK_STATUS_ENUM_WAIT) {
		if (gclient->priv->pulse_timeout == 0) {
			widget = glade_xml_get_widget (gclient->priv->glade_xml, "progressbar_percent");

			gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (widget ), 0.04);
			gclient->priv->pulse_timeout = g_timeout_add (75, (GSourceFunc) gpk_client_pulse_progress, gclient);
		}
	}
}

/**
 * gpk_client_error_code_cb:
 **/
static void
gpk_client_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, GpkClient *gclient)
{
	GtkWidget *widget;
	const gchar *title;
	gchar *title_bold;
	gchar *details_safe;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	gpk_client_set_page (gclient, GPK_CLIENT_PAGE_ERROR);

	/* set bold title */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "label_error_title");
	title = gpk_error_enum_to_localised_text (code);
	title_bold = g_strdup_printf ("<b>%s</b>", title);
	gtk_label_set_label (GTK_LABEL (widget), title_bold);
	g_free (title_bold);

	widget = glade_xml_get_widget (gclient->priv->glade_xml, "label_error_message");
	gtk_label_set_label (GTK_LABEL (widget), gpk_error_enum_to_localised_message (code));

	widget = glade_xml_get_widget (gclient->priv->glade_xml, "label_error_details");
	details_safe = g_markup_escape_text (details, -1);
	gtk_label_set_label (GTK_LABEL (widget), details_safe);
	g_free (details_safe);
}

/**
 * pk_client_package_cb:
 **/
static void
pk_client_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id,
		      const gchar *summary, GpkClient *gclient)
{
	gchar *text;
	GtkWidget *widget;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	text = gpk_package_id_pretty (package_id, summary);
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "label_package");
	gtk_label_set_markup (GTK_LABEL (widget), text);
	g_free (text);
}

/**
 * pk_client_allow_cancel_cb:
 **/
static void
pk_client_allow_cancel_cb (PkClient *client, gboolean allow_cancel, GpkClient *gclient)
{
	GtkWidget *widget;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_cancel");
	gtk_widget_set_sensitive (widget, allow_cancel);
}

/**
 * gpk_client_window_delete_event_cb:
 **/
static gboolean
gpk_client_window_delete_event_cb (GtkWidget *widget, GdkEvent *event, GpkClient *gclient)
{
	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	gtk_main_quit ();
	return FALSE;
}

/**
 * gpk_client_button_close_cb:
 **/
static void
gpk_client_button_close_cb (GtkWidget *widget, GpkClient *gclient)
{
	g_return_if_fail (GPK_IS_CLIENT (gclient));
	gtk_main_quit ();
}

/**
 * gpk_client_button_help_cb:
 **/
static void
gpk_client_button_help_cb (GtkWidget *widget, GpkClient *gclient)
{
	g_return_if_fail (GPK_IS_CLIENT (gclient));
	gpk_show_help (NULL);
}

/**
 * pk_client_button_cancel_cb:
 **/
static void
pk_client_button_cancel_cb (GtkWidget *widget, GpkClient *gclient)
{
	gboolean ret;
	GError *error = NULL;

	/* we might have a transaction running */
	ret = pk_client_cancel (gclient->priv->client_action, &error);
	if (!ret) {
		pk_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_client_error_message:
 **/
static void
gpk_client_error_msg (GpkClient *gclient, const gchar *title, const gchar *message)
{
	GtkWidget *widget;

	/* hide the main window */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "window_updates");
	gtk_widget_hide (widget);

	/* do a modal error */
	gpk_error_modal_dialog (title, message);
}

/**
 * gpk_client_install_file:
 * @gclient: a valid #GpkClient instance
 * @file: a file such as "./hal-devel-0.10.0.rpm"
 *
 * Install a file locally, and get the deps from the repositories.
 * This is useful for double clicking on a .rpm or .deb file.
 *
 * Return value: %TRUE if the method is running
 **/
gboolean
gpk_client_install_file (GpkClient *gclient, const gchar *file_rel)
{
	gboolean ret;
	GError *error = NULL;
	gchar *text;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (file_rel != NULL, FALSE);

	ret = pk_client_install_file (gclient->priv->client_action, file_rel, &error);
	if (!ret) {
		/* check if we got a permission denied */
		if (g_str_has_prefix (error->message, "org.freedesktop.packagekit.")) {
			gpk_client_error_msg (gclient, _("Failed to install file"),
					      _("You don't have the necessary privileges to install local files"));
		} else {
			text = g_markup_escape_text (error->message, -1);
			gpk_client_error_msg (gclient, _("Failed to install file"), text);
			g_free (text);
		}
		g_error_free (error);
		goto out;
	}

	/* wait for completion */
	gtk_main ();

	/* we're done */
	if (gclient->priv->pulse_timeout != 0) {
		g_source_remove (gclient->priv->pulse_timeout);
	}
out:
	return ret;
}

/**
 * gpk_client_install_package:
 * @gclient: a valid #GpkClient instance
 * @package: a file such as hal-info
 *
 * Install a package of the newest and most correct version.
 *
 * Return value: %TRUE if the method is running
 **/
gboolean
gpk_client_install_package (GpkClient *gclient, const gchar *package)
{
	gboolean ret;
	GError *error = NULL;
	gchar *text;
	guint len;
	guint i;
	PkPackageItem *item;
	gboolean already_installed = FALSE;
	gchar *package_id = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (package != NULL, FALSE);

	ret = pk_client_resolve (gclient->priv->client_resolve, "none", package, &error);
	if (!ret) {
		/* check if we got a permission denied */
		if (g_str_has_prefix (error->message, "org.freedesktop.packagekit.")) {
			gpk_client_error_msg (gclient, _("Failed to install package"),
					        _("You don't have the necessary privileges to install packages"));
		} else {
			text = g_markup_escape_text (error->message, -1);
			gpk_client_error_msg (gclient, _("Failed to install package"), text);
			g_free (text);
		}
		ret = FALSE;
		goto out;
	}

	/* found nothing? */
	len = pk_client_package_buffer_get_size	(gclient->priv->client_resolve);
	if (len == 0) {
		gpk_client_error_msg (gclient, _("Failed to find package"), _("The package could not be found online"));
		ret = FALSE;
		goto out;
	}

	/* see what we've got already */
	for (i=0; i<len; i++) {
		item = pk_client_package_buffer_get_item (gclient->priv->client_resolve, i);
		if (item->info == PK_INFO_ENUM_INSTALLED) {
			already_installed = TRUE;
			break;
		} else if (item->info == PK_INFO_ENUM_AVAILABLE) {
			pk_debug ("package '%s' resolved", item->package_id);
			package_id = g_strdup (item->package_id);
			break;
		}
	}

	/* already installed? */
	if (already_installed) {
		gpk_client_error_msg (gclient, _("Failed to install package"), _("The package is already installed"));
		ret = FALSE;
		goto out;
	}

	/* got junk? */
	if (package_id == NULL) {
		gpk_client_error_msg (gclient, _("Failed to find package"), _("Incorrect response from search"));
		ret = FALSE;
		goto out;
	}

	/* try to install out file */
	ret = pk_client_install_package (gclient->priv->client_action, package_id, &error);
	if (!ret) {
		g_error_free (error);
		goto out;
	}

	/* wait for completion */
	gtk_main ();

	/* we're done */
	if (gclient->priv->pulse_timeout != 0) {
		g_source_remove (gclient->priv->pulse_timeout);
	}
out:
	g_free (package_id);
	return ret;
}

/**
 * gpk_client_class_init:
 * @klass: The GpkClientClass
 **/
static void
gpk_client_class_init (GpkClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_client_finalize;
	g_type_class_add_private (klass, sizeof (GpkClientPrivate));
}

/**
 * gpk_client_init:
 * @client: This class instance
 **/
static void
gpk_client_init (GpkClient *gclient)
{
	GtkWidget *widget;

	gclient->priv = GPK_CLIENT_GET_PRIVATE (gclient);

	gclient->priv->glade_xml = NULL;
	gclient->priv->pulse_timeout = 0;

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   PK_DATA G_DIR_SEPARATOR_S "icons");

	gclient->priv->client_action = pk_client_new ();
	g_signal_connect (gclient->priv->client_action, "finished",
			  G_CALLBACK (gpk_client_finished_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "progress-changed",
			  G_CALLBACK (gpk_client_progress_changed_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "status-changed",
			  G_CALLBACK (gpk_client_status_changed_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "error-code",
			  G_CALLBACK (gpk_client_error_code_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "package",
			  G_CALLBACK (pk_client_package_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "allow-cancel",
			  G_CALLBACK (pk_client_allow_cancel_cb), gclient);

	gclient->priv->client_resolve = pk_client_new ();
	g_signal_connect (gclient->priv->client_resolve, "status-changed",
			  G_CALLBACK (gpk_client_status_changed_cb), gclient);
	pk_client_set_use_buffer (gclient->priv->client_resolve, TRUE, NULL);
	pk_client_set_synchronous (gclient->priv->client_resolve, TRUE, NULL);

	gclient->priv->glade_xml = glade_xml_new (PK_DATA "/gpk-install-file.glade", NULL, NULL);
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "window_updates");

	/* Get the main window quit */
	g_signal_connect (widget, "delete_event",
			  G_CALLBACK (gpk_client_window_delete_event_cb), gclient);

	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_client_button_close_cb), gclient);
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_close2");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_client_button_close_cb), gclient);
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_close3");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_client_button_close_cb), gclient);

	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_cancel");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_client_button_cancel_cb), gclient);
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_help3");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_client_button_help_cb), gclient);
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_help4");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_client_button_help_cb), gclient);
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_help5");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_client_button_help_cb), gclient);

	/* set the label blank initially */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "progress_part_label");
	gtk_label_set_label (GTK_LABEL (widget), "");

	gpk_client_set_page (gclient, GPK_CLIENT_PAGE_PROGRESS);
}

/**
 * gpk_client_finalize:
 * @object: The object to finalize
 **/
static void
gpk_client_finalize (GObject *object)
{
	GpkClient *gclient;

	g_return_if_fail (GPK_IS_CLIENT (object));

	gclient = GPK_CLIENT (object);
	g_return_if_fail (gclient->priv != NULL);
	g_object_unref (gclient->priv->client_action);
	g_object_unref (gclient->priv->client_resolve);

	G_OBJECT_CLASS (gpk_client_parent_class)->finalize (object);
}

/**
 * gpk_client_new:
 *
 * Return value: a new GpkClient object.
 **/
GpkClient *
gpk_client_new (void)
{
	GpkClient *gclient;
	gclient = g_object_new (GPK_TYPE_CLIENT, NULL);
	return GPK_CLIENT (gclient);
}

