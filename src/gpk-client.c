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

/**
 * SECTION:gpk-client
 * @short_description: GObject class for libpackagekit-gnome client access
 *
 * A nice GObject to use for installing software in GNOME applications
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gconf/gconf-client.h>
#include <polkit-gnome/polkit-gnome.h>
#include <pk-debug.h>
#include <pk-client.h>
#include <pk-package-id.h>
#include <pk-common.h>
#include <pk-control.h>

#include <gpk-client.h>
#include <gpk-client-eula.h>
#include <gpk-client-signature.h>
#include <gpk-client-untrusted.h>
#include <gpk-client-chooser.h>
#include <gpk-client-resolve.h>
#include <gpk-client-depends.h>
#include <gpk-client-requires.h>
#include <gpk-common.h>
#include <gpk-gnome.h>
#include <gpk-error.h>
#include "gpk-notify.h"
#include "gpk-consolekit.h"
#include "gpk-animated-icon.h"

static void     gpk_client_class_init	(GpkClientClass *klass);
static void     gpk_client_init		(GpkClient      *gclient);
static void     gpk_client_finalize	(GObject	*object);

#define GPK_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_CLIENT, GpkClientPrivate))
#define PK_STOCK_WINDOW_ICON		"system-software-installer"
#define GPK_CLIENT_FINISHED_AUTOCLOSE_DELAY	10 /* seconds */
/**
 * GpkClientPrivate:
 *
 * Private #GpkClient data
 **/
struct _GpkClientPrivate
{
	PkClient		*client_action;
	PkClient		*client_resolve;
	PkClient		*client_secondary;
	GpkNotify		*notify;
	GladeXML		*glade_xml;
	GConfClient		*gconf_client;
	guint			 pulse_timer_id;
	guint			 finished_timer_id;
	PkControl		*control;
	PkRoleEnum		 roles;
	gboolean		 using_secondary_client;
	gboolean		 retry_untrusted_value;
	gboolean		 show_finished;
	gboolean		 show_progress;
	gboolean 		 finished_okay;
};

typedef enum {
	GPK_CLIENT_PAGE_PROGRESS,
	GPK_CLIENT_PAGE_CONFIRM,
	GPK_CLIENT_PAGE_LAST
} GpkClientPageEnum;

enum {
	GPK_CLIENT_QUIT,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkClient, gpk_client, G_TYPE_OBJECT)

/**
 * gpk_client_error_quark:
 *
 * Return value: Our personal error quark.
 **/
GQuark
gpk_client_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark) {
		quark = g_quark_from_static_string ("gpk_client_error");
	}
	return quark;
}

/**
 * gpk_client_error_get_type:
 **/
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
gpk_client_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (GPK_CLIENT_ERROR_FAILED, "Failed"),
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("PkClientError", values);
	}
	return etype;
}

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

	if (!gclient->priv->show_progress) {
		widget = glade_xml_get_widget (gclient->priv->glade_xml, "window_updates");
		gtk_widget_hide (widget);
		return;
	}

	widget = glade_xml_get_widget (gclient->priv->glade_xml, "window_updates");
	gtk_widget_show (widget);

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
 * gpk_client_updates_button_close_cb:
 **/
static void
gpk_client_updates_button_close_cb (GtkWidget *widget_button, GpkClient *gclient)
{
	GtkWidget *widget;
	g_return_if_fail (GPK_IS_CLIENT (gclient));

	/* stop the timer */
	if (gclient->priv->finished_timer_id != 0) {
		g_source_remove (gclient->priv->finished_timer_id);
		gclient->priv->finished_timer_id = 0;
	}

	/* go! */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "window_updates");
	gtk_widget_hide (widget);
	g_signal_emit (gclient, signals [GPK_CLIENT_QUIT], 0);
}

/**
 * gpk_client_updates_window_delete_event_cb:
 **/
static gboolean
gpk_client_updates_window_delete_event_cb (GtkWidget *widget, GdkEvent *event, GpkClient *gclient)
{
	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* stop the timer */
	if (gclient->priv->finished_timer_id != 0) {
		g_source_remove (gclient->priv->finished_timer_id);
		gclient->priv->finished_timer_id = 0;
	}

	/* go! */
	gtk_widget_hide (widget);
	gtk_main_quit ();
	g_signal_emit (gclient, signals [GPK_CLIENT_QUIT], 0);
	return FALSE;
}

/**
 * gpk_install_finished_timeout:
 **/
static gboolean
gpk_install_finished_timeout (gpointer data)
{
	GtkWidget *widget;
	GpkClient *gclient = (GpkClient *) data;

	/* hide window manually to get it out of the way */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "window_updates");
	gtk_widget_hide (widget);

	/* the timer will be done */
	gclient->priv->finished_timer_id = 0;

	gtk_main_quit ();
	g_signal_emit (gclient, signals [GPK_CLIENT_QUIT], 0);
	return FALSE;
}

/**
 * gpk_client_show_finished:
 **/
void
gpk_client_show_finished (GpkClient *gclient, gboolean enabled)
{
	g_return_if_fail (GPK_IS_CLIENT (gclient));
	gclient->priv->show_finished = enabled;
}

/**
 * gpk_client_show_progress:
 **/
void
gpk_client_show_progress (GpkClient *gclient, gboolean enabled)
{
	g_return_if_fail (GPK_IS_CLIENT (gclient));
	gclient->priv->show_progress = enabled;
}

/**
 * gpk_client_finished_no_progress:
 **/
