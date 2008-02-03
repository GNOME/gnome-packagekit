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
#include <pk-extra.h>
#include <pk-extra-obj.h>

#include "pk-statusbar.h"
#include "pk-common-gui.h"
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
	PkClient		*client_search;
	PkClient		*client_action;
	PkClient		*client_description;
	PkClient		*client_files;
	PkConnection		*pconnection;
	PkStatusbar		*statusbar;
	PkExtra			*extra;
	gchar			*package;
	gchar			*url;
	PkEnumList		*role_list;
	PkEnumList		*filter_list;
	PkEnumList		*group_list;
	PkEnumList		*current_filter;
	gboolean		 search_in_progress;
	guint			 search_depth;
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
 * pk_application_error_message:
 **/
static void
pk_application_error_message (PkApplication *application, const gchar *title, const gchar *details)
{
	GtkWidget *main_window;
	GtkWidget *dialog;
	gchar *escaped_details;

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	pk_warning ("error %s:%s", title, details);
	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");

	/* we need to format this */
	escaped_details = pk_error_format_details (details);

	dialog = gtk_message_dialog_new (GTK_WINDOW (main_window), GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), escaped_details);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
	g_free (escaped_details);
}

/**
 * pk_application_install_cb:
 **/
static void
pk_application_install_cb (GtkWidget      *widget,
		           PkApplication  *application)
{
	gboolean ret;

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

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
	pk_execute_url (application->priv->url);
}

/**
 * pk_application_remove_only:
 **/
static gboolean
pk_application_remove_only (PkApplication *application, gboolean force)
{
	gboolean ret;

	g_return_val_if_fail (application != NULL, FALSE);
	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	pk_debug ("remove %s", application->priv->package);
	pk_client_reset (application->priv->client_action);
	ret = pk_client_remove_package (application->priv->client_action,
				        application->priv->package, force);
	/* ick, we failed so pretend we didn't do the action */
	if (ret == FALSE) {
		pk_application_error_message (application,
					      _("The package could not be removed"), NULL);
	}
	return ret;
}

/**
 * pk_application_requires_dialog_cb:
 **/
static void
pk_application_requires_dialog_cb (GtkDialog *dialog, gint id, PkApplication *application)
{
	if (id == -9) {
		pk_debug ("the user clicked no");
	} else if (id == -8) {
		pk_debug ("the user clicked yes, remove with deps");
		pk_application_remove_only (application, TRUE);
	} else {
		pk_warning ("id unknown=%i", id);
	}
}

/**
 * pk_application_requires_finished_cb:
 **/
static void
pk_application_requires_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, PkApplication *application)
{
	guint length;
	gchar *title;
	gchar *message;
	gchar *package_name;
	GString *text;
	PkPackageItem *item;
	GtkWidget *main_window;
	GtkWidget *dialog;
	guint i;

	/* see how many packages there are */
	length = pk_client_package_buffer_get_size (client);

	/* if there are no required packages, just do the remove */
	if (length == 0) {
		pk_debug ("no requires");
		pk_application_remove_only (application, FALSE);
		g_object_unref (client);
		return;
	}

	/* present this to the user */
	text = g_string_new (_("The following packages have to be removed:\n\n"));
	for (i=0; i<length; i++) {
		item = pk_client_package_buffer_get_item (client, i);
		message = pk_package_id_pretty_oneline (item->package_id, item->summary);
		g_string_append_printf (text, "%s\n", message);
		g_free (message);
	}

	/* remove last \n */
	g_string_set_size (text, text->len - 1);

	/* display messagebox  */
	message = g_string_free (text, FALSE);
	package_name = pk_package_get_name (application->priv->package);
	title = g_strdup_printf (_("Other software depends on %s"), package_name);
	g_free (package_name);

	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");
	dialog = gtk_message_dialog_new (GTK_WINDOW (main_window), GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING, GTK_BUTTONS_CANCEL, title);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Remove all packages"), -8, NULL);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), message);
	g_signal_connect (dialog, "response", G_CALLBACK (pk_application_requires_dialog_cb), application);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
	g_free (message);
	g_object_unref (client);
}

/**
 * pk_application_remove_cb:
 **/
