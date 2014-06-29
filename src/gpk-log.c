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

#include <glib.h>
#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <locale.h>
#include <sys/types.h>
#include <pwd.h>

#include <packagekit-glib2/packagekit.h>

#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-debug.h"

static GtkBuilder *builder = NULL;
static GtkListStore *list_store = NULL;
static PkClient *client = NULL;
static gchar *transaction_id = NULL;
static gchar *filter = NULL;
static GPtrArray *transactions = NULL;
static GtkTreePath *path_global = NULL;
static guint xid = 0;

enum
{
	GPK_LOG_COLUMN_ICON,
	GPK_LOG_COLUMN_TIMESPEC,
	GPK_LOG_COLUMN_DATE,
	GPK_LOG_COLUMN_DATE_TEXT,
	GPK_LOG_COLUMN_ROLE,
	GPK_LOG_COLUMN_DETAILS,
	GPK_LOG_COLUMN_ID,
	GPK_LOG_COLUMN_USER,
	GPK_LOG_COLUMN_TOOL,
	GPK_LOG_COLUMN_ACTIVE,
	GPK_LOG_COLUMN_LAST
};

/**
 * gpk_log_find_iter_model_cb:
 **/
static gboolean
gpk_log_find_iter_model_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, const gchar *id)
{
	gchar *id_tmp = NULL;
	gtk_tree_model_get (model, iter, GPK_LOG_COLUMN_ID, &id_tmp, -1);
	if (strcmp (id_tmp, id) == 0) {
		path_global = gtk_tree_path_copy (path);
		return TRUE;
	}
	return FALSE;
}

/**
 * gpk_log_mark_nonactive_cb:
 **/
static gboolean
gpk_log_mark_nonactive_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	gtk_list_store_set (GTK_LIST_STORE(model), iter, GPK_LOG_COLUMN_ACTIVE, FALSE, -1);
	return FALSE;
}

/**
 * gpk_log_mark_nonactive:
 **/
static void
gpk_log_mark_nonactive (GtkTreeModel *model)
{
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_log_mark_nonactive_cb, NULL);
}

/**
 * gpk_log_remove_nonactive_cb:
 **/
static gboolean
gpk_log_remove_nonactive_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gboolean *ret)
{
	gboolean active;
	gtk_tree_model_get (model, iter, GPK_LOG_COLUMN_ACTIVE, &active, -1);
	if (!active) {
		*ret = TRUE;
		gtk_list_store_remove (GTK_LIST_STORE(model), iter);
		return TRUE;
	}
	return FALSE;
}

/**
 * gpk_log_remove_nonactive:
 **/
static void
gpk_log_remove_nonactive (GtkTreeModel *model)
{
	gboolean ret;
	/* do this again and again as removing in gtk_tree_model_foreach causes errors */
	do {
		ret = FALSE;
		gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_log_remove_nonactive_cb, &ret);
	} while (ret);
}

/**
 * gpk_log_model_get_iter:
 **/
static gboolean
gpk_log_model_get_iter (GtkTreeModel *model, GtkTreeIter *iter, const gchar *id)
{
	gboolean ret = TRUE;
	path_global = NULL;
	gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) gpk_log_find_iter_model_cb, (gpointer) id);
	if (path_global == NULL) {
		gtk_list_store_append (GTK_LIST_STORE(model), iter);
	} else {
		ret = gtk_tree_model_get_iter (model, iter, path_global);
		gtk_tree_path_free (path_global);
	}
	return ret;
}

/**
 * gpk_log_get_localised_date:
 **/
static gchar *
gpk_log_get_localised_date (const gchar *timespec)
{
	GDate *date;
	GTimeVal timeval;
	gchar buffer[100];

	/* the old date */
	g_time_val_from_iso8601 (timespec, &timeval);

	/* get printed string */
	date = g_date_new ();
	g_date_set_time_val (date, &timeval);

	/* TRANSLATORS: strftime formatted please */
	g_date_strftime (buffer, 100, _("%d %B %Y"), date);

	g_date_free (date);
	return g_strdup (buffer);
}

/**
 * gpk_log_get_type_line:
 **/