static void
gpk_client_finished_no_progress (PkClient *client, PkExitEnum exit_code, guint runtime, GpkClient *gclient)
{
	PkRestartEnum restart;
	guint i;
	guint length;
	PkPackageId *ident;
	PkPackageItem *item;
	GString *message_text;
	guint skipped_number = 0;
	const gchar *message;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	/* check we got some packages */
	length = pk_client_package_buffer_get_size (client);
	pk_debug ("length=%i", length);
	if (length == 0) {
		pk_debug ("no updates");
		return;
	}

	message_text = g_string_new ("");

	/* find any we skipped */
	for (i=0; i<length; i++) {
		item = pk_client_package_buffer_get_item (client, i);
		pk_debug ("%s, %s, %s", pk_info_enum_to_text (item->info),
			  item->package_id, item->summary);
		ident = pk_package_id_new_from_string (item->package_id);
		if (item->info == PK_INFO_ENUM_BLOCKED) {
			skipped_number++;
			g_string_append_printf (message_text, "<b>%s</b> - %s\n",
						ident->name, item->summary);
		}
		pk_package_id_free (ident);
	}

	/* notify the user if there were skipped entries */
	if (skipped_number > 0) {
		message = ngettext (_("One package was skipped:"),
				    _("Some packages were skipped:"), skipped_number);
		g_string_prepend (message_text, message);
		g_string_append_c (message_text, '\n');
	}

	/* add a message that we need to restart */
	restart = pk_client_get_require_restart (client);
	if (restart != PK_RESTART_ENUM_NONE) {
		message = gpk_restart_enum_to_localised_text (restart);

		/* add a gap if we are putting both */
		if (skipped_number > 0) {
			g_string_append (message_text, "\n");
		}

		g_string_append (message_text, message);
		g_string_append_c (message_text, '\n');
	}

	/* trim off extra newlines */
	if (message_text->len != 0) {
		g_string_set_size (message_text, message_text->len-1);
	}

	/* this will not show if specified in gconf */
	gpk_notify_create (gclient->priv->notify,
			   _("The system update has completed"), message_text->str,
			   "software-update-available",
			   GPK_NOTIFY_URGENCY_LOW, GPK_NOTIFY_TIMEOUT_LONG);
	if (restart == PK_RESTART_ENUM_SYSTEM) {
		gpk_notify_button (gclient->priv->notify, GPK_NOTIFY_BUTTON_RESTART_COMPUTER, NULL);
		gpk_notify_button (gclient->priv->notify, GPK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN,
					      GPK_CONF_NOTIFY_UPDATE_COMPLETE_RESTART);
	} else {
		gpk_notify_button (gclient->priv->notify, GPK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN,
					      GPK_CONF_NOTIFY_UPDATE_COMPLETE);
	}
	gpk_notify_show (gclient->priv->notify);
	g_string_free (message_text, TRUE);
}

/**
 * gpk_client_finished_cb:
 **/
static void
gpk_client_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, GpkClient *gclient)
{
	GtkWidget *widget;
	PkRoleEnum role;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	/* save this */
	gclient->priv->finished_okay = (exit == PK_EXIT_ENUM_SUCCESS);

	pk_client_get_role (client, &role, NULL, NULL);
	/* do nothing */
	if (role == PK_ROLE_ENUM_GET_UPDATES) {
		goto out;
	}

	/* do we show a libnotify window instead? */
	if (!gclient->priv->show_progress) {
		gpk_client_finished_no_progress (client, exit, runtime, gclient);
		goto out;
	}

	if (exit == PK_EXIT_ENUM_SUCCESS &&
	    gclient->priv->show_finished) {
		gpk_client_set_page (gclient, GPK_CLIENT_PAGE_CONFIRM);

		widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_close2");
		gtk_widget_grab_default (widget);
		gclient->priv->finished_timer_id = g_timeout_add_seconds (GPK_CLIENT_FINISHED_AUTOCLOSE_DELAY,
									  gpk_install_finished_timeout, gclient);
	} else {
		widget = glade_xml_get_widget (gclient->priv->glade_xml, "window_updates");
		gtk_widget_hide (widget);
	}

	/* make insensitive */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_cancel");
	gtk_widget_set_sensitive (widget, FALSE);

	/* set to 100% */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "progressbar_percent");
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), 1.0f);
out:
	/* only quit if there is not another transaction scheduled to be finished */
	if (!gclient->priv->using_secondary_client) {
		pk_debug ("quitting");
		gtk_main_quit ();
	}
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
	if (gclient->priv->pulse_timer_id != 0) {
		g_source_remove (gclient->priv->pulse_timer_id);
		gclient->priv->pulse_timer_id = 0;
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

	/* set icon */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "image_status");
	gpk_set_animated_icon_from_status (GPK_ANIMATED_ICON (widget), status, GTK_ICON_SIZE_DIALOG);
	gtk_widget_show (widget);

	/* set label */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "progress_part_label");
	text = g_strdup_printf ("<b>%s</b>", gpk_status_enum_to_localised_text (status));
	gtk_label_set_markup (GTK_LABEL (widget), text);
	g_free (text);

	if (status == PK_STATUS_ENUM_WAIT) {
		if (gclient->priv->pulse_timer_id == 0) {
			widget = glade_xml_get_widget (gclient->priv->glade_xml, "progressbar_percent");

			gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (widget ), 0.04);
			gclient->priv->pulse_timer_id = g_timeout_add (75, (GSourceFunc) gpk_client_pulse_progress, gclient);
		}
	}
}

/**
 * gpk_client_error_code_cb:
 **/
