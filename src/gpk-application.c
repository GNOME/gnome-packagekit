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
#include <gconf/gconf-client.h>
#include <libsexy/sexy-icon-entry.h>
#include <math.h>
#include <string.h>
#include <locale.h>
#include <polkit-gnome/polkit-gnome.h>

#include <pk-debug.h>
#include <pk-enum.h>
#include <pk-client.h>
#include <pk-control.h>
#include <pk-common.h>
#include <pk-connection.h>
#include <pk-package-id.h>
#include <pk-extra.h>
#include <pk-extra-obj.h>

#include <gpk-client.h>
#include <gpk-common.h>
#include <gpk-gnome.h>

#include "gpk-statusbar.h"
#include "gpk-application.h"

static void     gpk_application_class_init (GpkApplicationClass *klass);
static void     gpk_application_init       (GpkApplication      *application);
static void     gpk_application_finalize   (GObject	    *object);

#define GPK_APPLICATION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_APPLICATION, GpkApplicationPrivate))
#define PK_STOCK_APP_ICON		"system-software-installer"

typedef enum {
	PK_SEARCH_NAME,
	PK_SEARCH_DETAILS,
	PK_SEARCH_FILE,
	PK_SEARCH_UNKNOWN
} PkSearchType;

typedef enum {
	PK_MODE_NAME_DETAILS_FILE,
	PK_MODE_GROUP,
	PK_MODE_ALL_PACKAGES,
	PK_MODE_UNKNOWN
} PkSearchMode;

struct GpkApplicationPrivate
{
	GladeXML		*glade_xml;
	GConfClient		*gconf_client;
	GtkListStore		*packages_store;
	GtkListStore		*groups_store;
	PkControl		*control;
	PkClient		*client_search;
	PkClient		*client_action;
	PkClient		*client_description;
	PkClient		*client_files;
	PkConnection		*pconnection;
	GpkStatusbar		*statusbar;
	PkExtra			*extra;
	gchar			*package;
	gchar			*group;
	gchar			*url;
	PkRoleEnum		 roles;
	PkFilterEnum		 filters;
	PkGroupEnum		 groups;
	PkFilterEnum		 filters_current;
	gboolean		 has_package; /* if we got a package in the search */
	PkSearchType		 search_type;
	PkSearchMode		 search_mode;
	PolKitGnomeAction	*install_action;
	PolKitGnomeAction	*remove_action;
	PolKitGnomeAction	*refresh_action;
};

enum {
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

static guint	     signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GpkApplication, gpk_application, G_TYPE_OBJECT)

/**
 * gpk_application_class_init:
 * @klass: This graph class instance
 **/
static void
gpk_application_class_init (GpkApplicationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_application_finalize;
	g_type_class_add_private (klass, sizeof (GpkApplicationPrivate));

	signals [ACTION_CLOSE] =
		g_signal_new ("action-close",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkApplicationClass, action_close),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * gpk_application_set_find_cancel_buttons:
 **/
static void
gpk_application_set_find_cancel_buttons (GpkApplication *application, gboolean find)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (application->priv->glade_xml, "notebook_search_cancel");

	/* if we can't do it, then just make the button insensitive */
	if (!pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_CANCEL)) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_cancel");
		gtk_widget_set_sensitive (widget, FALSE);
	}

	/* which tab to enable? */
	if (find) {
		gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), 0);
	} else {
		gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), 1);
	}
}

/**
 * gpk_application_error_message:
 **/