static gchar *
gpk_log_get_type_line (gchar **array, PkInfoEnum info)
{
	guint i;
	guint size;
	PkInfoEnum info_local;
	const gchar *info_text;
	GString *string;
	gchar *text;
	gchar *whole;
	gchar **sections;

	string = g_string_new ("");
	size = g_strv_length (array);
	info_text = gpk_info_enum_to_localised_past (info);

	/* find all of this type */
	for (i=0; i<size; i++) {
		sections = g_strsplit (array[i], "\t", 0);
		info_local = pk_info_enum_from_string (sections[0]);
		if (info_local == info) {
			text = gpk_package_id_format_oneline (sections[1], NULL);
			g_string_append_printf (string, "%s, ", text);
			g_free (text);
		}
		g_strfreev (sections);
	}

	/* nothing, so return NULL */
	if (string->len == 0) {
		g_string_free (string, TRUE);
		return NULL;
	}

	/* remove last comma space */
	g_string_set_size (string, string->len - 2);

	/* add a nice header, and make text italic */
	text = g_string_free (string, FALSE);
	whole = g_strdup_printf ("<b>%s</b>: %s\n", info_text, text);
	g_free (text);
	return whole;
}

/**
 * gpk_log_get_details_localised:
 **/
static gchar *
gpk_log_get_details_localised (const gchar *timespec, const gchar *data)
{
	GString *string;
	gchar *text;
	gchar **array;

	string = g_string_new ("");
	array = g_strsplit (data, "\n", 0);

	/* get each type */
	text = gpk_log_get_type_line (array, PK_INFO_ENUM_INSTALLING);
	if (text != NULL)
		g_string_append (string, text);
	g_free (text);
	text = gpk_log_get_type_line (array, PK_INFO_ENUM_REMOVING);
	if (text != NULL)
		g_string_append (string, text);
	g_free (text);
	text = gpk_log_get_type_line (array, PK_INFO_ENUM_UPDATING);
	if (text != NULL)
		g_string_append (string, text);
	g_free (text);
	g_strfreev (array);

	/* remove last \n */
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

/**
 * gpk_log_treeview_size_allocate_cb:
 **/
static void
gpk_log_treeview_size_allocate_cb (GtkWidget *widget, GtkAllocation *allocation, GtkCellRenderer *cell)
{
	GtkTreeViewColumn *column;
	gint width;

	column = gtk_tree_view_get_column (GTK_TREE_VIEW(widget), 2);
	width = gtk_tree_view_column_get_width (column);
	g_object_set (cell, "wrap-width", width - 10, NULL);
}

/**
 * pk_treeview_add_general_columns:
 **/
static void
pk_treeview_add_general_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* --- column for date --- */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "yalign", 0.0, NULL);
	/* TRANSLATORS: column for the date */
	column = gtk_tree_view_column_new_with_attributes (_("Date"), renderer,
							   "markup", GPK_LOG_COLUMN_DATE_TEXT, NULL);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, FALSE);
	gtk_tree_view_column_set_sort_column_id (column, GPK_LOG_COLUMN_DATE);

	/* --- column for image and text --- */
	column = gtk_tree_view_column_new ();
	/* TRANSLATORS: column for what was done, e.g. update-system */
	gtk_tree_view_column_set_title (column, _("Action"));

	/* image */
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_BUTTON, NULL);
	g_object_set (renderer, "yalign", 0.0, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "icon-name", GPK_LOG_COLUMN_ICON);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "yalign", 0.0, NULL);

	/* text */
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", GPK_LOG_COLUMN_ROLE);
	gtk_tree_view_column_set_expand (column, FALSE);
	gtk_tree_view_column_set_sort_column_id (column, GPK_LOG_COLUMN_ROLE);

	gtk_tree_view_append_column (treeview, GTK_TREE_VIEW_COLUMN(column));

	/* --- column for details --- */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "yalign", 0.0, NULL);
	g_object_set (renderer, "wrap-mode", PANGO_WRAP_WORD, NULL);
	g_object_set (renderer, "wrap-width", 400, NULL);
	g_signal_connect (treeview, "size-allocate", G_CALLBACK (gpk_log_treeview_size_allocate_cb), renderer);
	/* TRANSLATORS: column for what packages were upgraded */
	column = gtk_tree_view_column_new_with_attributes (_("Details"), renderer,
							   "markup", GPK_LOG_COLUMN_DETAILS, NULL);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, TRUE);

	/* TRANSLATORS: column for the user name, e.g. Richard Hughes */
	column = gtk_tree_view_column_new_with_attributes (_("User name"), renderer,
							   "markup", GPK_LOG_COLUMN_USER, NULL);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, FALSE);
	gtk_tree_view_column_set_sort_column_id (column, GPK_LOG_COLUMN_USER);

	/* TRANSLATORS: column for the application used for the install, e.g. Add/Remove Programs */
	column = gtk_tree_view_column_new_with_attributes (_("Application"), renderer,
							   "markup", GPK_LOG_COLUMN_TOOL, NULL);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, FALSE);
	gtk_tree_view_column_set_sort_column_id (column, GPK_LOG_COLUMN_TOOL);
}