static void
gpk_client_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, GpkClient *gclient)
{
	const gchar *title;
	const gchar *message;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	/* have we handled? */
	if (code == PK_ERROR_ENUM_GPG_FAILURE ||
	    code == PK_ERROR_ENUM_NO_LICENSE_AGREEMENT) {
		if (gclient->priv->using_secondary_client) {
			pk_debug ("ignoring error as handled");
			return;
		}
		pk_warning ("did not auth");
	}

	/* have we handled? */
	if (code == PK_ERROR_ENUM_BAD_GPG_SIGNATURE ||
	    code == PK_ERROR_ENUM_MISSING_GPG_SIGNATURE) {
		pk_debug ("handle and requeue");
		gclient->priv->retry_untrusted_value = gpk_client_untrusted_show (code);
		return;
	}

	/* ignore some errors */
	if (code == PK_ERROR_ENUM_PROCESS_KILL ||
	    code == PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
		pk_debug ("error ignored %s\n%s", pk_error_enum_to_text (code), details);
		return;
	}

	pk_debug ("code was %s", pk_error_enum_to_text (code));

	/* use a modal dialog if showing progress, else use libnotify */
	title = gpk_error_enum_to_localised_text (code);
	message = gpk_error_enum_to_localised_message (code);
	if (gclient->priv->show_progress) {
		gpk_error_dialog (title, message, details);
	} else {
		/* this will not show if specified in gconf */
		gpk_notify_create (gclient->priv->notify, title, message, "help-browser",
				   GPK_NOTIFY_URGENCY_LOW, GPK_NOTIFY_TIMEOUT_LONG);
		gpk_notify_button (gclient->priv->notify,
					      GPK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN, GPK_CONF_NOTIFY_ERROR);
		gpk_notify_show (gclient->priv->notify);
	}

}

/**
 * gpk_client_package_cb:
 **/
static void
gpk_client_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id,
		      const gchar *summary, GpkClient *gclient)
{
	gchar *text;
	GtkWidget *widget;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	text = gpk_package_id_format_twoline (package_id, summary);
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "label_package");
	gtk_widget_show (widget);
	gtk_label_set_markup (GTK_LABEL (widget), text);
	g_free (text);
}

/**
 * gpk_client_allow_cancel_cb:
 **/
static void
gpk_client_allow_cancel_cb (PkClient *client, gboolean allow_cancel, GpkClient *gclient)
{
	GtkWidget *widget;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_cancel");
	gtk_widget_set_sensitive (widget, allow_cancel);
}

/**
 * gpk_client_button_help_cb:
 **/
static void
gpk_client_button_help_cb (GtkWidget *widget, GpkClient *gclient)
{
	g_return_if_fail (GPK_IS_CLIENT (gclient));
	gpk_gnome_help (NULL);
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
 * gpk_client_error_msg:
 **/
static void
gpk_client_error_msg (GpkClient *gclient, const gchar *title, const gchar *message)
{
	GtkWidget *widget;
	GtkWidget *dialog;

	/* hide the main window */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "window_updates");
	gtk_widget_hide (widget);

	dialog = gtk_message_dialog_new (GTK_WINDOW (widget), GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE, "%s", title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

/**
 * gpk_client_error_set:
 *
 * Sets the correct error code (if allowed) and print to the screen
 * as a warning.
 **/
static gboolean
gpk_client_error_set (GError **error, gint code, const gchar *format, ...)
{
	va_list args;
	gchar *buffer = NULL;
	gboolean ret = TRUE;

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	/* dumb */
	if (error == NULL) {
		pk_warning ("No error set, so can't set: %s", buffer);
		ret = FALSE;
		goto out;
	}

	/* already set */
	if (*error != NULL) {
		pk_warning ("not NULL error!");
		g_clear_error (error);
	}

	/* propogate */
	g_set_error (error, GPK_CLIENT_ERROR, code, "%s", buffer);

out:
	g_free(buffer);
	return ret;
}

/**
 * gpk_client_install_local_files_internal:
 **/
static gboolean
gpk_client_install_local_files_internal (GpkClient *gclient, gboolean trusted,
					 gchar **files_rel, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *text;
	guint length;
	const gchar *title;

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset client"), _("Failed to reset resolve"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		return FALSE;
	}

	/* install local file */
	ret = pk_client_install_files (gclient->priv->client_action, trusted, files_rel, &error_local);
	if (ret) {
		return TRUE;
	}

	/* check if we got a permission denied */
	if (g_str_has_prefix (error_local->message, "org.freedesktop.packagekit.")) {
		gpk_client_error_msg (gclient, _("Failed to install files"),
				      _("You don't have the necessary privileges to install local files"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
	} else {
		text = g_markup_escape_text (error_local->message, -1);
		length = g_strv_length (files_rel);
		title = ngettext ("Failed to install file", "Failed to install files", length);
		gpk_client_error_msg (gclient, title, text);
		g_free (text);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
	}
	g_error_free (error_local);
	return FALSE;
}

/**
 * gpk_client_done:
 **/
static void
gpk_client_done (GpkClient *gclient)
{
	/* we're done */
	if (gclient->priv->pulse_timer_id != 0) {
		g_source_remove (gclient->priv->pulse_timer_id);
		gclient->priv->pulse_timer_id = 0;
	}
}

/**
 * gpk_client_setup_window:
 **/
static gboolean
gpk_client_setup_window (GpkClient *gclient, const gchar *title)
{
	GtkRequisition requisition;
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* set title */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "window_updates");
	gtk_window_set_title (GTK_WINDOW (widget), title);

	/* clear status and progress text */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "progress_part_label");
	gtk_label_set_label (GTK_LABEL (widget), "");
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "label_package");
	gtk_label_set_label (GTK_LABEL (widget), "The Linux kernel (the core of the Linux operating system)\n\n\n");
	gtk_widget_show (widget);

	/* set the correct height of the label to stop the window jumping around */
	gtk_widget_size_request (widget, &requisition);
	gtk_widget_set_size_request (widget, requisition.width * 1.1f, requisition.height);
	gtk_label_set_label (GTK_LABEL (widget), "");

	return TRUE;
}

