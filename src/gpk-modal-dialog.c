/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <sys/wait.h>
#include <fcntl.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-string.h"

#include "gpk-animated-icon.h"
#include "gpk-modal-dialog.h"
#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-enum.h"

static void     gpk_modal_dialog_finalize	(GObject		*object);

#define GPK_MODAL_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_CLIENT_DIALOG, GpkModalDialogPrivate))

struct _GpkModalDialogPrivate
{
	GtkBuilder		*builder;
	guint			 pulse_timer_id;
	gboolean		 show_progress_files;
	gboolean		 has_parent;
	GMainLoop		*loop;
	GtkResponseType		 response;
	GtkListStore		*store;
	gchar			*title;
	gboolean		 set_image;
	GpkModalDialogPage	 page;
	PkBitfield		 options;
	GtkWidget		*image_status;
};

enum {
	GPK_MODAL_DIALOG_CLOSE,
	GPK_MODAL_DIALOG_QUIT,
	GPK_MODAL_DIALOG_ACTION,
	GPK_MODAL_DIALOG_CANCEL,
	LAST_SIGNAL
};

enum {
	GPK_MODAL_DIALOG_STORE_IMAGE,
	GPK_MODAL_DIALOG_STORE_ID,
	GPK_MODAL_DIALOG_STORE_TEXT,
	GPK_MODAL_DIALOG_STORE_LAST
};

static guint signals [LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkModalDialog, gpk_modal_dialog, G_TYPE_OBJECT)

/**
 * gpk_modal_dialog_show_widget:
 **/
static void
gpk_modal_dialog_show_widget (GpkModalDialog *dialog, const gchar *name, gboolean enabled)
{
	GtkWidget *widget;
	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, name));
	if (enabled)
		gtk_widget_show (widget);
	else
		gtk_widget_hide (widget);
}

/**
 * gpk_modal_dialog_setup:
 **/
gboolean
gpk_modal_dialog_setup (GpkModalDialog *dialog, GpkModalDialogPage page, PkBitfield options)
{
	GtkLabel *label;
	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	/* reset state */
	dialog->priv->set_image = FALSE;
	dialog->priv->page = page;
	dialog->priv->options = options;
	label = GTK_LABEL (gtk_builder_get_object (dialog->priv->builder, "label_message"));
	gtk_label_set_label (label, "");
	gpk_modal_dialog_set_action (dialog, NULL);
	return TRUE;
}

/**
 * gpk_modal_dialog_present_with_time:
 **/
