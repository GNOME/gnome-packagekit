/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
#ifdef HAVE_LIBNOTIFY
#include <libnotify/notify.h>
#endif
#include <packagekit-glib2/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-console-kit.h"

#include "gpk-common.h"
#include "gpk-enum.h"
#include "gpk-error.h"
#include "gpk-inhibit.h"
#include "gpk-modal-dialog.h"
#include "gpk-session.h"
#include "gpk-task.h"
#include "gpk-watch.h"

static void     gpk_watch_finalize	(GObject       *object);

#define GPK_WATCH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_WATCH, GpkWatchPrivate))
#define GPK_WATCH_MAXIMUM_TOOLTIP_LINES		10
#define GPK_WATCH_SET_PROXY_RATE_LIMIT		200 /* ms */

struct GpkWatchPrivate
{
	PkControl		*control;
	GtkStatusIcon		*status_icon;
	GPtrArray		*cached_messages;
	GPtrArray		*restart_package_names;
	NotifyNotification	*notification_cached_messages;
	GpkInhibit		*inhibit;
	GpkModalDialog		*dialog;
	PkTask			*task;
	PkTransactionList	*tlist;
	PkRestartEnum		 restart;
	GSettings		*settings;
	guint			 set_proxy_id;
	gchar			*error_details;
	gboolean		 hide_warning;
	EggConsoleKit		*console;
	GCancellable		*cancellable;
	GPtrArray		*array_progress;
	gchar			*transaction_id;
};

typedef struct {
	PkMessageEnum	 type;
	gchar		*tid;
	gchar		*details;
} GpkWatchCachedMessage;

enum {
	GPK_WATCH_COLUMN_TEXT,
	GPK_WATCH_COLUMN_TID,
	GPK_WATCH_COLUMN_DETAILS,
	GPK_WATCH_COLUMN_LAST
};

G_DEFINE_TYPE (GpkWatch, gpk_watch, G_TYPE_OBJECT)

/**
 * gpk_watch_class_init:
 * @klass: The GpkWatchClass
 **/
static void
gpk_watch_class_init (GpkWatchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_watch_finalize;
	g_type_class_add_private (klass, sizeof (GpkWatchPrivate));
}

/**
 * gpk_watch_cached_message_free:
 **/
static void
gpk_watch_cached_message_free (GpkWatchCachedMessage *cached_message)
{
	if (cached_message == NULL)
		return;
	g_free (cached_message->tid);
	g_free (cached_message->details);
	g_free (cached_message);
}

/**
 * gpk_watch_get_restart_required_tooltip:
 **/
static gchar *
gpk_watch_get_restart_required_tooltip (GpkWatch *watch)
{
	gchar *package_loc = NULL;
	gchar **packages = NULL;
	guint len;
	const gchar *title;
	gchar *message = NULL;
	gchar *text = NULL;

	/* nothing */
	if (watch->priv->restart == PK_RESTART_ENUM_NONE)
		goto out;

	/* size */
	len = watch->priv->restart_package_names->len;
	if (len == 0)
		goto out;

	/* localised title */
	title = gpk_restart_enum_to_localised_text (watch->priv->restart);

	/* non-security require */
	if (watch->priv->restart == PK_RESTART_ENUM_SESSION ||
	    watch->priv->restart == PK_RESTART_ENUM_SYSTEM) {

		/* get localised list */
		packages = pk_ptr_array_to_strv (watch->priv->restart_package_names);
		package_loc = gpk_strv_join_locale (packages);
		if (package_loc != NULL) {
			/* TRANSLATORS: a list of packages is shown that need to restarted */
			message = g_strdup_printf (ngettext ("This is due to the %s package being updated.",
							     "This is due to the following packages being updated: %s.", len), package_loc);
		} else {
			/* TRANSLATORS: over 5 packages require the system to be restarted, don't list them all here */
			message = g_strdup_printf (ngettext ("This is because %i package has been updated.",
							     "This is because %i packages have been updated.", len), len);
		}

		/* join */
		text = g_strdup_printf ("%s %s", title, message);
		goto out;
	}

	/* just use title, as security requires are not the package that are updated */
	text = g_strdup (title);
out:
	g_strfreev (packages);
	g_free (package_loc);
	g_free (message);
	return text;
}

/**
 * gpk_watch_refresh_tooltip:
 **/