/**
 * gpk_client_install_local_file:
 * @gclient: a valid #GpkClient instance
 * @file_rel: a file such as <literal>./hal-devel-0.10.0.rpm</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a file locally, and get the deps from the repositories.
 * This is useful for double clicking on a .rpm or .deb file.
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_install_local_files (GpkClient *gclient, gchar **files_rel, GError **error)
{
	GtkWidget *dialog;
	GtkResponseType button;
	const gchar *title;
	gchar *message;
	guint length;
	gboolean ret;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (files_rel != NULL, FALSE);

	length = g_strv_length (files_rel);
	title = ngettext (_("Do you want to install this file?"),
			  _("Do you want to install these files?"), length);
	message = g_strjoinv ("\n", files_rel);

	/* show UI */
	dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
					 "%s", title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);

	button = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
	g_free (message);

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK) {
		title = ngettext (_("The file was not installed"),
				  _("The files were not installed"), length);
		dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
						 "%s", title);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (GTK_WIDGET (dialog));
		ret = FALSE;
		goto out;
	}

	gclient->priv->retry_untrusted_value = FALSE;
	ret = gpk_client_install_local_files_internal (gclient, TRUE, files_rel, error);
	if (!ret) {
		goto out;
	}

	/* set title */
	gpk_client_setup_window (gclient, _("Install local file"));

	/* setup the UI */
	gpk_client_set_page (gclient, GPK_CLIENT_PAGE_PROGRESS);

	/* wait for completion */
	gtk_main ();

	/* do we need to try again with better auth? */
	if (gclient->priv->retry_untrusted_value) {
		ret = gpk_client_install_local_files_internal (gclient, FALSE, files_rel, error);
		if (!ret) {
			goto out;
		}
		/* wait again */
		gtk_main ();
	}

	/* retval depends on SUCCESS */
	ret = gclient->priv->finished_okay;

	/* we're done */
	gpk_client_done (gclient);
out:
	return ret;
}

/**
 * gpk_client_remove_package_ids:
 * @gclient: a valid #GpkClient instance
 * @package_id: a package_id such as <literal>hal-info;0.20;i386;fedora</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_remove_package_ids (GpkClient *gclient, gchar **package_ids, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *text = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* set title */
	gpk_client_setup_window (gclient, _("Remove packages"));

	/* are we dumb and can't check for depends? */
	if (!pk_enums_contain (gclient->priv->roles, PK_ROLE_ENUM_GET_REQUIRES)) {
		pk_warning ("skipping depends check");
		goto skip_checks;
	}

	ret = gpk_client_depends_show (package_ids);
	/* did we click no or exit the window? */
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to remove package"), _("Additional packages were also not removed"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "user did not agree to additional requires");
		ret = FALSE;
		goto out;
	}

skip_checks:
	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset client"), _("Failed to reset resolve"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		ret = FALSE;
		goto out;
	}

	/* try to remove the package_ids */
	ret = pk_client_remove_packages (gclient->priv->client_action, package_ids, TRUE, FALSE, &error_local);
	if (!ret) {
		/* check if we got a permission denied */
		if (g_str_has_prefix (error_local->message, "org.freedesktop.packagekit.")) {
			gpk_client_error_msg (gclient, _("Failed to remove package"),
						_("You don't have the necessary privileges to remove packages"));
			gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		} else {
			text = g_markup_escape_text (error_local->message, -1);
			gpk_client_error_msg (gclient, _("Failed to remove package"), text);
			gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
			g_free (text);
		}
		g_error_free (error_local);
		goto out;
	}

	/* setup the UI */
	gpk_client_set_page (gclient, GPK_CLIENT_PAGE_PROGRESS);

	/* wait for completion */
	gtk_main ();

	/* retval depends on SUCCESS */
	ret = gclient->priv->finished_okay;

	/* we're done */
	gpk_client_done (gclient);
out:
	return ret;
}

/**
 * gpk_client_install_package_ids:
 * @gclient: a valid #GpkClient instance
 * @package_id: a package_id such as <literal>hal-info;0.20;i386;fedora</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_install_package_ids (GpkClient *gclient, gchar **package_ids, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *text;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* set title */
	gpk_client_setup_window (gclient, _("Install packages"));

	/* setup the UI */
	gpk_client_set_page (gclient, GPK_CLIENT_PAGE_PROGRESS);

	/* are we dumb and can't check for depends? */
	if (!pk_enums_contain (gclient->priv->roles, PK_ROLE_ENUM_GET_DEPENDS)) {
		pk_warning ("skipping depends check");
		goto skip_checks;
	}

	ret = gpk_client_depends_show (package_ids);
	/* did we click no or exit the window? */
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to install package"), _("Additional packages were not downloaded"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "user did not agree to additional deps");
		ret = FALSE;
		goto out;
	}

skip_checks:
	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset client"), _("Failed to reset resolve"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		return FALSE;
	}

	/* try to install the package_id */
	ret = pk_client_install_packages (gclient->priv->client_action, package_ids, &error_local);
	if (!ret) {
		/* check if we got a permission denied */
		if (g_str_has_prefix (error_local->message, "org.freedesktop.packagekit.")) {
			gpk_client_error_msg (gclient, _("Failed to install package"),
						_("You don't have the necessary privileges to install packages"));
			gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		} else {
			text = g_markup_escape_text (error_local->message, -1);
			gpk_client_error_msg (gclient, _("Failed to install package"), text);
			gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
			g_free (text);
		}
		g_error_free (error_local);
		goto out;
	}

	/* wait for completion */
	gtk_main ();

	/* retval depends on SUCCESS */
	ret = gclient->priv->finished_okay;

	/* we're done */
	gpk_client_done (gclient);
out:
	return ret;
}

/**
 * gpk_client_install_package_name:
 * @gclient: a valid #GpkClient instance
 * @package: a pakage name such as <literal>hal-info</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a package of the newest and most correct version.
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_install_package_names (GpkClient *gclient, gchar **packages, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar **package_ids = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (packages != NULL, FALSE);

	/* resolve a 2D array to package_id's */
	package_ids = gpk_client_resolve_show (packages);
	if (package_ids == NULL) {
		ret = FALSE;
		goto out;
	}

	/* install these packages */
	ret = gpk_client_install_package_ids (gclient, package_ids, &error_local);
	if (!ret) {
		/* copy error message */
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		goto out;
	}