gboolean
gpk_modal_dialog_present_with_time (GpkModalDialog *dialog, guint32 timestamp)
{
	GtkWidget *widget;
	PkBitfield bitfield = 0;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "label_title"));
	gtk_widget_show (widget);
	gtk_widget_show (dialog->priv->image_status);
	/* helper */
	if (dialog->priv->page == GPK_MODAL_DIALOG_PAGE_CONFIRM) {
		if (!dialog->priv->set_image)
			gpk_modal_dialog_set_image (dialog, "dialog-question");
		bitfield = pk_bitfield_from_enums (GPK_MODAL_DIALOG_WIDGET_BUTTON_CANCEL,
						   GPK_MODAL_DIALOG_WIDGET_BUTTON_ACTION,
						   GPK_MODAL_DIALOG_WIDGET_MESSAGE,
						   -1);
		gpk_modal_dialog_set_allow_cancel (dialog, TRUE);
	} else if (dialog->priv->page == GPK_MODAL_DIALOG_PAGE_FINISHED) {
		if (!dialog->priv->set_image)
			gpk_modal_dialog_set_image (dialog, "dialog-information");
		bitfield = pk_bitfield_from_enums (GPK_MODAL_DIALOG_WIDGET_BUTTON_CLOSE,
						   GPK_MODAL_DIALOG_WIDGET_MESSAGE,
						   -1);
	} else if (dialog->priv->page == GPK_MODAL_DIALOG_PAGE_CONFIRM) {
		if (!dialog->priv->set_image)
			gpk_modal_dialog_set_image (dialog, "dialog-question");
		bitfield = pk_bitfield_from_enums (GPK_MODAL_DIALOG_WIDGET_BUTTON_CLOSE,
						   GPK_MODAL_DIALOG_WIDGET_BUTTON_ACTION,
						   GPK_MODAL_DIALOG_WIDGET_MESSAGE,
						   -1);
	} else if (dialog->priv->page == GPK_MODAL_DIALOG_PAGE_WARNING) {
		if (!dialog->priv->set_image)
			gpk_modal_dialog_set_image (dialog, "dialog-warning");
		bitfield = pk_bitfield_from_enums (GPK_MODAL_DIALOG_WIDGET_BUTTON_CLOSE,
						   GPK_MODAL_DIALOG_WIDGET_MESSAGE,
						   -1);
	} else if (dialog->priv->page == GPK_MODAL_DIALOG_PAGE_PROGRESS) {
		if (!dialog->priv->set_image)
			gpk_modal_dialog_set_image (dialog, "dialog-warning");
		bitfield = pk_bitfield_from_enums (GPK_MODAL_DIALOG_WIDGET_BUTTON_CLOSE,
						   GPK_MODAL_DIALOG_WIDGET_BUTTON_CANCEL,
						   GPK_MODAL_DIALOG_WIDGET_PROGRESS_BAR,
						   -1);
	}

	/* we can specify extras */
	bitfield += dialog->priv->options;

	gpk_modal_dialog_show_widget (dialog, "button_cancel", pk_bitfield_contain (bitfield, GPK_MODAL_DIALOG_WIDGET_BUTTON_CANCEL));
	gpk_modal_dialog_show_widget (dialog, "button_close", pk_bitfield_contain (bitfield, GPK_MODAL_DIALOG_WIDGET_BUTTON_CLOSE));
	gpk_modal_dialog_show_widget (dialog, "button_action", pk_bitfield_contain (bitfield, GPK_MODAL_DIALOG_WIDGET_BUTTON_ACTION));
	gpk_modal_dialog_show_widget (dialog, "progressbar_percent", pk_bitfield_contain (bitfield, GPK_MODAL_DIALOG_WIDGET_PROGRESS_BAR));
	gpk_modal_dialog_show_widget (dialog, "label_message", pk_bitfield_contain (bitfield, GPK_MODAL_DIALOG_WIDGET_MESSAGE));
	gpk_modal_dialog_show_widget (dialog, "scrolledwindow_packages", pk_bitfield_contain (bitfield, GPK_MODAL_DIALOG_WIDGET_PACKAGE_LIST));
	gpk_modal_dialog_show_widget (dialog, "label_force_height", pk_bitfield_contain (bitfield, GPK_MODAL_DIALOG_WIDGET_PADDING));

	/* always force width */
	gpk_modal_dialog_show_widget (dialog, "label_force_width", TRUE);

	/* show */
	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "dialog_client"));
	gtk_widget_realize (widget);
	gtk_window_present_with_time (GTK_WINDOW (widget), timestamp);

	return TRUE;
}

/**
 * gpk_modal_dialog_present:
 **/
gboolean
gpk_modal_dialog_present (GpkModalDialog *dialog)
{
	return gpk_modal_dialog_present_with_time (dialog, 0);
}

/**
 * gpk_modal_dialog_set_parent:
 **/
