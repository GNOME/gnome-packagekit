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
#include <pk-common.h>
#include <pk-connection.h>
#include <pk-package-id.h>
#include <pk-enum-list.h>

#include "pk-common-gui.h"
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
	PkClient		*client_search;
	PkClient		*client_action;
	PkClient		*client_description;
	PkConnection		*pconnection;
	gchar			*package;
	gchar			*url;
	PkEnumList		*role_list;
	PkEnumList		*filter_list;
	PkEnumList		*group_list;
	PkEnumList		*current_filter;
	gboolean		 search_in_progress;
	guint			 search_depth;
	guint			 timer_id;
};

enum {
	ACTION_HELP,
	ACTION_CLOSE,
	LAST_SIGNAL
};

enum
{
	PACKAGES_COLUMN_IMAGE,
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
	pk_client_reset (application->priv->client_action);
	ret = pk_client_install_package (application->priv->client_action,
					 application->priv->package);
	/* ick, we failed so pretend we didn't do the action */
	if (ret == FALSE) {
		pk_application_error_message (application, _("The package could not be installed"), NULL);
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
	pk_client_reset (application->priv->client_action);
	ret = pk_client_remove_package (application->priv->client_action,
				        application->priv->package, FALSE);
	/* ick, we failed so pretend we didn't do the action */
	if (ret == FALSE) {
		pk_application_error_message (application,
					      _("The package could not be removed"), NULL);
	}
}

/**
 * pk_application_deps_cb:
 **/
static void
pk_application_deps_cb (GtkWidget      *widget,
		        PkApplication  *application)
{
	gboolean ret;
	pk_debug ("deps %s", application->priv->package);
	pk_client_reset (application->priv->client_action);
	ret = pk_client_get_depends (application->priv->client_action,
				     application->priv->package);
	/* ick, we failed so pretend we didn't do the action */
	if (ret == FALSE) {
		pk_application_error_message (application,
					      _("The package dependencies could not be found"), NULL);
	} else {
		/* clear existing list and wait for packages */
		gtk_list_store_clear (application->priv->packages_store);
	}
}

/**
 * pk_application_requires_cb:
 **/
static void
pk_application_requires_cb (GtkWidget      *widget,
		        PkApplication  *application)
{
	gboolean ret;
	pk_debug ("requires %s", application->priv->package);
	pk_client_reset (application->priv->client_action);
	ret = pk_client_get_requires (application->priv->client_action,
				     application->priv->package);
	/* ick, we failed so pretend we didn't do the action */
	if (ret == FALSE) {
		pk_application_error_message (application,
					      _("The package requires could not be found"), NULL);
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
			       guint64 size, const gchar *filelist,
			       PkApplication *application)
{
	GtkWidget *widget;
	GtkTextBuffer *buffer;
	const gchar *icon_name;

	pk_debug ("description = %s:%i:%s:%s", package_id, group, detail, url);
	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_description");
	gtk_widget_show (widget);

	/* ITS4: ignore, not used for allocation */
	if (strlen (url) > 0) {
		g_free (application->priv->url);
		/* save the url for the button */
		application->priv->url = g_strdup (url);
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_homepage");
		gtk_widget_set_sensitive (widget, TRUE);
	} else {
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_homepage");
		gtk_widget_set_sensitive (widget, FALSE);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "image_description");
	icon_name = pk_group_enum_to_icon_name (group);
	gtk_image_set_from_icon_name (GTK_IMAGE (widget), icon_name, GTK_ICON_SIZE_DIALOG);

	buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_set_text (buffer, detail, -1);
	widget = glade_xml_get_widget (application->priv->glade_xml, "textview_description");
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);

	/* if non-zero, set the size */
	if (size > 0) {
		gchar *value;
		widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_filesize");
		gtk_widget_show (widget);
		widget = glade_xml_get_widget (application->priv->glade_xml, "label_filesize");
		value = pk_size_to_si_size_text (size);
		gtk_label_set_label (GTK_LABEL (widget), value);
		g_free (value);
	} else {
		widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_filesize");
		gtk_widget_hide (widget);
	}

	buffer = gtk_text_buffer_new (NULL);

	/* ITS4: ignore, not used for allocation */
	if (strlen (filelist) > 0) {
		gchar *list;
		gchar **array;
		/* replace the ; with a newline */
		array = g_strsplit (filelist, ";", 0);
		list = g_strjoinv ("\n", array);

		/* apply the list */
		gtk_text_buffer_set_text (buffer, list, -1);
		g_strfreev (array);
		g_free (list);
	} else {
		/* no information */
		gtk_text_buffer_set_text (buffer, _("No files"), -1);
	}
	widget = glade_xml_get_widget (application->priv->glade_xml, "textview_files");
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);
}