static void
gpk_application_error_message (GpkApplication *application, const gchar *title, const gchar *details)
{
	GtkWidget *main_window;
	GtkWidget *dialog;
	gchar *escaped_details = NULL;

	g_return_if_fail (PK_IS_APPLICATION (application));

	pk_warning ("error %s:%s", title, details);
	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");

	dialog = gtk_message_dialog_new (GTK_WINDOW (main_window), GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", title);

	/* we need to be careful of markup */
	if (details != NULL) {
		escaped_details = g_markup_escape_text (details, -1);
		gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", escaped_details);
		g_free (escaped_details);
	}
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

/**
 * gpk_application_install:
 **/
static gboolean
gpk_application_install (GpkApplication *application, const gchar *package_id)
{
	gboolean ret;
	GError *error = NULL;
	GpkClient *gclient;

	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	pk_debug ("install %s", application->priv->package);

	/* xxx TODO: this is hacky code for testing only */
	gclient = gpk_client_new ();
	ret = gpk_client_install_package_id (gclient, package_id, NULL);
	g_object_unref (gclient);

	return ret;

	ret = pk_client_reset (application->priv->client_action, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* do the install */
	ret = pk_client_install_package (application->priv->client_action,
					 application->priv->package, NULL);
	if (!ret) {
		pk_warning ("failed to install package: %s", error->message);
		/* ick, we failed so pretend we didn't do the action */
		gpk_application_error_message (application, _("The package could not be installed"), error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * gpk_application_install_cb:
 **/
static void
gpk_application_install_cb (PolKitGnomeAction *action, GpkApplication *application)
{
	GtkWidget *widget;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* hide and show the right things */
	polkit_gnome_action_set_visible (application->priv->install_action, FALSE);
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_cancel2");
	gtk_widget_show (widget);

	gpk_application_install (application, application->priv->package);
}

/**
 * gpk_application_homepage_cb:
 **/
static void
gpk_application_homepage_cb (GtkWidget      *widget,
		            GpkApplication  *application)
{
	g_return_if_fail (PK_IS_APPLICATION (application));
	gpk_gnome_open (application->priv->url);
}

/**
 * gpk_application_remove_only:
 **/
static gboolean
gpk_application_remove_only (GpkApplication *application, gboolean force)
{
	gboolean ret;
	GError *error = NULL;
	GtkWidget *widget;

	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	pk_debug ("remove %s", application->priv->package);

	/* hide and show the right things */
	polkit_gnome_action_set_visible (application->priv->remove_action, FALSE);
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_cancel2");
	gtk_widget_show (widget);

	ret = pk_client_reset (application->priv->client_action, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* do the remove */
	ret = pk_client_remove_package (application->priv->client_action,
				        application->priv->package, force, FALSE, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		/* ick, we failed so pretend we didn't do the action */
		gpk_application_error_message (application,
					      _("The package could not be removed"), error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * gpk_application_requires_dialog_cb:
 **/
static void
gpk_application_requires_dialog_cb (GtkDialog *dialog, gint id, GpkApplication *application)
{
	g_return_if_fail (PK_IS_APPLICATION (application));

	if (id == -9) {
		pk_debug ("the user clicked no");
	} else if (id == -8) {
		pk_debug ("the user clicked yes, remove with deps");
		gpk_application_remove_only (application, TRUE);
	} else {
		pk_warning ("id unknown=%i", id);
	}
}

/**
 * gpk_application_requires_finished_cb:
 **/
static void
gpk_application_requires_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, GpkApplication *application)
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

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* see how many packages there are */
	length = pk_client_package_buffer_get_size (client);

	/* if there are no required packages, just do the remove */
	if (length == 0) {
		pk_debug ("no requires");
		gpk_application_remove_only (application, FALSE);
		g_object_unref (client);
		return;
	}

	/* present this to the user */
	text = g_string_new (_("The following packages have to be removed:"));
	g_string_append (text, "\n\n");
	for (i=0; i<length; i++) {
		item = pk_client_package_buffer_get_item (client, i);
		message = gpk_package_id_pretty_oneline (item->package_id, item->summary);
		g_string_append_printf (text, "%s\n", message);
		g_free (message);
	}

	/* remove last \n */
	g_string_set_size (text, text->len - 1);

	/* display messagebox  */
	message = g_string_free (text, FALSE);
	package_name = gpk_package_get_name (application->priv->package);
	title = g_strdup_printf (_("Other software depends on %s"), package_name);
	g_free (package_name);

	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");
	dialog = gtk_message_dialog_new (GTK_WINDOW (main_window), GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING, GTK_BUTTONS_CANCEL, "%s", title);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Remove all packages"), -8, NULL);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	g_signal_connect (dialog, "response", G_CALLBACK (gpk_application_requires_dialog_cb), application);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
	g_free (message);
	g_object_unref (client);
}

/**
 * gpk_application_remove_cb:
 **/
static void
gpk_application_remove_cb (PolKitGnomeAction *action,
		          GpkApplication     *application)
{
	gboolean ret;
	PkClient *client;
	GError *error = NULL;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* are we dumb and can't check for requires? */
	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_GET_REQUIRES) == FALSE) {
		/* no, just try to remove it without deps */
		gpk_application_remove_only (application, FALSE);
		return;
	}

	/* see if any packages require this one */
	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	g_signal_connect (client, "finished",
			  G_CALLBACK (gpk_application_requires_finished_cb), application);

	/* do the requires */
	pk_debug ("getting requires for %s", application->priv->package);
	ret = pk_client_get_requires (client, PK_FILTER_ENUM_INSTALLED, application->priv->package, TRUE, &error);
	if (!ret) {
		pk_warning ("failed to get requires: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_application_set_text_buffer:
 **/
static void
gpk_application_set_text_buffer (GtkWidget *widget, const gchar *text)
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
 * gpk_application_description_cb:
 **/
static void
gpk_application_description_cb (PkClient *client, const gchar *package_id,
			       const gchar *license, PkGroupEnum group,
			       const gchar *detail, const gchar *url,
			       guint64 size, GpkApplication *application)
{
	GtkWidget *widget;
	gchar *text;

	g_return_if_fail (PK_IS_APPLICATION (application));

	pk_debug ("description = %s:%i:%s:%s", package_id, group, detail, url);
	widget = glade_xml_get_widget (application->priv->glade_xml, "vbox_description_pane");
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

	/* set the description */
	text = g_markup_escape_text (detail, -1);
	widget = glade_xml_get_widget (application->priv->glade_xml, "textview_description");
	gpk_application_set_text_buffer (widget, text);
	g_free (text);

	/* if non-zero, set the size */
	if (size > 0) {
		gchar *value;
		widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_filesize");
		gtk_widget_show (widget);
		widget = glade_xml_get_widget (application->priv->glade_xml, "label_filesize");
		value = gpk_size_to_si_size_text (size);
		gtk_label_set_label (GTK_LABEL (widget), value);
		g_free (value);
	} else {
		widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_filesize");
		gtk_widget_hide (widget);
	}
}

/**
 * gpk_application_files_cb:
 **/
static void
gpk_application_files_cb (PkClient *client, const gchar *package_id,
			 const gchar *filelist, GpkApplication *application)
{
	GtkWidget *widget;

	g_return_if_fail (PK_IS_APPLICATION (application));

	widget = glade_xml_get_widget (application->priv->glade_xml, "vbox_description_pane");
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
		gpk_application_set_text_buffer (widget, list);
		g_strfreev (array);
		g_free (list);
	} else {
		/* no information */
		gpk_application_set_text_buffer (widget, _("No files"));
	}
}

/**
 * gpk_icon_valid:
 *
 * Check icon actually exists and is valid in this theme
 **/
gboolean
gpk_icon_valid (const gchar *icon)
{
	GtkIconInfo *icon_info;
	static GtkIconTheme *icon_theme = NULL;
	gboolean ret = TRUE;

	/* trivial case */
	if (pk_strzero (icon)) {
		return FALSE;
	}

	/* no unref required */
	if (icon_theme == NULL) {
		icon_theme = gtk_icon_theme_get_default ();
	}

	/* default to 32x32 */
	icon_info = gtk_icon_theme_lookup_icon (icon_theme, icon, 32, GTK_ICON_LOOKUP_USE_BUILTIN);
	if (icon_info == NULL) {
		pk_debug ("ignoring broken icon %s", icon);
		ret = FALSE;
	} else {
		/* we only used this to see if it was valid */
		gtk_icon_info_free (icon_info);
	}
	return ret;
}

/**
 * gpk_application_package_cb:
 **/
static void
gpk_application_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id,
			   const gchar *summary, GpkApplication *application)
{
	GtkTreeIter iter;
	PkExtraObj *eobj;
	gboolean valid = FALSE;
	gchar *text;

	g_return_if_fail (PK_IS_APPLICATION (application));

	pk_debug ("package = %s:%s:%s", pk_info_enum_to_text (info), package_id, summary);

	/* ignore progress */
	if (info != PK_INFO_ENUM_INSTALLED && info != PK_INFO_ENUM_AVAILABLE) {
		return;
	}

	/* mark as got so we don't warn */
	application->priv->has_package = TRUE;

	/* get convenience object */
	eobj = pk_extra_obj_new_from_package_id_summary (package_id, summary);

	/* check icon actually exists and is valid in this theme */
	valid = gpk_icon_valid (eobj->icon);

	/* nothing in the detail database or invalid */
	if (valid == FALSE) {
		g_free (eobj->icon);
		eobj->icon = g_strdup (gpk_info_enum_to_icon_name (info));
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
 * gpk_application_error_code_cb:
 **/
static void
gpk_application_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, GpkApplication *application)
{
	g_return_if_fail (PK_IS_APPLICATION (application));

	/* obvious message, don't tell the user */
	if (code == PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
		return;
	}

	gpk_application_error_message (application,
				      gpk_error_enum_to_localised_text (code), details);
}

/**
 * gpk_application_package_buffer_to_name_version:
 **/
static gchar *
gpk_application_package_buffer_to_name_version (PkClient *client)
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
		text_pretty = gpk_package_id_name_version (item->package_id);
		g_string_append_printf (string, "%s\n", text_pretty);
		g_free (text_pretty);
	}
	g_string_set_size (string, string->len - 1);
	return g_string_free (string, FALSE);
}

/**
 * gpk_application_refresh_search_results:
 **/
static gboolean
gpk_application_refresh_search_results (GpkApplication *application)
{
	GtkWidget *widget;
	gboolean ret;
	GError *error = NULL;
	PkRoleEnum role;

	/* get role -- do we actually need to do anything */
	pk_client_get_role (application->priv->client_search, &role, NULL, NULL);
	if (role == PK_ROLE_ENUM_UNKNOWN) {
		pk_debug ("no defined role, no not requeuing");
		return FALSE;
	}

	gtk_list_store_clear (application->priv->packages_store);
	ret = pk_client_requeue (application->priv->client_search, &error);
	if (!ret) {
		pk_warning ("failed to requeue the search: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* hide details */
	widget = glade_xml_get_widget (application->priv->glade_xml, "vbox_description_pane");
	gtk_widget_hide (widget);
	return TRUE;
}

/**
 * gpk_application_finished_cb:
 **/
static void
gpk_application_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, GpkApplication *application)
{
	GtkWidget *widget;
	PkRoleEnum role;
	gchar *text;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* get role */
	pk_client_get_role (client, &role, NULL, NULL);
	/* do we need to fill in the tab box? */
	if (role == PK_ROLE_ENUM_GET_DEPENDS) {
		text = gpk_application_package_buffer_to_name_version (client);
		widget = glade_xml_get_widget (application->priv->glade_xml, "textview_depends");
		gpk_application_set_text_buffer (widget, text);
		g_free (text);
	} else if (role == PK_ROLE_ENUM_GET_REQUIRES) {
		text = gpk_application_package_buffer_to_name_version (client);
		widget = glade_xml_get_widget (application->priv->glade_xml, "textview_requires");
		gpk_application_set_text_buffer (widget, text);
		g_free (text);
	}

	if (role == PK_ROLE_ENUM_SEARCH_NAME ||
	    role == PK_ROLE_ENUM_SEARCH_DETAILS ||
	    role == PK_ROLE_ENUM_SEARCH_GROUP) {

		/* switch round buttons */
		gpk_application_set_find_cancel_buttons (application, TRUE);

		/* were there no entries found? */
		if (exit == PK_EXIT_ENUM_SUCCESS && !application->priv->has_package) {
			GtkTreeIter iter;
			gtk_list_store_append (application->priv->packages_store, &iter);
			gtk_list_store_set (application->priv->packages_store, &iter,
					    PACKAGES_COLUMN_INSTALLED, FALSE,
					    PACKAGES_COLUMN_TEXT, _("No results were found"),
					    PACKAGES_COLUMN_IMAGE, "search",
					    -1);
		}

		/* focus back to the text extry */
		widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
		gtk_widget_grab_focus (widget);
	}

	/* hide widget */
	gpk_statusbar_hide (application->priv->statusbar);

	/* do we need to update the search? */
	if (role == PK_ROLE_ENUM_INSTALL_PACKAGE ||
	    role == PK_ROLE_ENUM_REMOVE_PACKAGE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_cancel2");
		gtk_widget_hide (widget);
		/* refresh the search as the items may have changed and the filter has not changed */
		gpk_application_refresh_search_results (application);
	}
}

/**
 * gpk_application_progress_changed_cb:
 **/
static void
gpk_application_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				    guint elapsed, guint remaining, GpkApplication *application)
{
	g_return_if_fail (PK_IS_APPLICATION (application));

	gpk_statusbar_set_percentage (application->priv->statusbar, percentage);
	gpk_statusbar_set_remaining (application->priv->statusbar, remaining);
}

/**
 * gpk_application_cancel_cb:
 **/
static void
gpk_application_cancel_cb (GtkWidget *button_widget, GpkApplication *application)
{
	gboolean ret;

	g_return_if_fail (PK_IS_APPLICATION (application));

	ret = pk_client_cancel (application->priv->client_search, NULL);
	pk_debug ("canceled? %i", ret);

	/* switch buttons around */
	if (ret) {
		gpk_application_set_find_cancel_buttons (application, TRUE);
		application->priv->search_mode = PK_MODE_UNKNOWN;
	}
}

/**
 * gpk_application_perform_search_name_details_file:
 **/
static gboolean
gpk_application_perform_search_name_details_file (GpkApplication *application)
{
	GtkWidget *widget;
	const gchar *package;
	GError *error = NULL;
	gboolean ret;

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	package = gtk_entry_get_text (GTK_ENTRY (widget));

	/* have we got input? */
	if (pk_strzero (package)) {
		pk_debug ("no input");
		return FALSE;
	}

	ret = pk_strvalidate (package);
	if (!ret) {
		pk_debug ("invalid input text, will fail");
		/* TODO - make the dialog turn red... */
		gpk_application_error_message (application, _("Invalid search text"),
					      _("The search text contains invalid characters"));
		return FALSE;
	}
	pk_debug ("find %s", package);

	/* reset */
	ret = pk_client_reset (application->priv->client_search, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* do the search */
	if (application->priv->search_type == PK_SEARCH_NAME) {
		ret = pk_client_search_name (application->priv->client_search, application->priv->filters_current, package, &error);
	} else if (application->priv->search_type == PK_SEARCH_DETAILS) {
		ret = pk_client_search_details (application->priv->client_search, application->priv->filters_current, package, &error);
	} else if (application->priv->search_type == PK_SEARCH_FILE) {
		ret = pk_client_search_file (application->priv->client_search, application->priv->filters_current, package, &error);
	} else {
		pk_warning ("invalid search type");
		return FALSE;
	}

	if (!ret) {
		gpk_application_error_message (application,
					      _("The search could not be completed"), error->message);
		g_error_free (error);
		return FALSE;
	}

	/* clear existing list */
	gtk_list_store_clear (application->priv->packages_store);
	application->priv->has_package = FALSE;

	/* hide details */
	widget = glade_xml_get_widget (application->priv->glade_xml, "vbox_description_pane");
	gtk_widget_hide (widget);

	/* switch around buttons */
	gpk_application_set_find_cancel_buttons (application, FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "notebook_search_cancel");
	gtk_notebook_set_current_page (GTK_NOTEBOOK (widget), 1);


	return TRUE;
}

/**
 * gpk_application_perform_search_others:
 **/
static gboolean
gpk_application_perform_search_others (GpkApplication *application)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);
	g_return_val_if_fail (application->priv->group != NULL, FALSE);

	/* cancel this, we don't care about old results that are pending */
	ret = pk_client_reset (application->priv->client_search, &error);
	if (!ret) {
		pk_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* refresh the search as the items may have changed */
	gtk_list_store_clear (application->priv->packages_store);

	if (application->priv->search_mode == PK_MODE_GROUP) {
		ret = pk_client_search_group (application->priv->client_search,
					      application->priv->filters_current,
					      application->priv->group, &error);
	} else {
		ret = pk_client_get_packages (application->priv->client_search,
					      application->priv->filters_current, &error);
	}
	if (ret) {
		/* switch around buttons */
		gpk_application_set_find_cancel_buttons (application, FALSE);
	} else {
		gpk_application_error_message (application,
					      _("The group could not be queried"), error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * gpk_application_perform_search:
 **/
static gboolean
gpk_application_perform_search (GpkApplication *application)
{
	gboolean ret = FALSE;
	if (application->priv->search_mode == PK_MODE_NAME_DETAILS_FILE) {
		ret = gpk_application_perform_search_name_details_file (application);
	} else if (application->priv->search_mode == PK_MODE_GROUP ||
		   application->priv->search_mode == PK_MODE_ALL_PACKAGES) {
		ret = gpk_application_perform_search_others (application);
	} else {
		pk_debug ("doing nothing");
	}
	return ret;
}

/**
 * gpk_application_find_cb:
 **/
static void
gpk_application_find_cb (GtkWidget *button_widget, GpkApplication *application)
{
	g_return_if_fail (PK_IS_APPLICATION (application));

	application->priv->search_mode = PK_MODE_NAME_DETAILS_FILE;
	gpk_application_perform_search (application);
}

/**
 * gpk_application_quit:
 * @event: The event type, unused.
 **/
static gboolean
gpk_application_quit (GpkApplication *application)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	/* we might have visual stuff running, close them down */
	ret = pk_client_cancel (application->priv->client_search, &error);
	if (!ret) {
		pk_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
		error = NULL;
	}
	ret = pk_client_cancel (application->priv->client_description, &error);
	if (!ret) {
		pk_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
		error = NULL;
	}
	ret = pk_client_cancel (application->priv->client_files, &error);
	if (!ret) {
		pk_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
		error = NULL;
	}

	pk_debug ("emitting action-close");
	g_signal_emit (application, signals [ACTION_CLOSE], 0);
	return TRUE;
}

/**
 * gpk_application_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
gpk_application_delete_event_cb (GtkWidget	*widget,
				GdkEvent	*event,
				GpkApplication	*application)
{
	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	gpk_application_quit (application);
	return FALSE;
}

static gboolean
gpk_application_text_changed_cb (GtkEntry *entry, GdkEventKey *event, GpkApplication *application)
{
	gboolean valid;
	GtkWidget *widget;
	const gchar *package;
	GtkTreeSelection *selection;

	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	package = gtk_entry_get_text (GTK_ENTRY (widget));

	/* clear group selection if we have the tab */
	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_groups");
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
		gtk_tree_selection_unselect_all (selection);
	}

	/* check for invalid chars */
	valid = pk_strvalidate (package);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	if (valid == FALSE || pk_strzero (package)) {
		gtk_widget_set_sensitive (widget, FALSE);
	} else {
		gtk_widget_set_sensitive (widget, TRUE);
	}
	return FALSE;
}

static void
gpk_application_packages_add_columns (GtkTreeView *treeview)
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
gpk_application_groups_add_columns (GtkTreeView *treeview)
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
 * gpk_application_groups_treeview_clicked_cb:
 **/
static void
gpk_application_groups_treeview_clicked_cb (GtkTreeSelection *selection, GpkApplication *application)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkWidget *widget;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* hide the details */
	widget = glade_xml_get_widget (application->priv->glade_xml, "vbox_description_pane");
	gtk_widget_hide (widget);

	/* clear the search text if we clicked the group list */
	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	gtk_entry_set_text (GTK_ENTRY (widget), "");

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		g_free (application->priv->group);
		gtk_tree_model_get (model, &iter, GROUPS_COLUMN_ID, &application->priv->group, -1);
		pk_debug ("selected row is: %s", application->priv->group);

		/* GetPackages? */
		if (pk_strequal (application->priv->group, "all-packages")) {
			application->priv->search_mode = PK_MODE_ALL_PACKAGES;
		} else {
			application->priv->search_mode = PK_MODE_GROUP;
		}

		/* actually do the search */
		gpk_application_perform_search (application);
	}
}

/**
 * gpk_application_notebook_populate:
 **/
static gboolean
gpk_application_notebook_populate (GpkApplication *application, gint page)
{
	gboolean ret;
	GtkWidget *widget;
	GtkWidget *child;
	gint potential;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_APPLICATION (application), FALSE);

	/* are we just removing tabs? */
	if (application->priv->package == NULL) {
		return FALSE;
	}

	/* show the box */
	widget = glade_xml_get_widget (application->priv->glade_xml, "vbox_description_pane");
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
		gpk_application_set_text_buffer (widget, NULL);

		/* hide the homepage button until we get data */
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_homepage");
		gtk_widget_hide (widget);

		/* cancel any previous request */
		ret = pk_client_reset (application->priv->client_description, &error);
		if (!ret) {
			pk_warning ("failed to cancel, and adding to queue: %s", error->message);
			g_error_free (error);
			return FALSE;
		}

		/* get the description */
		ret = pk_client_get_description (application->priv->client_description,
						 application->priv->package, &error);
		if (!ret) {
			pk_warning ("failed to get description: %s", error->message);
			g_error_free (error);
		}
		return ret;
	}

	/* are we description? */
	child = glade_xml_get_widget (application->priv->glade_xml, "vbox_files");
	potential = gtk_notebook_page_num (GTK_NOTEBOOK (widget), child);
	pk_debug ("potential=%i", potential);
	if (potential == page) {
		/* clear the old text */
		widget = glade_xml_get_widget (application->priv->glade_xml, "textview_files");
		gpk_application_set_text_buffer (widget, NULL);

		/* cancel any previous request */
		ret = pk_client_reset (application->priv->client_files, &error);
		if (!ret) {
			pk_warning ("failed to cancel, and adding to queue: %s", error->message);
			g_error_free (error);
			return FALSE;
		}

		/* get the filelist */
		ret = pk_client_get_files (application->priv->client_files,
					   application->priv->package, &error);
		if (!ret) {
			pk_warning ("failed to det depends: %s", error->message);
			g_error_free (error);
		}
		return ret;
	}

	/* are we depends? */
	child = glade_xml_get_widget (application->priv->glade_xml, "vbox_depends");
	potential = gtk_notebook_page_num (GTK_NOTEBOOK (widget), child);
	pk_debug ("potential=%i", potential);
	if (potential == page) {
		/* clear the old text */
		widget = glade_xml_get_widget (application->priv->glade_xml, "textview_depends");
		gpk_application_set_text_buffer (widget, NULL);

		/* cancel any previous request */
		ret = pk_client_reset (application->priv->client_files, &error);
		if (!ret) {
			pk_warning ("failed to cancel, and adding to queue: %s", error->message);
			g_error_free (error);
			return FALSE;
		}
		/* get the depends */
		ret = pk_client_get_depends (application->priv->client_files, PK_FILTER_ENUM_NONE,
					     application->priv->package, FALSE, &error);

		if (!ret) {
			pk_warning ("failed to det depends: %s", error->message);
			g_error_free (error);
		}
		return ret;
	}

	/* are we requires? */
	child = glade_xml_get_widget (application->priv->glade_xml, "vbox_requires");
	potential = gtk_notebook_page_num (GTK_NOTEBOOK (widget), child);
	pk_debug ("potential=%i", potential);
	if (potential == page) {
		/* clear the old text */
		widget = glade_xml_get_widget (application->priv->glade_xml, "textview_requires");
		gpk_application_set_text_buffer (widget, NULL);

		/* cancel any previous request */
		ret = pk_client_reset (application->priv->client_files, &error);
		if (!ret) {
			pk_warning ("failed to cancel, and adding to queue: %s", error->message);
			g_error_free (error);
			return FALSE;
		}
		/* get the requires */
		ret = pk_client_get_requires (application->priv->client_files, PK_FILTER_ENUM_NONE,
					      application->priv->package, TRUE, &error);

		if (!ret) {
			pk_warning ("failed to det depends: %s", error->message);
			g_error_free (error);
		}
		return ret;
	}
	pk_warning ("unknown tab %i!", page);
	return FALSE;
}

/**
 * gpk_application_notebook_changed_cb:
 **/
static void
gpk_application_notebook_changed_cb (GtkWidget *widget, gboolean arg1,
				    gint page, GpkApplication *application)
{
	g_return_if_fail (PK_IS_APPLICATION (application));

	gpk_application_notebook_populate (application, page);
}

/**
 * gpk_application_packages_treeview_clicked_cb:
 **/
static void
gpk_application_packages_treeview_clicked_cb (GtkTreeSelection *selection,
				 GpkApplication *application)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean installed;
	gchar *package_id;
	guint page;

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
		pk_debug ("selected row is: %i %s", installed, application->priv->package);

		if (installed == FALSE &&
		    pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_INSTALL_PACKAGE)) {
			polkit_gnome_action_set_visible (application->priv->install_action, TRUE);
		} else {
			polkit_gnome_action_set_visible (application->priv->install_action, FALSE);
		}
		if (installed &&
		    pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_REMOVE_PACKAGE)) {
			polkit_gnome_action_set_visible (application->priv->remove_action, TRUE);
		} else {
			polkit_gnome_action_set_visible (application->priv->remove_action, FALSE);
		}

		/* refresh */
		widget = glade_xml_get_widget (application->priv->glade_xml, "notebook_description");
		page = gtk_notebook_get_current_page (GTK_NOTEBOOK (widget));
		gpk_application_notebook_populate (application, page);

	} else {
		pk_debug ("no row selected");
		polkit_gnome_action_set_visible (application->priv->install_action, FALSE);
		polkit_gnome_action_set_visible (application->priv->remove_action, FALSE);
		widget = glade_xml_get_widget (application->priv->glade_xml, "button_homepage");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_filesize");
		gtk_widget_hide (widget);
	}
}