static gboolean
gpk_watch_refresh_tooltip (GpkWatch *watch)
{
	guint i;
	guint idx = 0;
	PkProgress *progress;
	guint len;
	GString *string;
	PkStatusEnum status;
	PkRoleEnum role;
	gchar *text;
	GPtrArray *array;

	g_return_val_if_fail (GPK_IS_WATCH (watch), FALSE);

	string = g_string_new ("");
	array = watch->priv->array_progress;
	egg_debug ("refresh tooltip %i", array->len);
	if (array->len == 0) {

		/* any restart required? */
		text = gpk_watch_get_restart_required_tooltip (watch);
		if (text != NULL)
			g_string_append (string, text);
		g_free (text);

		/* do we have any cached messages to show? */
		len = watch->priv->cached_messages->len;
		if (len > 0) {
			if (string->len > 0)
				g_string_append_c (string, '\n');
			g_string_append_printf (string, ngettext ("%i message from the package manager",
								  "%i messages from the package manager", len), len);
			goto out;
		}

		egg_debug ("nothing to show");
		goto out;
	}

	/* print all the running transactions */
	for (i=0; i<array->len; i++) {
		progress = g_ptr_array_index (array, i);
		g_object_get (progress,
			      "role", &role,
			      "status", &status,
			      NULL);

		/* ignore boring status values */
		if (status == PK_STATUS_ENUM_FINISHED)
			continue;

		/* should we display the text */
		g_string_append_printf (string, "%s: %s\n", gpk_role_enum_to_localised_present (role), gpk_status_enum_to_localised_text (status));

		/* don't fill the screen with a giant tooltip */
		if (idx++ > GPK_WATCH_MAXIMUM_TOOLTIP_LINES)
			break;
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size (string, string->len-1);
out:
	gtk_status_icon_set_tooltip_text (watch->priv->status_icon, string->str);
	g_string_free (string, TRUE);
	return TRUE;
}

/**
 * gpk_watch_task_list_to_status_bitfield:
 **/
static PkBitfield
gpk_watch_task_list_to_status_bitfield (GpkWatch *watch)
{
	gboolean active;
	gboolean watch_active;
	guint i;
	PkBitfield bitfield = 0;
	PkStatusEnum status;
	PkProgress *progress;
	gchar *transaction_id;
	GPtrArray *array;

	g_return_val_if_fail (GPK_IS_WATCH (watch), 0);

	/* shortcut */
	array = watch->priv->array_progress;
	if (array->len == 0)
		goto out;

	/* do we watch active transactions */
	watch_active = g_settings_get_boolean (watch->priv->settings, GPK_SETTINGS_WATCH_ACTIVE_TRANSACTIONS);

	/* add each status to a list */
	for (i=0; i<array->len; i++) {
		progress = g_ptr_array_index (array, i);

		/* only show an icon for this if the application isn't still on the bus */
		g_object_get (progress,
			      "caller-active", &active,
			      "status", &status,
			      "transaction-id", &transaction_id,
			      NULL);

		/* add to bitfield calculation */
		egg_debug ("%s %s (active:%i)", transaction_id, pk_status_enum_to_text (status), active);
		if ((!active || watch_active) && status != PK_STATUS_ENUM_FINISHED)
			pk_bitfield_add (bitfield, status);
		g_free (transaction_id);
	}
out:
	return bitfield;
}

/**
 * gpk_watch_refresh_icon:
 **/
static gboolean
gpk_watch_refresh_icon (GpkWatch *watch)
{
	const gchar *icon_name = NULL;
	PkBitfield status;
	gint value = -1;
	guint len;

	g_return_val_if_fail (GPK_IS_WATCH (watch), FALSE);

	egg_debug ("rescan");
	status = gpk_watch_task_list_to_status_bitfield (watch);

	/* something in list */
	if (status != 0) {
		/* get the most important icon */
		value = pk_bitfield_contain_priority (status,
						      PK_STATUS_ENUM_REFRESH_CACHE,
						      PK_STATUS_ENUM_LOADING_CACHE,
						      PK_STATUS_ENUM_CANCEL,
						      PK_STATUS_ENUM_INSTALL,
						      PK_STATUS_ENUM_REMOVE,
						      PK_STATUS_ENUM_CLEANUP,
						      PK_STATUS_ENUM_OBSOLETE,
						      PK_STATUS_ENUM_SETUP,
						      PK_STATUS_ENUM_RUNNING,
						      PK_STATUS_ENUM_UPDATE,
						      PK_STATUS_ENUM_DOWNLOAD,
						      PK_STATUS_ENUM_DOWNLOAD_REPOSITORY,
						      PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST,
						      PK_STATUS_ENUM_DOWNLOAD_FILELIST,
						      PK_STATUS_ENUM_DOWNLOAD_CHANGELOG,
						      PK_STATUS_ENUM_DOWNLOAD_GROUP,
						      PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO,
						      PK_STATUS_ENUM_SCAN_APPLICATIONS,
						      PK_STATUS_ENUM_GENERATE_PACKAGE_LIST,
						      PK_STATUS_ENUM_QUERY,
						      PK_STATUS_ENUM_INFO,
						      PK_STATUS_ENUM_DEP_RESOLVE,
						      PK_STATUS_ENUM_ROLLBACK,
						      PK_STATUS_ENUM_TEST_COMMIT,
						      PK_STATUS_ENUM_COMMIT,
						      PK_STATUS_ENUM_REQUEST,
						      PK_STATUS_ENUM_SIG_CHECK,
						      PK_STATUS_ENUM_CLEANUP,
						      PK_STATUS_ENUM_REPACKAGING,
						      PK_STATUS_ENUM_WAIT,
						      PK_STATUS_ENUM_WAITING_FOR_LOCK,
						      -1);
	}

	/* only set if in the list and not unknown */
	if (value != PK_STATUS_ENUM_UNKNOWN && value != -1) {
		icon_name = gpk_status_enum_to_icon_name (value);
		goto out;
	}

	/* any restart required? */
	if (watch->priv->restart != PK_RESTART_ENUM_NONE &&
	    watch->priv->hide_warning == FALSE) {
		icon_name = gpk_restart_enum_to_icon_name (watch->priv->restart);
		goto out;
	}

	/* do we have any cached messages to show? */
	len = watch->priv->cached_messages->len;
	if (len > 0) {
		icon_name = "pk-setup";
		goto out;
	}

out:
	/* no icon, hide */
	if (icon_name == NULL) {
		gtk_status_icon_set_visible (watch->priv->status_icon, FALSE);
		return FALSE;
	}
	gtk_status_icon_set_from_icon_name (watch->priv->status_icon, icon_name);
	gtk_status_icon_set_visible (watch->priv->status_icon, TRUE);
	return TRUE;
}

#ifdef HAVE_LIBNOTIFY
/**
 * gpk_watch_libnotify_cb:
 **/
static void
gpk_watch_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data)
{
	GpkWatch *watch = GPK_WATCH (data);

	if (g_strcmp0 (action, "do-not-show-notify-complete") == 0) {
		egg_debug ("set %s to FALSE", GPK_SETTINGS_NOTIFY_COMPLETED);
		g_settings_set_boolean (watch->priv->settings, GPK_SETTINGS_NOTIFY_COMPLETED, FALSE);

	} else if (g_strcmp0 (action, "show-error-details") == 0) {
		/* TRANSLATORS: The detailed error if the user clicks "more info" */
		gpk_error_dialog (_("Error details"), _("Package manager error details"), watch->priv->error_details);

	} else {
		egg_warning ("unknown action id: %s", action);
	}
}
#endif

/**
 * gpk_watch_about_dialog_url_cb:
 **/
static void
gpk_watch_about_dialog_url_cb (GtkAboutDialog *about, const char *address, gpointer data)
{
	GError *error = NULL;
	gboolean ret;
	GdkScreen *gscreen;
	gchar *url;
	gchar *protocol = (gchar*) data;

	if (protocol != NULL)
		url = g_strconcat (protocol, address, NULL);
	else
		url = g_strdup (address);

	gscreen = gtk_window_get_screen (GTK_WINDOW (about));

	ret = gtk_show_uri (gscreen, url, gtk_get_current_event_time (), &error);

	if (!ret) {
		/* TRANSLATORS: We couldn't launch the tool, normally a packaging problem */
		gpk_error_dialog (_("Internal error"), _("Failed to show url"), error->message);
		g_error_free (error);
	}

	g_free (url);
}

/**
 * gpk_watch_show_about_cb:
 **/