static void
pk_application_remove_cb (GtkWidget      *widget,
		          PkApplication  *application)
{
	PkClient *client;

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	/* are we dumb and can't check for requires? */
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_GET_REQUIRES) == FALSE) {
		/* no, just try to remove it without deps */
		pk_application_remove_only (application, FALSE);
		return;
	}

	/* see if any packages require this one */
	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE);
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_application_requires_finished_cb), application);
	pk_debug ("getting requires for %s", application->priv->package);
	pk_client_get_requires (client, application->priv->package, TRUE);
}

/**
 * pk_application_set_text_buffer:
 **/
static void
pk_application_set_text_buffer (GtkWidget *widget, const gchar *text)
{
	GtkTextBuffer *buffer;
	buffer = gtk_text_buffer_new (NULL);
	/* ITS4: ignore, not used for allocation */
	if (pk_strzero (text) == FALSE) {
		gtk_text_buffer_set_text (buffer, text, -1);
	} else {
		/* no information */
		gtk_text_buffer_set_text (buffer, "", -1);
	}
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);
}

/**
 * pk_application_description_cb:
 **/
static void
pk_application_description_cb (PkClient *client, const gchar *package_id,
			       const gchar *license, PkGroupEnum group,
			       const gchar *detail, const gchar *url,
			       guint64 size, PkApplication *application)
{
	GtkWidget *widget;
	const gchar *icon_name;
	gchar *text;

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	pk_debug ("description = %s:%i:%s:%s", package_id, group, detail, url);
	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_description");
	gtk_widget_show (widget);

	/* homepage button? */
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_homepage");
	if (pk_strzero (url) == FALSE) {
		gtk_widget_show (widget);
		g_free (application->priv->url);
		/* save the url for the button */
		application->priv->url = g_strdup (url);
	} else {
		gtk_widget_hide (widget);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "image_description");
	icon_name = pk_group_enum_to_icon_name (group);
	gtk_image_set_from_icon_name (GTK_IMAGE (widget), icon_name, GTK_ICON_SIZE_DIALOG);

	/* set the description */
	text = pk_error_format_details (detail);
	widget = glade_xml_get_widget (application->priv->glade_xml, "textview_description");
	pk_application_set_text_buffer (widget, text);
	g_free (text);

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
}

/**
 * pk_application_files_cb:
 **/
static void
pk_application_files_cb (PkClient *client, const gchar *package_id,
			 const gchar *filelist, PkApplication *application)
{
	GtkWidget *widget;

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_description");
	gtk_widget_show (widget);

	/* set the text box */
	widget = glade_xml_get_widget (application->priv->glade_xml, "textview_files");
	/* ITS4: ignore, not used for allocation */
	if (pk_strzero (filelist) == FALSE) {
		gchar *list;
		gchar **array;
		/* replace the ; with a newline */
		array = g_strsplit (filelist, ";", 0);
		list = g_strjoinv ("\n", array);

		/* apply the list */
		pk_application_set_text_buffer (widget, list);
		g_strfreev (array);
		g_free (list);
	} else {
		/* no information */
		pk_application_set_text_buffer (widget, _("No files"));
	}
}

/**
 * pk_application_package_cb:
 **/
static void
pk_application_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id,
			   const gchar *summary, PkApplication *application)
{
	GtkTreeIter iter;
	PkExtraObj *eobj;
	gboolean valid = FALSE;
	gchar *text;

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	pk_debug ("package = %s:%s:%s", pk_info_enum_to_text (info), package_id, summary);

	/* ignore progress */
	if (info != PK_INFO_ENUM_INSTALLED && info != PK_INFO_ENUM_AVAILABLE) {
		return;
	}

	/* get convenience object */
	eobj = pk_extra_obj_new_from_package_id_summary (package_id, summary);

	/* check icon actually exists and is valid in this theme */
	valid = pk_icon_valid (eobj->icon);

	/* nothing in the detail database or invalid */
	if (valid == FALSE) {
		g_free (eobj->icon);
		eobj->icon = g_strdup (pk_info_enum_to_icon_name (info));
	}

	text = g_markup_printf_escaped ("<b>%s-%s (%s)</b>\n%s", eobj->id->name,
					eobj->id->version, eobj->id->arch, eobj->summary);

	gtk_list_store_append (application->priv->packages_store, &iter);
	gtk_list_store_set (application->priv->packages_store, &iter,
			    PACKAGES_COLUMN_INSTALLED, (info == PK_INFO_ENUM_INSTALLED),
			    PACKAGES_COLUMN_TEXT, text,
			    PACKAGES_COLUMN_ID, package_id,
			    PACKAGES_COLUMN_IMAGE, eobj->icon,
			    -1);

	pk_extra_obj_free (eobj);
	g_free (text);
}

