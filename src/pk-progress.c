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

#include <glib.h>
#include <glib/gi18n.h>

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

#include <pk-debug.h>
#include <pk-task-monitor.h>
#include <pk-connection.h>
#include <pk-package-id.h>

#include "pk-common.h"
#include "pk-progress.h"

static void     pk_progress_class_init (PkProgressClass *klass);
static void     pk_progress_init       (PkProgress      *progress);
static void     pk_progress_finalize   (GObject	    *object);

#define PK_PROGRESS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PROGRESS, PkProgressPrivate))

struct PkProgressPrivate
{
	GladeXML		*glade_xml;
	PkTaskMonitor		*tmonitor;
	guint			 job;
	gboolean		 task_ended;
	guint			 no_percentage_evt;
};

enum {
	ACTION_UNREF,
	ACTION_CLOSE,
	LAST_SIGNAL
};

static guint	     signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkProgress, pk_progress, G_TYPE_OBJECT)

/**
 * pk_progress_class_init:
 * @klass: This graph class instance
 **/
static void
pk_progress_class_init (PkProgressClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_progress_finalize;
	g_type_class_add_private (klass, sizeof (PkProgressPrivate));

	signals [ACTION_UNREF] =
		g_signal_new ("action-unref",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * pk_progress_error_code_cb:
 **/
static void
pk_progress_error_message (PkProgress *progress, const gchar *title, const gchar *details)
{
	GtkWidget *main_window;
	GtkWidget *dialog;

	pk_warning ("error %s:%s", title, details);
	main_window = glade_xml_get_widget (progress->priv->glade_xml, "window_progress");

	dialog = gtk_message_dialog_new (GTK_WINDOW (main_window), GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), details);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

/**
 * pk_progress_clean_exit:
 **/
static void
pk_progress_clean_exit (PkProgress *progress)
{
	/* remove the back and forth progress bar update */
	if (progress->priv->no_percentage_evt != 0) {
		g_source_remove (progress->priv->no_percentage_evt);
		progress->priv->no_percentage_evt = 0;
	}
	g_signal_emit (progress, signals [ACTION_UNREF], 0);
}

/**
 * pk_progress_help_cb:
 **/
static void
pk_progress_help_cb (GtkWidget  *widget,
		     PkProgress *progress)
{
	pk_debug ("emitting help");
	pk_progress_clean_exit (progress);
}

/**
 * pk_progress_cancel_cb:
 **/
static void
pk_progress_cancel_cb (GtkWidget  *widget,
		       PkProgress *progress)
{
	pk_debug ("emitting cancel");
	pk_task_monitor_cancel (progress->priv->tmonitor);
}

/**
 * pk_progress_hide_cb:
 **/
static void
pk_progress_hide_cb (GtkWidget   *widget,
		     PkProgress  *progress)
{
	pk_debug ("hide");
	pk_progress_clean_exit (progress);
}

/**
 * pk_progress_error_code_cb:
 **/
static void
pk_progress_error_code_cb (PkTaskMonitor *tmonitor, PkErrorCodeEnum code, const gchar *details, PkProgress *progress)
{
	pk_progress_error_message (progress, pk_error_enum_to_localised_text (code), details);
}

/**
 * pk_progress_finished_timeout:
 **/
gboolean
pk_progress_finished_timeout (gpointer data)
{
	PkProgress *progress = PK_PROGRESS (data);
	pk_debug ("emit unref");
	pk_progress_clean_exit (progress);
	return FALSE;
}

/**
 * pk_progress_finished_cb:
 **/
static void
pk_progress_finished_cb (PkTaskMonitor *tmonitor, PkStatusEnum status, guint runtime, PkProgress *progress)
{
	pk_debug ("finished");
	progress->priv->task_ended = TRUE;
	/* wait 5 seconds */
	progress->priv->no_percentage_evt = g_timeout_add (5000, pk_progress_finished_timeout, progress);
}

/**
 * pk_progress_package_cb:
 */
static void
pk_progress_package_cb (PkTaskMonitor *tmonitor,
			guint          value,
			const gchar   *package_id,
			const gchar   *summary,
			PkProgress    *progress)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (progress->priv->glade_xml, "label_package");

	/* prefer the proper description, but fall back to the package_id name */
	if (summary != NULL) {
		gtk_label_set_label (GTK_LABEL (widget), summary);
	} else {
		PkPackageId *ident;
		ident = pk_package_id_new_from_string (package_id);
		gtk_label_set_label (GTK_LABEL (widget), ident->name);
		pk_package_id_free (ident);
	}

	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_status");
	gtk_widget_show (widget);
}

/**
 * pk_progress_percentage_changed_cb:
 **/
static void
pk_progress_percentage_changed_cb (PkTaskMonitor *tmonitor, guint percentage, PkProgress *progress)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_percentage");
	gtk_widget_show (widget);
	widget = glade_xml_get_widget (progress->priv->glade_xml, "progressbar_percentage");
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);
	gtk_widget_show (widget);
}