static void
gpk_watch_show_about_cb (GtkMenuItem *item, gpointer data)
{
	const char *authors[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *documenters[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *license[] = {
		N_("Licensed under the GNU General Public License Version 2"),
		N_("PackageKit is free software; you can redistribute it and/or "
		   "modify it under the terms of the GNU General Public License "
		   "as published by the Free Software Foundation; either version 2 "
		   "of the License, or (at your option) any later version."),
		N_("PackageKit is distributed in the hope that it will be useful, "
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of "
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
		   "GNU General Public License for more details."),
		N_("You should have received a copy of the GNU General Public License "
		   "along with this program; if not, write to the Free Software "
		   "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA "
		   "02110-1301, USA.")
	};
	const char *translators = _("translator-credits");
	char *license_trans;

	/* Translators comment: put your own name here to appear in the about dialog. */
	if (!strcmp (translators, "translator-credits"))
		translators = NULL;

	license_trans = g_strconcat (_(license[0]), "", _(license[1]), "",
				     _(license[2]), "", _(license[3]), "",  NULL);

	gtk_about_dialog_set_url_hook (gpk_watch_about_dialog_url_cb, NULL, NULL);
	gtk_about_dialog_set_email_hook (gpk_watch_about_dialog_url_cb, (gpointer) "mailto:", NULL);

	gtk_window_set_default_icon_name (GPK_ICON_SOFTWARE_LOG);
	gtk_show_about_dialog (NULL,
			       "version", VERSION,
			       "copyright", "Copyright \xc2\xa9 2007-2009 Richard Hughes",
			       "license", license_trans,
			       "wrap-license", TRUE,	
			       "website-label", _("PackageKit Website"),
			       "website", "http://www.packagekit.org",
			       "comments", "PackageKit",
			       "authors", authors,
			       "documenters", documenters,
			       "translator-credits", translators,
			       "logo-icon-name", GPK_ICON_SOFTWARE_LOG,
			       NULL);
	g_free (license_trans);
}

/**
 * gpk_watch_popup_menu_cb:
 *
 * Display the popup menu.
 **/
static void
gpk_watch_popup_menu_cb (GtkStatusIcon *status_icon, guint button, guint32 timestamp, GpkWatch *watch)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;
	GtkWidget *image;

	g_return_if_fail (GPK_IS_WATCH (watch));
	egg_debug ("icon right clicked");

	/* TRANSLATORS: this is the right click menu item */
	item = gtk_image_menu_item_new_with_mnemonic (_("_About"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (gpk_watch_show_about_cb), watch);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	/* show the menu */
	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gtk_status_icon_position_menu, status_icon,
			button, timestamp);
	if (button == 0)
		gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
}

/**
 * gpk_watch_menu_show_messages_cb:
 **/
static void
gpk_watch_menu_show_messages_cb (GtkMenuItem *item, gpointer data)
{
	GpkWatch *watch = GPK_WATCH (data);
	GtkBuilder *builder;
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkListStore *list_store;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	GtkTreeModel *model;
	guint i;
	GpkWatchCachedMessage *cached_message;
	guint retval;
	GError *error = NULL;

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-repo.ui", &error);
	if (retval == 0) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out_build;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_repo"));
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GPK_ICON_SOFTWARE_LOG);
	gtk_window_set_title (GTK_WINDOW (main_window), _("Package Manager Messages"));

	/* set a size, if the screen allows */
	gpk_window_set_size_request (GTK_WINDOW(main_window), 500, 200);

	/* Get the main window quit */
	g_signal_connect_swapped (main_window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_help"));
	gtk_widget_hide (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "checkbutton_detail"));
	gtk_widget_hide (widget);

	/* create list stores */
	list_store = gtk_list_store_new (GPK_WATCH_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* create repo tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_repo"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget), GTK_TREE_MODEL (list_store));

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "yalign", 0.0, NULL);
	g_object_set (renderer, "wrap-mode", PANGO_WRAP_WORD, NULL);
	g_object_set (renderer, "wrap-width", 400, NULL);

	/* TRANSLATORS: column for the message type */
	column = gtk_tree_view_column_new_with_attributes (_("Message"), renderer,
							   "markup", GPK_WATCH_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_WATCH_COLUMN_TEXT);
	gtk_tree_view_append_column (GTK_TREE_VIEW(widget), column);

	/* column for details */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "yalign", 0.0, NULL);
	g_object_set (renderer, "wrap-mode", PANGO_WRAP_WORD, NULL);
	g_object_set (renderer, "wrap-width", 400, NULL);

	/* TRANSLATORS: column for the message description */
	column = gtk_tree_view_column_new_with_attributes (_("Details"), renderer,
							   "markup", GPK_WATCH_COLUMN_DETAILS, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPK_WATCH_COLUMN_TEXT);
	gtk_tree_view_append_column (GTK_TREE_VIEW(widget), column);

	gtk_tree_view_columns_autosize (GTK_TREE_VIEW(widget));

	/* add items to treeview */
	model = gtk_tree_view_get_model (GTK_TREE_VIEW(widget));
	for (i=0; i<watch->priv->cached_messages->len; i++) {
		cached_message = g_ptr_array_index (watch->priv->cached_messages, i);
		gtk_list_store_append (GTK_LIST_STORE(model), &iter);
		gtk_list_store_set (list_store, &iter,
				    GPK_WATCH_COLUMN_TEXT, gpk_message_enum_to_localised_text (cached_message->type),
				    GPK_WATCH_COLUMN_TID, cached_message->tid,
				    GPK_WATCH_COLUMN_DETAILS, cached_message->details,
				    -1);
	}

	/* show window */
	gtk_widget_show (main_window);

	/* focus back to the close button */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	gtk_widget_grab_focus (widget);

	/* wait */
	gtk_main ();

	gtk_widget_hide (main_window);

	g_ptr_array_set_size (watch->priv->cached_messages, 0);

	g_object_unref (list_store);
out_build:
	g_object_unref (builder);

	/* refresh UI */
	gpk_watch_refresh_icon (watch);
	gpk_watch_refresh_tooltip (watch);
}

/**
 * gpk_watch_set_status:
 **/
