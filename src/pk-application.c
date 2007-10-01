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
#include <pk-client.h>
#include <pk-connection.h>
#include <pk-package-id.h>
#include <pk-enum-list.h>

#include "pk-common.h"
#include "pk-application.h"

static void     pk_application_class_init (PkApplicationClass *klass);
static void     pk_application_init       (PkApplication      *application);
static void     pk_application_finalize   (GObject	    *object);

#define PK_APPLICATION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_APPLICATION, PkApplicationPrivate))

struct PkApplicationPrivate
{
	GladeXML		*glade_xml;
	GtkWidget		*progress_bar;
	GtkListStore		*packages_store;
	GtkListStore		*groups_store;
	PkClient		*client;
	PkConnection		*pconnection;
	gchar			*package;
	gchar			*url;
	PkEnumList		*role_list;
	PkEnumList		*filter_list;
	gboolean		 task_ended;
	gboolean		 search_in_progress;
	gboolean		 find_installed;
	gboolean		 find_available;
	gboolean		 find_devel;
	gboolean		 find_non_devel;
	gboolean		 find_gui;
	gboolean		 find_text;
	guint			 search_depth;
};

enum {
	ACTION_HELP,
	ACTION_CLOSE,
	LAST_SIGNAL
};

enum
{
	PACKAGES_COLUMN_INSTALLED,
	PACKAGES_COLUMN_TEXT,
	PACKAGES_COLUMN_ID,
	PACKAGES_COLUMN_LAST
};

enum
{
	GROUPS_COLUMN_ICON,
	GROUPS_COLUMN_NAME,
	GROUPS_COLUMN_ID,
	GROUPS_COLUMN_LAST
};

static guint	     signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkApplication, pk_application, G_TYPE_OBJECT)

/**
 * pk_application_class_init:
 * @klass: This graph class instance
 **/