/**
 * gpk_log_treeview_clicked_cb:
 **/
static void
gpk_log_treeview_clicked_cb (GtkTreeSelection *selection, gpointer data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *id;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (transaction_id);
		gtk_tree_model_get (model, &iter, GPK_LOG_COLUMN_ID, &id, -1);

		/* show transaction_id */
		g_debug ("selected row is: %s", id);
		g_free (id);
	} else {
		g_debug ("no row selected");
	}
}

/**
 * gpk_log_filter:
 **/
static gboolean
gpk_log_filter (PkTransactionPast *item)
{
	gboolean ret = FALSE;
	guint i;
	guint length;
	gchar **sections;
	gchar **packages;
	gchar **split;
	gchar *tid;
	gboolean succeeded;
	gchar *cmdline;
	gchar *data;

	/* get data */
	g_object_get (item,
		      "tid", &tid,
		      "succeeded", &succeeded,
		      "cmdline", &cmdline,
		      "data", &data,
		      NULL);

	/* only show transactions that succeeded */
	if (!succeeded) {
		g_debug ("tid %s did not succeed, so not adding", tid);
		return FALSE;
	}

	if (filter == NULL)
		return TRUE;

	/* matches cmdline */
	if (cmdline != NULL && g_strrstr (cmdline, filter) != NULL)
		ret = TRUE;

	/* look in all the data for the filter string */
	packages = g_strsplit (data, "\n", 0);
	length = g_strv_length (packages);
	for (i=0; i<length; i++) {
		sections = g_strsplit (packages[i], "\t", 0);

		/* check if type matches filter */
		if (g_strrstr (sections[0], filter) != NULL)
			ret = TRUE;

		/* check to see if package name, version or arch matches */
		split = pk_package_id_split (sections[1]);
		if (g_strrstr (split[0], filter) != NULL)
			ret = TRUE;
		if (split[1] != NULL && g_strrstr (split[1], filter) != NULL)
			ret = TRUE;
		if (split[2] != NULL && g_strrstr (split[2], filter) != NULL)
			ret = TRUE;

		g_strfreev (split);
		g_strfreev (sections);

		/* shortcut for speed */
		if (ret)
			break;
	}

	g_free (tid);
	g_free (cmdline);
	g_free (data);
	g_strfreev (packages);

	return ret;
}

/**
 * gpk_log_add_item
 **/