gboolean
gpk_modal_dialog_set_parent (GpkModalDialog *dialog, GdkWindow *window)
{
	GtkWidget *widget;
	GdkWindow *window_ours;
	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	/* never set, and nothing now */
	if (window == NULL && !dialog->priv->has_parent)
		return TRUE;

	/* not sure what to do here, should probably unparent somehow */
	if (window == NULL) {
		g_warning ("parent set NULL when already modal with another window, setting non-modal");
		widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "dialog_client"));
		gtk_window_set_modal (GTK_WINDOW (widget), FALSE);
		dialog->priv->has_parent = FALSE;

		/* use the saved title if it exists */
		if (dialog->priv->title != NULL)
			gpk_modal_dialog_set_title (dialog, dialog->priv->title);

		return FALSE;
	}

	/* check we are a valid window */
	if (!GDK_WINDOW (window)) {
		g_warning ("not a valid GdkWindow!");
		return FALSE;
	}

	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "dialog_client"));
	gtk_widget_realize (widget);
	gtk_window_set_modal (GTK_WINDOW (widget), TRUE);
	window_ours = gtk_widget_get_window (widget);
	gdk_window_set_transient_for (window_ours, window);
	dialog->priv->has_parent = TRUE;
	return TRUE;
}

/**
 * gpk_modal_dialog_set_window_title:
 **/
static gboolean
gpk_modal_dialog_set_window_title (GpkModalDialog *dialog, const gchar *title)
{
	GtkWindow *window;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (title != NULL, FALSE);

	g_debug ("setting window title: %s", title);
	window = GTK_WINDOW (gtk_builder_get_object (dialog->priv->builder, "dialog_client"));
	gtk_window_set_title (window, title);
	return TRUE;
}

/**
 * gpk_modal_dialog_set_window_icon:
 **/
gboolean
gpk_modal_dialog_set_window_icon (GpkModalDialog *dialog, const gchar *icon)
{
	GtkWindow *window;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (icon != NULL, FALSE);

	g_debug ("setting window icon: %s", icon);
	window = GTK_WINDOW (gtk_builder_get_object (dialog->priv->builder, "dialog_client"));
	gtk_window_set_icon_name (window, icon);
	return TRUE;
}

/**
 * gpk_modal_dialog_set_title:
 **/
gboolean
gpk_modal_dialog_set_title (GpkModalDialog *dialog, const gchar *title)
{
	GtkLabel *label;
	GtkWidget *widget;
	gchar *title_bold;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (title != NULL, FALSE);

	/* only set the window title if we are non-modal */
	if (!dialog->priv->has_parent) {
		widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder,
							     "dialog_client"));
		gtk_window_set_modal (GTK_WINDOW (widget), FALSE);
	}

	/* we save this in case we are non-modal and have to use a title */
	g_free (dialog->priv->title);
	dialog->priv->title = g_strdup (title);

	title_bold = g_strdup_printf ("<b><big>%s</big></b>", title);
	g_debug ("setting title: %s", title_bold);
	label = GTK_LABEL (gtk_builder_get_object (dialog->priv->builder, "label_title"));
	gtk_label_set_markup (label, title_bold);
	g_free (title_bold);
	return TRUE;
}

/**
 * gpk_modal_dialog_set_message:
 **/
gboolean
gpk_modal_dialog_set_message (GpkModalDialog *dialog, const gchar *message)
{
	GtkLabel *label;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (message != NULL, FALSE);

	/* ignore this if it's uninteresting */
	if (!dialog->priv->show_progress_files)
		return FALSE;

	g_debug ("setting message: %s", message);
	label = GTK_LABEL (gtk_builder_get_object (dialog->priv->builder, "label_message"));
	gtk_label_set_markup (label, message);
	return TRUE;
}

/**
 * gpk_modal_dialog_set_action:
 **/
gboolean
gpk_modal_dialog_set_action (GpkModalDialog *dialog, const gchar *action)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	g_debug ("setting action: %s", action);
	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "button_action"));
	if (action != NULL)
		gtk_button_set_label (GTK_BUTTON (widget), action);
	else
		gtk_widget_hide (widget);
	return TRUE;
}

/**
 * gpk_modal_dialog_pulse_progress:
 **/