/**
 * gpk_application_connection_changed_cb:
 **/
static void
gpk_application_connection_changed_cb (PkConnection *pconnection, gboolean connected, GpkApplication *application)
{
	g_return_if_fail (PK_IS_APPLICATION (application));

	pk_debug ("connected=%i", connected);
}

/**
 * gpk_application_group_add_data:
 **/
static void
gpk_application_group_add_data (GpkApplication *application, PkGroupEnum group)
{
	GtkTreeIter iter;
	const gchar *icon_name;
	const gchar *text;

	gtk_list_store_append (application->priv->groups_store, &iter);

	text = gpk_group_enum_to_localised_text (group);
	icon_name = gpk_group_enum_to_icon_name (group);
	gtk_list_store_set (application->priv->groups_store, &iter,
			    GROUPS_COLUMN_NAME, text,
			    GROUPS_COLUMN_ID, pk_group_enum_to_text (group),
			    GROUPS_COLUMN_ICON, icon_name,
			    -1);
}

/**
 * gpk_application_create_custom_widget:
 **/
static GtkWidget *
gpk_application_create_custom_widget (GladeXML *xml, gchar *func_name, gchar *name,
				     gchar *string1, gchar *string2,
				     gint int1, gint int2, gpointer user_data)
{
	if (pk_strequal (name, "entry_text")) {
		pk_debug ("creating sexy icon=%s", name);
		return sexy_icon_entry_new ();
	}
	pk_warning ("name unknown=%s", name);
	return NULL;
}