static void
pk_application_class_init (PkApplicationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_application_finalize;
	g_type_class_add_private (klass, sizeof (PkApplicationPrivate));

	signals [ACTION_HELP] =
		g_signal_new ("action-help",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkApplicationClass, action_help),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [ACTION_CLOSE] =
		g_signal_new ("action-close",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkApplicationClass, action_close),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * pk_application_error_code_cb:
 **/
static void
pk_application_error_message (PkApplication *application, const gchar *title, const gchar *details)
{
	GtkWidget *main_window;
	GtkWidget *dialog;

	pk_warning ("error %s:%s", title, details);
	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");

	dialog = gtk_message_dialog_new (GTK_WINDOW (main_window), GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), details);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

/**
 * pk_application_help_cb:
 **/
static void
pk_application_help_cb (GtkWidget *widget,
		   PkApplication  *application)
{
	pk_debug ("emitting action-help");
	g_signal_emit (application, signals [ACTION_HELP], 0);
}

/**
 * pk_application_install_cb:
 **/
static void
pk_application_install_cb (GtkWidget      *widget,
		           PkApplication  *application)
{
	gboolean ret;
	pk_debug ("install %s", application->priv->package);
	ret = pk_client_install_package (application->priv->client,
					      application->priv->package);
	/* ick, we failed so pretend we didn't do the action */
	if (ret == FALSE) {
		pk_client_reset (application->priv->client);
		pk_application_error_message (application,
					      _("The package could not be installed"), NULL);
	}
}

/**
 * pk_application_homepage_cb:
 **/
static void
pk_application_homepage_cb (GtkWidget      *widget,
		            PkApplication  *application)
{
	gchar *data;
	gboolean ret;

	data = g_strconcat ("gnome-open ", application->priv->url, NULL);
	ret = g_spawn_command_line_async (data, NULL);
	if (ret == FALSE) {
		pk_warning ("spawn of '%s' failed", data);
	}
	g_free (data);
}

/**
 * pk_application_remove_cb:
 **/
static void
pk_application_remove_cb (GtkWidget      *widget,
		          PkApplication  *application)
{
	gboolean ret;
	pk_debug ("remove %s", application->priv->package);
	ret = pk_client_remove_package (application->priv->client,
				             application->priv->package,
				             FALSE);
	/* ick, we failed so pretend we didn't do the action */
	if (ret == FALSE) {
		pk_client_reset (application->priv->client);
		pk_application_error_message (application,
					      _("The package could not be removed"), NULL);
	}
}

/**
 * pk_application_deps_cb:
 **/
static void
pk_application_deps_cb (GtkWidget *widget,
		   PkApplication  *application)
{
	gboolean ret;
	pk_debug ("deps %s", application->priv->package);
	ret = pk_client_get_depends (application->priv->client,
				       application->priv->package);
	/* ick, we failed so pretend we didn't do the action */
	if (ret == FALSE) {
		pk_client_reset (application->priv->client);
		pk_application_error_message (application,
					      _("The package dependencies could not be found"), NULL);
	} else {
		/* clear existing list and wait for packages */
		gtk_list_store_clear (application->priv->packages_store);
	}
}

/**
 * pk_application_description_cb:
 **/
static void
pk_application_description_cb (PkClient *client, const gchar *package_id,
			       const gchar *licence, PkGroupEnum group,
			       const gchar *detail, const gchar *url,
			       PkApplication *application)
{
	GtkWidget *widget;
	GtkTextBuffer *buffer;
	const gchar *icon_name;

	pk_debug ("description = %s:%i:%s:%s", package_id, group, detail, url);
	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_description");
	gtk_widget_show (widget);

	/* save the url for the button */
	if (url != NULL && strlen (url) > 0) {
		g_free (application->priv->url);
		application->priv->url = g_strdup (url);
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_homepage");
		gtk_widget_set_sensitive (widget, TRUE);
	} else {
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_homepage");
		gtk_widget_set_sensitive (widget, FALSE);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "image_description");
	icon_name = pk_group_enum_to_icon_name (group);
	gtk_image_set_from_icon_name (GTK_IMAGE (widget), icon_name, GTK_ICON_SIZE_DIALOG);

	buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_set_text (buffer, detail, -1);
	widget = glade_xml_get_widget (application->priv->glade_xml, "textview_description");
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);
}

/**
 * pk_application_package_cb:
 **/
static void
pk_application_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id,
			const gchar *summary, PkApplication *application)
{
	PkPackageId *ident;
	GtkTreeIter iter;
	gchar *text;
	pk_debug ("package = %s:%s:%s", pk_info_enum_to_text (info), package_id, summary);

	/* split by delimeter */
	ident = pk_package_id_new_from_string (package_id);

	text = g_markup_printf_escaped ("<b>%s-%s (%s)</b>\n%s", ident->name, ident->version, ident->arch, summary);

	gtk_list_store_append (application->priv->packages_store, &iter);
	gtk_list_store_set (application->priv->packages_store, &iter,
			    PACKAGES_COLUMN_INSTALLED, (info == PK_INFO_ENUM_INSTALLED),
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_ID, package_id, -1);
	pk_package_id_free (ident);
	g_free (text);
}

/**
 * pk_application_error_code_cb:
 **/
static void
pk_application_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, PkApplication *application)
{
	pk_application_error_message (application,
				      pk_error_enum_to_localised_text (code), details);
}

/**
 * pk_application_finished_cb:
 **/
static void
pk_application_finished_cb (PkClient *client, PkStatusEnum status, guint runtime, PkApplication *application)
{
	GtkWidget *widget;

	application->priv->task_ended = TRUE;

	/* hide widget */
	gtk_widget_hide (application->priv->progress_bar);

	/* Correct text on button */
	if (application->priv->search_in_progress == TRUE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "label_button_find");
		gtk_label_set_label (GTK_LABEL (widget), _("Find"));
		application->priv->search_in_progress = FALSE;
	}

	/* reset client */
	pk_client_reset (application->priv->client);

	/* panic */
	if (status == PK_EXIT_ENUM_FAILED) {
		pk_application_error_message (application,
					      _("The action did not complete"),
					      NULL);
	}
}