static gboolean
gpk_modal_dialog_pulse_progress (GpkModalDialog *dialog)
{
	GtkWidget *widget;
	static guint rate_limit = 0;
	
	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	/* debug so we can catch polling */
	if (rate_limit++ % 20 == 0)
		g_debug ("polling check");

	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "progressbar_percent"));
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (widget));
	return TRUE;
}

/**
 * gpk_modal_dialog_make_progressbar_pulse:
 **/
static void
gpk_modal_dialog_make_progressbar_pulse (GpkModalDialog *dialog)
{
	GtkProgressBar *progress_bar;
	if (dialog->priv->pulse_timer_id == 0) {
		progress_bar = GTK_PROGRESS_BAR (gtk_builder_get_object (dialog->priv->builder, "progressbar_percent"));
		gtk_progress_bar_set_pulse_step (progress_bar, 0.04);
		dialog->priv->pulse_timer_id = g_timeout_add (75, (GSourceFunc) gpk_modal_dialog_pulse_progress, dialog);
		g_source_set_name_by_id (dialog->priv->pulse_timer_id, "[GpkModalDialog] pulse");
	}
}

/**
 * gpk_modal_dialog_set_percentage:
 **/
gboolean
gpk_modal_dialog_set_percentage (GpkModalDialog *dialog, gint percentage)
{
	GtkProgressBar *progress_bar;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (percentage <= 100, FALSE);

	g_debug ("setting percentage: %u", percentage);

	progress_bar = GTK_PROGRESS_BAR (gtk_builder_get_object (dialog->priv->builder, "progressbar_percent"));
	if (dialog->priv->pulse_timer_id != 0) {
		g_source_remove (dialog->priv->pulse_timer_id);
		dialog->priv->pulse_timer_id = 0;
	}

	/* either pulse or set percentage */
	if (percentage < 0)
		gpk_modal_dialog_make_progressbar_pulse (dialog);
	else
		gtk_progress_bar_set_fraction (progress_bar, (gfloat) percentage / 100.0);
	return TRUE;
}

/**
 * gpk_modal_dialog_set_remaining:
 **/
gboolean
gpk_modal_dialog_set_remaining (GpkModalDialog *dialog, guint remaining)
{
	GtkProgressBar *progress_bar;
	gchar *timestring = NULL;
	gchar *text = NULL;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	g_debug ("setting remaining: %u", remaining);
	progress_bar = GTK_PROGRESS_BAR (gtk_builder_get_object (dialog->priv->builder, "progressbar_percent"));

	/* unknown */
	if (remaining == 0) {
		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (progress_bar), "");
		goto out;
	}

	/* get time text */
	timestring = gpk_time_to_imprecise_string (remaining);
	text = g_strdup_printf (_("Remaining time: %s"), timestring);
	gtk_progress_bar_set_text (progress_bar, text);
out:
	g_free (timestring);
	g_free (text);
	return TRUE;
}

/**
 * gpk_modal_dialog_set_image:
 **/
gboolean
gpk_modal_dialog_set_image (GpkModalDialog *dialog, const gchar *image)
{
	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);
	g_return_val_if_fail (image != NULL, FALSE);

	/* set state */
	dialog->priv->set_image = TRUE;

	g_debug ("setting image: %s", image);
	gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (dialog->priv->image_status), FALSE);
	gtk_image_set_from_icon_name (GTK_IMAGE (dialog->priv->image_status), image, GTK_ICON_SIZE_DIALOG);
	return TRUE;
}

/**
 * gpk_modal_dialog_set_image_status:
 **/
gboolean
gpk_modal_dialog_set_image_status (GpkModalDialog *dialog, PkStatusEnum status)
{
	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	/* set state */
	dialog->priv->set_image = TRUE;
	gpk_set_animated_icon_from_status (GPK_ANIMATED_ICON (dialog->priv->image_status), status, GTK_ICON_SIZE_DIALOG);
	return TRUE;
}

/**
 * gpk_modal_dialog_get_window:
 **/