/**
 * pk_application_package_cb:
 **/
static void
pk_application_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id,
			   const gchar *summary, PkApplication *application)
{
	GdkPixbuf *icon;
	const gchar *icon_name;
	PkPackageId *ident;
	GtkTreeIter iter;
	gchar *text;
	pk_debug ("package = %s:%s:%s", pk_info_enum_to_text (info), package_id, summary);

	/* ignore progress */
	if (info != PK_INFO_ENUM_INSTALLED && info != PK_INFO_ENUM_AVAILABLE) {
		return;
	}

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

	if (info == PK_INFO_ENUM_INSTALLED) {
		icon_name = "package-x-generic";
	} else {
		icon_name = "network-workgroup";
	}
	icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), icon_name, 22, 0, NULL);
	if (icon) {
		gtk_list_store_set (application->priv->packages_store, &iter, PACKAGES_COLUMN_IMAGE, icon, -1);
		gdk_pixbuf_unref (icon);
	}

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
	gboolean ret;
	PkRoleEnum role;

	/* don't spin anymore */
	if (application->priv->timer_id != 0) {
		g_source_remove (application->priv->timer_id);
		application->priv->timer_id = 0;
	}

	/* hide widget */
	gtk_widget_hide (application->priv->progress_bar);

	/* Correct text on button */
	if (application->priv->search_in_progress == TRUE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "label_button_find");
		gtk_label_set_label (GTK_LABEL (widget), _("Find"));
		application->priv->search_in_progress = FALSE;
	} else {
		/* get role */
		pk_client_get_role (client, &role, NULL);

		/* do we need to update the search? */
		if (role == PK_ROLE_ENUM_INSTALL_PACKAGE ||
		    role == PK_ROLE_ENUM_REMOVE_PACKAGE) {
			/* refresh the search as the items may have changed */
			gtk_list_store_clear (application->priv->packages_store);
			application->priv->search_in_progress = TRUE;
			ret = pk_client_requeue (application->priv->client_search);
			if (ret == FALSE) {
				application->priv->search_in_progress = FALSE;
				pk_warning ("failed to requeue the search");
			}
		}
	}

	/* panic */
	if (status == PK_EXIT_ENUM_FAILED) {
		pk_application_error_message (application, _("The action did not complete"), NULL);
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
	PkApplication *application = (PkApplication *) data;
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (application->priv->progress_bar));
	return TRUE;
}

/**
 * pk_application_no_percentage_updates_cb:
 **/