out:
	if (error_local != NULL) {
		g_error_free (error_local);
	}
	g_strfreev (package_ids);
	return ret;
}

/**
 * gpk_client_install_provide_file:
 * @gclient: a valid #GpkClient instance
 * @full_path: a file path name such as <literal>/usr/sbin/packagekitd</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a package which provides a file on the system.
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_install_provide_file (GpkClient *gclient, const gchar *full_path, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	guint len;
	guint i;
	gboolean already_installed = FALSE;
	gchar *package_id = NULL;
	PkPackageItem *item;
	PkPackageId *ident;
	gchar **package_ids = NULL;
	gchar *text;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (full_path != NULL, FALSE);

	ret = pk_client_search_file (gclient->priv->client_resolve, PK_FILTER_ENUM_NONE, full_path, &error_local);
	if (!ret) {
		text = g_strdup_printf ("%s: %s", _("Incorrect response from search"), error_local->message);
		gpk_client_error_msg (gclient, _("Failed to search for file"), text);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_free (text);
		ret = FALSE;
		goto out;
	}

	/* found nothing? */
	len = pk_client_package_buffer_get_size	(gclient->priv->client_resolve);
	if (len == 0) {
		gpk_client_error_msg (gclient, _("Failed to find package"), _("The file could not be found in any packages"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, NULL);
		ret = FALSE;
		goto out;
	}

	/* see what we've got already */
	for (i=0; i<len; i++) {
		item = pk_client_package_buffer_get_item (gclient->priv->client_resolve, i);
		if (item->info == PK_INFO_ENUM_INSTALLED) {
			already_installed = TRUE;
			g_free (package_id);
			package_id = g_strdup (item->package_id);
			break;
		} else if (item->info == PK_INFO_ENUM_AVAILABLE) {
			pk_debug ("package '%s' resolved to:", item->package_id);
			package_id = g_strdup (item->package_id);
		}
	}

	/* already installed? */
	if (already_installed) {
		ident = pk_package_id_new_from_string (package_id);
		text = g_strdup_printf (_("The %s package already provides the file %s"), ident->name, full_path);
		gpk_client_error_msg (gclient, _("Failed to install file"), text);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_free (text);
		pk_package_id_free (ident);
		ret = FALSE;
		goto out;
	}

	/* got junk? */
	if (package_id == NULL) {
		gpk_client_error_msg (gclient, _("Failed to install file"), _("Incorrect response from file search"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		ret = FALSE;
		goto out;
	}

	/* install this specific package */
	package_ids = g_strsplit (package_id, "|", 1);
	ret = gpk_client_install_package_ids (gclient, package_ids, error);
out:
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * gpk_client_install_mime_type:
 * @gclient: a valid #GpkClient instance
 * @mime_type: a mime_type such as <literal>application/text</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a application to handle a mime type
 *
 * Return value: %TRUE if the method succeeded
 **/
gboolean
gpk_client_install_mime_type (GpkClient *gclient, const gchar *mime_type, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *package_id = NULL;
	gchar **package_ids = NULL;
	gchar *text;
	guint len;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);
	g_return_val_if_fail (mime_type != NULL, FALSE);

	ret = pk_client_what_provides (gclient->priv->client_resolve, PK_FILTER_ENUM_NOT_INSTALLED,
				       PK_PROVIDES_ENUM_MIMETYPE, mime_type, &error_local);
	if (!ret) {
		text = g_strdup_printf ("%s: %s", _("Incorrect response from search"), error_local->message);
		gpk_client_error_msg (gclient, _("Failed to search for provides"), text);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_free (text);
		ret = FALSE;
		goto out;
	}

	/* found nothing? */
	len = pk_client_package_buffer_get_size	(gclient->priv->client_resolve);
	if (len == 0) {
		gpk_client_error_msg (gclient, _("Failed to find software"),
				      _("No new applications can be found to handle this type of file"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, NULL);
		ret = FALSE;
		goto out;
	}

	/* populate a chooser */
	package_id = gpk_client_chooser_show (gclient->priv->client_resolve, 0, _("Applications that can open this type of file"));

	/* selected nothing */
	if (package_id == NULL) {
		gpk_client_error_msg (gclient, _("Failed to install software"), _("No spplications were chosen to be installed"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, "user chose nothing");
		ret = FALSE;
		goto out;
	}

	/* install this specific package */
	package_ids = g_strsplit (package_id, "|", 1);
	ret = gpk_client_install_package_ids (gclient, package_ids, error);
out:
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * gpk_client_update_system:
 **/
gboolean
gpk_client_update_system (GpkClient *gclient, GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	gchar *text = NULL;
	gchar *message = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset client"), _("Failed to reset resolve"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		goto out;
	}

	/* set title */
	gpk_client_setup_window (gclient, _("System update"));

	/* wrap update, but handle all the GPG and EULA stuff */
	ret = pk_client_update_system (gclient->priv->client_action, &error_local);
	if (!ret) {
		/* print a proper error if we have it */
		if (error_local->code == PK_CLIENT_ERROR_FAILED_AUTH) {
			message = g_strdup (_("Authorisation could not be obtained"));
		} else {
			message = g_strdup_printf (_("The error was: %s"), error_local->message);
		}

		/* display and set */
		text = g_strdup_printf ("%s: %s", _("Failed to update system"), message);
		gpk_client_error_msg (gclient, _("Failed to update system"), text);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, message);
		goto out;
	}

	/* setup the UI */
	gpk_client_set_page (gclient, GPK_CLIENT_PAGE_PROGRESS);

	/* if we are not showing UI, then notify the user what we are doing (just on the active terminal) */
	if (!gclient->priv->show_progress) {
		/* this will not show if specified in gconf */
		gpk_notify_create (gclient->priv->notify,
				   _("Updates are being installed"),
				   _("Updates are being automatically installed on your computer"),
				   "software-update-urgent",
				   GPK_NOTIFY_URGENCY_LOW, GPK_NOTIFY_TIMEOUT_LONG);
		gpk_notify_button (gclient->priv->notify, GPK_NOTIFY_BUTTON_CANCEL_UPDATE, NULL);
		gpk_notify_button (gclient->priv->notify, GPK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN,
					      GPK_CONF_NOTIFY_UPDATE_STARTED);
		gpk_notify_show (gclient->priv->notify);
	}

	/* wait for completion */
	gtk_main ();

	/* retval depends on SUCCESS */
	ret = gclient->priv->finished_okay;

out:
	if (error_local != NULL) {
		g_error_free (error_local);
	}
	g_free (message);
	g_free (text);
	return ret;
}

/**
 * gpk_client_refresh_cache:
 **/
gboolean
gpk_client_refresh_cache (GpkClient *gclient, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *text = NULL;
	gchar *message = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset client"), _("Failed to reset resolve"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		goto out;
	}

	/* set title */
	gpk_client_setup_window (gclient, _("Refresh package lists"));

	/* wrap update, but handle all the GPG and EULA stuff */
	ret = pk_client_refresh_cache (gclient->priv->client_action, TRUE, &error_local);
	if (!ret) {
		/* print a proper error if we have it */
		if (error_local->code == PK_CLIENT_ERROR_FAILED_AUTH) {
			message = g_strdup (_("Authorisation could not be obtained"));
		} else {
			message = g_strdup_printf (_("The error was: %s"), error_local->message);
		}

		/* display and set */
		text = g_strdup_printf ("%s: %s", _("Failed to update package lists"), message);
		gpk_client_error_msg (gclient, _("Failed to update package lists"), text);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, message);
		goto out;
	}

	/* setup the UI */
	gpk_client_set_page (gclient, GPK_CLIENT_PAGE_PROGRESS);

	/* wait for completion */
	gtk_main ();

	/* retval depends on SUCCESS */
	ret = gclient->priv->finished_okay;

out:
	if (error_local != NULL) {
		g_error_free (error_local);
	}
	g_free (message);
	g_free (text);
	return ret;
}

/**
 * gpk_client_get_updates:
 **/
PkPackageList *
gpk_client_get_updates (GpkClient *gclient, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	PkPackageList *list = NULL;
	PkPackageItem *item;
	guint length;
	guint i;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset client"), _("Failed to reset get-updates"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		return FALSE;
	}

	/* set title */
	gpk_client_setup_window (gclient, _("Getting update lists"));

	/* wrap update, but handle all the GPG and EULA stuff */
	ret = pk_client_get_updates (gclient->priv->client_action, PK_FILTER_ENUM_NONE, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Getting update lists failed"), _("Failed to get updates"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		goto out;
	}

	/* setup the UI */
	gpk_client_set_page (gclient, GPK_CLIENT_PAGE_PROGRESS);

	/* wait for completion */
	gtk_main ();

	/* copy from client to local */
	list = pk_package_list_new ();
	length = pk_client_package_buffer_get_size (gclient->priv->client_action);
	for (i=0;i<length;i++) {
		item = pk_client_package_buffer_get_item (gclient->priv->client_action, i);
		pk_package_list_add (list, item->info, item->package_id, item->summary);
	}
out:
	return list;
}