GtkWindow *
gpk_modal_dialog_get_window (GpkModalDialog *dialog)
{
	GtkWindow *window;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), NULL);

	window = GTK_WINDOW (gtk_builder_get_object (dialog->priv->builder, "dialog_client"));
	return window;
}

/**
 * gpk_modal_dialog_set_allow_cancel:
 **/
gboolean
gpk_modal_dialog_set_allow_cancel (GpkModalDialog *dialog, gboolean can_cancel)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "button_cancel"));
	gtk_widget_set_sensitive (widget, can_cancel);

	return TRUE;
}

/**
 * gpk_modal_dialog_run:
 **/
GtkResponseType
gpk_modal_dialog_run (GpkModalDialog *dialog)
{
	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	/* already running */
	if (g_main_loop_is_running (dialog->priv->loop))
		g_main_loop_quit (dialog->priv->loop);

	dialog->priv->response = GTK_RESPONSE_NONE;
	g_main_loop_run (dialog->priv->loop);

	return dialog->priv->response;
}

/**
 * gpk_modal_dialog_close:
 **/
gboolean
gpk_modal_dialog_close (GpkModalDialog *dialog)
{
	GtkWidget *widget;

	g_return_val_if_fail (GPK_IS_CLIENT_DIALOG (dialog), FALSE);

	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "dialog_client"));
	gtk_widget_hide (widget);

	if (dialog->priv->pulse_timer_id != 0) {
		g_source_remove (dialog->priv->pulse_timer_id);
		dialog->priv->pulse_timer_id = 0;
	}

	gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (dialog->priv->image_status), FALSE);
	return TRUE;
}

/**
 * gpk_modal_dialog_window_delete_cb:
 **/
static gboolean
gpk_modal_dialog_window_delete_cb (GtkWidget *widget, GdkEvent *event, GpkModalDialog *dialog)
{
	dialog->priv->response = GTK_RESPONSE_DELETE_EVENT;
	gpk_modal_dialog_close (dialog);
	if (g_main_loop_is_running (dialog->priv->loop))
		g_main_loop_quit (dialog->priv->loop);
	/* do not destroy the window */
	return TRUE;
}

/**
 * gpk_modal_dialog_button_close_cb:
 **/
static void
gpk_modal_dialog_button_close_cb (GtkWidget *widget_button, GpkModalDialog *dialog)
{
	dialog->priv->response = GTK_RESPONSE_CLOSE;
	g_main_loop_quit (dialog->priv->loop);

	if (dialog->priv->pulse_timer_id != 0) {
		g_source_remove (dialog->priv->pulse_timer_id);
		dialog->priv->pulse_timer_id = 0;
	}

	gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (dialog->priv->image_status), FALSE);
	if (g_main_loop_is_running (dialog->priv->loop))
		g_main_loop_quit (dialog->priv->loop);
	else
		g_signal_emit (dialog, signals [GPK_MODAL_DIALOG_CLOSE], 0);
}

/**
 * gpk_modal_dialog_button_action_cb:
 **/
static void
gpk_modal_dialog_button_action_cb (GtkWidget *widget_button, GpkModalDialog *dialog)
{
	dialog->priv->response = GTK_RESPONSE_OK;
	g_main_loop_quit (dialog->priv->loop);
	if (g_main_loop_is_running (dialog->priv->loop))
		g_main_loop_quit (dialog->priv->loop);
	else
		g_signal_emit (dialog, signals [GPK_MODAL_DIALOG_ACTION], 0);
}

/**
 * gpk_modal_dialog_button_cancel_cb:
 **/
static void
gpk_modal_dialog_button_cancel_cb (GtkWidget *widget_button, GpkModalDialog *dialog)
{
	dialog->priv->response = GTK_RESPONSE_CANCEL;
	if (g_main_loop_is_running (dialog->priv->loop))
		g_main_loop_quit (dialog->priv->loop);
	else
		g_signal_emit (dialog, signals [GPK_MODAL_DIALOG_CANCEL], 0);
}