static void
gpk_log_add_item (PkTransactionPast *item)
{
	GtkTreeIter iter;
	gchar *details;
	gchar *date;
	const gchar *icon_name;
	const gchar *role_text;
	const gchar *username = NULL;
	const gchar *tool;
	static guint count;
	struct passwd *pw;
	gchar *tid;
	gchar *timespec;
	gboolean succeeded;
	guint duration;
	gchar *cmdline;
	guint uid;
	gchar *data;
	PkRoleEnum role;
	GtkTreeView *treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_simple"));
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);

	/* get data */
	g_object_get (item,
		      "role", &role,
		      "tid", &tid,
		      "timespec", &timespec,
		      "succeeded", &succeeded,
		      "duration", &duration,
		      "cmdline", &cmdline,
		      "uid", &uid,
		      "data", &data,
		      NULL);

	/* put formatted text into treeview */
	details = gpk_log_get_details_localised (timespec, data);
	date = gpk_log_get_localised_date (timespec);

	icon_name = gpk_role_enum_to_icon_name (role);
	role_text = gpk_role_enum_to_localised_past (role);

	/* query real name */
	pw = getpwuid(uid);
	if (pw != NULL) {
		if (pw->pw_gecos != NULL)
			username = pw->pw_gecos;
		else if (pw->pw_name != NULL)
			username = pw->pw_name;
	}

	/* get nice name for tool name */
	if (strstr (cmdline, "pkcon") != NULL)
		/* TRANSLATORS: user-friendly name for pkcon */
		tool = _("Command line client");
	else if (strstr (cmdline, "gpk-application") != NULL)
		/* TRANSLATORS: user-friendly name for gpk-update-viewer */
		tool = _("GNOME Packages");
	else if (strstr (cmdline, "gpk-update-viewer") != NULL)
		/* TRANSLATORS: user-friendly name for gpk-update-viewer */
		tool = _("GNOME Package Updater");
	else if (strstr (cmdline, "gpk-update-icon") != NULL)
		/* TRANSLATORS: user-friendly name for gpk-update-icon, which used to exist */
		tool = _("Update Icon");
	else if (strstr (cmdline, "pk-command-not-found") != NULL)
		/* TRANSLATORS: user-friendly name for the command not found plugin */
		tool = _("Bash – Command Not Found");
	else if (strstr (cmdline, "gnome-settings-daemon") != NULL)
		/* TRANSLATORS: user-friendly name for gnome-settings-daemon, which used to handle updates */
		tool = _("GNOME Session");
	else if (strstr (cmdline, "gnome-software") != NULL)
		/* TRANSLATORS: user-friendly name for gnome-software */
		tool = _("GNOME Software");
	else
		tool = cmdline;

	gpk_log_model_get_iter (model, &iter, tid);
	gtk_list_store_set (list_store, &iter,
			    GPK_LOG_COLUMN_ICON, icon_name,
			    GPK_LOG_COLUMN_TIMESPEC, timespec,
			    GPK_LOG_COLUMN_DATE_TEXT, date,
			    GPK_LOG_COLUMN_DATE, timespec,
			    GPK_LOG_COLUMN_ROLE, role_text,
			    GPK_LOG_COLUMN_DETAILS, details,
			    GPK_LOG_COLUMN_ID, tid,
			    GPK_LOG_COLUMN_USER, username,
			    GPK_LOG_COLUMN_TOOL, tool,
			    GPK_LOG_COLUMN_ACTIVE, TRUE, -1);

	/* spin the gui */
	if (count++ % 10 == 0)
		while (gtk_events_pending ())
			gtk_main_iteration ();

	g_free (tid);
	g_free (timespec);
	g_free (cmdline);
	g_free (data);
	g_free (details);
	g_free (date);
}

/**
 * gpk_log_refilter
 **/
static void
gpk_log_refilter (void)
{
	guint i;
	gboolean ret;
	PkTransactionPast *item;
	GtkWidget *widget;
	const gchar *package;
	GtkTreeView *treeview;
	GtkTreeModel *model;

	/* set the new filter */
	g_free (filter);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
	package = gtk_entry_get_text (GTK_ENTRY(widget));
	if (package != NULL && package[0] != '\0')
		filter = g_strdup (package);
	else
		filter = NULL;

	g_debug ("len=%i", transactions->len);

	/* mark the items as not used */
	treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder, "treeview_simple"));
	model = gtk_tree_view_get_model (treeview);
	gpk_log_mark_nonactive (model);

	/* go through the list, adding and removing the items as required */
	for (i=0; i<transactions->len; i++) {
		item = g_ptr_array_index (transactions, i);
		ret = gpk_log_filter (item);
		if (ret)
			gpk_log_add_item (item);
	}

	/* remove the items that are not used */
	gpk_log_remove_nonactive (model);
}

/**
 * gpk_log_get_old_transactions_cb
 **/
static void
gpk_log_get_old_transactions_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
//	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to get old transactions: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get old transactions: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* get the list */
	if (transactions != NULL)
		g_ptr_array_unref (transactions);
	transactions = pk_results_get_transaction_array (results);
	gpk_log_refilter ();
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_log_refresh
 **/
static void
gpk_log_refresh (void)
{
	/* get the list async */
	pk_client_get_old_transactions_async (client, 0, NULL, NULL, NULL,
					      (GAsyncReadyCallback) gpk_log_get_old_transactions_cb, NULL);
}

/**
 * gpk_log_button_refresh_cb:
 **/
static void
gpk_log_button_refresh_cb (GtkWidget *widget, gpointer data)
{
	/* refresh */
	gpk_log_refresh ();
}

/**
 * gpk_log_button_filter_cb:
 **/
static void
gpk_log_button_filter_cb (GtkWidget *widget2, gpointer data)
{
	gpk_log_refilter ();
}

/**
 * gpk_log_entry_filter_cb:
 **/
static gboolean
gpk_log_entry_filter_cb (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	gpk_log_refilter ();
	return FALSE;
}

/**
 * gpk_log_button_close_cb:
 **/