/**
 * gpk_application_popup_position_menu:
 **/
static void
gpk_application_popup_position_menu (GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer user_data)
{
	GtkWidget     *widget;
	GtkRequisition requisition;
	gint menu_xpos = 0;
	gint menu_ypos = 0;

	widget = GTK_WIDGET (user_data);

	/* find the location */
	gdk_window_get_origin (widget->window, &menu_xpos, &menu_ypos);
	gtk_widget_size_request (GTK_WIDGET (widget), &requisition);

	/* set the position */
	*x = menu_xpos;
	*y = menu_ypos + requisition.height - 1;
	*push_in = TRUE;
}

/**
 * gpk_application_menu_search_by_name:
 **/
static void
gpk_application_menu_search_by_name (GtkMenuItem *item, gpointer data)
{
	GtkWidget *icon;
	GtkWidget *widget;
	GpkApplication *application = GPK_APPLICATION (data);

	/* change type */
	application->priv->search_type = PK_SEARCH_NAME;
	pk_debug ("set search type=%i", application->priv->search_type);

	/* set the new icon */
	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	gtk_widget_set_tooltip_text (widget, _("Searching by name"));
	icon = gtk_image_new_from_stock (GTK_STOCK_FIND, GTK_ICON_SIZE_MENU);
	sexy_icon_entry_set_icon (SEXY_ICON_ENTRY (widget), SEXY_ICON_ENTRY_PRIMARY, GTK_IMAGE (icon));
}