/**
 * gpk_modal_dialog_set_package_list:
 **/
gboolean
gpk_modal_dialog_set_package_list (GpkModalDialog *dialog, const GPtrArray *list)
{
	GtkTreeIter iter;
	PkPackage *item;
	const gchar *icon;
	gchar *text;
	guint i;
	GtkWidget *widget;
	gchar **split;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *summary = NULL;

	gtk_list_store_clear (dialog->priv->store);

	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "scrolledwindow_packages"));
	if (list->len > 5)
		gtk_widget_set_size_request (widget, -1, 300);
	else if (list->len > 1)
		gtk_widget_set_size_request (widget, -1, 150);

	/* add each well */
	for (i=0; i<list->len; i++) {
		item = g_ptr_array_index (list, i);
		g_object_get (item,
			      "info", &info,
			      NULL);

		/* not installed, so ignore icon */
		if (info == PK_INFO_ENUM_DOWNLOADING ||
		    info == PK_INFO_ENUM_CLEANUP)
			continue;

		g_object_get (item,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);

		text = gpk_package_id_format_twoline (gtk_widget_get_style_context (widget),
						      package_id, summary);

		/* get the icon */
		split = pk_package_id_split (package_id);
		icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_INSTALLED);
		gtk_list_store_append (dialog->priv->store, &iter);
		gtk_list_store_set (dialog->priv->store, &iter,
				    GPK_MODAL_DIALOG_STORE_IMAGE, icon,
				    GPK_MODAL_DIALOG_STORE_ID, package_id,
				    GPK_MODAL_DIALOG_STORE_TEXT, text,
				    -1);
		g_strfreev (split);
		g_free (package_id);
		g_free (summary);
		g_free (text);
	}

	return TRUE;
}

/**
 * gpk_dialog_treeview_for_package_list:
 **/
static gboolean
gpk_dialog_treeview_for_package_list (GpkModalDialog *dialog)
{
	GtkTreeView *treeview;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (dialog->priv->builder, "treeview_packages"));

	/* column for images */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_DND, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "icon-name", GPK_MODAL_DIALOG_STORE_IMAGE);
	gtk_tree_view_append_column (treeview, column);

	/* column for name */
	renderer = gtk_cell_renderer_text_new ();
	/* TRANSLATORS: column for the package name */
	column = gtk_tree_view_column_new_with_attributes (_("Name"), renderer,
							   "markup", GPK_MODAL_DIALOG_STORE_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_MODAL_DIALOG_STORE_TEXT);
	gtk_tree_view_append_column (treeview, column);

	/* set some common options */
	gtk_tree_view_set_headers_visible (treeview, FALSE);
	selection = gtk_tree_view_get_selection (treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_NONE);
	gtk_tree_selection_unselect_all (selection);

	return TRUE;
}

/**
 * gpk_modal_dialog_class_init:
 * @klass: The GpkModalDialogClass
 **/