/**
 * pk_application_error_code_cb:
 **/
static void
pk_application_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, PkApplication *application)
{
	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	pk_application_error_message (application,
				      pk_error_enum_to_localised_text (code), details);
}

/**
 * pk_application_package_buffer_to_name_version:
 **/
static gchar *
pk_application_package_buffer_to_name_version (PkClient *client)
{
	guint i;
	PkPackageItem *item;
	gchar *text_pretty;
	guint length;
	GString *string;

	length = pk_client_package_buffer_get_size (client);
	if (length == 0) {
		return g_strdup ("No packages");
	}

	string = g_string_new ("");
	for (i=0; i<length; i++) {
		item = pk_client_package_buffer_get_item (client, i);
		/* just use the name */
		text_pretty = pk_package_id_name_version (item->package_id);
		g_string_append_printf (string, "%s\n", text_pretty);
		g_free (text_pretty);
	}
	g_string_set_size (string, string->len - 1);
	return g_string_free (string, FALSE);
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
	gchar *text;

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	/* get role */
	pk_client_get_role (client, &role, NULL);
	/* do we need to fill in the tab box? */
	if (role == PK_ROLE_ENUM_GET_DEPENDS) {
		text = pk_application_package_buffer_to_name_version (client);
		widget = glade_xml_get_widget (application->priv->glade_xml, "textview_depends");
		pk_application_set_text_buffer (widget, text);
		g_free (text);
	} else if (role == PK_ROLE_ENUM_GET_REQUIRES) {
		text = pk_application_package_buffer_to_name_version (client);
		widget = glade_xml_get_widget (application->priv->glade_xml, "textview_requires");
		pk_application_set_text_buffer (widget, text);
		g_free (text);
	}

	/* hide widget */
	pk_statusbar_hide (application->priv->statusbar);

	/* Correct text on button */
	if (application->priv->search_in_progress == TRUE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "label_button_find");
		gtk_label_set_label (GTK_LABEL (widget), _("Find"));

		widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
		gtk_widget_set_tooltip_text(widget, _("Find packages"));

		application->priv->search_in_progress = FALSE;
	} else {
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
 * pk_application_progress_changed_cb:
 **/
static void
pk_application_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				    guint elapsed, guint remaining, PkApplication *application)
{
	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	pk_statusbar_set_percentage (application->priv->statusbar, percentage);
	pk_statusbar_set_remaining (application->priv->statusbar, remaining);
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

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	if (application->priv->search_in_progress == TRUE) {
		pk_debug ("trying to cancel task...");
		ret = pk_client_cancel (application->priv->client_search);
		pk_warning ("canceled? %i", ret);
		return;
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	package = gtk_entry_get_text (GTK_ENTRY (widget));
	ret = pk_strvalidate (package);
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
		pk_client_set_name_filter (application->priv->client_search, TRUE);
		ret = pk_client_search_name (application->priv->client_search, filter_all, package);
	} else if (application->priv->search_depth == 1) {
		pk_client_reset (application->priv->client_search);
		pk_client_set_name_filter (application->priv->client_search, TRUE);
		ret = pk_client_search_details (application->priv->client_search, filter_all, package);
	} else {
		pk_client_reset (application->priv->client_search);
		pk_client_set_name_filter (application->priv->client_search, TRUE);
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

	widget = glade_xml_get_widget (application->priv->glade_xml, "label_button_find");
	gtk_label_set_label (GTK_LABEL (widget), _("Cancel"));

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	gtk_widget_set_tooltip_text(widget, _("Cancel search"));

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
	g_return_val_if_fail (application != NULL, FALSE);
	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	pk_debug ("emitting action-close");
	g_signal_emit (application, signals [ACTION_CLOSE], 0);
	return FALSE;
}

static gboolean
pk_application_text_changed_cb (GtkEntry *entry, GdkEventKey *event, PkApplication *application)
{
	gboolean valid;
	GtkWidget *widget;
	const gchar *package;
	GtkTreeSelection *selection;

	g_return_val_if_fail (application != NULL, FALSE);
	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	package = gtk_entry_get_text (GTK_ENTRY (widget));

	/* clear group selection if we have the tab */
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_SEARCH_GROUP) == TRUE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_groups");
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
		gtk_tree_selection_unselect_all (selection);
	}

	/* check for invalid chars */
	valid = pk_strvalidate (package);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	if (valid == FALSE || pk_strzero (package) == TRUE) {
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
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_DIALOG, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "icon-name", PACKAGES_COLUMN_IMAGE);
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
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_LARGE_TOOLBAR, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "icon-name", GROUPS_COLUMN_ICON);
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
	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));
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

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	value = gtk_combo_box_get_active (combobox);
	if (value == 0) {
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_INSTALLED);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_NOT_INSTALLED);
	} else if (value == 1) {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_INSTALLED);
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_NOT_INSTALLED);
	} else {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_INSTALLED);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_NOT_INSTALLED);
	}
}

