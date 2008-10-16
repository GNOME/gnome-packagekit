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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <dbus/dbus-glib.h>
#include <locale.h>

#include <polkit-gnome/polkit-gnome.h>
#include <gconf/gconf-client.h>
#include <packagekit-glib/packagekit.h>

#include "egg-unique.h"
#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-enum.h"

static GladeXML *glade_xml = NULL;
static GtkListStore *list_store = NULL;
static PkClient *client = NULL;
static gchar *transaction_id = NULL;
static gchar *filter = NULL;
static PolKitGnomeAction *button_action = NULL;

enum
{
	GPK_LOG_COLUMN_ICON,
	GPK_LOG_COLUMN_DAY,
	GPK_LOG_COLUMN_DATE,
	GPK_LOG_COLUMN_ROLE,
	GPK_LOG_COLUMN_DURATION,
	GPK_LOG_COLUMN_DETAILS,
	GPK_LOG_COLUMN_ID,
	GPK_LOG_COLUMN_LAST
};

/**
 * gpk_log_button_help_cb:
 **/
static void
gpk_log_button_help_cb (GtkWidget *widget, gpointer data)
{
	gpk_gnome_help ("software-log");
}

/**
 * gpk_log_button_rollback_cb:
 **/
static void
gpk_log_button_rollback_cb (PolKitGnomeAction *action, gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	/* rollback */
	ret = pk_client_rollback (client, transaction_id, &error);
	if (!ret) {
		egg_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
	}
	gtk_main_quit ();
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

	g_date_strftime (buffer, 100, _("%A, %d %B %Y"), date);
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
	PkPackageId *id;

	string = g_string_new ("");
	size = g_strv_length (array);
	info_text = gpk_info_enum_to_localised_past (info);

	/* find all of this type */
	for (i=0; i<size; i++) {
		sections = g_strsplit (array[i], "\t", 0);
		info_local = pk_info_enum_from_text (sections[0]);
		if (info_local == info) {
			id = pk_package_id_new_from_string (sections[1]);
			text = gpk_package_id_format_oneline (id, NULL);
			g_string_append_printf (string, "%s, ", text);
			g_free (text);
			pk_package_id_free (id);
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
 * gpk_log_filter:
 **/
static gboolean
gpk_log_filter (const gchar *data)
{
	gboolean ret = FALSE;
	guint i;
	guint length;
	gchar **sections;
	gchar **packages;
	PkPackageId *id;

	if (filter == NULL)
		return TRUE;

	/* look in all the data for the filter string */
	packages = g_strsplit (data, "\n", 0);
	length = g_strv_length (packages);
	for (i=0; i<length; i++) {
		sections = g_strsplit (packages[i], "\t", 0);

		/* check if type matches filter */
		if (g_strrstr (sections[0], filter) != NULL)
			ret = TRUE;

		/* check to see if package name, version or arch matches */
		id = pk_package_id_new_from_string (sections[1]);
		if (g_strrstr (id->name, filter) != NULL)
			ret = TRUE;
		if (id->version != NULL && g_strrstr (id->version, filter) != NULL)
			ret = TRUE;
		if (id->arch != NULL && g_strrstr (id->arch, filter) != NULL)
			ret = TRUE;

		pk_package_id_free (id);
		g_strfreev (sections);

		/* shortcut for speed */
		if (ret)
			break;
	}

	g_strfreev (packages);

	return ret;
}


/**
 * gpk_log_transaction_cb:
 **/
static void
gpk_log_transaction_cb (PkClient *client, const PkTransactionObj *obj, gpointer user_data)
{
	GtkTreeIter iter;
	gchar *details;
	gchar *date;
	gchar **date_part;
	gchar *time;
	gboolean ret;
	const gchar *icon_name;
	const gchar *role_text;

	/* only show transactions that succeeded */
	if (!obj->succeeded) {
		egg_debug ("tid %s did not succeed, so not adding", obj->tid);
		return;
	}

	/* filter */
	ret = gpk_log_filter (obj->data);
	if (!ret) {
		egg_debug ("tid %s did not match, so not adding", obj->tid);
		return;
	}

	/* put formatted text into treeview */
	details = gpk_log_get_details_localised (obj->timespec, obj->data);
	date = gpk_log_get_localised_date (obj->timespec);
	date_part = g_strsplit (date, ", ", 2);

	if (obj->duration > 0)
		time = gpk_time_to_localised_string (obj->duration / 1000);
	else
		time = g_strdup (_("No data"));
	icon_name = gpk_role_enum_to_icon_name (obj->role);
	role_text = gpk_role_enum_to_localised_past (obj->role);

	gtk_list_store_append (list_store, &iter);
	gtk_list_store_set (list_store, &iter,
			    GPK_LOG_COLUMN_ICON, icon_name,
			    GPK_LOG_COLUMN_DAY, date_part[0],
			    GPK_LOG_COLUMN_DATE, date_part[1],
			    GPK_LOG_COLUMN_ROLE, role_text,
			    GPK_LOG_COLUMN_DURATION, time,
			    GPK_LOG_COLUMN_DETAILS, details,
			    GPK_LOG_COLUMN_ID, obj->tid, -1);

	/* spin the gui */
	while (gtk_events_pending ())
		gtk_main_iteration ();

	g_strfreev (date_part);
	g_free (details);
	g_free (date);
	g_free (time);
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
	column = gtk_tree_view_column_new_with_attributes (_("Date"), renderer,
							   "markup", GPK_LOG_COLUMN_DATE, NULL);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, FALSE);

	/* --- column for image and text --- */
	column = gtk_tree_view_column_new ();
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

	gtk_tree_view_append_column (treeview, GTK_TREE_VIEW_COLUMN(column));

	/* --- column for details --- */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "yalign", 0.0, NULL);
	g_object_set(renderer, "wrap-mode", PANGO_WRAP_WORD, NULL);
	g_object_set(renderer, "wrap-width", 400, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Details"), renderer,
							   "markup", GPK_LOG_COLUMN_DETAILS, NULL);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, TRUE);
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
		egg_debug ("selected row is: %s", id);
		g_free (id);
	} else {
		egg_debug ("no row selected");
	}
}