static gboolean
gpk_watch_set_status (GpkWatch *watch, PkStatusEnum status)
{
	/* do we force progress? */
	if (status == PK_STATUS_ENUM_DOWNLOAD_REPOSITORY ||
	    status == PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST ||
	    status == PK_STATUS_ENUM_DOWNLOAD_FILELIST ||
	    status == PK_STATUS_ENUM_DOWNLOAD_CHANGELOG ||
	    status == PK_STATUS_ENUM_DOWNLOAD_GROUP ||
	    status == PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO ||
	    status == PK_STATUS_ENUM_REFRESH_CACHE) {
		gpk_modal_dialog_setup (watch->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	}

	/* set icon */
	gpk_modal_dialog_set_image_status (watch->priv->dialog, status);

	/* set label */
	gpk_modal_dialog_set_title (watch->priv->dialog, gpk_status_enum_to_localised_text (status));

	/* spin */
	if (status == PK_STATUS_ENUM_WAIT)
		gpk_modal_dialog_set_percentage (watch->priv->dialog, -1);

	/* do visual stuff when finished */
	if (status == PK_STATUS_ENUM_FINISHED) {
		/* make insensitive */
		gpk_modal_dialog_set_allow_cancel (watch->priv->dialog, FALSE);

		/* stop spinning */
		gpk_modal_dialog_set_percentage (watch->priv->dialog, 100);
	}
	return TRUE;
}

/**
 * gpk_watch_lookup_progress_from_transaction_id:
 **/
static PkProgress *
gpk_watch_lookup_progress_from_transaction_id (GpkWatch *watch, const gchar *transaction_id)
{
	GPtrArray *array;
	guint i;
	gchar *tid_tmp;
	gboolean ret;
	PkProgress *progress;

	array = watch->priv->array_progress;
	for (i=0; i<array->len; i++) {
		progress = g_ptr_array_index (array, i);
		g_object_get (progress,
			      "transaction-id", &tid_tmp,
			      NULL);
		ret = (g_strcmp0 (transaction_id, tid_tmp) == 0);
		g_free (tid_tmp);
		if (ret)
			goto out;
	}
	progress = NULL;
out:
	return progress;
}

/**
 * gpk_watch_monitor_tid:
 **/
static gboolean
gpk_watch_monitor_tid (GpkWatch *watch, const gchar *transaction_id)
{
	gboolean allow_cancel;
	gchar *package_id = NULL;
	gchar *text;
	guint percentage;
	guint remaining_time;
	PkProgress *progress;
	PkRoleEnum role;
	PkStatusEnum status;

	g_free (watch->priv->transaction_id);
	watch->priv->transaction_id = g_strdup (transaction_id);

	/* find progress */
	progress = gpk_watch_lookup_progress_from_transaction_id (watch, transaction_id);
	if (progress == NULL) {
		egg_warning ("could not find: %s", transaction_id);
		return FALSE;
	}

	/* coldplug */
	g_object_get (progress,
		      "role", &role,
		      "status", &status,
		      "allow-cancel", &allow_cancel,
		      "percentage", &percentage,
		      "remaining-time", &remaining_time,
		      "package-id", &package_id,
		      NULL);

	/* fill in role */
	gpk_modal_dialog_set_title (watch->priv->dialog, gpk_role_enum_to_localised_present (role));

	/* are we cancellable? */
	gpk_modal_dialog_set_allow_cancel (watch->priv->dialog, allow_cancel);
	gpk_modal_dialog_set_percentage (watch->priv->dialog, percentage);
	gpk_modal_dialog_set_remaining (watch->priv->dialog, remaining_time);

	/* setup the UI */
	if (role == PK_ROLE_ENUM_SEARCH_NAME ||
	    role == PK_ROLE_ENUM_SEARCH_GROUP ||
	    role == PK_ROLE_ENUM_SEARCH_DETAILS ||
	    role == PK_ROLE_ENUM_SEARCH_FILE ||
	    role == PK_ROLE_ENUM_SEARCH_NAME ||
	    role == PK_ROLE_ENUM_GET_UPDATES)
		gpk_modal_dialog_setup (watch->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	else
		gpk_modal_dialog_setup (watch->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, GPK_MODAL_DIALOG_PACKAGE_PADDING);

	/* set the status */
	gpk_watch_set_status (watch, status);

	/* do the best we can, and get the last package */
	text = gpk_package_id_format_twoline (package_id, NULL);
	gpk_modal_dialog_set_message (watch->priv->dialog, text);

	gpk_modal_dialog_present (watch->priv->dialog);
	g_free (package_id);
	g_free (text);
	return TRUE;
}

/**
 * gpk_watch_menu_job_status_cb:
 **/
static void
gpk_watch_menu_job_status_cb (GtkMenuItem *item, GpkWatch *watch)
{
	gchar *tid;

	g_return_if_fail (GPK_IS_WATCH (watch));

	/* find the job we should bind to */
	tid = (gchar *) g_object_get_data (G_OBJECT (item), "tid");
	if (egg_strzero(tid) || tid[0] != '/') {
		egg_warning ("invalid job, maybe transaction already removed");
		return;
	}

	/* launch the UI */
	gpk_watch_monitor_tid (watch, tid);
}

/**
 * gpk_watch_populate_menu_with_jobs:
 **/
static void
gpk_watch_populate_menu_with_jobs (GpkWatch *watch, GtkMenu *menu)
{
	guint i;
	PkProgress *progress;
	GtkWidget *widget;
	GtkWidget *image;
	PkRoleEnum role;
	PkStatusEnum status;
	const gchar *icon_name;
	gchar *text;
	gchar *transaction_id;
	GPtrArray *array;

	g_return_if_fail (GPK_IS_WATCH (watch));

	array = watch->priv->array_progress;
	if (array->len == 0)
		goto out;

	/* do a menu item for each job */
	for (i=0; i<array->len; i++) {
		progress = g_ptr_array_index (array, i);
		g_object_get (progress,
			      "role", &role,
			      "status", &status,
			      NULL);

		/* ignore boring status values */
		if (status == PK_STATUS_ENUM_FINISHED)
			continue;

		/* do this in two steps as this data needs to be freed */
		g_object_get (progress,
			      "transaction-id", &transaction_id,
			      NULL);

		icon_name = gpk_status_enum_to_icon_name (status);
		text = g_strdup_printf ("%s (%s)",
					gpk_role_enum_to_localised_present (role),
					gpk_status_enum_to_localised_text (status));

		/* add a job */
		widget = gtk_image_menu_item_new_with_mnemonic (text);

		/* we need the job ID so we know what transaction to show */
		g_object_set_data_full (G_OBJECT (widget), "tid", g_strdup (transaction_id), g_free);

		image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
		g_signal_connect (G_OBJECT (widget), "activate",
				  G_CALLBACK (gpk_watch_menu_job_status_cb), watch);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
		g_free (transaction_id);
		g_free (text);
	}
out:
	return;
}

/**
 * gpk_watch_menu_hide_restart_cb:
 **/
static void
gpk_watch_menu_hide_restart_cb (GtkMenuItem *item, gpointer data)
{
	GpkWatch *watch = GPK_WATCH (data);
	g_return_if_fail (GPK_IS_WATCH (watch));

	/* hide */
	watch->priv->hide_warning = TRUE;
	gpk_watch_refresh_icon (watch);
}

/**
 * gpk_watch_menu_log_out_cb:
 **/
static void
gpk_watch_menu_log_out_cb (GtkMenuItem *item, gpointer data)
{
	GpkWatch *watch = GPK_WATCH (data);
	GpkSession *session;
	g_return_if_fail (GPK_IS_WATCH (watch));

	/* just ask for logout */
	session = gpk_session_new ();
	gpk_session_logout (session);
	g_object_unref (session);
}

/**
 * gpk_watch_menu_restart_cb:
 **/
static void
gpk_watch_menu_restart_cb (GtkMenuItem *item, GpkWatch *watch)
{
	gboolean ret;
	GError *error = NULL;

	/* restart using ConsoleKit */
	ret = egg_console_kit_restart (watch->priv->console, &error);
	if (!ret) {
		egg_warning ("restarting failed: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_watch_activate_status_cb:
 * @button: Which buttons are pressed
 *
 * Callback when the icon is clicked
 **/
static void
gpk_watch_activate_status_cb (GtkStatusIcon *status_icon, GpkWatch *watch)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *widget;
	GtkWidget *image;
	guint len;
	gboolean show_hide = FALSE;
	gboolean can_restart = FALSE;

	g_return_if_fail (GPK_IS_WATCH (watch));

	egg_debug ("icon left clicked");

	/* add jobs as drop down */
	gpk_watch_populate_menu_with_jobs (watch, menu);

	/* any messages to show? */
	len = watch->priv->cached_messages->len;
	if (len > 0) {
		/* TRANSLATORS: messages from the transaction */
		widget = gtk_image_menu_item_new_with_mnemonic (_("_Show messages"));
		image = gtk_image_new_from_icon_name ("edit-paste", GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
		g_signal_connect (G_OBJECT (widget), "activate",
				  G_CALLBACK (gpk_watch_menu_show_messages_cb), watch);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
		show_hide = TRUE;
	}

	/* log out session */
	if (watch->priv->restart == PK_RESTART_ENUM_SESSION ||
	    watch->priv->restart == PK_RESTART_ENUM_SECURITY_SESSION) {
		/* TRANSLATORS: log out of the session */
		widget = gtk_image_menu_item_new_with_mnemonic (_("_Log out"));
		image = gtk_image_new_from_icon_name ("system-log-out", GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
		g_signal_connect (G_OBJECT (widget), "activate",
				  G_CALLBACK (gpk_watch_menu_log_out_cb), watch);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
		show_hide = TRUE;
	}

	/* restart computer */
	egg_console_kit_can_restart (watch->priv->console, &can_restart, NULL);
	if (can_restart &&
	    (watch->priv->restart == PK_RESTART_ENUM_SYSTEM ||
	     watch->priv->restart == PK_RESTART_ENUM_SECURITY_SYSTEM)) {
		/* TRANSLATORS: this menu item restarts the computer after an update */
		widget = gtk_image_menu_item_new_with_mnemonic (_("_Restart computer"));
		image = gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
		g_signal_connect (G_OBJECT (widget), "activate",
				  G_CALLBACK (gpk_watch_menu_restart_cb), watch);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
		show_hide = TRUE;
	}

	/* anything we're allowed to hide? */
	if (show_hide) {
		/* TRANSLATORS: This hides the 'restart required' icon */
		widget = gtk_image_menu_item_new_with_mnemonic (_("_Hide this icon"));
		image = gtk_image_new_from_icon_name ("dialog-information", GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget), image);
		g_signal_connect (G_OBJECT (widget), "activate",
				  G_CALLBACK (gpk_watch_menu_hide_restart_cb), watch);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), widget);
	}

	/* show the menu */
	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gtk_status_icon_position_menu, status_icon,
			1, gtk_get_current_event_time());
}

/**
 * gpk_watch_get_proxy_ftp:
 * Return value: server.lan:8080
 **/
static gchar *
gpk_watch_get_proxy_ftp (GpkWatch *watch)
{
	gchar *connection = NULL;
#ifdef USE_GCONF_COMPAT_GNOME_VFS
	gchar *mode = NULL;
	gchar *host = NULL;
	gint port;

	g_return_val_if_fail (GPK_IS_WATCH (watch), NULL);

	/* common case, a direct connection */
	mode = g_settings_get_string (watch->priv->settings, "/system/proxy/mode");
	if (g_strcmp0 (mode, "none") == 0) {
		egg_debug ("not using session proxy");
		goto out;
	}

	host = g_settings_get_string (watch->priv->settings, "/system/proxy/ftp_host");
	if (egg_strzero (host)) {
		egg_debug ("no hostname for ftp proxy");
		goto out;
	}
	port = g_settings_get_int (watch->priv->settings, "/system/proxy/ftp_port");

	/* ftp has no username or password */
	if (port == 0)
		connection = g_strdup (host);
	else
		connection = g_strdup_printf ("%s:%i", host, port);
out:
	g_free (mode);
	g_free (host);
#endif
	return connection;
}

/**
 * gpk_watch_get_proxy_ftp:
 * Return value: username:password@server.lan:8080
 **/
static gchar *
gpk_watch_get_proxy_http (GpkWatch *watch)
{
	gchar *proxy_http = NULL;
#ifdef USE_GCONF_COMPAT_GNOME_VFS
	gchar *mode = NULL;
	gchar *host = NULL;
	gchar *auth = NULL;
	gchar *connection = NULL;
	gint port;
	gboolean ret;

	g_return_val_if_fail (GPK_IS_WATCH (watch), NULL);

	/* common case, a direct connection */
	mode = g_settings_get_string (watch->priv->settings, "/system/proxy/mode");
	if (g_strcmp0 (mode, "none") == 0) {
		egg_debug ("not using session proxy");
		goto out;
	}

	/* do we use this? */
	ret = g_settings_get_boolean (watch->priv->settings, "/system/http_proxy/use_http_proxy");
	if (!ret) {
		egg_debug ("not using http proxy");
		goto out;
	}

	/* http has 4 parameters */
	host = g_settings_get_string (watch->priv->settings, "/system/http_proxy/host");
	if (egg_strzero (host)) {
		egg_debug ("no hostname for http proxy");
		goto out;
	}

	/* user and password are both optional */
	ret = g_settings_get_boolean (watch->priv->settings, "/system/http_proxy/use_authentication");
	if (ret) {
		gchar *user = NULL;
		gchar *password = NULL;

		user = g_settings_get_string (watch->priv->settings, "/system/http_proxy/authentication_user");
		password = g_settings_get_string (watch->priv->settings, "/system/http_proxy/authentication_password");

		if (user != NULL && password != NULL)
			auth = g_strdup_printf ("%s:%s", user, password);
		else if (user != NULL)
			auth = g_strdup (user);
		else if (password != NULL)
			auth = g_strdup_printf (":%s", user);

		g_free (user);
		g_free (password);
	}

	/* port is optional too */
	port = g_settings_get_int (watch->priv->settings, "/system/http_proxy/port");
	if (port == 0)
		connection = g_strdup (host);
	else
		connection = g_strdup_printf ("%s:%i", host, port);

	/* the whole auth section is optional */
	if (egg_strzero (auth))
		proxy_http = g_strdup (connection);
	else
		proxy_http = g_strdup_printf ("%s@%s", auth, connection);
out:
	g_free (mode);
	g_free (connection);
	g_free (auth);
	g_free (host);
#endif
	return proxy_http;
}

/**
 * gpk_watch_set_proxy_cb:
 **/
static void
gpk_watch_set_proxy_cb (GObject *object, GAsyncResult *res, GpkWatch *watch)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	gboolean ret;

	/* we can run again */
	watch->priv->set_proxy_id = 0;

	/* get the result */
	ret = pk_control_set_proxy_finish (control, res, &error);
	if (!ret) {
		egg_warning ("failed to set proxies: %s", error->message);
		g_error_free (error);
		return;
	}
}

/**
 * gpk_watch_set_proxies_ratelimit:
 **/
static gboolean
gpk_watch_set_proxies_ratelimit (GpkWatch *watch)
{
	gchar *proxy_http;
	gchar *proxy_ftp;

	/* debug so we can catch polling */
	egg_debug ("polling check");

	proxy_http = gpk_watch_get_proxy_http (watch);
	proxy_ftp = gpk_watch_get_proxy_ftp (watch);

	egg_debug ("set proxy_http=%s, proxy_ftp=%s", proxy_http, proxy_ftp);
	pk_control_set_proxy_async (watch->priv->control, proxy_http, proxy_ftp, watch->priv->cancellable,
				    (GAsyncReadyCallback) gpk_watch_set_proxy_cb, watch);
	g_free (proxy_http);
	g_free (proxy_ftp);
	return FALSE;
}

/**
 * gpk_watch_set_proxies:
 **/
static gboolean
gpk_watch_set_proxies (GpkWatch *watch)
{
	if (watch->priv->set_proxy_id != 0) {
		egg_debug ("already scheduled");
		return FALSE;
	}
	watch->priv->set_proxy_id = g_timeout_add (GPK_WATCH_SET_PROXY_RATE_LIMIT,
							(GSourceFunc) gpk_watch_set_proxies_ratelimit, watch);
#if GLIB_CHECK_VERSION(2,25,8)
	g_source_set_name_by_id (watch->priv->set_proxy_id, "[GpkWatch] set-proxies");
#endif
	return TRUE;
}

#if PK_CHECK_VERSION(0,6,4)
/**
 * gpk_watch_set_root_cb:
 **/
static void
gpk_watch_set_root_cb (GObject *object, GAsyncResult *res, GpkWatch *watch)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	gboolean ret;

	/* get the result */
	ret = pk_control_set_root_finish (control, res, &error);
	if (!ret) {
		egg_warning ("failed to set install root: %s", error->message);
		g_error_free (error);
		return;
	}
}