/**
 * pk_application_percentage_changed_cb:
 **/
static void
pk_application_percentage_changed_cb (PkClient *client, guint percentage, PkApplication *application)
{
	gtk_widget_show (application->priv->progress_bar);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (application->priv->progress_bar), (gfloat) percentage / 100.0);
}

/**
 * pk_application_no_percentage_updates_timeout:
 **/
gboolean
pk_application_no_percentage_updates_timeout (gpointer data)
{
	gfloat fraction;
	PkApplication *application = (PkApplication *) data;

	fraction = gtk_progress_bar_get_fraction (GTK_PROGRESS_BAR (application->priv->progress_bar));
	fraction += 0.05;
	if (fraction > 1.00) {
		fraction = 0.0;
	}
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (application->priv->progress_bar), fraction);
	if (application->priv->task_ended == TRUE) {
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_application_no_percentage_updates_cb:
 **/
static void
pk_application_no_percentage_updates_cb (PkClient *client, PkApplication *application)
{
	g_timeout_add (100, pk_application_no_percentage_updates_timeout, application);
}

/**
 * pk_application_find_options_available_cb:
 **/
static void
pk_application_find_options_available_cb (GtkToggleButton *togglebutton,
		    			  PkApplication	*application)
{
	application->priv->find_available = gtk_toggle_button_get_active (togglebutton);
	pk_debug ("available %i", application->priv->find_available);
}

/**
 * pk_application_find_options_installed_cb:
 **/
static void
pk_application_find_options_installed_cb (GtkToggleButton *togglebutton,
		    			  PkApplication	*application)
{
	application->priv->find_installed = gtk_toggle_button_get_active (togglebutton);
	pk_debug ("installed %i", application->priv->find_installed);
}

/**
 * pk_application_find_options_devel_cb:
 **/
static void
pk_application_find_options_devel_cb (GtkToggleButton *togglebutton,
		    			  PkApplication	*application)
{
	application->priv->find_devel = gtk_toggle_button_get_active (togglebutton);
	pk_debug ("devel %i", application->priv->find_devel);
}

/**
 * pk_application_find_options_non_devel_cb:
 **/
static void
pk_application_find_options_non_devel_cb (GtkToggleButton *togglebutton,
		    			  PkApplication	*application)
{
	application->priv->find_non_devel = gtk_toggle_button_get_active (togglebutton);
	pk_debug ("non_devel %i", application->priv->find_non_devel);
}

/**
 * pk_application_find_options_gui_cb:
 **/
static void
pk_application_find_options_gui_cb (GtkToggleButton *togglebutton,
		    			  PkApplication	*application)
{
	application->priv->find_gui = gtk_toggle_button_get_active (togglebutton);
	pk_debug ("gui %i", application->priv->find_gui);
}

/**
 * pk_application_find_options_text_cb:
 **/
static void
pk_application_find_options_text_cb (GtkToggleButton *togglebutton,
		    			  PkApplication	*application)
{
	application->priv->find_text = gtk_toggle_button_get_active (togglebutton);
	pk_debug ("gui %i", application->priv->find_text);
}

/**
 * pk_application_find_cb:
 **/
static void
pk_application_find_cb (GtkWidget	*button_widget,
		        PkApplication	*application)
{
	GtkWidget *widget;
	const gchar *package;
	const gchar *filter;
	gchar *filter_all;
	gboolean ret;
	GString *string;

	if (application->priv->search_in_progress == TRUE) {
		pk_debug ("trying to cancel task...");
		ret = pk_client_cancel (application->priv->client);
		pk_warning ("canceled? %i", ret);
		return;
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	package = gtk_entry_get_text (GTK_ENTRY (widget));
	pk_debug ("find %s", package);


	string = g_string_new ("");
	/* add ~installed */
	if (application->priv->find_installed == TRUE &&
	    application->priv->find_available == TRUE) {
		filter = NULL;
	} else if (application->priv->find_installed == TRUE) {
		filter = "installed;";
	} else {
		filter = "~installed;";
	}
	if (filter != NULL) {
		g_string_append (string, filter);
	}

	/* add ~devel */
	if (application->priv->find_devel == TRUE &&
	    application->priv->find_non_devel == TRUE) {
		filter = NULL;
	} else if (application->priv->find_devel == TRUE) {
		filter = "devel;";
	} else {
		filter = "~devel;";
	}
	if (filter != NULL) {
		g_string_append (string, filter);
	}

	/* add ~devel */
	if (application->priv->find_gui == TRUE &&
	    application->priv->find_text == TRUE) {
		filter = NULL;
	} else if (application->priv->find_gui == TRUE) {
		filter = "gui;";
	} else {
		filter = "~gui;";
	}
	if (filter != NULL) {
		g_string_append (string, filter);
	}

	/* remove last ";" if exists */
	if (string->len == 0) {
		g_string_append (string, "none");
	} else {
		g_string_set_size (string, string->len - 1);
	}

	filter_all = g_string_free (string, FALSE);
	pk_debug ("filter = %s", filter_all);

	if (application->priv->search_depth == 0) {
		ret = pk_client_search_name (application->priv->client, filter_all, package);
	} else if (application->priv->search_depth == 1) {
		ret = pk_client_search_details (application->priv->client, filter_all, package);
	} else {
		ret = pk_client_search_file (application->priv->client, filter_all, package);
	}

	if (ret == FALSE) {
		pk_application_error_message (application,
					      _("The search could not be completed"), NULL);
		return;
	}

	/* clear existing list */
	gtk_list_store_clear (application->priv->packages_store);

	application->priv->search_in_progress = TRUE;
	application->priv->task_ended = FALSE;

	/* hide details */
	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_description");
	gtk_widget_hide (widget);

	/* reset to 0 */
	gtk_widget_show (application->priv->progress_bar);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (application->priv->progress_bar), 0.0);

	widget = glade_xml_get_widget (application->priv->glade_xml, "label_button_find");
	gtk_label_set_label (GTK_LABEL (widget), _("Cancel"));
	g_free (filter_all);
}

/**
 * pk_application_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
pk_application_delete_event_cb (GtkWidget	*widget,
				GdkEvent	*event,
				PkApplication	*application)
{
	pk_debug ("emitting action-close");
	g_signal_emit (application, signals [ACTION_CLOSE], 0);
	return FALSE;
}

static gboolean
pk_application_text_changed_cb (GtkEntry *entry, GdkEventKey *event, PkApplication *application)
{
	GtkWidget *widget;
	const gchar *package;
	GtkTreeSelection *selection;

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	package = gtk_entry_get_text (GTK_ENTRY (widget));

	/* clear group selection */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_groups");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	gtk_tree_selection_unselect_all (selection);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	if (package == NULL || strlen (package) == 0) {
		gtk_widget_set_sensitive (widget, FALSE);
	} else {
		gtk_widget_set_sensitive (widget, TRUE);
	}
	return FALSE;
}