/**
 * gpk_client_update_packages:
 **/
gboolean
gpk_client_update_packages (GpkClient *gclient, gchar **package_ids, GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	gchar *text = NULL;
	gchar *message = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* reset */
	ret = pk_client_reset (gclient->priv->client_action, &error_local);
	if (!ret) {
		gpk_client_error_msg (gclient, _("Failed to reset client"), _("Failed to reset resolve"));
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, error_local->message);
		goto out;
	}

	/* set title */
	gpk_client_setup_window (gclient, _("Update packages"));

	/* wrap update, but handle all the GPG and EULA stuff */
	ret = pk_client_update_packages (gclient->priv->client_action, package_ids, &error_local);
	if (!ret) {
		/* print a proper error if we have it */
		if (error_local->code == PK_CLIENT_ERROR_FAILED_AUTH) {
			message = g_strdup (_("Authorisation could not be obtained"));
		} else {
			message = g_strdup_printf (_("The error was: %s"), error_local->message);
		}

		/* display and set */
		text = g_strdup_printf ("%s: %s", _("Failed to update packages"), message);
		gpk_client_error_msg (gclient, _("Failed to update packages"), text);
		gpk_client_error_set (error, GPK_CLIENT_ERROR_FAILED, message);
		goto out;
	}

	/* setup the UI */
	gpk_client_set_page (gclient, GPK_CLIENT_PAGE_PROGRESS);

	/* wait for completion */
	gtk_main ();

	/* retval depends on SUCCESS */
	ret = gclient->priv->finished_okay;

out:
	if (error_local != NULL) {
		g_error_free (error_local);
	}
	g_free (message);
	g_free (text);
	return ret;
}

/**
 * gpk_client_repo_signature_required_cb:
 **/
static void
gpk_client_repo_signature_required_cb (PkClient *client, const gchar *package_id, const gchar *repository_name,
				       const gchar *key_url, const gchar *key_userid, const gchar *key_id,
				       const gchar *key_fingerprint, const gchar *key_timestamp,
				       PkSigTypeEnum type, GpkClient *gclient)
{
	gboolean ret;
	GError *error = NULL;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	ret = gpk_client_signature_show (package_id, repository_name, key_url, key_userid,
					 key_id, key_fingerprint, key_timestamp);
	/* disagreed with auth */
	if (!ret) {
		return;
	}

	/* install signature */
	pk_debug ("install signature %s", key_id);
	ret = pk_client_reset (gclient->priv->client_secondary, &error);
	if (!ret) {
		gpk_error_dialog (_("Failed to install signature"), _("The client could not be reset"), error->message);
		g_error_free (error);
		return;
	}
	/* this is asynchronous, else we get into livelock */
	ret = pk_client_install_signature (gclient->priv->client_secondary, PK_SIGTYPE_ENUM_GPG,
					   key_id, package_id, &error);
	gclient->priv->using_secondary_client = ret;
	if (!ret) {
		gpk_error_dialog (_("Failed to install signature"), _("The method failed"), error->message);
		g_error_free (error);
	}
}