/**
 * gpk_watch_set_root:
 **/
static void
gpk_watch_set_root (GpkWatch *watch)
{
	gchar *root;

	/* get install root */
	root = g_settings_get_string (watch->priv->settings, GPK_SETTINGS_INSTALL_ROOT);
	if (root == NULL) {
		egg_warning ("could not read install root");
		goto out;
	}

	pk_control_set_root_async (watch->priv->control, root, watch->priv->cancellable,
				   (GAsyncReadyCallback) gpk_watch_set_root_cb, watch);
out:
	g_free (root);
}
#else
static void gpk_watch_set_root (GpkWatch *watch) {}
#endif

/**
 * gpk_watch_key_changed_cb:
 *
 * We might have to do things when the keys change; do them here.
 **/
static void
gpk_watch_key_changed_cb (GSettings *client, const gchar *key, GpkWatch *watch)
{
	egg_debug ("keys have changed");
	gpk_watch_set_proxies (watch);
	gpk_watch_set_root (watch);
}

/**
 * gpk_watch_button_close_cb:
 **/
static void
gpk_watch_button_close_cb (GtkWidget *widget, GpkWatch *watch)
{
	/* close, don't abort */
	gpk_modal_dialog_close (watch->priv->dialog);
}

/**
 * gpk_watch_button_cancel_cb:
 **/