/**
 * pk_application_filter_devel_combobox_changed_cb:
 **/
static void
pk_application_filter_devel_combobox_changed_cb (GtkComboBox *combobox, PkApplication *application)
{
	guint value;

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	value = gtk_combo_box_get_active (combobox);
	if (value == 0) {
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_DEVELOPMENT);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	} else if (value == 1) {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_DEVELOPMENT);
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	} else {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_DEVELOPMENT);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	}
}

/**
 * pk_application_filter_free_combobox_changed_cb:
 **/
static void
pk_application_filter_free_combobox_changed_cb (GtkComboBox *combobox, PkApplication *application)
{
	guint value;

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	value = gtk_combo_box_get_active (combobox);
	if (value == 0) {
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_FREE);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_NOT_FREE);
	} else if (value == 1) {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_FREE);
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_NOT_FREE);
	} else {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_FREE);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_NOT_FREE);
	}
}

/**
 * pk_application_filter_gui_combobox_changed_cb:
 **/
static void
pk_application_filter_gui_combobox_changed_cb (GtkComboBox *combobox, PkApplication *application)
{
	guint value;

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	value = gtk_combo_box_get_active (combobox);
	if (value == 0) {
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_GUI);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_NOT_GUI);
	} else if (value == 1) {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_GUI);
		pk_enum_list_append (application->priv->current_filter, PK_FILTER_ENUM_NOT_GUI);
	} else {
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_GUI);
		pk_enum_list_remove (application->priv->current_filter, PK_FILTER_ENUM_NOT_GUI);
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
	GtkWidget *widget;
	gboolean ret;
	gchar *id;

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

	/* hide the details */
	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_description");
	gtk_widget_hide (widget);

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter, GROUPS_COLUMN_ID, &id, -1);
		g_print ("selected row is: %s\n", id);

		/* refresh the search as the items may have changed */
		gtk_list_store_clear (application->priv->packages_store);

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
 * pk_notebook_populate:
 **/
