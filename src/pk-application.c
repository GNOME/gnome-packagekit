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
#include <pk-task-client.h>
#include <pk-connection.h>

#include "pk-common.h"
#include "pk-application.h"

static void     pk_application_class_init (PkApplicationClass *klass);
static void     pk_application_init       (PkApplication      *application);
static void     pk_application_finalize   (GObject	    *object);

#define PK_APPLICATION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_APPLICATION, PkApplicationPrivate))

struct PkApplicationPrivate
{
	GladeXML		*glade_xml;
	GtkListStore		*packages_store;
	GtkListStore		*groups_store;
	PkTaskClient		*tclient;
	PkConnection		*pconnection;
	gchar			*package;
	gchar			*actions;
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
	PACKAGES_COLUMN_NAME,
	PACKAGES_COLUMN_VERSION,
	PACKAGES_COLUMN_ARCH,
	PACKAGES_COLUMN_DESCRIPTION,
	PACKAGES_COLUMN_DATA,
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
	ret = pk_task_client_install_package (application->priv->tclient,
					      application->priv->package);
	/* ick, we failed so pretend we didn't do the action */
	if (ret == FALSE) {
		pk_task_client_reset (application->priv->tclient);
		pk_application_error_message (application,
					      _("The package could not be installed"), NULL);
	}
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
	ret = pk_task_client_remove_package (application->priv->tclient,
				             application->priv->package,
				             FALSE);
	/* ick, we failed so pretend we didn't do the action */
	if (ret == FALSE) {
		pk_task_client_reset (application->priv->tclient);
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
	ret = pk_task_client_get_deps (application->priv->tclient,
				       application->priv->package);
	/* ick, we failed so pretend we didn't do the action */
	if (ret == FALSE) {
		pk_task_client_reset (application->priv->tclient);
		pk_application_error_message (application,
					      _("The package dependencies could not be found"), NULL);
	} else {
		/* clear existing list and wait for packages */
		gtk_list_store_clear (application->priv->packages_store);
	}
}

/**
 * pk_application_close_cb:
 **/
static void
pk_application_close_cb (GtkWidget	*widget,
		    PkApplication	*application)
{
	pk_debug ("emitting action-close");
	g_signal_emit (application, signals [ACTION_CLOSE], 0);
}

/**
 * pk_application_package_cb:
 **/
static void
pk_application_description_cb (PkTaskClient *tclient, const gchar *package_id, PkTaskGroup group,
			   const gchar *detail, const gchar *url, PkApplication *application)
{
	GtkWidget *widget;
	const gchar *icon_name;

	pk_debug ("description = %s:%i:%s:%s", package_id, group, detail, url);
	widget = glade_xml_get_widget (application->priv->glade_xml, "frame_description");
	gtk_widget_show (widget);

	widget = glade_xml_get_widget (application->priv->glade_xml, "image_description");
	icon_name = pk_task_group_to_icon_name (group);
	gtk_image_set_from_icon_name (GTK_IMAGE (widget), icon_name, GTK_ICON_SIZE_DIALOG);

	widget = glade_xml_get_widget (application->priv->glade_xml, "label_description_text");
	gtk_label_set_label (GTK_LABEL (widget), detail);
}

/**
 * pk_application_package_cb:
 **/
static void
pk_application_package_cb (PkTaskClient *tclient, guint value, const gchar *package_id,
			const gchar *summary, PkApplication *application)
{
	PkPackageIdent *ident;
	GtkTreeIter iter;
	pk_debug ("package = %i:%s:%s", value, package_id, summary);

	/* split by delimeter */
	ident = pk_task_package_ident_from_string (package_id);

	gtk_list_store_append (application->priv->packages_store, &iter);
	gtk_list_store_set (application->priv->packages_store, &iter,
			    PACKAGES_COLUMN_INSTALLED, value,
			    PACKAGES_COLUMN_NAME, ident->name,
			    PACKAGES_COLUMN_VERSION, ident->version,
			    PACKAGES_COLUMN_ARCH, ident->arch,
			    PACKAGES_COLUMN_DATA, ident->data,
			    PACKAGES_COLUMN_DESCRIPTION, summary,
			    -1);
	pk_task_package_ident_free (ident);
}

/**
 * pk_application_error_code_cb:
 **/
static void
pk_application_error_code_cb (PkTaskClient *tclient, PkTaskErrorCode code, const gchar *details, PkApplication *application)
{
	pk_application_error_message (application,
				      pk_task_error_code_to_localised_text (code), details);
}

/**
 * pk_application_finished_cb:
 **/
static void
pk_application_finished_cb (PkTaskClient *tclient, PkTaskStatus status, guint runtime, PkApplication *application)
{
	GtkWidget *widget;

	application->priv->task_ended = TRUE;

	/* hide widget */
	widget = glade_xml_get_widget (application->priv->glade_xml, "frame_progress");
	gtk_widget_hide (widget);

	/* Correct text on button */
	if (application->priv->search_in_progress == TRUE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "label_button_find");
		gtk_label_set_label (GTK_LABEL (widget), _("Find"));
		application->priv->search_in_progress = FALSE;
	}