static void
gpk_watch_button_cancel_cb (GtkWidget *widget, GpkWatch *watch)
{
	/* we might have a transaction running */
	egg_debug ("cancelling transaction: %p", watch->priv->cancellable);
	g_cancellable_cancel (watch->priv->cancellable);
}

/**
 * gpk_watch_set_connected:
 **/
static void
gpk_watch_set_connected (GpkWatch *watch, gboolean connected)
{
	if (!connected)
		return;

	/* daemon has just appeared */
	egg_debug ("dameon has just appeared");
	gpk_watch_refresh_icon (watch);
	gpk_watch_refresh_tooltip (watch);
	gpk_watch_set_proxies (watch);
	gpk_watch_set_root (watch);
}

/**
 * gpk_watch_notify_connected_cb:
 **/
static void
gpk_watch_notify_connected_cb (PkControl *control, GParamSpec *pspec, GpkWatch *watch)
{
	gboolean connected;
	g_object_get (control, "connected", &connected, NULL);
	gpk_watch_set_connected (watch, connected);
}

/**
 * gpk_watch_notify_locked_cb:
 **/
static void
gpk_watch_notify_locked_cb (PkControl *control, GParamSpec *pspec, GpkWatch *watch)
{
	gboolean locked;
	g_object_get (control, "locked", &locked, NULL);
	if (locked)
		gpk_inhibit_create (watch->priv->inhibit);
	else
		gpk_inhibit_remove (watch->priv->inhibit);
}

/**
 * gpk_watch_is_message_ignored:
 **/
static gboolean
gpk_watch_is_message_ignored (GpkWatch *watch, PkMessageEnum message)
{
	guint i;
	gboolean ret = FALSE;
	gchar *ignored_str;
	gchar **ignored = NULL;
	const gchar *message_str;

	/* get from settings */
	ignored_str = g_settings_get_string (watch->priv->settings, GPK_SETTINGS_IGNORED_MESSAGES);
	if (ignored_str == NULL) {
		egg_warning ("could not read ignored list");
		goto out;
	}

	/* nothing in list, common case */
	if (egg_strzero (ignored_str)) {
		egg_debug ("nothing in ignored list");
		goto out;
	}

	/* split using "," */
	ignored = g_strsplit (ignored_str, ",", 0);

	/* remove any ignored pattern matches */
	message_str = pk_message_enum_to_text (message);
	for (i=0; ignored[i] != NULL; i++) {
		ret = g_pattern_match_simple (ignored[i], message_str);
		if (ret) {
			egg_debug ("match %s for %s, ignoring", ignored[i], message_str);
			break;
		}
	}
out:
	g_free (ignored_str);
	g_strfreev (ignored);
	return ret;
}

/**
 * gpk_watch_process_messages_cb:
 **/
static void
gpk_watch_process_messages_cb (PkMessage *item, GpkWatch *watch)
{
	gboolean ret;
	GError *error = NULL;
	gboolean value;
	NotifyNotification *notification;
	GpkWatchCachedMessage *cached_message;
	PkMessageEnum type;
	gchar *details;

	g_return_if_fail (GPK_IS_WATCH (watch));

	/* get data */
	g_object_get (item,
		      "type", &type,
		      "details", &details,
		      NULL);

	/* is ignored */
	ret = gpk_watch_is_message_ignored (watch, type);
	if (ret) {
		egg_debug ("ignoring message");
		goto out;
	}

	/* add to list */
	cached_message = g_new0 (GpkWatchCachedMessage, 1);
	cached_message->type = type;
	cached_message->tid = NULL;
	cached_message->details = g_strdup (details);
	g_ptr_array_add (watch->priv->cached_messages, cached_message);

	/* close existing */
	if (watch->priv->notification_cached_messages != NULL) {
		ret = notify_notification_close (watch->priv->notification_cached_messages, &error);
		if (!ret) {
			egg_warning ("error: %s", error->message);
			g_error_free (error);
			error = NULL;
		}
	}

	/* are we accepting notifications */
	value = g_settings_get_boolean (watch->priv->settings, GPK_SETTINGS_NOTIFY_MESSAGE);
	if (!value) {
		egg_debug ("not showing notification as prevented in settings");
		goto out;
	}

	/* do the bubble */
	notification = notify_notification_new_with_status_icon (_("New package manager message"), NULL, "emblem-important", watch->priv->status_icon);
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_NEVER);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
	watch->priv->notification_cached_messages = notification;
out:
	g_free (details);
}

/**
 * gpk_watch_process_error_code:
 **/
static void
gpk_watch_process_error_code (GpkWatch *watch, PkError *error_code)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *title;
	gchar *title_prefix = NULL;
	const gchar *message;
	gboolean value;
	NotifyNotification *notification;
	PkErrorEnum code;

	g_return_if_fail (GPK_IS_WATCH (watch));

	code = pk_error_get_code (error_code);
	title = gpk_error_enum_to_localised_text (code);

	/* ignore some errors */
	if (code == PK_ERROR_ENUM_NOT_SUPPORTED ||
	    code == PK_ERROR_ENUM_NO_NETWORK ||
	    code == PK_ERROR_ENUM_PROCESS_KILL ||
	    code == PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
		egg_debug ("error ignored %s%s", title, pk_error_get_details (error_code));
		goto out;
	}

	/* are we accepting notifications */
	value = g_settings_get_boolean (watch->priv->settings, GPK_SETTINGS_NOTIFY_ERROR);
	if (!value) {
		egg_debug ("not showing notification as prevented in settings");
		goto out;
	}

	/* we need to format this */
	message = gpk_error_enum_to_localised_message (code);

	/* save this globally */
	g_free (watch->priv->error_details);
	watch->priv->error_details = g_markup_escape_text (pk_error_get_details (error_code), -1);

	/* TRANSLATORS: Prefix to the title shown in the libnotify popup */
	title_prefix = g_strdup_printf ("%s: %s", _("Package Manager"), title);

	/* do the bubble */
	notification = notify_notification_new (title_prefix, message, "help-browser", NULL);
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, "show-error-details",
					/* TRANSLATORS: This is a link in a libnotify bubble that shows the detailed error */
					_("Show details"), gpk_watch_libnotify_cb, watch, NULL);

	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