static void
pk_application_no_percentage_updates_cb (PkClient *client, PkApplication *application)
{
	/* don't spin twice as fast if more than one signal */
	if (application->priv->timer_id != 0) {
		return;
	}
	application->priv->timer_id = g_timeout_add (PK_PROGRESS_BAR_PULSE_DELAY,
						     pk_application_no_percentage_updates_timeout,
						     application);
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
	gchar *filter_all;
	gboolean ret;

	if (application->priv->search_in_progress == TRUE) {
		pk_debug ("trying to cancel task...");
		ret = pk_client_cancel (application->priv->client_search);
		pk_warning ("canceled? %i", ret);
		return;
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	package = gtk_entry_get_text (GTK_ENTRY (widget));
	ret = pk_validate_input (package);
	if (ret == FALSE) {
		pk_debug ("invalid input text, will fail");
		/* todo - make the dialog turn red... */
		pk_application_error_message (application, _("Invalid search text"),
					      _("The search text contains invalid characters"));
		return;
	}
	pk_debug ("find %s", package);

	/* make a valid filter string */
	filter_all = pk_enum_list_to_string (application->priv->current_filter);
	pk_debug ("filter = %s", filter_all);

	if (application->priv->search_depth == 0) {
		pk_client_reset (application->priv->client_search);
		ret = pk_client_search_name (application->priv->client_search, filter_all, package);
	} else if (application->priv->search_depth == 1) {
		pk_client_reset (application->priv->client_search);
		ret = pk_client_search_details (application->priv->client_search, filter_all, package);
	} else {
		pk_client_reset (application->priv->client_search);
		ret = pk_client_search_file (application->priv->client_search, filter_all, package);
	}

	if (ret == FALSE) {
		pk_application_error_message (application,
					      _("The search could not be completed"), NULL);
		return;
	}

	/* clear existing list */
	gtk_list_store_clear (application->priv->packages_store);

	application->priv->search_in_progress = TRUE;

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
	/* ITS4: ignore, not used for allocation */
	if (strlen (package) == 0) {
		gtk_widget_set_sensitive (widget, FALSE);
	} else {
		gtk_widget_set_sensitive (widget, TRUE);
	}
	return FALSE;
}

static void
pk_packages_add_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* column for installed toggles */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "pixbuf", PACKAGES_COLUMN_IMAGE);
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
 * pk_application_depth_combobox_changed_cb:
 **/
static void
pk_application_depth_combobox_changed_cb (GtkComboBox *combobox, PkApplication *application)
{
	application->priv->search_depth = gtk_combo_box_get_active (combobox);
	pk_debug ("search depth: %i", application->priv->search_depth);
}

/**
 * pk_application_filter_installed_combobox_changed_cb:
 **/
static void
pk_application_filter_installed_combobox_changed_cb (GtkComboBox *combobox, PkApplication *application)
{
	guint value;
	value = gtk_combo_box_get_active (combobox);
	if (value == 0) {
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_INSTALLED);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_AVAILABLE);
	} else if (value == 1) {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_INSTALLED);
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_AVAILABLE);
	} else {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_INSTALLED);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_AVAILABLE);
	}
}

/**
 * pk_application_filter_devel_combobox_changed_cb:
 **/
static void
pk_application_filter_devel_combobox_changed_cb (GtkComboBox *combobox, PkApplication *application)
{
	guint value;
	value = gtk_combo_box_get_active (combobox);
	if (value == 0) {
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_DEVELOPMENT);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_NORMAL);
	} else if (value == 1) {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_DEVELOPMENT);
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_NORMAL);
	} else {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_DEVELOPMENT);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_NORMAL);
	}
}

/**
 * pk_application_filter_gui_combobox_changed_cb:
 **/