/**
 * gpk_client_eula_required_cb:
 **/
static void
gpk_client_eula_required_cb (PkClient *client, const gchar *eula_id, const gchar *package_id,
			     const gchar *vendor_name, const gchar *license_agreement, GpkClient *gclient)
{
	gboolean ret;
	GError *error = NULL;

	/* do a helper */
	ret = gpk_client_eula_show (eula_id, package_id, vendor_name, license_agreement);

	/* disagreed with auth */
	if (!ret) {
		return;
	}

	/* install signature */
	pk_debug ("accept EULA %s", eula_id);
	ret = pk_client_reset (gclient->priv->client_secondary, &error);
	if (!ret) {
		gpk_error_dialog (_("Failed to accept EULA"), _("The client could not be reset"), error->message);
		g_error_free (error);
		return;
	}

	/* this is asynchronous, else we get into livelock */
	ret = pk_client_accept_eula (gclient->priv->client_secondary, eula_id, &error);
	if (!ret) {
		gpk_error_dialog (_("Failed to accept EULA"), _("The method failed"), error->message);
		g_error_free (error);
	}
	gclient->priv->using_secondary_client = ret;
}

/**
 * gpk_client_secondary_now_requeue:
 **/
static gboolean
gpk_client_secondary_now_requeue (GpkClient *gclient)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	/* go back to the UI */
	gpk_client_set_page (gclient, GPK_CLIENT_PAGE_PROGRESS);
	gclient->priv->using_secondary_client = FALSE;

	pk_debug ("trying to requeue install");
	ret = pk_client_requeue (gclient->priv->client_action, &error);
	if (!ret) {
		gpk_error_dialog (_("Failed to install"), _("The install task could not be requeued"), error->message);
		g_error_free (error);
	}

	return FALSE;
}

/**
 * gpk_client_secondary_finished_cb:
 **/
static void
gpk_client_secondary_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, GpkClient *gclient)
{
	g_return_if_fail (GPK_IS_CLIENT (gclient));
	/* we have to do this idle add, else we get into deadlock */
	g_idle_add ((GSourceFunc) gpk_client_secondary_now_requeue, gclient);
}

/**
 * gpk_client_notify_button_cb:
 **/
static void
gpk_client_notify_button_cb (GpkNotify *notify, GpkNotifyButton button,
			     const gchar *data, GpkClient *gclient)
{
	gboolean ret;

	g_return_if_fail (GPK_IS_CLIENT (gclient));

	pk_debug ("got: %i with data %s", button, data);
	/* find the localised text */
	if (button == GPK_NOTIFY_BUTTON_DO_NOT_SHOW_AGAIN ||
	    button == GPK_NOTIFY_BUTTON_DO_NOT_WARN_AGAIN) {
		if (data == NULL) {
			pk_warning ("data NULL");
		} else {
			pk_debug ("setting %s to FALSE", data);
			gconf_client_set_bool (gclient->priv->gconf_client, data, FALSE, NULL);
		}
	} else if (button == GPK_NOTIFY_BUTTON_CANCEL_UPDATE) {
		pk_client_button_cancel_cb (NULL, gclient);
	} else if (button == GPK_NOTIFY_BUTTON_RESTART_COMPUTER) {
		/* restart using gnome-power-manager */
		ret = gpk_restart_system ();
		if (!ret) {
			pk_warning ("failed to reboot");
		}
	}
}

/**
 * pk_common_get_role_text:
 **/
static gchar *
pk_common_get_role_text (PkClient *client)
{
	const gchar *role_text;
	gchar *package_id;
	gchar *text;
	gchar *package;
	PkRoleEnum role;
	GError *error = NULL;
	gboolean ret;

	/* get role and text */
	ret = pk_client_get_role (client, &role, &package_id, &error);
	if (!ret) {
		pk_warning ("failed to get role: %s", error->message);
		g_error_free (error);
		return NULL;
	}

	/* backup */
	role_text = gpk_role_enum_to_localised_present (role);

	if (!pk_strzero (package_id) && role != PK_ROLE_ENUM_UPDATE_PACKAGES) {
		package = gpk_package_get_name (package_id);
		text = g_strdup_printf ("%s: %s", role_text, package);
		g_free (package);
	} else {
		text = g_strdup_printf ("%s", role_text);
	}
	g_free (package_id);

	return text;
}

/**
 * gpk_client_monitor_tid:
 **/