static gboolean
pk_notebook_populate (PkApplication *application, gint page)
{
	gboolean ret;
	GtkWidget *widget;
	GtkWidget *child;
	gint potential;

	g_return_val_if_fail (application != NULL, FALSE);
	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	/* show the box */
	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_description");
	gtk_widget_show (widget);

	/* get the notebook reference */
	widget = glade_xml_get_widget (application->priv->glade_xml, "notebook_description");

	/* are we description? */
	child = glade_xml_get_widget (application->priv->glade_xml, "vbox_description");
	potential = gtk_notebook_page_num (GTK_NOTEBOOK (widget), child);
	pk_debug ("potential=%i", potential);
	if (potential == page) {
		/* clear the old text */
		widget = glade_xml_get_widget (application->priv->glade_xml, "textview_description");
		pk_application_set_text_buffer (widget, NULL);

		/* hide the homepage button until we get data */
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_homepage");
		gtk_widget_hide (widget);

		/* cancel any previous request */
		ret = pk_client_cancel (application->priv->client_description);
		if (ret == FALSE) {
			pk_debug ("failed to cancel, and adding to queue");
		}
		/* get the description */
		pk_client_reset (application->priv->client_description);
		pk_client_get_description (application->priv->client_description,
					   application->priv->package);
		return TRUE;
	}

	/* are we description? */
	child = glade_xml_get_widget (application->priv->glade_xml, "vbox_files");
	potential = gtk_notebook_page_num (GTK_NOTEBOOK (widget), child);
	pk_debug ("potential=%i", potential);
	if (potential == page) {
		/* clear the old text */
		widget = glade_xml_get_widget (application->priv->glade_xml, "textview_files");
		pk_application_set_text_buffer (widget, NULL);

		/* cancel any previous request */
		ret = pk_client_cancel (application->priv->client_files);
		if (ret == FALSE) {
			pk_debug ("failed to cancel, and adding to queue");
		}
		/* get the filelist */
		pk_client_reset (application->priv->client_files);
		pk_client_get_files (application->priv->client_files,
				     application->priv->package);

		return TRUE;
	}

	/* are we depends? */
	child = glade_xml_get_widget (application->priv->glade_xml, "vbox_depends");
	potential = gtk_notebook_page_num (GTK_NOTEBOOK (widget), child);
	pk_debug ("potential=%i", potential);
	if (potential == page) {
		/* clear the old text */
		widget = glade_xml_get_widget (application->priv->glade_xml, "textview_depends");
		pk_application_set_text_buffer (widget, NULL);

		/* cancel any previous request */
		ret = pk_client_cancel (application->priv->client_files);
		if (ret == FALSE) {
			pk_debug ("failed to cancel, and adding to queue");
		}
		/* get the filelist */
		pk_client_reset (application->priv->client_files);
		pk_client_set_use_buffer (application->priv->client_files, TRUE);
		pk_client_get_depends (application->priv->client_files,
				       application->priv->package, FALSE);

		return TRUE;
	}

	/* are we requires? */
	child = glade_xml_get_widget (application->priv->glade_xml, "vbox_requires");
	potential = gtk_notebook_page_num (GTK_NOTEBOOK (widget), child);
	pk_debug ("potential=%i", potential);
	if (potential == page) {
		/* clear the old text */
		widget = glade_xml_get_widget (application->priv->glade_xml, "textview_requires");
		pk_application_set_text_buffer (widget, NULL);

		/* cancel any previous request */
		ret = pk_client_cancel (application->priv->client_files);
		if (ret == FALSE) {
			pk_debug ("failed to cancel, and adding to queue");
		}
		/* get the filelist */
		pk_client_reset (application->priv->client_files);
		pk_client_set_use_buffer (application->priv->client_files, TRUE);
		pk_client_get_requires (application->priv->client_files,
				        application->priv->package, TRUE);

		return TRUE;
	}
	pk_warning ("unknown tab %i!", page);
	return FALSE;
}

/**
 * pk_application_notebook_changed_cb:
 **/
static void
pk_application_notebook_changed_cb (GtkWidget *widget, gboolean arg1,
				    gint page, PkApplication *application)
{
	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));
	pk_notebook_populate (application, page);
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
	guint page;

	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));

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

		widget = glade_xml_get_widget (application->priv->glade_xml, "button_install");
		if (installed == FALSE &&
		    pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_INSTALL_PACKAGE) == TRUE) {
			gtk_widget_show (widget);
		} else {
			gtk_widget_hide (widget);
		}
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_remove");
		if (installed == TRUE &&
		    pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_REMOVE_PACKAGE) == TRUE) {
			gtk_widget_show (widget);
		} else {
			gtk_widget_hide (widget);
		}

		/* refresh */
		widget = glade_xml_get_widget (application->priv->glade_xml, "notebook_description");
		page = gtk_notebook_get_current_page (GTK_NOTEBOOK (widget));
		pk_notebook_populate (application, page);

	} else {
		g_print ("no row selected.\n");
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_install");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_remove");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_homepage");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_filesize");
		gtk_widget_hide (widget);
	}
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkApplication *application)
{
	g_return_if_fail (application != NULL);
	g_return_if_fail (PK_IS_APPLICATION (application));
	pk_debug ("connected=%i", connected);
}