static void
pk_application_filter_gui_combobox_changed_cb (GtkComboBox *combobox, PkApplication *application)
{
	guint value;
	value = gtk_combo_box_get_active (combobox);
	if (value == 0) {
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_GUI);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_TEXT);
	} else if (value == 1) {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_GUI);
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_TEXT);
	} else {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_GUI);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_TEXT);
	}
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

		pk_client_reset (application->priv->client_search);
		ret = pk_client_search_group (application->priv->client_search, "none", id);
		/* ick, we failed so pretend we didn't do the action */
		if (ret == FALSE) {
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
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_deps");
		gtk_widget_set_sensitive (widget, pk_enum_list_contains (application->priv->role_list,
						PK_ROLE_ENUM_GET_DEPENDS));
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_requires");
		gtk_widget_set_sensitive (widget, pk_enum_list_contains (application->priv->role_list,
						PK_ROLE_ENUM_GET_REQUIRES));

		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_install");
		gtk_widget_set_sensitive (widget, !installed);
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_remove");
		gtk_widget_set_sensitive (widget, installed);

		/* don't do the description if we don't support the action */
		if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_GET_DESCRIPTION) == FALSE) {
			return;
		}

		/* get the description */
		pk_client_reset (application->priv->client_description);
		pk_client_get_description (application->priv->client_description, application->priv->package);
	} else {
		g_print ("no row selected.\n");
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_deps");
		gtk_widget_set_sensitive (widget, FALSE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_requires");
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
}

/**
 * pk_group_add_data:
 **/
static void
pk_group_add_data (PkApplication *application, PkGroupEnum group)
{
	GtkTreeIter iter;
	GdkPixbuf *icon;
	const gchar *icon_name;
	const gchar *text;

	gtk_list_store_append (application->priv->groups_store, &iter);

	text = pk_group_enum_to_localised_text (group);
	gtk_list_store_set (application->priv->groups_store, &iter,
			    GROUPS_COLUMN_NAME, text,
			    GROUPS_COLUMN_ID, pk_group_enum_to_text (group),
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
	PkGroupEnum group;
	guint length;
	guint i;

	application->priv = PK_APPLICATION_GET_PRIVATE (application);
	application->priv->package = NULL;
	application->priv->url = NULL;
	application->priv->search_in_progress = FALSE;
	application->priv->timer_id = 0;

	application->priv->search_depth = 0;
	application->priv->current_filter = pk_enum_list_new ();
	pk_enum_list_set_type (application->priv->current_filter, PK_ENUM_LIST_TYPE_FILTER);

	application->priv->client_search = pk_client_new ();
	g_signal_connect (application->priv->client_search, "package",
			  G_CALLBACK (pk_application_package_cb), application);
	g_signal_connect (application->priv->client_search, "error-code",
			  G_CALLBACK (pk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_search, "finished",
			  G_CALLBACK (pk_application_finished_cb), application);
	g_signal_connect (application->priv->client_search, "no-percentage-updates",
			  G_CALLBACK (pk_application_no_percentage_updates_cb), application);
	g_signal_connect (application->priv->client_search, "percentage-changed",
			  G_CALLBACK (pk_application_percentage_changed_cb), application);

	application->priv->client_action = pk_client_new ();
	g_signal_connect (application->priv->client_action, "package",
			  G_CALLBACK (pk_application_package_cb), application);
	g_signal_connect (application->priv->client_action, "error-code",
			  G_CALLBACK (pk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_action, "finished",
			  G_CALLBACK (pk_application_finished_cb), application);
	g_signal_connect (application->priv->client_action, "no-percentage-updates",
			  G_CALLBACK (pk_application_no_percentage_updates_cb), application);
	g_signal_connect (application->priv->client_action, "percentage-changed",
			  G_CALLBACK (pk_application_percentage_changed_cb), application);

	application->priv->client_description = pk_client_new ();
	g_signal_connect (application->priv->client_description, "description",
			  G_CALLBACK (pk_application_description_cb), application);
	g_signal_connect (application->priv->client_description, "error-code",
			  G_CALLBACK (pk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_description, "finished",
			  G_CALLBACK (pk_application_finished_cb), application);

	/* get actions */
	application->priv->role_list = pk_client_get_actions (application->priv->client_action);
	pk_debug ("actions=%s", pk_enum_list_to_string (application->priv->role_list));

	/* get filters supported */
	application->priv->filter_list = pk_client_get_filters (application->priv->client_action);
	pk_debug ("filter=%s", pk_enum_list_to_string (application->priv->filter_list));

	/* get groups supported */
	application->priv->group_list = pk_client_get_groups (application->priv->client_action);
	pk_debug ("groups=%s", pk_enum_list_to_string (application->priv->group_list));

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

	/*
	 * FIXME: We don't want to set a style for the toolbar, so the gconf
	 * setting is used. You can leave this value out of the glade xml file
	 * and libglade will honour it. However, glade-2 will just drop in a value
	 * any time the glade file is saved. So work around it for now.
	 */
	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbar1");
	gtk_toolbar_unset_style (GTK_TOOLBAR (widget));

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_help_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_install");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_install_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_INSTALL_PACKAGE) == FALSE) {
		gtk_widget_hide (widget);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_remove");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_remove_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_REMOVE_PACKAGE) == FALSE) {
		gtk_widget_hide (widget);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_deps");
	g_signal_connect (widget, "clicked",
			G_CALLBACK (pk_application_deps_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_GET_DEPENDS) == FALSE) {
		gtk_widget_hide (widget);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_requires");
	g_signal_connect (widget, "clicked",
			G_CALLBACK (pk_application_requires_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_GET_REQUIRES) == FALSE) {
		gtk_widget_hide (widget);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "toolbutton_homepage");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_homepage_cb), application);
	gtk_widget_set_sensitive (widget, FALSE);
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_GET_DESCRIPTION) == FALSE) {
		gtk_widget_hide (widget);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_description");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_filesize");
	gtk_widget_hide (widget);

	/* until we get the mugshot stuff, disable this */
	widget = glade_xml_get_widget (application->priv->glade_xml, "image_description");
	gtk_widget_hide (widget);

	/* hide the group selector if we don't support search-groups */
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_SEARCH_GROUP) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "frame_groups");
		gtk_widget_hide (widget);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_find_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (pk_application_find_cb), application);

	/* search */
	widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_depth");
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_SEARCH_NAME) == TRUE) {
		gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("By package name"));
	}
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_SEARCH_DETAILS) == TRUE) {
		gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("By description"));
	}
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_SEARCH_FILE) == TRUE) {
		gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("By file"));
	}
	g_signal_connect (GTK_COMBO_BOX (widget), "changed",
			  G_CALLBACK (pk_application_depth_combobox_changed_cb), application);
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);

	/* filter installed */
	widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_filter_installed");
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("Installed"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("Available"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("All packages"));
	g_signal_connect (GTK_COMBO_BOX (widget), "changed",
			  G_CALLBACK (pk_application_filter_installed_combobox_changed_cb), application);
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 2);

	/* filter devel */
	widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_filter_gui");
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("Only graphical"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("Only text"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("All packages"));
	g_signal_connect (GTK_COMBO_BOX (widget), "changed",
			  G_CALLBACK (pk_application_filter_gui_combobox_changed_cb), application);
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 2);

	/* filter gui */
	widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_filter_devel");
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("Only development"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("Non-development"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("All packages"));
	g_signal_connect (GTK_COMBO_BOX (widget), "changed",
			  G_CALLBACK (pk_application_filter_devel_combobox_changed_cb), application);
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 2);

	/* hide the filters we can't support */
	if (pk_enum_list_contains (application->priv->filter_list, PK_FILTER_ENUM_INSTALLED) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_filter_installed");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (application->priv->glade_xml, "label_filter_installed");
		gtk_widget_hide (widget);
	}
	if (pk_enum_list_contains (application->priv->filter_list, PK_FILTER_ENUM_DEVELOPMENT) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_filter_devel");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (application->priv->glade_xml, "label_filter_devel");
		gtk_widget_hide (widget);
	}
	if (pk_enum_list_contains (application->priv->filter_list, PK_FILTER_ENUM_GUI) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_filter_gui");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (application->priv->glade_xml, "label_filter_gui");
		gtk_widget_hide (widget);
	}

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
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (application->priv->progress_bar), 0.0);
	gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (application->priv->progress_bar), PK_PROGRESS_BAR_PULSE_STEP);

	/* create list stores */
	application->priv->packages_store = gtk_list_store_new (PACKAGES_COLUMN_LAST,
						       GDK_TYPE_PIXBUF,
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

	/* add all the groups supported */
	length = pk_enum_list_size (application->priv->group_list);
	for (i=0; i<length; i++) {
		group = pk_enum_list_get_item (application->priv->group_list, i);
		pk_group_add_data (application, group);
	}
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

	/* don't spin anymore */
	if (application->priv->timer_id != 0) {
		g_source_remove (application->priv->timer_id);
	}

	g_object_unref (application->priv->packages_store);
	g_object_unref (application->priv->client_search);
	g_object_unref (application->priv->client_action);
	g_object_unref (application->priv->client_description);
	g_object_unref (application->priv->pconnection);
	g_object_unref (application->priv->filter_list);
	g_object_unref (application->priv->group_list);
	g_object_unref (application->priv->role_list);
	g_object_unref (application->priv->current_filter);
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