static void
pk_misc_installed_toggled (GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	GtkTreeModel *model = (GtkTreeModel *)data;
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	gboolean installed;

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_INSTALLED, &installed, -1);

	/* do something with the value */
//	installed ^= 1;

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, PACKAGES_COLUMN_INSTALLED, installed, -1);

	/* clean up */
	gtk_tree_path_free (path);
}

static void
pk_packages_add_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);

	/* column for installed toggles */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (pk_misc_installed_toggled), model);

	column = gtk_tree_view_column_new_with_attributes (_("Installed"), renderer,
							   "active", PACKAGES_COLUMN_INSTALLED, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for name */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Name"), renderer,
							   "markup", PACKAGES_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
}

static void
pk_groups_add_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);

	/* column for installed toggles */
	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled", G_CALLBACK (pk_misc_installed_toggled), model);


	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "pixbuf", GROUPS_COLUMN_ICON);
	gtk_tree_view_append_column (treeview, column);

	/* column for name */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Name"), renderer,
							   "text", GROUPS_COLUMN_NAME, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GROUPS_COLUMN_NAME);
	gtk_tree_view_append_column (treeview, column);

}

/**
 * pk_application_combobox_changed_cb:
 **/
static void
pk_application_combobox_changed_cb (GtkComboBox *combobox, PkApplication *application)
{
	application->priv->search_depth = gtk_combo_box_get_active (combobox);
	pk_debug ("search depth: %i", application->priv->search_depth);
}