/**
 * gpk_application_menu_search_by_description:
 **/
static void
gpk_application_menu_search_by_description (GtkMenuItem *item, gpointer data)
{
	GtkWidget *icon;
	GtkWidget *widget;
	GpkApplication *application = GPK_APPLICATION (data);

	/* set type */
	application->priv->search_type = PK_SEARCH_DETAILS;
	pk_debug ("set search type=%i", application->priv->search_type);

	/* set the new icon */
	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	gtk_widget_set_tooltip_text (widget, _("Searching by description"));
	icon = gtk_image_new_from_stock (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
	sexy_icon_entry_set_icon (SEXY_ICON_ENTRY (widget), SEXY_ICON_ENTRY_PRIMARY, GTK_IMAGE (icon));
}

/**
 * gpk_application_menu_search_by_file:
 **/
static void
gpk_application_menu_search_by_file (GtkMenuItem *item, gpointer data)
{
	GtkWidget *icon;
	GtkWidget *widget;
	GpkApplication *application = GPK_APPLICATION (data);

	/* set type */
	application->priv->search_type = PK_SEARCH_FILE;
	pk_debug ("set search type=%i", application->priv->search_type);

	/* set the new icon */
	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	gtk_widget_set_tooltip_text (widget, _("Searching by file"));
	icon = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU);
	sexy_icon_entry_set_icon (SEXY_ICON_ENTRY (widget), SEXY_ICON_ENTRY_PRIMARY, GTK_IMAGE (icon));
}

/**
 * gpk_application_entry_text_icon_pressed_cb:
 **/
static void
gpk_application_entry_text_icon_pressed_cb (SexyIconEntry *entry, gint icon_pos, gint button, gpointer data)
{
	GtkMenu *menu = (GtkMenu*) gtk_menu_new ();
	GtkWidget *item;
	GtkWidget *image;
	GpkApplication *application = GPK_APPLICATION (data);

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* only respond to left button */
	if (button != 1) {
		return;
	}
	pk_debug ("icon_pos=%i", icon_pos);

	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_NAME)) {
		item = gtk_image_menu_item_new_with_mnemonic (_("Search by name"));
		image = gtk_image_new_from_stock (GTK_STOCK_FIND, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_name), application);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_DETAILS)) {
		item = gtk_image_menu_item_new_with_mnemonic (_("Search by description"));
		image = gtk_image_new_from_stock (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_description), application);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_FILE)) {
		item = gtk_image_menu_item_new_with_mnemonic (_("Search by file"));
		image = gtk_image_new_from_stock (GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU);
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (gpk_application_menu_search_by_file), application);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	gtk_widget_show_all (GTK_WIDGET (menu));
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
			gpk_application_popup_position_menu, entry,
			1, gtk_get_current_event_time());
}

/**
 * gpk_application_create_completion_model:
 *
 * Creates a tree model containing the completions
 **/
static GtkTreeModel *
gpk_application_create_completion_model (void)
{
	GtkListStore *store;
	GtkTreeIter iter;

	store = gtk_list_store_new (1, G_TYPE_STRING);

	/* append one word */
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, "gnome-power-manager", -1);

	/* append another word */
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, "gnome-screensaver", -1);

	/* and another word */
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, "hal", -1);

	return GTK_TREE_MODEL (store);
}


/**
 *  * gpk_application_about_dialog_url_cb:
 *   **/
static void
gpk_application_about_dialog_url_cb (GtkAboutDialog *about, const char *address, gpointer data)
{
	GError *error = NULL;
	gboolean ret;

	char *cmdline;
	GdkScreen *gscreen;
	GtkWidget *error_dialog;
	gchar *url;
	gchar *protocol = (gchar*) data;

	if (protocol != NULL)
		url = g_strconcat (protocol, address, NULL);
	else
		url = g_strdup (address);

	gscreen = gtk_window_get_screen (GTK_WINDOW (about));

	cmdline = g_strconcat ("xdg-open ", url, NULL);
	ret = gdk_spawn_command_line_on_screen (gscreen, cmdline, &error);
	g_free (cmdline);

	if (ret)
		goto out;

	g_error_free (error);
	error = NULL;
	cmdline = g_strconcat ("gnome-open ", url, NULL);
	ret = gdk_spawn_command_line_on_screen (gscreen, cmdline, &error);
	g_free (cmdline);

	if (!ret) {
		error_dialog = gtk_message_dialog_new (GTK_WINDOW (about),
						       GTK_DIALOG_MODAL,
						       GTK_MESSAGE_INFO,
						       GTK_BUTTONS_OK,
						       _("Failed to show url"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (error_dialog),
							  "%s", error->message);
		gtk_dialog_run (GTK_DIALOG (error_dialog));
		gtk_widget_destroy (error_dialog);
		g_error_free (error);
	}

out:
	g_free (url);
}

/**
 * gpk_application_menu_help_cb:
 **/
static void
gpk_application_menu_help_cb (GtkAction *action, GpkApplication *application)
{
	gpk_gnome_help ("add-remove");
}

/**
 * gpk_application_menu_about_cb:
 **/
static void
gpk_application_menu_about_cb (GtkAction *action, GpkApplication *application)
{
	static gboolean been_here = FALSE;
	GtkWidget *main_window;
	const char *authors[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *documenters[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *artists[] = {
		"Richard Hughes <richard@hughsie.com>",
		NULL};
	const char *license[] = {
		N_("Licensed under the GNU General Public License Version 2"),
		N_("PackageKit is free software; you can redistribute it and/or\n"
		   "modify it under the terms of the GNU General Public License\n"
		   "as published by the Free Software Foundation; either version 2\n"
		   "of the License, or (at your option) any later version."),
		N_("PackageKit is distributed in the hope that it will be useful,\n"
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		   "GNU General Public License for more details."),
		N_("You should have received a copy of the GNU General Public License\n"
		   "along with this program; if not, write to the Free Software\n"
		   "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA\n"
		   "02110-1301, USA.")
	};
	const char  *translators = _("translator-credits");
	char	    *license_trans;

	/* Translators comment: put your own name here to appear in the about dialog. */
	if (!strcmp (translators, "translator-credits")) {
		translators = NULL;
	}

	license_trans = g_strconcat (_(license[0]), "\n\n", _(license[1]), "\n\n",
				     _(license[2]), "\n\n", _(license[3]), "\n",  NULL);

	if (!been_here) {
		been_here = TRUE;
		gtk_about_dialog_set_url_hook (gpk_application_about_dialog_url_cb, NULL, NULL);
		gtk_about_dialog_set_email_hook (gpk_application_about_dialog_url_cb, "mailto:", NULL);
	}

	/* use parent */
	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");

	gtk_window_set_default_icon_name (PK_STOCK_APP_ICON);
	gtk_show_about_dialog (GTK_WINDOW (main_window),
			       "version", PACKAGE_VERSION,
			       "copyright", "Copyright \xc2\xa9 2007-2008 Richard Hughes",
			       "license", license_trans,
			       "website-label", _("PackageKit Website"),
			       "website", "http://www.packagekit.org",
			       "comments", _("Package Manager for GNOME"),
			       "authors", authors,
			       "documenters", documenters,
			       "artists", artists,
			       "translator-credits", translators,
			       "logo-icon-name", PK_STOCK_APP_ICON,
			       NULL);
	g_free (license_trans);
}

/**
 * gpk_application_menu_refresh_cb:
 **/
static void
gpk_application_menu_refresh_cb (GtkAction *action, GpkApplication *application)
{
	gboolean ret;
	GError *error = NULL;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* can we cancel what we are doing? */
	ret = pk_client_reset (application->priv->client_action, &error);
	if (!ret) {
		gpk_application_error_message (application, _("Package list could not be refreshed"), error->message);
		g_error_free (error);
		return;
	}

	/* try to refresh the cache */
	ret = pk_client_refresh_cache (application->priv->client_action, FALSE, &error);
	if (!ret) {
		gpk_application_error_message (application, _("The package could not be installed"), error->message);
		g_error_free (error);
		return;
	}
	pk_debug ("should be refreshing...");
}

/**
 * gpk_application_menu_sources_cb:
 **/
static void
gpk_application_menu_sources_cb (GtkAction *action, GpkApplication *application)
{
	gboolean ret;

	g_return_if_fail (PK_IS_APPLICATION (application));

	ret = g_spawn_command_line_async ("gpk-repo", NULL);
	if (!ret) {
		pk_warning ("spawn of pk-repo failed");
	}
}

/**
 * gpk_application_menu_quit_cb:
 **/
static void
gpk_application_menu_quit_cb (GtkAction *action, GpkApplication *application)
{
	g_return_if_fail (PK_IS_APPLICATION (application));

	gpk_application_quit (application);
}

/**
 * gpk_application_menu_filter_installed_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_installed_cb (GtkWidget *widget, GpkApplication *application)
{
	const gchar *name;

	g_return_if_fail (PK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))) {
		return;
	}

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_enums_add (application->priv->filters_current, PK_FILTER_ENUM_INSTALLED);
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_INSTALLED);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_INSTALLED);
		pk_enums_add (application->priv->filters_current, PK_FILTER_ENUM_NOT_INSTALLED);
	} else {
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_INSTALLED);
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_INSTALLED);
	}

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_menu_filter_devel_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_devel_cb (GtkWidget *widget, GpkApplication *application)
{
	const gchar *name;

	g_return_if_fail (PK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))) {
		return;
	}

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_enums_add (application->priv->filters_current, PK_FILTER_ENUM_DEVELOPMENT);
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_DEVELOPMENT);
		pk_enums_add (application->priv->filters_current, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	} else {
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_DEVELOPMENT);
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	}

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_menu_filter_gui_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_gui_cb (GtkWidget *widget, GpkApplication *application)
{
	const gchar *name;

	g_return_if_fail (PK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))) {
		return;
	}

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_enums_add (application->priv->filters_current, PK_FILTER_ENUM_GUI);
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_GUI);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_GUI);
		pk_enums_add (application->priv->filters_current, PK_FILTER_ENUM_NOT_GUI);
	} else {
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_GUI);
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_GUI);
	}

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_menu_filter_free_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_free_cb (GtkWidget *widget, GpkApplication *application)
{
	const gchar *name;

	g_return_if_fail (PK_IS_APPLICATION (application));

	name = gtk_widget_get_name (widget);

	/* only care about new state */
	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget))) {
		return;
	}

	/* set new filter */
	if (g_str_has_suffix (name, "_yes")) {
		pk_enums_add (application->priv->filters_current, PK_FILTER_ENUM_FREE);
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_FREE);
	} else if (g_str_has_suffix (name, "_no")) {
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_FREE);
		pk_enums_add (application->priv->filters_current, PK_FILTER_ENUM_NOT_FREE);
	} else {
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_FREE);
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_NOT_FREE);
	}

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_menu_filter_basename_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_basename_cb (GtkWidget *widget, GpkApplication *application)
{
	gboolean enabled;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* save users preference to gconf */
	enabled = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	gconf_client_set_bool (application->priv->gconf_client,
			       GPK_CONF_APPLICATION_FILTER_BASENAME, enabled, NULL);

	/* change the filter */
	if (enabled) {
		pk_enums_add (application->priv->filters_current, PK_FILTER_ENUM_BASENAME);
	} else {
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_BASENAME);
	}

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_menu_filter_newest_cb:
 * @widget: The GtkWidget object
 **/