static void
gpk_log_button_close_cb (GtkWidget *widget, GtkApplication *application)
{
	g_application_release (G_APPLICATION (application));
}

/**
 * gpk_log_activate_cb:
 **/
static void
gpk_log_activate_cb (GtkApplication *application, gpointer user_data)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_simple"));
	gtk_window_present (window);
}

/**
 * gpk_log_startup_cb:
 **/
static void
gpk_log_startup_cb (GtkApplication *application, gpointer user_data)
{
	gboolean ret;
	GError *error = NULL;
	GSettings *settings;
	GtkEntryCompletion *completion;
	GtkTreeSelection *selection;
	GtkWidget *widget;
	GtkWindow *window;
	guint retval;

	client = pk_client_new ();
	g_object_set (client,
		      "background", FALSE,
		      NULL);

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GPK_DATA "/gpk-log.ui", &error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out;
	}

	window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_simple"));
	gtk_window_set_icon_name (window, GPK_ICON_SOFTWARE_LOG);
	gtk_window_set_application (window, application);

	/* set a size, as the screen allows */
	gpk_window_set_size_request (window, 1200, 1200);

	/* if command line arguments are set, then setup UI */
	if (filter != NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
		gtk_entry_set_text (GTK_ENTRY(widget), filter);
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_log_button_close_cb), application);
	gtk_widget_grab_default (widget);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_refresh"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_log_button_refresh_cb), NULL);
	gtk_widget_hide (widget);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_filter"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_log_button_filter_cb), NULL);

	/* hit enter in the search box for filter */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
	g_signal_connect (widget, "activate", G_CALLBACK (gpk_log_button_filter_cb), NULL);

	/* autocompletion can be turned off as it's slow */
	settings = g_settings_new (GPK_SETTINGS_SCHEMA);
	ret = g_settings_get_boolean (settings, GPK_SETTINGS_AUTOCOMPLETE);
	if (ret) {
		/* create the completion object */
		completion = gpk_package_entry_completion_new ();
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "entry_package"));
		gtk_entry_set_completion (GTK_ENTRY (widget), completion);
		g_object_unref (completion);
	} else {
		/* use search as you type */
		g_signal_connect (widget, "key-release-event", G_CALLBACK (gpk_log_entry_filter_cb), NULL);
	}
	g_object_unref (settings);

	/* create list stores */
	list_store = gtk_list_store_new (GPK_LOG_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING,
					 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);

	/* create transaction_id tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_simple"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_log_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	pk_treeview_add_general_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (list_store),
					      GPK_LOG_COLUMN_TIMESPEC, GTK_SORT_DESCENDING);

	/* show */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_simple"));
	gtk_widget_show (widget);

	/* set the parent window if it is specified */
	if (xid != 0) {
		g_debug ("Setting xid %i", xid);
		gpk_window_set_parent_xid (GTK_WINDOW (widget), xid);
	}

	/* get the update list */
	gpk_log_refresh ();
out:
	g_object_unref (list_store);
	g_object_unref (client);
	g_free (transaction_id);
	g_free (filter);
	if (transactions != NULL)
		g_ptr_array_unref (transactions);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean ret;
	gint status = 1;
	GOptionContext *context;
	GtkApplication *application = NULL;

	const GOptionEntry options[] = {
		{ "filter", 'f', 0, G_OPTION_ARG_STRING, &filter,
		  /* TRANSLATORS: preset the GtktextBox with this filter text */
		  N_("Set the filter to this value"), NULL },
		{ "parent-window", 'p', 0, G_OPTION_ARG_INT, &xid,
		  /* TRANSLATORS: we can make this modal (stay on top of) another window */
		  _("Set the parent window to make this modal"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("Software Log Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, gpk_debug_get_option_group ());
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	/* are we running privileged */
	ret = gpk_check_privileged_user (_("Log viewer"), TRUE);
	if (!ret)
		goto out;

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* are we already activated? */
	application = gtk_application_new ("org.freedesktop.PackageKit.LogViewer", 0);
	g_signal_connect (application, "startup",
			  G_CALLBACK (gpk_log_startup_cb), NULL);
	g_signal_connect (application, "activate",
			  G_CALLBACK (gpk_log_activate_cb), NULL);

	/* run */
	status = g_application_run (G_APPLICATION (application), argc, argv);
out:
	if (builder != NULL)
		g_object_unref (builder);
	if (application != NULL)
		g_object_unref (application);
	return status;
}