/**
 * pk_progress_sub_percentage_changed_cb:
 **/
static void
pk_progress_sub_percentage_changed_cb (PkTaskMonitor *tmonitor, guint percentage, PkProgress *progress)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_subpercentage");
	gtk_widget_show (widget);
	widget = glade_xml_get_widget (progress->priv->glade_xml, "progressbar_subpercentage");
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);
	gtk_widget_show (widget);
}

/**
 * pk_progress_job_status_changed_cb:
 */
static void
pk_progress_job_status_changed_cb (PkTaskMonitor *tmonitor,
				   PkStatusEnum   status,
				   PkProgress    *progress)
{
	GtkWidget *widget;
	const gchar *icon_name;

	widget = glade_xml_get_widget (progress->priv->glade_xml, "label_status");
	gtk_label_set_label (GTK_LABEL (widget), pk_status_enum_to_localised_text (status));

	widget = glade_xml_get_widget (progress->priv->glade_xml, "image_status");
	icon_name = pk_status_enum_to_icon_name (status);
	gtk_image_set_from_icon_name (GTK_IMAGE (widget), icon_name, GTK_ICON_SIZE_DIALOG);
}

/**
 * pk_progress_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
pk_progress_delete_event_cb (GtkWidget	*widget,
			     GdkEvent	*event,
			     PkProgress	*progress)
{
	pk_progress_clean_exit (progress);
	return FALSE;
}

/**
 * pk_common_get_role_text:
 **/
static gchar *
pk_common_get_role_text (PkTaskMonitor *tmonitor)
{
	const gchar *role_text;
	gchar *package_id;
	gchar *text;
	PkRoleEnum role;
	PkPackageId *ident;

	pk_task_monitor_get_role (tmonitor, &role, &package_id);
	role_text = pk_role_enum_to_localised_text (role);

	/* check to see if we have a package_id or just a search term */
	if (package_id == NULL || strlen (package_id) == 0) {
		text = g_strdup (role_text);
	} else if (pk_package_id_check (package_id) == FALSE) {
		text = g_strdup_printf ("%s: %s", role_text, package_id);
	} else {
		ident = pk_package_id_new_from_string (package_id);
		text = g_strdup_printf ("%s: %s", role_text, ident->name);
		pk_package_id_free (ident);
	}
	return text;
}

/**
 * pk_progress_spin_timeout:
 **/
gboolean
pk_progress_spin_timeout (gpointer data)
{
	gfloat fraction;
	GtkWidget *widget;
	PkProgress *progress = PK_PROGRESS (data);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "progressbar_percentage");
	fraction = gtk_progress_bar_get_fraction (GTK_PROGRESS_BAR (widget));
	fraction += 0.05;
	if (fraction > 1.00) {
		fraction = 0.0;
	}
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), fraction);

	/* show the box */
	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_percentage");
	gtk_widget_show (widget);

	if (progress->priv->task_ended == TRUE) {
		/* hide the box */
		widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_percentage");
		gtk_widget_hide (widget);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_progress_no_percentage_updates_cb:
 **/
static void
pk_progress_no_percentage_updates_cb (PkTaskMonitor *tmonitor, PkProgress *progress)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_percentage");
	gtk_widget_show (widget);
	//g_timeout_add (100, pk_progress_spin_timeout, progress);
}

/**
 * pk_progress_monitor_tid:
 **/