static void
gpk_application_menu_filter_newest_cb (GtkWidget *widget, GpkApplication *application)
{
	gboolean enabled;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* save users preference to gconf */
	enabled = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	gconf_client_set_bool (application->priv->gconf_client,
			       GPK_CONF_APPLICATION_FILTER_NEWEST, enabled, NULL);

	/* change the filter */
	if (enabled) {
		pk_enums_add (application->priv->filters_current, PK_FILTER_ENUM_NEWEST);
	} else {
		pk_enums_remove (application->priv->filters_current, PK_FILTER_ENUM_NEWEST);
	}

	/* refresh the search results */
	gpk_application_perform_search (application);
}

/**
 * gpk_application_status_changed_cb:
 **/
static void
gpk_application_status_changed_cb (PkClient *client, PkStatusEnum status, GpkApplication *application)
{
	g_return_if_fail (PK_IS_APPLICATION (application));
	gpk_statusbar_set_status (application->priv->statusbar, status);
}

/**
 * gpk_application_allow_cancel_cb:
 **/
static void
gpk_application_allow_cancel_cb (PkClient *client, gboolean allow_cancel, GpkApplication *application)
{
	GtkWidget *widget;

	g_return_if_fail (PK_IS_APPLICATION (application));

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_cancel");
	gtk_widget_set_sensitive (widget, allow_cancel);
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_cancel2");
	gtk_widget_set_sensitive (widget, allow_cancel);
}

/**
 * gpk_application_package_row_activated_cb:
 **/
void
gpk_application_package_row_activated_cb (GtkTreeView *treeview, GtkTreePath *path,
					 GtkTreeViewColumn *col, GpkApplication *application)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *package_id = NULL;
	gboolean installed;
	gboolean ret;

	g_return_if_fail (PK_IS_APPLICATION (application));

	/* get selection */
	model = gtk_tree_view_get_model (treeview);
	ret = gtk_tree_model_get_iter (model, &iter, path);
	if (!ret) {
		pk_warning ("failed to get selection");
		return;
	}

	/* get details */
	gtk_tree_model_get (model, &iter,
			    PACKAGES_COLUMN_INSTALLED, &installed,
			    PACKAGES_COLUMN_ID, &package_id, -1);
	if (!installed) {
		pk_debug ("auto installing due to double click");
		gpk_application_install (application, package_id);
	}
	g_free (package_id);
}

/**
 * gpk_application_init:
 **/