/**
 * pk_groups_treeview_clicked_cb:
 **/
static void
pk_groups_treeview_clicked_cb (GtkTreeSelection *selection,
			       PkApplication *application)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean ret;
	gchar *id;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter, GROUPS_COLUMN_ID, &id, -1);
		g_print ("selected row is: %s\n", id);

		ret = pk_client_search_group (application->priv->client, "none", id);
		/* ick, we failed so pretend we didn't do the action */
		if (ret == FALSE) {
			pk_client_reset (application->priv->client);
			pk_application_error_message (application,
						      _("The group could not be queried"), NULL);
		}
	}
}

/**
 * pk_packages_treeview_clicked_cb:
 **/
static void
pk_packages_treeview_clicked_cb (GtkTreeSelection *selection,
				 PkApplication *application)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean installed;
	gchar *package_id;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (application->priv->package);
		gtk_tree_model_get (model, &iter,
				    PACKAGES_COLUMN_INSTALLED, &installed,
				    PACKAGES_COLUMN_ID, &package_id, -1);

		/* make back into package ID */
		application->priv->package = g_strdup (package_id);
		g_free (package_id);
		g_print ("selected row is: %i %s\n", installed, application->priv->package);

		/* make the button sensitivities correct */
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_deps");
		gtk_widget_set_sensitive (widget, TRUE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_install");
		gtk_widget_set_sensitive (widget, !installed);
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_remove");
		gtk_widget_set_sensitive (widget, installed);

		/* don't do the description if we don't support the action */
		if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_GET_DESCRIPTION) == FALSE) {
			return;
		}

		/* get the description */
		pk_client_get_description (application->priv->client, application->priv->package);
	} else {
		g_print ("no row selected.\n");
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_deps");
		gtk_widget_set_sensitive (widget, FALSE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_install");
		gtk_widget_set_sensitive (widget, FALSE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_remove");
		gtk_widget_set_sensitive (widget, FALSE);
	}
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkApplication *application)
{
	pk_debug ("connected=%i", connected);
	if (connected == FALSE && application->priv->task_ended == FALSE) {
		/* forcibly end the transaction */
		pk_application_finished_cb (application->priv->client, PK_EXIT_ENUM_FAILED, 0, application);
	}
}

/**
 * pk_group_add_data:
 **/
static void
pk_group_add_data (PkApplication *application, const gchar *type)
{
	GtkTreeIter iter;
	PkGroupEnum group;
	GdkPixbuf *icon;
	const gchar *icon_name;
	const gchar *text;

	group = pk_group_enum_from_text (type);
	gtk_list_store_append (application->priv->groups_store, &iter);

	text = pk_group_enum_to_localised_text (group);
	gtk_list_store_set (application->priv->groups_store, &iter,
			    GROUPS_COLUMN_NAME, text,
			    GROUPS_COLUMN_ID, type,
			    -1);

	icon_name = pk_group_enum_to_icon_name (group);
	icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), icon_name, 22, 0, NULL);
	if (icon) {
		gtk_list_store_set (application->priv->groups_store, &iter, GROUPS_COLUMN_ICON, icon, -1);
		gdk_pixbuf_unref (icon);
	}
}

/**
 * pk_application_init:
 **/