out:
	g_free (title_prefix);
}

/**
 * gpk_watch_process_require_restart_cb:
 **/
static void
gpk_watch_process_require_restart_cb (PkRequireRestart *item, GpkWatch *watch)
{
	GPtrArray *array = NULL;
	GPtrArray *names = NULL;
	const gchar *name;
	gchar **split = NULL;
	guint i;
	PkRestartEnum restart;
	gchar *package_id = NULL;

	/* get data */
	g_object_get (item,
		      "restart", &restart,
		      "package-id", &package_id,
		      NULL);

	/* if less important than what we are already showing */
	if (restart <= watch->priv->restart) {
		egg_debug ("restart already %s, not processing %s",
			   pk_restart_enum_to_text (watch->priv->restart),
			   pk_restart_enum_to_text (restart));
		goto out;
	}

	/* save new restart */
	watch->priv->restart = restart;

	/* add name if not already in the list */
	split = pk_package_id_split (package_id);
	names = watch->priv->restart_package_names;
	for (i=0; i<names->len; i++) {
		name = g_ptr_array_index (names, i);
		if (g_strcmp0 (name, split[PK_PACKAGE_ID_NAME]) == 0) {
			egg_debug ("already got %s", name);
			goto out;
		}
	}

	/* add to list */
	egg_debug ("adding %s to restart list", split[PK_PACKAGE_ID_NAME]);
	g_ptr_array_add (names, g_strdup (split[PK_PACKAGE_ID_NAME]));
out:
	g_free (package_id);
	g_strfreev (split);
	if (array != NULL)
		g_object_unref (array);
}

/**
 * gpk_watch_adopt_cb:
 **/