static void
gpk_application_init (GpkApplication *application)
{
	GtkWidget *main_window;
	GtkWidget *vbox;
	GtkWidget *widget;
	GtkEntryCompletion *completion;
	GtkTreeModel *completion_model;
	GtkTreeSelection *selection;
	gboolean autocomplete;
	gboolean enabled;
	gchar *locale; /* does not need to be freed */
	guint page;
	guint i;
	gboolean ret;
	PolKitAction *pk_action;
	GtkWidget *button;
	GtkWidget *item;

	application->priv = GPK_APPLICATION_GET_PRIVATE (application);
	application->priv->package = NULL;
	application->priv->group = NULL;
	application->priv->url = NULL;
	application->priv->has_package = FALSE;
	application->priv->gconf_client = gconf_client_get_default ();

	application->priv->search_type = PK_SEARCH_UNKNOWN;
	application->priv->search_mode = PK_MODE_UNKNOWN;
	application->priv->filters_current = PK_FILTER_ENUM_NONE;

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
                                           PK_DATA G_DIR_SEPARATOR_S "icons");

	/* use a sexy widget */
	glade_set_custom_handler (gpk_application_create_custom_widget, application);

	application->priv->control = pk_control_new ();

	application->priv->client_search = pk_client_new ();
	g_signal_connect (application->priv->client_search, "package",
			  G_CALLBACK (gpk_application_package_cb), application);
	g_signal_connect (application->priv->client_search, "error-code",
			  G_CALLBACK (gpk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_search, "finished",
			  G_CALLBACK (gpk_application_finished_cb), application);
	g_signal_connect (application->priv->client_search, "progress-changed",
			  G_CALLBACK (gpk_application_progress_changed_cb), application);
	g_signal_connect (application->priv->client_search, "status-changed",
			  G_CALLBACK (gpk_application_status_changed_cb), application);
	g_signal_connect (application->priv->client_search, "allow-cancel",
			  G_CALLBACK (gpk_application_allow_cancel_cb), application);

	application->priv->client_action = pk_client_new ();
	g_signal_connect (application->priv->client_action, "package",
			  G_CALLBACK (gpk_application_package_cb), application);
	g_signal_connect (application->priv->client_action, "error-code",
			  G_CALLBACK (gpk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_action, "finished",
			  G_CALLBACK (gpk_application_finished_cb), application);
	g_signal_connect (application->priv->client_action, "progress-changed",
			  G_CALLBACK (gpk_application_progress_changed_cb), application);
	g_signal_connect (application->priv->client_action, "status-changed",
			  G_CALLBACK (gpk_application_status_changed_cb), application);
	g_signal_connect (application->priv->client_action, "allow-cancel",
			  G_CALLBACK (gpk_application_allow_cancel_cb), application);

	application->priv->client_description = pk_client_new ();
	g_signal_connect (application->priv->client_description, "description",
			  G_CALLBACK (gpk_application_description_cb), application);
	g_signal_connect (application->priv->client_description, "error-code",
			  G_CALLBACK (gpk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_description, "finished",
			  G_CALLBACK (gpk_application_finished_cb), application);
	g_signal_connect (application->priv->client_description, "progress-changed",
			  G_CALLBACK (gpk_application_progress_changed_cb), application);
	g_signal_connect (application->priv->client_description, "status-changed",
			  G_CALLBACK (gpk_application_status_changed_cb), application);
	g_signal_connect (application->priv->client_description, "allow-cancel",
			  G_CALLBACK (gpk_application_allow_cancel_cb), application);

	application->priv->client_files = pk_client_new ();
	pk_client_set_use_buffer (application->priv->client_files, TRUE, NULL);
	g_signal_connect (application->priv->client_files, "files",
			  G_CALLBACK (gpk_application_files_cb), application);
	g_signal_connect (application->priv->client_files, "error-code",
			  G_CALLBACK (gpk_application_error_code_cb), application);
	g_signal_connect (application->priv->client_files, "finished",
			  G_CALLBACK (gpk_application_finished_cb), application);
	g_signal_connect (application->priv->client_files, "progress-changed",
			  G_CALLBACK (gpk_application_progress_changed_cb), application);
	g_signal_connect (application->priv->client_files, "status-changed",
			  G_CALLBACK (gpk_application_status_changed_cb), application);
	g_signal_connect (application->priv->client_files, "allow-cancel",
			  G_CALLBACK (gpk_application_allow_cancel_cb), application);

	/* get enums */
	application->priv->roles = pk_control_get_actions (application->priv->control);
	application->priv->filters = pk_control_get_filters (application->priv->control);
	application->priv->groups = pk_control_get_groups (application->priv->control);

	application->priv->pconnection = pk_connection_new ();
	g_signal_connect (application->priv->pconnection, "connection-changed",
			  G_CALLBACK (gpk_application_connection_changed_cb), application);

	/* single instance, so this is valid */
	application->priv->extra = pk_extra_new ();
	ret = pk_extra_set_database (application->priv->extra, NULL);
	if (!ret) {
		pk_warning ("Failure setting database");
	}

	/* set the locale */
	locale = setlocale (LC_ALL, NULL);
	pk_extra_set_locale (application->priv->extra, locale);

	application->priv->glade_xml = glade_xml_new (PK_DATA "/gpk-application.glade", NULL, NULL);
	main_window = glade_xml_get_widget (application->priv->glade_xml, "window_manager");

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), PK_STOCK_APP_ICON);

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (gpk_application_delete_event_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_package");
	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.install");
	application->priv->install_action = polkit_gnome_action_new_default ("install",
									     pk_action,
									     _("_Install"),
									     _("Install selected package"));
	g_object_set (application->priv->install_action,
		      "no-icon-name", GTK_STOCK_FLOPPY,
		      "auth-icon-name", GTK_STOCK_FLOPPY,
		      "yes-icon-name", GTK_STOCK_FLOPPY,
		      "self-blocked-icon-name", GTK_STOCK_FLOPPY,
		      NULL);
	polkit_action_unref (pk_action);
	g_signal_connect (application->priv->install_action, "activate",
			  G_CALLBACK (gpk_application_install_cb), application);
        button = polkit_gnome_action_create_button (application->priv->install_action);

	gtk_box_pack_start (GTK_BOX (widget), button, FALSE, FALSE, 0);
	gtk_box_reorder_child (GTK_BOX (widget), button, 0);

	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.remove");
	application->priv->remove_action = polkit_gnome_action_new_default ("remove",
									    pk_action,
									    _("_Remove"),
									    _("Remove selected package"));
	g_object_set (application->priv->remove_action,
		      "no-icon-name", GTK_STOCK_DIALOG_ERROR,
		      "auth-icon-name", GTK_STOCK_DIALOG_ERROR,
		      "yes-icon-name", GTK_STOCK_DIALOG_ERROR,
		      "self-blocked-icon-name", GTK_STOCK_DIALOG_ERROR,
		      NULL);
	polkit_action_unref (pk_action);
	g_signal_connect (application->priv->remove_action, "activate",
			  G_CALLBACK (gpk_application_remove_cb), application);
        button = polkit_gnome_action_create_button (application->priv->remove_action);

	gtk_box_pack_start (GTK_BOX (widget), button, FALSE, FALSE, 0);
	gtk_box_reorder_child (GTK_BOX (widget), button, 1);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_homepage");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_homepage_cb), application);
	gtk_widget_set_tooltip_text (widget, _("Visit homepage for selected package"));

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_about");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_about_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_help");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_help_cb), application);

	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.refresh-cache");
	application->priv->refresh_action = polkit_gnome_action_new_default ("refresh",
									     pk_action,
									     _("_Refresh application lists"),
									     NULL);
	g_object_set (application->priv->refresh_action,
		      "no-icon-name", "gtk-redo-ltr",
		      "auth-icon-name", "gtk-redo-ltr",
		      "yes-icon-name", "gtk-redo-ltr",
		      "self-blocked-icon-name", "gtk-redo-ltr",
		      NULL);
	polkit_action_unref (pk_action);
	g_signal_connect (application->priv->refresh_action, "activate",
			  G_CALLBACK (gpk_application_menu_refresh_cb), application);
	item = gtk_action_create_menu_item (GTK_ACTION (application->priv->refresh_action));

	widget = glade_xml_get_widget (application->priv->glade_xml, "menu_system");
	gtk_menu_shell_prepend (GTK_MENU_SHELL (widget), item);

	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_sources");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_sources_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "imagemenuitem_quit");
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_menu_quit_cb), application);

	/* installed filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_installed_yes");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_installed_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_installed_no");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_installed_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_installed_both");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_installed_cb), application);

	/* devel filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_devel_yes");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_devel_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_devel_no");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_devel_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_devel_both");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_devel_cb), application);

	/* gui filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_gui_yes");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_gui_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_gui_no");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_gui_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_gui_both");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_gui_cb), application);

	/* free filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_free_yes");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_free_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_free_no");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_free_cb), application);
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_free_both");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_free_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "vbox_description_pane");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (application->priv->glade_xml, "hbox_filesize");
	gtk_widget_hide (widget);

	/* basename filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_basename");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_basename_cb), application);

	/* newest filter */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_newest");
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gpk_application_menu_filter_newest_cb), application);

	/* Remove description/file list if needed. */
	widget = glade_xml_get_widget (application->priv->glade_xml, "notebook_description");
	g_signal_connect (widget, "switch-page",
			  G_CALLBACK (gpk_application_notebook_changed_cb), application);
	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_GET_DESCRIPTION) == FALSE) {
		vbox = glade_xml_get_widget (application->priv->glade_xml, "vbox_description");
		page = gtk_notebook_page_num (GTK_NOTEBOOK (widget), vbox);
		gtk_notebook_remove_page (GTK_NOTEBOOK (widget), page);
	}
	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_GET_FILES) == FALSE) {
		vbox = glade_xml_get_widget (application->priv->glade_xml, "vbox_files");
		page = gtk_notebook_page_num (GTK_NOTEBOOK (widget), vbox);
		gtk_notebook_remove_page (GTK_NOTEBOOK (widget), page);
	}
	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_GET_DEPENDS) == FALSE) {
		vbox = glade_xml_get_widget (application->priv->glade_xml, "vbox_depends");
		page = gtk_notebook_page_num (GTK_NOTEBOOK (widget), vbox);
		gtk_notebook_remove_page (GTK_NOTEBOOK (widget), page);
	}
	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_GET_REQUIRES) == FALSE) {
		vbox = glade_xml_get_widget (application->priv->glade_xml, "vbox_requires");
		page = gtk_notebook_page_num (GTK_NOTEBOOK (widget), vbox);
		gtk_notebook_remove_page (GTK_NOTEBOOK (widget), page);
	}

	/* hide the group selector if we don't support search-groups */
	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_GROUP) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "scrolledwindow_groups");
		gtk_widget_hide (widget);
	}

	/* hide the refresh cache button if we can't do it */
	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_REFRESH_CACHE) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "imagemenuitem_refresh");
		gtk_widget_hide (widget);
	}

	/* hide the software-sources button if we can't do it */
	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_GET_REPO_LIST) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "imagemenuitem_sources");
		gtk_widget_hide (widget);
	}

	/* simple find button */
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_find_cb), application);
	gtk_widget_set_tooltip_text (widget, _("Find packages"));

	/* search cancel button */
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_cancel");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_cancel_cb), application);
	gtk_widget_set_tooltip_text (widget, _("Cancel search"));

	/* cancel button */
	widget = glade_xml_get_widget (application->priv->glade_xml, "button_cancel2");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_application_cancel_cb), application);
	gtk_widget_set_tooltip_text (widget, _("Cancel action"));
	gtk_widget_hide (widget);

	/* the fancy text entry widget */
	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");

	/* autocompletion can be turned off as it's slow */
	autocomplete = gconf_client_get_bool (application->priv->gconf_client, GPK_CONF_AUTOCOMPLETE, NULL);
	if (autocomplete) {
		/* create the completion object */
		completion = gtk_entry_completion_new ();

		/* assign the completion to the entry */
		gtk_entry_set_completion (GTK_ENTRY (widget), completion);
		g_object_unref (completion);

		/* create a tree model and use it as the completion model */
		completion_model = gpk_application_create_completion_model ();
		gtk_entry_completion_set_model (completion, completion_model);
		g_object_unref (completion_model);

		/* use model column 0 as the text column */
		gtk_entry_completion_set_text_column (completion, 0);
		gtk_entry_completion_set_inline_completion (completion, TRUE);
	}

	/* set focus on entry text */
	gtk_widget_grab_focus (widget);
	gtk_widget_show (widget);
	sexy_icon_entry_set_icon_highlight (SEXY_ICON_ENTRY (widget), SEXY_ICON_ENTRY_PRIMARY, TRUE);
	g_signal_connect (widget, "activate",
			  G_CALLBACK (gpk_application_find_cb), application);
	g_signal_connect (widget, "icon-pressed",
			  G_CALLBACK (gpk_application_entry_text_icon_pressed_cb), application);

	/* coldplug icon to default to search by name*/
	gpk_application_menu_search_by_name (NULL, application);

	/* hide the filters we can't support */
	if (pk_enums_contain (application->priv->filters, PK_FILTER_ENUM_INSTALLED) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_installed");
		gtk_widget_hide (widget);
	}
	if (pk_enums_contain (application->priv->filters, PK_FILTER_ENUM_DEVELOPMENT) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_devel");
		gtk_widget_hide (widget);
	}
	if (pk_enums_contain (application->priv->filters, PK_FILTER_ENUM_GUI) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_gui");
		gtk_widget_hide (widget);
	}
	if (pk_enums_contain (application->priv->filters, PK_FILTER_ENUM_FREE) == FALSE) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_free");
		gtk_widget_hide (widget);
	}

	/* BASENAME, use by default, or hide */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_basename");
	if (pk_enums_contain (application->priv->filters, PK_FILTER_ENUM_BASENAME)) {
		enabled = gconf_client_get_bool (application->priv->gconf_client,
						 GPK_CONF_APPLICATION_FILTER_BASENAME, NULL);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), enabled);
		/* work round a gtk2+ bug: toggled should be fired when doing gtk_check_menu_item_set_active */
		gpk_application_menu_filter_basename_cb (widget, application);
	} else {
		gtk_widget_hide (widget);
	}

	/* NEWEST, use by default, or hide */
	widget = glade_xml_get_widget (application->priv->glade_xml, "menuitem_newest");
	if (pk_enums_contain (application->priv->filters, PK_FILTER_ENUM_NEWEST)) {
		/* set from remembered state */
		enabled = gconf_client_get_bool (application->priv->gconf_client,
						 GPK_CONF_APPLICATION_FILTER_NEWEST, NULL);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), enabled);
		/* work round a gtk2+ bug: toggled should be fired when doing gtk_check_menu_item_set_active */
		gpk_application_menu_filter_newest_cb (widget, application);
	} else {
		gtk_widget_hide (widget);
	}

	widget = glade_xml_get_widget (application->priv->glade_xml, "entry_text");
	g_signal_connect (widget, "key-press-event",
			  G_CALLBACK (gpk_application_text_changed_cb), application);
	g_signal_connect (widget, "key-release-event",
			  G_CALLBACK (gpk_application_text_changed_cb), application);

	widget = glade_xml_get_widget (application->priv->glade_xml, "button_find");
	gtk_widget_set_sensitive (widget, FALSE);

	gtk_widget_set_size_request (main_window, 800, 500);
	gtk_widget_show (main_window);

	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));
	g_signal_connect (GTK_TREE_VIEW (widget), "row-activated",
			  G_CALLBACK (gpk_application_package_row_activated_cb), application);

	/* use the in-statusbar for progress */
	application->priv->statusbar = gpk_statusbar_new ();
	widget = glade_xml_get_widget (application->priv->glade_xml, "statusbar_status");
	gpk_statusbar_set_widget (application->priv->statusbar, widget);

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

	/* sorted */
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (application->priv->packages_store),
					      PACKAGES_COLUMN_TEXT, GTK_SORT_ASCENDING);

	/* create package tree view */
	widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_packages");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (application->priv->packages_store));

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gpk_application_packages_treeview_clicked_cb), application);

	/* add columns to the tree view */
	gpk_application_packages_add_columns (GTK_TREE_VIEW (widget));

	/* add an "all" entry if we can GetPackages */
	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_GET_PACKAGES)) {
		GtkTreeIter iter;
		const gchar *icon_name;
		gtk_list_store_append (application->priv->groups_store, &iter);
		icon_name = gpk_role_enum_to_icon_name (PK_ROLE_ENUM_GET_PACKAGES);
		gtk_list_store_set (application->priv->groups_store, &iter,
				    GROUPS_COLUMN_NAME, _("All packages"),
				    GROUPS_COLUMN_ID, "all-packages",
				    GROUPS_COLUMN_ICON, icon_name, -1);
	}

	/* create group tree view if we can search by group */
	if (pk_enums_contain (application->priv->roles, PK_ROLE_ENUM_SEARCH_GROUP)) {
		widget = glade_xml_get_widget (application->priv->glade_xml, "treeview_groups");
		gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
					 GTK_TREE_MODEL (application->priv->groups_store));

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
		g_signal_connect (selection, "changed",
				  G_CALLBACK (gpk_application_groups_treeview_clicked_cb), application);

		/* add columns to the tree view */
		gpk_application_groups_add_columns (GTK_TREE_VIEW (widget));

		/* add all the groups supported */
		for (i=1; i<PK_GROUP_ENUM_UNKNOWN; i*=2) {
			if (pk_enums_contain (application->priv->groups, i)) {
				gpk_application_group_add_data (application, i);
			}
		}
	}
}