/**
 * gpk_update_viewer_create_custom_widget:
 **/
static GtkWidget *
gpk_update_viewer_create_custom_widget (GladeXML *xml, gchar *func_name, gchar *name,
				        gchar *string1, gchar *string2,
				        gint int1, gint int2, gpointer user_data)
{
	if (egg_strequal (name, "button_action"))
		return polkit_gnome_action_create_button (button_action);
	egg_warning ("name unknown=%s", name);
	return NULL;
}

/**
 * gpk_update_viewer_setup_policykit:
 *
 * We have to do this before the glade stuff if done as the custom handler needs the actions setup
 **/
static void
gpk_update_viewer_setup_policykit (void)
{
	PolKitAction *pk_action;
	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.system-rollback");
	button_action = polkit_gnome_action_new_default ("rollback", pk_action, _("_Rollback"), NULL);
	g_object_set (button_action,
		      "no-icon-name", "gtk-go-back-ltr",
		      "auth-icon-name", "gtk-go-back-ltr",
		      "yes-icon-name", "gtk-go-back-ltr",
		      "self-blocked-icon-name", "gtk-go-back-ltr",
		      NULL);
	polkit_action_unref (pk_action);
}

/**
 * gpk_log_activated_cb
 **/
static void
gpk_log_activated_cb (EggUnique *egg_unique, gpointer data)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (glade_xml, "window_simple");
	gtk_window_present (GTK_WINDOW (widget));
}

/**
 * gpk_log_refresh
 **/