gboolean
gpk_client_monitor_tid (GpkClient *gclient, const gchar *tid)
{
	GtkWidget *widget;
	PkStatusEnum status;
	gboolean ret;
	gboolean allow_cancel;
	gchar *text;
	guint percentage;
	guint subpercentage;
	guint elapsed;
	guint remaining;
	GError *error = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT (gclient), FALSE);

	ret = pk_client_set_tid (gclient->priv->client_action, tid, &error);
	if (!ret) {
		pk_warning ("could not set tid: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* fill in role */
	text = pk_common_get_role_text (gclient->priv->client_action);
	gpk_client_setup_window (gclient, text);
	g_free (text);

	/* coldplug */
	ret = pk_client_get_status (gclient->priv->client_action, &status, NULL);
	/* no such transaction? */
	if (!ret) {
		pk_warning ("could not get status");
		return FALSE;
	}

	/* are we cancellable? */
	pk_client_get_allow_cancel (gclient->priv->client_action, &allow_cancel, NULL);
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_cancel");
	gtk_widget_set_sensitive (widget, allow_cancel);

	gpk_client_status_changed_cb (gclient->priv->client_action, status, gclient);

	/* coldplug */
	ret = pk_client_get_progress (gclient->priv->client_action,
				      &percentage, &subpercentage, &elapsed, &remaining, NULL);
	if (ret) {
		gpk_client_progress_changed_cb (gclient->priv->client_action, percentage,
						subpercentage, elapsed, remaining, gclient);
	} else {
		pk_warning ("GetProgress failed");
		gpk_client_progress_changed_cb (gclient->priv->client_action,
						PK_CLIENT_PERCENTAGE_INVALID,
						PK_CLIENT_PERCENTAGE_INVALID, 0, 0, gclient);
	}

	/* do the best we can */
	ret = pk_client_get_package (gclient->priv->client_action, &text, NULL);
	if (ret) {
		gpk_client_package_cb (gclient->priv->client_action, 0, text, NULL, gclient);
	}

	gpk_client_set_page (gclient, GPK_CLIENT_PAGE_PROGRESS);

	/* wait for completion */
	gtk_main ();

	return TRUE;
}

/**
 * gpk_client_create_custom_widget:
 **/
static GtkWidget *
gpk_client_create_custom_widget (GladeXML *xml, gchar *func_name, gchar *name,
				 gchar *string1, gchar *string2,
				 gint int1, gint int2, gpointer user_data)
{
	if (pk_strequal (name, "image_status")) {
		return gpk_animated_icon_new ();
	}
	pk_warning ("name unknown=%s", name);
	return NULL;
}

/**
 * gpk_client_class_init:
 * @klass: The #GpkClientClass
 **/
static void
gpk_client_class_init (GpkClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_client_finalize;
	g_type_class_add_private (klass, sizeof (GpkClientPrivate));
	signals [GPK_CLIENT_QUIT] =
		g_signal_new ("quit",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * gpk_client_init:
 * @gclient: a valid #GpkClient instance
 **/
static void
gpk_client_init (GpkClient *gclient)
{
	GtkWidget *widget;

	gclient->priv = GPK_CLIENT_GET_PRIVATE (gclient);

	gclient->priv->glade_xml = NULL;
	gclient->priv->pulse_timer_id = 0;
	gclient->priv->using_secondary_client = FALSE;
	gclient->priv->finished_okay = TRUE;
	gclient->priv->show_finished = TRUE;
	gclient->priv->show_progress = TRUE;
	gclient->priv->finished_timer_id = 0;

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   PK_DATA G_DIR_SEPARATOR_S "icons");

	/* use custom widgets */
	glade_set_custom_handler (gpk_client_create_custom_widget, gclient);

	/* use gconf for session settings */
	gclient->priv->gconf_client = gconf_client_get_default ();

	/* get actions */
	gclient->priv->control = pk_control_new ();
	gclient->priv->roles = pk_control_get_actions (gclient->priv->control);

	gclient->priv->client_action = pk_client_new ();
	pk_client_set_use_buffer (gclient->priv->client_action, TRUE, NULL);
	g_signal_connect (gclient->priv->client_action, "finished",
			  G_CALLBACK (gpk_client_finished_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "progress-changed",
			  G_CALLBACK (gpk_client_progress_changed_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "status-changed",
			  G_CALLBACK (gpk_client_status_changed_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "error-code",
			  G_CALLBACK (gpk_client_error_code_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "package",
			  G_CALLBACK (gpk_client_package_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "allow-cancel",
			  G_CALLBACK (gpk_client_allow_cancel_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "repo-signature-required",
			  G_CALLBACK (gpk_client_repo_signature_required_cb), gclient);
	g_signal_connect (gclient->priv->client_action, "eula-required",
			  G_CALLBACK (gpk_client_eula_required_cb), gclient);

	gclient->priv->notify = gpk_notify_new ();
	g_signal_connect (gclient->priv->notify, "notification-button",
			  G_CALLBACK (gpk_client_notify_button_cb), gclient);

	gclient->priv->client_resolve = pk_client_new ();
	g_signal_connect (gclient->priv->client_resolve, "status-changed",
			  G_CALLBACK (gpk_client_status_changed_cb), gclient);
	pk_client_set_use_buffer (gclient->priv->client_resolve, TRUE, NULL);
	pk_client_set_synchronous (gclient->priv->client_resolve, TRUE, NULL);

	/* this is asynchronous, else we get into livelock */
	gclient->priv->client_secondary = pk_client_new ();
	g_signal_connect (gclient->priv->client_secondary, "finished",
			  G_CALLBACK (gpk_client_secondary_finished_cb), gclient);

	gclient->priv->glade_xml = glade_xml_new (PK_DATA "/gpk-client.glade", NULL, NULL);

	/* common stuff */
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "window_updates");
	g_signal_connect (widget, "delete_event", G_CALLBACK (gpk_client_updates_window_delete_event_cb), gclient);
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_close");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_updates_button_close_cb), gclient);
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_close2");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_updates_button_close_cb), gclient);
	widget = glade_xml_get_widget (gclient->priv->glade_xml, "button_close3");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_client_updates_button_close_cb), gclient);

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

	/* stop the timer if running */
	if (gclient->priv->finished_timer_id != 0) {
		g_source_remove (gclient->priv->finished_timer_id);
	}

	g_object_unref (gclient->priv->client_action);
	g_object_unref (gclient->priv->client_resolve);
	g_object_unref (gclient->priv->client_secondary);
	g_object_unref (gclient->priv->control);
	g_object_unref (gclient->priv->gconf_client);
	g_object_unref (gclient->priv->notify);

	G_OBJECT_CLASS (gpk_client_parent_class)->finalize (object);
}

/**
 * gpk_client_new:
 *
 * PkClient is a nice GObject wrapper for gnome-packagekit and makes installing software easy
 *
 * Return value: A new %GpkClient instance
 **/
GpkClient *
gpk_client_new (void)
{
	GpkClient *gclient;
	gclient = g_object_new (GPK_TYPE_CLIENT, NULL);
	return GPK_CLIENT (gclient);
}