/**
 * gpk_application_finalize:
 * @object: This graph class instance
 **/
static void
gpk_application_finalize (GObject *object)
{
	GpkApplication *application;
	g_return_if_fail (PK_IS_APPLICATION (object));

	application = GPK_APPLICATION (object);
	application->priv = GPK_APPLICATION_GET_PRIVATE (application);

	g_object_unref (application->priv->glade_xml);
	g_object_unref (application->priv->packages_store);
	g_object_unref (application->priv->control);
	g_object_unref (application->priv->client_search);
	g_object_unref (application->priv->client_action);
	g_object_unref (application->priv->client_description);
	g_object_unref (application->priv->client_files);
	g_object_unref (application->priv->pconnection);
	g_object_unref (application->priv->statusbar);
	g_object_unref (application->priv->extra);
	g_object_unref (application->priv->gconf_client);
	g_object_unref (application->priv->install_action);
	g_object_unref (application->priv->remove_action);
	g_object_unref (application->priv->refresh_action);

	g_free (application->priv->url);
	g_free (application->priv->group);
	g_free (application->priv->package);

	G_OBJECT_CLASS (gpk_application_parent_class)->finalize (object);
}

/**
 * gpk_application_new:
 * Return value: new GpkApplication instance.
 **/
GpkApplication *
gpk_application_new (void)
{
	GpkApplication *application;
	application = g_object_new (GPK_TYPE_APPLICATION, NULL);
	return GPK_APPLICATION (application);
}