/**
 * pk_group_add_data:
 **/
static void
pk_group_add_data (PkApplication *application, PkGroupEnum group)
{
	GtkTreeIter iter;
	const gchar *icon_name;
	const gchar *text;

	gtk_list_store_append (application->priv->groups_store, &iter);

	text = pk_group_enum_to_localised_text (group);
	icon_name = pk_group_enum_to_icon_name (group);
	gtk_list_store_set (application->priv->groups_store, &iter,
			    GROUPS_COLUMN_NAME, text,
			    GROUPS_COLUMN_ID, pk_group_enum_to_text (group),
			    GROUPS_COLUMN_ICON, icon_name,
			    -1);
}

/**
 * pk_application_init:
 **/
static void
pk_application_init (PkApplication *application)
{
	GtkWidget *main_window;
	GtkWidget *vbox;
	GtkWidget *widget;
	PkGroupEnum group;
	guint length;
	guint page;
	guint i;

	application->priv = PK_APPLICATION_GET_PRIVATE (application);
	application->priv->package = NULL;
	application->priv->url = NULL;
	application->priv->search_in_progress = FALSE;

	application->priv->search_depth = 0;
	application->priv->current_filter = pk_enum_list_new ();
	pk_enum_list_set_type (application->priv->current_filter, PK_ENUM_LIST_TYPE_FILTER);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
                                           PK_DATA G_DIR_SEPARATOR_S "icons");

	application->priv->client_search = pk_client_new ();
	g_signal_connect (application->priv->client_search, "package",
			  G_CALLBACK (pk_application_package_cb), application);
	g_signal_connect (application->priv->client_search, "error-code",
			  G_CALLBACK (pk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_search, "finished",
			  G_CALLBACK (pk_application_finished_cb), application);
	g_signal_connect (application->priv->client_search, "progress-changed",
			  G_CALLBACK (pk_application_progress_changed_cb), application);

	application->priv->client_action = pk_client_new ();
	g_signal_connect (application->priv->client_action, "package",
			  G_CALLBACK (pk_application_package_cb), application);
	g_signal_connect (application->priv->client_action, "error-code",
			  G_CALLBACK (pk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_action, "finished",
			  G_CALLBACK (pk_application_finished_cb), application);
	g_signal_connect (application->priv->client_action, "progress-changed",
			  G_CALLBACK (pk_application_progress_changed_cb), application);

	application->priv->client_description = pk_client_new ();
	g_signal_connect (application->priv->client_description, "description",
			  G_CALLBACK (pk_application_description_cb), application);
	g_signal_connect (application->priv->client_description, "error-code",
			  G_CALLBACK (pk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_description, "finished",
			  G_CALLBACK (pk_application_finished_cb), application);
	g_signal_connect (application->priv->client_description, "progress-changed",
			  G_CALLBACK (pk_application_progress_changed_cb), application);

	application->priv->client_files = pk_client_new ();
	pk_client_set_use_buffer (application->priv->client_files, TRUE);
	g_signal_connect (application->priv->client_files, "files",
			  G_CALLBACK (pk_application_files_cb), application);
	g_signal_connect (application->priv->client_files, "error-code",
			  G_CALLBACK (pk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_files, "finished",
			  G_CALLBACK (pk_application_finished_cb), application);
	g_signal_connect (application->priv->client_files, "progress-changed",
			  G_CALLBACK (pk_application_progress_changed_cb), application);

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

	/* single instance, so this is valid */
	application->priv->extra = pk_extra_new ();
	pk_extra_set_database (application->priv->extra, "/var/lib/PackageKit/extra-data.db");
	pk_extra_set_locale (application->priv->extra, "en_GB");

	application->priv->glade_xml = glade_xml_new (PK_DATA "/pk-application.glade", NULL, NULL);
	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), "system-installer");

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (pk_application_delete_event_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_install");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_install_cb), application);
	gtk_widget_set_tooltip_text(widget, _("Install selected package"));

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_remove");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_remove_cb), application);
	gtk_widget_set_tooltip_text(widget, _("Remove selected package"));

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_homepage");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_homepage_cb), application);
	gtk_widget_set_tooltip_text(widget, _("Visit homepage for selected package"));

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_description");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_filesize");
	gtk_widget_hide (widget);

	/* Remove description/file list if needed. */
	widget = glade_xml_get_widget (application->priv->glade_xml, "notebook_description");
	g_signal_connect (widget, "switch-page",
			  G_CALLBACK (pk_application_notebook_changed_cb), application);
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_GET_DESCRIPTION) == FALSE) {
		vbox = glade_xml_get_widget (application->priv->glade_xml, "vbox_description");
		page = gtk_notebook_page_num (GTK_NOTEBOOK (widget), vbox);
		gtk_notebook_remove_page (GTK_NOTEBOOK (widget), page);
	}
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_GET_FILES) == FALSE) {
		vbox = glade_xml_get_widget (application->priv->glade_xml, "vbox_files");
		page = gtk_notebook_page_num (GTK_NOTEBOOK (widget), vbox);
		gtk_notebook_remove_page (GTK_NOTEBOOK (widget), page);
	}
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_GET_DEPENDS) == FALSE) {
		vbox = glade_xml_get_widget (application->priv->glade_xml, "vbox_depends");
		page = gtk_notebook_page_num (GTK_NOTEBOOK (widget), vbox);
		gtk_notebook_remove_page (GTK_NOTEBOOK (widget), page);
	}
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_GET_REQUIRES) == FALSE) {
		vbox = glade_xml_get_widget (application->priv->glade_xml, "vbox_requires");
		page = gtk_notebook_page_num (GTK_NOTEBOOK (widget), vbox);
		gtk_notebook_remove_page (GTK_NOTEBOOK (widget), page);
	}

	/* until we get the mugshot stuff, disable this */
	widget = glade_xml_get_widget (application->priv->glade_xml, "image_description");
	gtk_widget_hide (widget);

	/* hide the group selector if we don't support search-groups */
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_SEARCH_GROUP) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "notebook_type");
		gtk_notebook_remove_page (GTK_NOTEBOOK (widget), 1);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_application_find_cb), application);
	gtk_widget_set_tooltip_text(widget, _("Find packages"));

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	/* set focus on entry text */
	gtk_widget_grab_focus (widget);
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

	/* filter free */
	widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_filter_free");
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("Only free"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("Non-free"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("All packages"));
	g_signal_connect (GTK_COMBO_BOX (widget), "changed",
			  G_CALLBACK (pk_application_filter_free_combobox_changed_cb), application);
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
	if (pk_enum_list_contains (application->priv->filter_list, PK_FILTER_ENUM_FREE) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "combobox_filter_free");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (application->priv->glade_xml, "label_filter_free");
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
	application->priv->statusbar = pk_statusbar_new ();
	widget = glade_xml_get_widget (application->priv->glade_xml, "statusbar_status");
	pk_statusbar_set_widget (application->priv->statusbar, widget);

	/* create list stores */
	application->priv->packages_store = gtk_list_store_new (PACKAGES_COLUMN_LAST,
						       G_TYPE_STRING,
						       G_TYPE_BOOLEAN,
						       G_TYPE_STRING,
						       G_TYPE_STRING);
	application->priv->groups_store = gtk_list_store_new (GROUPS_COLUMN_LAST,
						       G_TYPE_STRING,
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

	/* create group tree view if we can search by group */
	if (pk_enum_list_contains (application->priv->role_list, PK_ROLE_ENUM_SEARCH_GROUP) == TRUE) {
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
			if (group != PK_GROUP_ENUM_UNKNOWN) {
				pk_group_add_data (application, group);
			}
		}
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

	g_object_unref (application->priv->glade_xml);
	g_object_unref (application->priv->packages_store);
	g_object_unref (application->priv->client_search);
	g_object_unref (application->priv->client_action);
	g_object_unref (application->priv->client_description);
	g_object_unref (application->priv->client_files);
	g_object_unref (application->priv->pconnection);
	g_object_unref (application->priv->filter_list);
	g_object_unref (application->priv->group_list);
	g_object_unref (application->priv->role_list);
	g_object_unref (application->priv->current_filter);
	g_object_unref (application->priv->statusbar);
	g_object_unref (application->priv->extra);

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