	/* reset tclient */
	pk_task_client_reset (application->priv->tclient);

	/* panic */
	if (status == PK_TASK_EXIT_FAILED) {
		pk_application_error_message (application,
					      _("The action did not complete"),
					      NULL);
	}
}

/**
 * pk_application_percentage_changed_cb:
 **/
static void
pk_application_percentage_changed_cb (PkTaskClient *tclient, guint percentage, PkApplication *application)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_progress_percentage");
	gtk_widget_show (widget);
	widget = glade_xml_get_widget (application->priv->glade_xml, "progressbar_percentage");
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);
}

/**
 * pk_application_sub_percentage_changed_cb:
 **/
static void
pk_application_sub_percentage_changed_cb (PkTaskClient *tclient, guint percentage, PkApplication *application)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_progress_subpercentage");
	gtk_widget_show (widget);
	widget = glade_xml_get_widget (application->priv->glade_xml, "progressbar_subpercentage");
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);
}

/**
 * pk_application_no_percentage_updates_timeout:
 **/
gboolean
pk_application_no_percentage_updates_timeout (gpointer data)
{
	gfloat fraction;
	GtkWidget *widget;
	PkApplication *application = (PkApplication *) data;

	widget = glade_xml_get_widget (application->priv->glade_xml, "progressbar_percentage");
	fraction = gtk_progress_bar_get_fraction (GTK_PROGRESS_BAR (widget));
	fraction += 0.05;
	if (fraction > 1.00) {
		fraction = 0.0;
	}
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), fraction);
	if (application->priv->task_ended == TRUE) {
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_application_no_percentage_updates_cb:
 **/
static void
pk_application_no_percentage_updates_cb (PkTaskClient *tclient, PkApplication *application)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_progress_percentage");
	gtk_widget_show (widget);
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
		ret = pk_task_client_cancel_job_try (application->priv->tclient);
		pk_warning ("canceled? %i", ret);
		return;
	}

	application->priv->search_in_progress = TRUE;
	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	package = gtk_entry_get_text (GTK_ENTRY (widget));

	/* clear existing list */
	gtk_list_store_clear (application->priv->packages_store);

	pk_debug ("find %s", package);
	application->priv->task_ended = FALSE;

	/* show pane */
	widget = glade_xml_get_widget (application->priv->glade_xml, "frame_progress");
	gtk_widget_show (widget);

	/* hide details */
	widget = glade_xml_get_widget (application->priv->glade_xml, "frame_description");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_progress_percentage");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_progress_subpercentage");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_progress_status");
	gtk_widget_hide (widget);

	/* reset to 0 */
	widget = glade_xml_get_widget (application->priv->glade_xml, "progressbar_percentage");
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), 0.0);
	widget = glade_xml_get_widget (application->priv->glade_xml, "progressbar_subpercentage");
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), 0.0);

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
		g_string_set_size (string, string->len);
	}

	filter_all = g_string_free (string, FALSE);
	pk_debug ("filter = %s", filter_all);

	if (application->priv->search_depth == 0) {
		pk_task_client_search_name (application->priv->tclient, filter_all, package);
	} else {
		pk_task_client_search_details (application->priv->tclient, filter_all, package);
	}

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
	pk_application_close_cb (widget, application);
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
							   "text", PACKAGES_COLUMN_NAME, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_NAME);
	gtk_tree_view_append_column (treeview, column);

	/* column for version */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Version"), renderer,
							   "text", PACKAGES_COLUMN_VERSION, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_VERSION);
	gtk_tree_view_append_column (treeview, column);

	/* column for arch */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Arch"), renderer,
							   "text", PACKAGES_COLUMN_ARCH, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_ARCH);
	gtk_tree_view_append_column (treeview, column);

	/* column for description */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Summary"), renderer,
							   "text", PACKAGES_COLUMN_DESCRIPTION, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_DESCRIPTION);
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

		ret = pk_task_client_search_group (application->priv->tclient, "none", id);
		/* ick, we failed so pretend we didn't do the action */
		if (ret == FALSE) {
			pk_task_client_reset (application->priv->tclient);
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
	gchar *name;
	gchar *version;
	gchar *arch;
	gchar *data;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (application->priv->package);
		gtk_tree_model_get (model, &iter,
				    PACKAGES_COLUMN_INSTALLED, &installed,
				    PACKAGES_COLUMN_NAME, &name,
				    PACKAGES_COLUMN_VERSION, &version,
				    PACKAGES_COLUMN_ARCH, &arch,
				    PACKAGES_COLUMN_DATA, &data, -1);

		/* make back into package ID */
		application->priv->package = pk_task_package_ident_build (name, version, arch, data);
		g_free (name);
		g_free (version);
		g_free (arch);
		g_free (data);

		g_print ("selected row is: %i %s\n", installed, application->priv->package);
		/* get the decription */
		pk_task_client_get_description (application->priv->tclient, application->priv->package);

		/* make the button sensitivities correct */
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_deps");
		gtk_widget_set_sensitive (widget, TRUE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_install");
		gtk_widget_set_sensitive (widget, !installed);
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_remove");
		gtk_widget_set_sensitive (widget, installed);
	} else {
		g_print ("no row selected.\n");
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_deps");
		gtk_widget_set_sensitive (widget, FALSE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_install");
		gtk_widget_set_sensitive (widget, FALSE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_remove");
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
		pk_application_finished_cb (application->priv->tclient, PK_TASK_EXIT_FAILED, 0, application);
	}
}

/**
 * pk_group_add_data:
 **/
static void
pk_group_add_data (PkApplication *application, const gchar *type)
{
	GtkTreeIter iter;
	PkTaskGroup group;
	GdkPixbuf *icon;
	const gchar *icon_name;
	const gchar *text;

	group = pk_task_group_from_text (type);
	gtk_list_store_append (application->priv->groups_store, &iter);

	text = pk_task_group_to_localised_text (group);
	gtk_list_store_set (application->priv->groups_store, &iter,
			    GROUPS_COLUMN_NAME, text,
			    GROUPS_COLUMN_ID, type,
			    -1);

	icon_name = pk_task_group_to_icon_name (group);
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
	application->priv->task_ended = TRUE;
	application->priv->find_installed = TRUE;
	application->priv->find_available = TRUE;
	application->priv->find_devel = TRUE;
	application->priv->find_non_devel = TRUE;
	application->priv->find_gui = TRUE;
	application->priv->find_text = TRUE;
	application->priv->search_in_progress = FALSE;

	application->priv->search_depth = 0;

	application->priv->tclient = pk_task_client_new ();
	g_signal_connect (application->priv->tclient, "package",
			  G_CALLBACK (pk_application_package_cb), application);
	g_signal_connect (application->priv->tclient, "description",
			  G_CALLBACK (pk_application_description_cb), application);
	g_signal_connect (application->priv->tclient, "error-code",
			  G_CALLBACK (pk_application_error_code_cb), application);
	g_signal_connect (application->priv->tclient, "finished",
			  G_CALLBACK (pk_application_finished_cb), application);
	g_signal_connect (application->priv->tclient, "no-percentage-updates",
			  G_CALLBACK (pk_application_no_percentage_updates_cb), application);
	g_signal_connect (application->priv->tclient, "percentage-changed",
			  G_CALLBACK (pk_application_percentage_changed_cb), application);
	g_signal_connect (application->priv->tclient, "sub-percentage-changed",
			  G_CALLBACK (pk_application_sub_percentage_changed_cb), application);

	/* get actions */
	application->priv->actions = pk_task_client_get_actions (application->priv->tclient);

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

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_close_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_help_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_install");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_install_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_remove");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_remove_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_deps");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_deps_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "frame_progress");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (application->priv->glade_xml, "frame_description");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_progress_percentage");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_progress_subpercentage");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_progress_status");
	gtk_widget_hide (widget);

	/* hide the group selector if we don't support search-groups */
	if (pk_task_action_contains (application->priv->actions, PK_TASK_ACTION_SEARCH_GROUP) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "frame_groups");
		gtk_widget_hide (widget);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_find_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (pk_application_find_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_depth");
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

	/* create list stores */
	application->priv->packages_store = gtk_list_store_new (PACKAGES_COLUMN_LAST,
						       G_TYPE_BOOLEAN,
						       G_TYPE_STRING,
						       G_TYPE_STRING,
						       G_TYPE_STRING,
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
	g_object_unref (application->priv->tclient);
	g_object_unref (application->priv->pconnection);
	g_free (application->priv->package);
	g_free (application->priv->actions);

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