static void
gpk_watch_adopt_cb (PkClient *client, GAsyncResult *res, GpkWatch *watch)
{
	const gchar *message = NULL;
	gboolean caller_active;
	gboolean ret;
	gchar *transaction_id = NULL;
	GError *error = NULL;
	GPtrArray *array;
	guint elapsed_time;
	NotifyNotification *notification;
	PkProgress *progress = NULL;
	PkResults *results;
	PkRoleEnum role;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to adopt: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get data about the transaction */
	g_object_get (results,
		      "role", &role,
		      "progress", &progress,
		      NULL);

	/* get data */
	g_object_get (progress,
		      "transaction-id", &transaction_id,
		      "caller-active", &caller_active,
		      "elapsed-time", &elapsed_time,
		      NULL);

	egg_debug ("%s finished (%s)", transaction_id, pk_role_enum_to_text (role));

	/* get the error */
	error_code = pk_results_get_error_code (results);

	/* is the watched transaction */
	if (g_strcmp0 (transaction_id, watch->priv->transaction_id) == 0) {
		egg_debug ("watched transaction %s", watch->priv->transaction_id);

		/* stop spinning */
		gpk_modal_dialog_set_percentage (watch->priv->dialog, 100);

		/* autoclose if success */
		if (error_code == NULL)
			gpk_modal_dialog_close (watch->priv->dialog);
	}

	/* process messages */
	if (error_code == NULL) {
		array = pk_results_get_message_array (results);
		g_ptr_array_foreach (array, (GFunc) gpk_watch_process_messages_cb, watch);
		g_ptr_array_unref (array);
	}

	/* only process errors if caller is no longer on the bus */
	if (error_code != NULL && !caller_active)
		gpk_watch_process_error_code (watch, error_code);

	/* process restarts */
	if (role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		array = pk_results_get_require_restart_array (results);
		g_ptr_array_foreach (array, (GFunc) gpk_watch_process_require_restart_cb, watch);
		g_ptr_array_unref (array);
	}

	/* are we accepting notifications */
	ret = g_settings_get_boolean (watch->priv->settings, GPK_SETTINGS_NOTIFY_COMPLETED);
	if (!ret) {
		egg_debug ("not showing notification as prevented in settings");
		goto out;
	}

	/* is it worth showing a UI? */
	if (elapsed_time < 3000) {
		egg_debug ("no notification, too quick");
		goto out;
	}

	/* is caller able to handle the messages itself? */
	if (caller_active) {
		egg_debug ("not showing notification as caller is still present");
		goto out;
	}

	if (role == PK_ROLE_ENUM_REMOVE_PACKAGES)
		/* TRANSLATORS: This is the message in the libnotify body */
		message = _("Packages have been removed");
	else if (role == PK_ROLE_ENUM_INSTALL_PACKAGES)
		/* TRANSLATORS: This is the message in the libnotify body */
		message = _("Packages have been installed");
	else if (role == PK_ROLE_ENUM_UPDATE_SYSTEM)
		/* TRANSLATORS: This is the message in the libnotify body */
		message = _("System has been updated");

	/* nothing of interest */
	if (message == NULL)
		goto out;

	/* TRANSLATORS: title: an action has finished, and we are showing the libnotify bubble */
	notification = notify_notification_new (_("Task completed"), message, "help-browser", NULL);
	notify_notification_set_timeout (notification, 5000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, "do-not-show-notify-complete",
					_("Do not show this again"), gpk_watch_libnotify_cb, watch, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
out:
	g_free (transaction_id);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (progress != NULL)
		g_object_unref (progress);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_watch_progress_cb:
 **/
static void
gpk_watch_progress_cb (PkProgress *progress, PkProgressType type, GpkWatch *watch)
{
	PkStatusEnum status;
	guint percentage;
	gboolean allow_cancel;
	gchar *package_id = NULL;
	gchar *transaction_id = NULL;
	GPtrArray *array;
	guint i;
	gboolean ret = FALSE;
	PkProgress *progress_tmp;
	guint remaining_time;
	gchar *text = NULL;

	/* add if not already in list */
	array = watch->priv->array_progress;
	for (i=0; i<array->len; i++) {
		progress_tmp = g_ptr_array_index (array, i);
		if (progress_tmp == progress)
			ret = TRUE;
	}
	if (!ret) {
		egg_debug ("adding progress %p", progress);
		g_ptr_array_add (array, g_object_ref (progress));
	}

	/* get data */
	g_object_get (progress,
		      "status", &status,
		      "percentage", &percentage,
		      "allow-cancel", &allow_cancel,
		      "package-id", &package_id,
		      "remaining-time", &remaining_time,
		      "transaction-id", &transaction_id,
		      NULL);

	/* refresh both */
	if (type == PK_PROGRESS_TYPE_STATUS) {
		gpk_watch_refresh_icon (watch);
		gpk_watch_refresh_tooltip (watch);
	}

	/* is not the watched transaction */
	if (g_strcmp0 (transaction_id, watch->priv->transaction_id) != 0)
		goto out;

	if (type == PK_PROGRESS_TYPE_PACKAGE_ID) {
		text = gpk_package_id_format_twoline (package_id, NULL);
		gpk_modal_dialog_set_message (watch->priv->dialog, text);
	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		gpk_modal_dialog_set_percentage (watch->priv->dialog, percentage);
	} else if (type == PK_PROGRESS_TYPE_REMAINING_TIME) {
		gpk_modal_dialog_set_remaining (watch->priv->dialog, remaining_time);
	} else if (type == PK_PROGRESS_TYPE_ALLOW_CANCEL) {
		gpk_modal_dialog_set_allow_cancel (watch->priv->dialog, allow_cancel);
	} else if (type == PK_PROGRESS_TYPE_STATUS) {
		gpk_watch_set_status (watch, status);
	}
out:
	g_free (text);
	g_free (package_id);
	g_free (transaction_id);
}

/**
 * gpk_watch_transaction_list_added_cb:
 **/
static void
gpk_watch_transaction_list_added_cb (PkTransactionList *tlist, const gchar *transaction_id, GpkWatch *watch)
{
	PkProgress *progress;

	/* find progress */
	progress = gpk_watch_lookup_progress_from_transaction_id (watch, transaction_id);
	if (progress != NULL) {
		egg_warning ("already added: %s", transaction_id);
		return;
	}
	egg_debug ("added: %s", transaction_id);
	pk_client_adopt_async (PK_CLIENT(watch->priv->task), transaction_id, watch->priv->cancellable,
			       (PkProgressCallback) gpk_watch_progress_cb, watch,
			       (GAsyncReadyCallback) gpk_watch_adopt_cb, watch);
}

/**
 * gpk_watch_transaction_list_removed_cb:
 **/
static void
gpk_watch_transaction_list_removed_cb (PkTransactionList *tlist, const gchar *transaction_id, GpkWatch *watch)
{
	PkProgress *progress;

	/* find progress */
	progress = gpk_watch_lookup_progress_from_transaction_id (watch, transaction_id);
	if (progress == NULL) {
		egg_warning ("could not find: %s", transaction_id);
		return;
	}
	egg_debug ("removed: %s", transaction_id);
	g_ptr_array_remove_fast (watch->priv->array_progress, progress);

	/* refresh both */
	gpk_watch_refresh_icon (watch);
	gpk_watch_refresh_tooltip (watch);
}

/**
 * gpk_check_update_get_properties_cb:
 **/
static void
gpk_check_update_get_properties_cb (GObject *object, GAsyncResult *res, GpkWatch *watch)
{
	gboolean connected;
	GError *error = NULL;
	PkControl *control = PK_CONTROL(object);
	gboolean ret;

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	if (!ret) {
		/* TRANSLATORS: backend is broken, and won't tell us what it supports */
		egg_warning ("details could not be retrieved: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get values */
	g_object_get (control,
		      "connected", &connected,
		      NULL);

	/* coldplug daemon */
	gpk_watch_set_connected (watch, connected);
out:
	return;
}

/**
 * gpk_watch_init:
 * @watch: This class instance
 **/
static void
gpk_watch_init (GpkWatch *watch)
{
	watch->priv = GPK_WATCH_GET_PRIVATE (watch);
	watch->priv->error_details = NULL;
	watch->priv->notification_cached_messages = NULL;
	watch->priv->transaction_id = NULL;
	watch->priv->restart = PK_RESTART_ENUM_NONE;
	watch->priv->hide_warning = FALSE;
	watch->priv->console = egg_console_kit_new ();
	watch->priv->cancellable = g_cancellable_new ();
	watch->priv->array_progress = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	watch->priv->settings = g_settings_new (GPK_SETTINGS_SCHEMA);
	g_signal_connect (watch->priv->settings, "changed", G_CALLBACK (gpk_watch_key_changed_cb), watch);

	watch->priv->status_icon = gtk_status_icon_new ();
	watch->priv->set_proxy_id = 0;
	watch->priv->cached_messages = g_ptr_array_new_with_free_func ((GDestroyNotify) gpk_watch_cached_message_free);
	watch->priv->restart_package_names = g_ptr_array_new_with_free_func (g_free);
	watch->priv->task = PK_TASK(gpk_task_new ());
	g_object_set (watch->priv->task,
		      "background", TRUE,
		      NULL);
	watch->priv->dialog = gpk_modal_dialog_new ();
	gpk_modal_dialog_set_window_icon (watch->priv->dialog, "pk-package-installed");
	g_signal_connect (watch->priv->dialog, "cancel",
			  G_CALLBACK (gpk_watch_button_cancel_cb), watch);
	g_signal_connect (watch->priv->dialog, "close",
			  G_CALLBACK (gpk_watch_button_close_cb), watch);

	/* we need to get ::locked */
	watch->priv->control = pk_control_new ();
	g_signal_connect (watch->priv->control, "notify::locked",
			  G_CALLBACK (gpk_watch_notify_locked_cb), watch);
	g_signal_connect (watch->priv->control, "notify::connected",
			  G_CALLBACK (gpk_watch_notify_connected_cb), watch);

	/* get properties */
	pk_control_get_properties_async (watch->priv->control, NULL, (GAsyncReadyCallback) gpk_check_update_get_properties_cb, watch);

	/* do session inhibit */
	watch->priv->inhibit = gpk_inhibit_new ();

	/* right click actions are common */
	g_signal_connect_object (G_OBJECT (watch->priv->status_icon),
				 "popup_menu", G_CALLBACK (gpk_watch_popup_menu_cb), watch, 0);
	g_signal_connect_object (G_OBJECT (watch->priv->status_icon),
				 "activate", G_CALLBACK (gpk_watch_activate_status_cb), watch, 0);

	watch->priv->tlist = pk_transaction_list_new ();
	g_signal_connect (watch->priv->tlist, "added",
			  G_CALLBACK (gpk_watch_transaction_list_added_cb), watch);
	g_signal_connect (watch->priv->tlist, "removed",
			  G_CALLBACK (gpk_watch_transaction_list_removed_cb), watch);

	/* set the proxy */
	gpk_watch_set_proxies (watch);
	gpk_watch_set_root (watch);
}

/**
 * gpk_watch_finalize:
 * @object: The object to finalize
 **/
static void
gpk_watch_finalize (GObject *object)
{
	GpkWatch *watch;

	g_return_if_fail (GPK_IS_WATCH (object));

	watch = GPK_WATCH (object);

	g_return_if_fail (watch->priv != NULL);

	/* we might we waiting for a proxy update */
	if (watch->priv->set_proxy_id != 0)
		g_source_remove (watch->priv->set_proxy_id);

	g_free (watch->priv->error_details);
	g_free (watch->priv->transaction_id);
	g_object_unref (watch->priv->cancellable);
	g_object_unref (PK_CLIENT(watch->priv->task));
	g_object_unref (watch->priv->console);
	g_object_unref (watch->priv->control);
	g_object_unref (watch->priv->dialog);
	g_object_unref (watch->priv->settings);
	g_object_unref (watch->priv->inhibit);
	g_object_unref (watch->priv->status_icon);
	g_object_unref (watch->priv->tlist);
	g_ptr_array_unref (watch->priv->array_progress);
	g_ptr_array_unref (watch->priv->cached_messages);
	g_ptr_array_unref (watch->priv->restart_package_names);

	G_OBJECT_CLASS (gpk_watch_parent_class)->finalize (object);
}

/**
 * gpk_watch_new:
 *
 * Return value: a new GpkWatch object.
 **/
GpkWatch *
gpk_watch_new (void)
{
	GpkWatch *watch;
	watch = g_object_new (GPK_TYPE_WATCH, NULL);
	return GPK_WATCH (watch);
}