static void
gpk_modal_dialog_class_init (GpkModalDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_modal_dialog_finalize;
	g_type_class_add_private (klass, sizeof (GpkModalDialogPrivate));
	signals [GPK_MODAL_DIALOG_QUIT] =
		g_signal_new ("quit",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [GPK_MODAL_DIALOG_CLOSE] =
		g_signal_new ("close",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [GPK_MODAL_DIALOG_ACTION] =
		g_signal_new ("action",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [GPK_MODAL_DIALOG_CANCEL] =
		g_signal_new ("cancel",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * gpk_modal_dialog_init:
 * @dialog: This class instance
 **/
static void
gpk_modal_dialog_init (GpkModalDialog *dialog)
{
	GtkWidget *widget;
	GtkTreeView *treeview;
	guint retval;
	GError *error = NULL;
	GtkBox *box;

	dialog->priv = GPK_MODAL_DIALOG_GET_PRIVATE (dialog);

	dialog->priv->loop = g_main_loop_new (NULL, FALSE);
	dialog->priv->response = GTK_RESPONSE_NONE;
	dialog->priv->pulse_timer_id = 0;
	dialog->priv->show_progress_files = TRUE;
	dialog->priv->has_parent = FALSE;
	dialog->priv->set_image = FALSE;
	dialog->priv->page = GPK_MODAL_DIALOG_PAGE_UNKNOWN;
	dialog->priv->options = 0;
	dialog->priv->title = NULL;

	dialog->priv->store = gtk_list_store_new (GPK_MODAL_DIALOG_STORE_LAST,
						  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* get UI */
	dialog->priv->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (dialog->priv->builder, GPK_DATA "/gpk-client.ui", &error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out_build;
	}

	/* add animated widget */
	dialog->priv->image_status = gpk_animated_icon_new ();
	box = GTK_BOX (gtk_builder_get_object (dialog->priv->builder, "hbox_status"));
	gtk_box_pack_start (box, dialog->priv->image_status, FALSE, FALSE, 0);
	gtk_widget_show (dialog->priv->image_status);

	gpk_dialog_treeview_for_package_list (dialog);

	treeview = GTK_TREE_VIEW (gtk_builder_get_object (dialog->priv->builder, "treeview_packages"));
	gtk_tree_view_set_model (treeview, GTK_TREE_MODEL (dialog->priv->store));

	/* common stuff */
	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "dialog_client"));
	g_signal_connect (widget, "delete_event", G_CALLBACK (gpk_modal_dialog_window_delete_cb), dialog);
	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "button_close"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_modal_dialog_button_close_cb), dialog);
	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "button_action"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_modal_dialog_button_action_cb), dialog);
	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "button_cancel"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_modal_dialog_button_cancel_cb), dialog);

	/* set the message text an absolute width so it's forced to wrap */
	widget = GTK_WIDGET (gtk_builder_get_object (dialog->priv->builder, "label_message"));
	gtk_label_set_max_width_chars (GTK_LABEL (widget), 80);

out_build:
	/* clear status and progress text */
	gpk_modal_dialog_set_window_title (dialog, "");
	gpk_modal_dialog_set_title (dialog, "");
	gpk_modal_dialog_set_message (dialog, "");
}

/**
 * gpk_modal_dialog_finalize:
 * @object: The object to finalize
 **/
static void
gpk_modal_dialog_finalize (GObject *object)
{
	GpkModalDialog *dialog;
	g_return_if_fail (GPK_IS_CLIENT_DIALOG (object));

	dialog = GPK_MODAL_DIALOG (object);
	g_return_if_fail (dialog->priv != NULL);

	/* no updates, we're about to rip the builder up  */
	if (dialog->priv->pulse_timer_id != 0)
		g_source_remove (dialog->priv->pulse_timer_id);

	/* if it's closed, then hide */
	gpk_modal_dialog_close (dialog);

	/* shouldn't be, but just in case */
	if (g_main_loop_is_running (dialog->priv->loop)) {
		g_warning ("mainloop running on exit");
		g_main_loop_quit (dialog->priv->loop);
	}

	g_object_unref (dialog->priv->store);
	g_object_unref (dialog->priv->builder);
	g_main_loop_unref (dialog->priv->loop);
	g_free (dialog->priv->title);

	G_OBJECT_CLASS (gpk_modal_dialog_parent_class)->finalize (object);
}

/**
 * gpk_modal_dialog_new:
 *
 * Return value: a new GpkModalDialog object.
 **/
GpkModalDialog *
gpk_modal_dialog_new (void)
{
	GpkModalDialog *dialog;
	dialog = g_object_new (GPK_TYPE_CLIENT_DIALOG, NULL);
	return GPK_MODAL_DIALOG (dialog);
}