gboolean
pk_progress_monitor_tid (PkProgress *progress, const gchar *tid)
{
	GtkWidget *widget;
	PkStatusEnum status;
	gboolean ret;
	gchar *text;
	guint percentage;

	pk_task_monitor_set_tid (progress->priv->tmonitor, tid);

	/* fill in role */
	text = pk_common_get_role_text (progress->priv->tmonitor);
	widget = glade_xml_get_widget (progress->priv->glade_xml, "label_role");
	gtk_label_set_label (GTK_LABEL (widget), text);
	g_free (text);

	/* coldplug */
	ret = pk_task_monitor_get_status (progress->priv->tmonitor, &status);
	/* no such job? */
	if (ret == FALSE) {
		g_signal_emit (progress, signals [ACTION_UNREF], 0);
		return FALSE;
	}

	pk_progress_job_status_changed_cb (progress->priv->tmonitor, status, progress);

	ret = pk_task_monitor_get_percentage (progress->priv->tmonitor, &percentage);
	if (ret == TRUE) {
		pk_progress_percentage_changed_cb (progress->priv->tmonitor, percentage, progress);
	} else {
		/* We have to spin */
		progress->priv->no_percentage_evt = g_timeout_add (50, pk_progress_spin_timeout, progress);
	}

	/* no need to spin */
	ret = pk_task_monitor_get_sub_percentage (progress->priv->tmonitor, &percentage);
	if (ret == TRUE) {
		pk_progress_sub_percentage_changed_cb (progress->priv->tmonitor, percentage, progress);
	}

	/* do the best we can */
	ret = pk_task_monitor_get_package (progress->priv->tmonitor, &text);
	if (ret == TRUE) {
		pk_progress_package_cb (progress->priv->tmonitor, 0, text, NULL, progress);
	}

	widget = glade_xml_get_widget (progress->priv->glade_xml, "window_progress");
	gtk_widget_show (widget);
	return TRUE;
}

/**
 * pk_progress_init:
 **/
static void
pk_progress_init (PkProgress *progress)
{
	GtkWidget *main_window;
	GtkWidget *widget;

	progress->priv = PK_PROGRESS_GET_PRIVATE (progress);
	progress->priv->job = 0;
	progress->priv->task_ended = FALSE;
	progress->priv->no_percentage_evt = 0;

	progress->priv->tmonitor = pk_task_monitor_new ();
	g_signal_connect (progress->priv->tmonitor, "error-code",
			  G_CALLBACK (pk_progress_error_code_cb), progress);
	g_signal_connect (progress->priv->tmonitor, "finished",
			  G_CALLBACK (pk_progress_finished_cb), progress);
	g_signal_connect (progress->priv->tmonitor, "package",
			  G_CALLBACK (pk_progress_package_cb), progress);
	g_signal_connect (progress->priv->tmonitor, "no-percentage-updates",
			  G_CALLBACK (pk_progress_no_percentage_updates_cb), progress);
	g_signal_connect (progress->priv->tmonitor, "percentage-changed",
			  G_CALLBACK (pk_progress_percentage_changed_cb), progress);
	g_signal_connect (progress->priv->tmonitor, "sub-percentage-changed",
			  G_CALLBACK (pk_progress_sub_percentage_changed_cb), progress);
	g_signal_connect (progress->priv->tmonitor, "job-status-changed",
			  G_CALLBACK (pk_progress_job_status_changed_cb), progress);

	progress->priv->glade_xml = glade_xml_new (PK_DATA "/pk-progress.glade", NULL, NULL);
	main_window = glade_xml_get_widget (progress->priv->glade_xml, "window_progress");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), "system-installer");

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (pk_progress_delete_event_cb), progress);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_percentage");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_subpercentage");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_status");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_progress_help_cb), progress);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "button_cancel");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_progress_cancel_cb), progress);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "button_hide");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_progress_hide_cb), progress);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "button_help");
	gtk_widget_set_sensitive (widget, FALSE);
}

/**
 * pk_progress_finalize:
 * @object: This graph class instance
 **/
static void
pk_progress_finalize (GObject *object)
{
	PkProgress *progress;
	GtkWidget *widget;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_PROGRESS (object));

	progress = PK_PROGRESS (object);
	progress->priv = PK_PROGRESS_GET_PRIVATE (progress);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "window_progress");
	gtk_widget_hide (widget);

	g_object_unref (progress->priv->tmonitor);
	g_object_unref (progress->priv->glade_xml);

	G_OBJECT_CLASS (pk_progress_parent_class)->finalize (object);
}

/**
 * pk_progress_new:
 * Return value: new PkProgress instance.
 **/
PkProgress *
pk_progress_new (void)
{
	PkProgress *progress;
	progress = g_object_new (PK_TYPE_PROGRESS, NULL);
	return PK_PROGRESS (progress);
}