static void
pk_application_init (PkApplication *application)
{
	GtkWidget *main_window;
	GtkWidget *widget;

	application->priv = PK_APPLICATION_GET_PRIVATE (application);
	application->priv->package = NULL;
	application->priv->url = NULL;
	application->priv->task_ended = TRUE;
	application->priv->find_installed = TRUE;
	application->priv->find_available = TRUE;
	application->priv->find_devel = TRUE;
	application->priv->find_non_devel = TRUE;
	application->priv->find_gui = TRUE;
	application->priv->find_text = TRUE;
	application->priv->search_in_progress = FALSE;

	application->priv->search_depth = 0;

	application->priv->client = pk_client_new ();
	g_signal_connect (application->priv->client, "package",
			  G_CALLBACK (pk_application_package_cb), application);
	g_signal_connect (application->priv->client, "description",
			  G_CALLBACK (pk_application_description_cb), application);
	g_signal_connect (application->priv->client, "error-code",
			  G_CALLBACK (pk_application_error_code_cb), application);
	g_signal_connect (application->priv->client, "finished",
			  G_CALLBACK (pk_application_finished_cb), application);
	g_signal_connect (application->priv->client, "no-percentage-updates",
			  G_CALLBACK (pk_application_no_percentage_updates_cb), application);
	g_signal_connect (application->priv->client, "percentage-changed",
			  G_CALLBACK (pk_application_percentage_changed_cb), application);

	/* get actions */
	application->priv->role_list = pk_client_get_actions (application->priv->client);
	pk_debug ("actions=%s", pk_enum_list_to_string (application->priv->role_list));

	/* get filters supported */
	application->priv->filter_list = pk_client_get_filters (application->priv->client);
	pk_debug ("filter=%s", pk_enum_list_to_string (application->priv->filter_list));

	application->priv->pconnection = pk_connection_new ();
	g_signal_connect (application->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), application);

	application->priv->glade_xml = glade_xml_new (PK_DATA "/pk-application.glade", NULL, NULL);
	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), "system-installer");

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (pk_application_delete_event_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_help_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_install");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_install_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_remove");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_remove_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_deps");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_deps_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_requires");
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_homepage");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_homepage_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_files");
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_description");
	gtk_widget_hide (widget);

	/* until we get the mugshot stuff, disable this */
	widget = glade_xml_get_widget (application->priv->glade_xml, "image_description");
	gtk_widget_hide (widget);

	/* hide the group selector if we don't support search-groups */
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_SEARCH_GROUP) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "frame_groups");
		gtk_widget_hide (widget);
	}

	/* hide the filters we can't support */
	if (pk_enum_list_contains (application->priv->filter_list, PK_FILTER_ENUM_INSTALLED) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_filter_install");
		gtk_widget_hide (widget);
	}
	if (pk_enum_list_contains (application->priv->filter_list, PK_FILTER_ENUM_DEVELOPMENT) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_filter_devel");
		gtk_widget_hide (widget);
	}
	if (pk_enum_list_contains (application->priv->filter_list, PK_FILTER_ENUM_GUI) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_filter_gui");
		gtk_widget_hide (widget);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_find_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (pk_application_find_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_depth");
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_SEARCH_NAME) == TRUE) {
		gtk_combo_box_append_text (GTK_COMBO_BOX (widget), "By package name");
	}
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_SEARCH_DETAILS) == TRUE) {
		gtk_combo_box_append_text (GTK_COMBO_BOX (widget), "By description");
	}
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_SEARCH_FILE) == TRUE) {
		gtk_combo_box_append_text (GTK_COMBO_BOX (widget), "By file");
	}
	g_signal_connect (GTK_COMBO_BOX (widget), "changed",
			  G_CALLBACK (pk_application_combobox_changed_cb), application);
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);

	widget = glade_xml_get_widget (application->priv->glade_xml, "checkbutton_installed");
	g_signal_connect (GTK_TOGGLE_BUTTON (widget), "toggled",
			  G_CALLBACK (pk_application_find_options_installed_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "checkbutton_available");
	g_signal_connect (GTK_TOGGLE_BUTTON (widget), "toggled",
			  G_CALLBACK (pk_application_find_options_available_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "checkbutton_devel");
	g_signal_connect (GTK_TOGGLE_BUTTON (widget), "toggled",
			  G_CALLBACK (pk_application_find_options_devel_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "checkbutton_non_devel");
	g_signal_connect (GTK_TOGGLE_BUTTON (widget), "toggled",
			  G_CALLBACK (pk_application_find_options_non_devel_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "checkbutton_gui");
	g_signal_connect (GTK_TOGGLE_BUTTON (widget), "toggled",
			  G_CALLBACK (pk_application_find_options_gui_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "checkbutton_text");
	g_signal_connect (GTK_TOGGLE_BUTTON (widget), "toggled",
			  G_CALLBACK (pk_application_find_options_text_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	g_signal_connect (widget, "key-press-event",
			  G_CALLBACK (pk_application_text_changed_cb), application);
	g_signal_connect (widget, "key-release-event",
			  G_CALLBACK (pk_application_text_changed_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	gtk_widget_set_sensitive (widget, FALSE);

	gtk_widget_set_size_request (main_window, 800, 400);
	gtk_widget_show (main_window);

	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* FIXME: There's got to be a better way than this */
	gtk_widget_hide (GTK_WIDGET (widget));
	gtk_widget_show (GTK_WIDGET (widget));

	GtkTreeSelection *selection;

	/* use the in-statusbar for progress */
	widget = glade_xml_get_widget (application->priv->glade_xml, "statusbar_status");
	application->priv->progress_bar = gtk_progress_bar_new ();
	gtk_box_pack_end (GTK_BOX (widget), application->priv->progress_bar, TRUE, TRUE, 0);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (application->priv->progress_bar), 0.5);

	/* create list stores */
	application->priv->packages_store = gtk_list_store_new (PACKAGES_COLUMN_LAST,
						       G_TYPE_BOOLEAN,
						       G_TYPE_STRING,
						       G_TYPE_STRING);
	application->priv->groups_store = gtk_list_store_new (GROUPS_COLUMN_LAST,
						       GDK_TYPE_PIXBUF,
						       G_TYPE_STRING,
						       G_TYPE_STRING);

	/* create package tree view */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (application->priv->packages_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (pk_packages_treeview_clicked_cb), application);

	/* add columns to the tree view */
	pk_packages_add_columns (GTK_TREE_VIEW (widget));

	/* create group tree view */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_groups");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (application->priv->groups_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (pk_groups_treeview_clicked_cb), application);

	/* add columns to the tree view */
	pk_groups_add_columns (GTK_TREE_VIEW (widget));
	pk_group_add_data (application, "accessibility");
	pk_group_add_data (application, "accessories");
	pk_group_add_data (application, "education");
	pk_group_add_data (application, "games");
	pk_group_add_data (application, "graphics");
	pk_group_add_data (application, "internet");
	pk_group_add_data (application, "office");
	pk_group_add_data (application, "other");
	pk_group_add_data (application, "programming");
	pk_group_add_data (application, "sound-video");
	pk_group_add_data (application, "system");
}

/**
 * pk_application_finalize:
 * @object: This graph class instance
 **/
static void
pk_application_finalize (GObject *object)
{
	PkApplication *application;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_APPLICATION (object));

	application = PK_APPLICATION (object);
	application->priv = PK_APPLICATION_GET_PRIVATE (application);

	g_object_unref (application->priv->packages_store);
	g_object_unref (application->priv->client);
	g_object_unref (application->priv->pconnection);
	g_object_unref (application->priv->filter_list);
	g_object_unref (application->priv->role_list);
	g_free (application->priv->url);
	g_free (application->priv->package);

	G_OBJECT_CLASS (pk_application_parent_class)->finalize (object);
}

/**
 * pk_application_new:
 * Return value: new PkApplication instance.
 **/
PkApplication *
pk_application_new (void)
{
	PkApplication *application;
	application = g_object_new (PK_TYPE_APPLICATION, NULL);
	return PK_APPLICATION (application);
}