static gboolean
gpk_log_refresh (void)
{
	gboolean ret;
	GError *error = NULL;
	gtk_list_store_clear (list_store);
	ret = pk_client_reset (client, &error);
	if (!ret) {
		egg_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = pk_client_get_old_transactions (client, 0, &error);
	if (!ret) {
		egg_warning ("failed to get list: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return ret;
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
	GtkWidget *widget;
	const gchar *package;

	/* set the new filter */
	g_free (filter);
	widget = glade_xml_get_widget (glade_xml, "entry_package");
	package = gtk_entry_get_text (GTK_ENTRY(widget));
	if (!egg_strzero (package))
		filter = g_strdup (package);
	else
		filter = NULL;

	/* refresh */
	gpk_log_refresh ();
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean verbose = FALSE;
	GOptionContext *context;
	GConfClient *gconf_client;
	GtkWidget *widget;
	GtkTreeSelection *selection;
	GtkEntryCompletion *completion;
	PkBitfield roles;
	PkControl *control;
	EggUnique *egg_unique;
	gboolean ret;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  N_("Show extra debugging information"), NULL },
		{ "filter", 'f', 0, G_OPTION_ARG_STRING, &filter,
		  N_("Set the filter to this value"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("Software Log Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);
	gtk_init (&argc, &argv);

	/* are we running privileged */
	ret = gpk_check_privileged_user (_("Log viewer"));
	if (!ret)
		return 1;

	/* are we already activated? */
	egg_unique = egg_unique_new ();
	ret = egg_unique_assign (egg_unique, "org.freedesktop.PackageKit.LogViewer");
	if (!ret)
		goto unique_out;

	g_signal_connect (egg_unique, "activated",
			  G_CALLBACK (gpk_log_activated_cb), NULL);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* we have to do this before we connect up the glade file */
	gpk_update_viewer_setup_policykit ();

	/* use custom widgets */
	glade_set_custom_handler (gpk_update_viewer_create_custom_widget, NULL);

	client = pk_client_new ();
	g_signal_connect (client, "transaction", G_CALLBACK (gpk_log_transaction_cb), NULL);

	/* get actions */
	control = pk_control_new ();
	roles = pk_control_get_actions (control, NULL);
	g_object_unref (control);

	glade_xml = glade_xml_new (GPK_DATA "/gpk-log.glade", NULL, NULL);
	widget = glade_xml_get_widget (glade_xml, "window_simple");
	gtk_window_set_icon_name (GTK_WINDOW (widget), GPK_ICON_SOFTWARE_LOG);
	gtk_widget_set_size_request (widget, 750, 300);

	/* if command line arguments are set, then setup UI */
	if (filter != NULL) {
		widget = glade_xml_get_widget (glade_xml, "entry_package");
		gtk_entry_set_text (GTK_ENTRY(widget), filter);
	}

	/* Get the main window quit */
	g_signal_connect_swapped (widget, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);
	gtk_widget_grab_default (widget);

	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_log_button_help_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_refresh");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_log_button_refresh_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_filter");
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_log_button_filter_cb), NULL);

	/* hit enter in the search box for filter */
	widget = glade_xml_get_widget (glade_xml, "entry_package");
	g_signal_connect (widget, "activate", G_CALLBACK (gpk_log_button_filter_cb), NULL);

	/* autocompletion can be turned off as it's slow */
	gconf_client = gconf_client_get_default ();
	ret = gconf_client_get_bool (gconf_client, GPK_CONF_AUTOCOMPLETE, NULL);
	if (ret) {
		/* create the completion object */
		completion = gpk_package_entry_completion_new ();
		widget = glade_xml_get_widget (glade_xml, "entry_package");
		gtk_entry_set_completion (GTK_ENTRY (widget), completion);
		g_object_unref (completion);
	}
	g_object_unref (gconf_client);

	/* connect up PolicyKit actions */
	g_signal_connect (button_action, "activate", G_CALLBACK (gpk_log_button_rollback_cb), NULL);

	/* hide the rollback button if we can't do the action */
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_ROLLBACK))
		polkit_gnome_action_set_visible (button_action, TRUE);
	else
		polkit_gnome_action_set_visible (button_action, FALSE);

	/* create list stores */
	list_store = gtk_list_store_new (GPK_LOG_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* create transaction_id tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_simple");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_log_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	pk_treeview_add_general_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* get the update list */
	gpk_log_refresh ();

	/* show */
	widget = glade_xml_get_widget (glade_xml, "window_simple");
	gtk_widget_show (widget);
	gtk_main ();

	g_object_unref (glade_xml);
	g_object_unref (list_store);
	g_object_unref (client);
	g_free (transaction_id);
	g_free (filter);
unique_out:
	g_object_unref (egg_unique);
	return 0;
}
