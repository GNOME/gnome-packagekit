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
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <string.h>
#include <dbus/dbus-glib.h>
#include <locale.h>

#include <polkit-gnome/polkit-gnome.h>
#include <gconf/gconf-client.h>
#include <packagekit-glib/packagekit.h>
#include <libnotify/notify.h>

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-unique.h"

#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-error.h"
#include "gpk-consolekit.h"
#include "gpk-cell-renderer-uri.h"
#include "gpk-animated-icon.h"
#include "gpk-client.h"
#include "gpk-enum.h"

static GladeXML *glade_xml = NULL;
static GtkListStore *list_store_preview = NULL;
static GtkListStore *list_store_details = NULL;
static GtkListStore *list_store_description = NULL;
static PkClient *client_action = NULL;
static PkClient *client_query = NULL;
static PkControl *control = NULL;
static PkTaskList *tlist = NULL;
static gchar *cached_package_id = NULL;
static GpkClient *gclient = NULL;
static gboolean are_updates_available = FALSE;
static guint description_event_id = 0;

static PolKitGnomeAction *refresh_action = NULL;
static PolKitGnomeAction *update_system_action = NULL;
static PolKitGnomeAction *update_packages_action = NULL;
static PolKitGnomeAction *restart_action = NULL;

/* for the preview throbber */
static void gpk_update_viewer_add_preview_item (const gchar *icon, const gchar *message, gboolean clear);
static void gpk_update_viewer_description_animation_stop (void);
static void gpk_update_viewer_get_new_update_list (void);

enum {
	PREVIEW_COLUMN_ICON,
	PREVIEW_COLUMN_TEXT,
	PREVIEW_COLUMN_PROGRESS,
	PREVIEW_COLUMN_LAST
};

enum {
	DESC_COLUMN_TITLE,
	DESC_COLUMN_TEXT,
	DESC_COLUMN_URI,
	DESC_COLUMN_LAST
};

enum {
	HISTORY_COLUMN_ICON,
	HISTORY_COLUMN_TEXT,
	HISTORY_COLUMN_LAST
};

enum {
	PACKAGES_COLUMN_ICON,
	PACKAGES_COLUMN_TEXT,
	PACKAGES_COLUMN_ID,
	PACKAGES_COLUMN_INFO,
	PACKAGES_COLUMN_SELECT,
	PACKAGES_COLUMN_SENSITIVE,
	PACKAGES_COLUMN_CLICKABLE,
	PACKAGES_COLUMN_LAST
};

typedef enum {
	PAGE_PREVIEW,
	PAGE_DETAILS,
	PAGE_CONFIRM,
	PAGE_LAST
} PkPageEnum;

/**
 * pk_button_help_cb:
 **/
static void
pk_button_help_cb (GtkWidget *widget, gpointer data)
{
	const char *id = data;
	gpk_gnome_help (id);
}

/**
 * gpk_update_viewer_set_page:
 **/
static void
gpk_update_viewer_set_page (PkPageEnum page)
{
	GtkWidget *widget;
	GList *list, *l;
	guint i;

	widget = glade_xml_get_widget (glade_xml, "window_updates");
	if (page == PAGE_LAST) {
		gtk_widget_hide (widget);
		gpk_client_set_parent (gclient, NULL);
		return;
	}

	/* restore modalness */
	gpk_client_set_parent (gclient, GTK_WINDOW (widget));

	/* some pages are resizeable */
	if (page == PAGE_DETAILS)
		gtk_window_set_resizable (GTK_WINDOW (widget), TRUE);
	else
		gtk_window_set_resizable (GTK_WINDOW (widget), FALSE);
	if (page == PAGE_CONFIRM)
		gtk_window_unmaximize (GTK_WINDOW (widget));
	gtk_widget_show (widget);

	/* clear */
	if (page == PAGE_DETAILS) {
		widget = glade_xml_get_widget (glade_xml, "scrolledwindow_description");
		gtk_widget_hide (widget);
	}

	widget = glade_xml_get_widget (glade_xml, "hbox_hidden");
	list = gtk_container_get_children (GTK_CONTAINER (widget));
	for (l=list, i=0; l; l=l->next, i++) {
		if (i == page)
			gtk_widget_show (l->data);
		else
			gtk_widget_hide (l->data);
	}
}

/**
 * gpk_update_viewer_update_system_cb:
 **/
static void
gpk_update_viewer_update_system_cb (PolKitGnomeAction *action, gpointer data)
{
	GtkWidget *widget;
	gboolean ret;

	egg_debug ("Doing the system update");

	widget = glade_xml_get_widget (glade_xml, "button_overview2");
	gtk_widget_hide (widget);

	gpk_update_viewer_set_page (PAGE_LAST);
	gpk_client_set_interaction (gclient, GPK_CLIENT_INTERACT_WARNING_PROGRESS);
	ret = gpk_client_update_system (gclient, NULL);

	/* did we succeed updating the system */
	if (!ret)
		gpk_update_viewer_set_page (PAGE_PREVIEW);
	else
		gpk_update_viewer_set_page (PAGE_CONFIRM);
}

/**
 * gpk_update_viewer_apply_cb:
 **/
static void
gpk_update_viewer_apply_cb (PolKitGnomeAction *action, gpointer data)
{
	GtkWidget *widget;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean ret;
	gboolean valid;
	gboolean update;
	gboolean selected_all = TRUE;
	gboolean selected_any = FALSE;
	gchar *package_id;
	GPtrArray *array;
	gchar **package_ids;

	egg_debug ("Doing the package updates");
	array = g_ptr_array_new ();

	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));

	/* get the first iter in the list */
	valid = gtk_tree_model_get_iter_first (model, &iter);

	/* find out how many we should update */
	while (valid) {
		gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_SELECT, &update,
				    PACKAGES_COLUMN_ID, &package_id, -1);

		if (!update)
			selected_all = FALSE;
		else
			selected_any = TRUE;

		/* do something with the data */
		if (update) {
			egg_debug ("%s", package_id);
			g_ptr_array_add (array, package_id);
		} else {
			/* need to free the one in the array later */
			g_free (package_id);
		}
		valid = gtk_tree_model_iter_next (model, &iter);
	}

	/* we have no checkboxes selected */
	if (!selected_any) {
		widget = glade_xml_get_widget (glade_xml, "window_updates");
		gpk_error_dialog_modal (GTK_WINDOW (widget),
					/* TRANSLATORS: we clicked apply, but had no packages selected */
					_("No updates selected"),
					_("No updates are selected"), NULL);
		return;
	}

	widget = glade_xml_get_widget (glade_xml, "button_overview2");
	if (selected_all)
		gtk_widget_hide (widget);
	else
		gtk_widget_show (widget);

	/* set correct view */
	gpk_update_viewer_set_page (PAGE_LAST);
	package_ids = pk_package_ids_from_array (array);
	gpk_client_set_interaction (gclient, GPK_CLIENT_INTERACT_WARNING_PROGRESS);
	ret = gpk_client_update_packages (gclient, package_ids, NULL);
	g_strfreev (package_ids);

	/* did we succeed updating the system */
	if (!ret)
		gpk_update_viewer_set_page (PAGE_PREVIEW);
	else
		gpk_update_viewer_set_page (PAGE_CONFIRM);

	/* get rid of the array, and free the contents */
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);
}

/**
 * gpk_update_viewer_preview_animation_start:
 **/
static void
gpk_update_viewer_preview_animation_start (const gchar *text)
{
	GtkWidget *widget;
	gchar *text_bold;

	widget = glade_xml_get_widget (glade_xml, "image_animation_preview");
	gpk_animated_icon_set_frame_delay (GPK_ANIMATED_ICON (widget), 50);
	gpk_animated_icon_set_filename_tile (GPK_ANIMATED_ICON (widget), GTK_ICON_SIZE_DIALOG, "process-working");
	gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (widget), TRUE);
	gtk_widget_show (widget);

	text_bold = g_strdup_printf ("<b>%s</b>", text);
	widget = glade_xml_get_widget (glade_xml, "label_animation_preview");
	gtk_label_set_label (GTK_LABEL (widget), text_bold);
	g_free (text_bold);

	widget = glade_xml_get_widget (glade_xml, "viewport_animation_preview");
	gtk_widget_show (widget);

	widget = glade_xml_get_widget (glade_xml, "scrolledwindow_preview");
	gtk_widget_hide (widget);
}

/**
 * gpk_update_viewer_preview_animation_stop:
 **/
static void
gpk_update_viewer_preview_animation_stop (void)
{
	GtkWidget *widget;

	widget = glade_xml_get_widget (glade_xml, "image_animation_preview");
	gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (widget), FALSE);

	widget = glade_xml_get_widget (glade_xml, "label_animation_preview");
	gtk_label_set_label (GTK_LABEL (widget), "");

	widget = glade_xml_get_widget (glade_xml, "viewport_animation_preview");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (glade_xml, "scrolledwindow_preview");
	gtk_widget_show (widget);
}

/**
 * gpk_update_viewer_description_animation_start_really:
 **/
static gboolean
gpk_update_viewer_description_animation_start_really (void)
{
	GtkWidget *widget;
	gchar *text_bold;

	widget = glade_xml_get_widget (glade_xml, "image_animation_description");
	gpk_animated_icon_set_frame_delay (GPK_ANIMATED_ICON (widget), 50);
	gpk_animated_icon_set_filename_tile (GPK_ANIMATED_ICON (widget), GTK_ICON_SIZE_DIALOG, "process-working");
	gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (widget), TRUE);
	gtk_widget_show (widget);

	/* TRANSLATORS: getting information about the update -- can take some time */
	text_bold = g_strdup_printf ("<b>%s</b>", _("Getting Description..."));
	widget = glade_xml_get_widget (glade_xml, "label_animation_description");
	gtk_label_set_label (GTK_LABEL (widget), text_bold);
	g_free (text_bold);

	widget = glade_xml_get_widget (glade_xml, "viewport_animation_description");
	gtk_widget_show (widget);

	widget = glade_xml_get_widget (glade_xml, "scrolledwindow_description");
	gtk_widget_hide (widget);

	/* we've done the event */
	description_event_id = 0;

	/* never repeat */
	return FALSE;
}

/**
 * gpk_update_viewer_description_animation_start:
 **/
static void
gpk_update_viewer_description_animation_start (void)
{
	/* only clear the last data and show the spinner if it takes a little
	 * while, else we flicker the display too much */
	if (description_event_id > 0)
		g_source_remove (description_event_id);
	description_event_id = g_timeout_add (100, (GSourceFunc) gpk_update_viewer_description_animation_start_really, NULL);
}

/**
 * gpk_update_viewer_description_animation_stop:
 **/
static void
gpk_update_viewer_description_animation_stop (void)
{
	GtkWidget *widget;

	/* if we are not showing, clear timeout and return */
	if (description_event_id > 0) {
		g_source_remove (description_event_id);
		description_event_id = 0;
		return;
	}

	widget = glade_xml_get_widget (glade_xml, "image_animation_description");
	gpk_animated_icon_enable_animation (GPK_ANIMATED_ICON (widget), FALSE);

	widget = glade_xml_get_widget (glade_xml, "label_animation_description");
	gtk_label_set_label (GTK_LABEL (widget), "");

	widget = glade_xml_get_widget (glade_xml, "viewport_animation_description");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (glade_xml, "scrolledwindow_description");
	gtk_widget_show (widget);
}

/**
 * gpk_update_viewer_refresh_cb:
 **/
static void
gpk_update_viewer_refresh_cb (PolKitGnomeAction *action, gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	/* refresh the cache */
	gpk_client_set_interaction (gclient, GPK_CLIENT_INTERACT_WARNING_PROGRESS);
	polkit_gnome_action_set_sensitive (refresh_action, FALSE);
	ret = gpk_client_refresh_cache (gclient, &error);
	polkit_gnome_action_set_sensitive (refresh_action, TRUE);
	if (ret == FALSE) {
		egg_warning ("failed: %s", error->message);
		g_error_free (error);
	}
	/* get new list */
	gpk_update_viewer_get_new_update_list ();
}

/**
 * gpk_update_viewer_button_close_and_cancel_cb:
 **/
static void
gpk_update_viewer_button_close_and_cancel_cb (GtkWidget *widget, gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	/* we might have a transaction running */
	ret = pk_client_cancel (client_action, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
	}

	gtk_main_quit ();
}

/**
 * gpk_update_viewer_review_cb:
 **/
static void
gpk_update_viewer_review_cb (GtkWidget *widget, gpointer data)
{
	GtkWidget *treeview;
	GtkTreeSelection *selection;
	GtkTreeIter iter;

	treeview = glade_xml_get_widget (glade_xml, "treeview_updates");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	if (!gtk_tree_selection_get_selected (selection, NULL, NULL)) {
		if (gtk_tree_model_get_iter_first (gtk_tree_view_get_model (GTK_TREE_VIEW (treeview)),
						   &iter))
			gtk_tree_selection_select_iter (selection, &iter);
	}

	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	gtk_widget_set_size_request (GTK_WIDGET (widget), 500, 200);

	widget = glade_xml_get_widget (glade_xml, "treeview_description");
	gtk_widget_set_size_request (GTK_WIDGET (widget), 500, 200);

	/* set correct view */
	gpk_update_viewer_set_page (PAGE_DETAILS);
}


/**
 * gpk_update_viewer_populate_preview:
 **/
static void
gpk_update_viewer_populate_preview (PkPackageList *list)
{
	GtkWidget *widget;
	guint length;
	const PkPackageObj *obj;
	guint i;
	guint num_low = 0;
	guint num_normal = 0;
	guint num_important = 0;
	guint num_security = 0;
	guint num_bugfix = 0;
	guint num_enhancement = 0;
	guint num_blocked = 0;
	const gchar *icon;
	gchar *text;

	length = pk_package_list_get_size (list);
	if (length == 0) {
		/* TRANSLATORS: no updates available for the user */
		gpk_update_viewer_add_preview_item ("dialog-information", _("There are no updates available"), TRUE);
		widget = glade_xml_get_widget (glade_xml, "button_close3");
		gtk_widget_grab_default (widget);
	} else {

		for (i=0;i<length;i++) {
			obj = pk_package_list_get_obj (list, i);
			if (obj->info == PK_INFO_ENUM_LOW)
				num_low++;
			else if (obj->info == PK_INFO_ENUM_IMPORTANT)
				num_important++;
			else if (obj->info == PK_INFO_ENUM_SECURITY)
				num_security++;
			else if (obj->info == PK_INFO_ENUM_BUGFIX)
				num_bugfix++;
			else if (obj->info == PK_INFO_ENUM_ENHANCEMENT)
				num_enhancement++;
			else if (obj->info == PK_INFO_ENUM_BLOCKED)
				num_blocked++;
			else
				num_normal++;
		}

		/* clear existing list */
		gtk_list_store_clear (list_store_preview);

		/* add to preview box in order of priority */
		if (num_security > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_SECURITY);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_SECURITY, num_security);
			gpk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}
		if (num_important > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_IMPORTANT);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_IMPORTANT, num_important);
			gpk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}
		if (num_bugfix > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_BUGFIX);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_BUGFIX, num_bugfix);
			gpk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}
		if (num_enhancement > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_ENHANCEMENT);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_ENHANCEMENT, num_enhancement);
			gpk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}
		if (num_blocked > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_BLOCKED);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_BLOCKED, num_blocked);
			gpk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}
		if (num_low > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_LOW);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_LOW, num_low);
			gpk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}
		if (num_normal > 0) {
			icon = gpk_info_enum_to_icon_name (PK_INFO_ENUM_NORMAL);
			text = gpk_update_enum_to_localised_text (PK_INFO_ENUM_NORMAL, num_normal);
			gpk_update_viewer_add_preview_item (icon, text, FALSE);
			g_free (text);
		}

		/* set visible and sensitive */
		widget = glade_xml_get_widget (glade_xml, "button_review");
		gtk_widget_set_sensitive (widget, TRUE);
		polkit_gnome_action_set_sensitive (update_system_action, TRUE);
		polkit_gnome_action_set_sensitive (update_packages_action, TRUE);
	}
}

/**
 * gpk_update_viewer_do_precache:
 **/
static gboolean
gpk_update_viewer_do_precache (const PkPackageList *list)
{
	gboolean ret;
	GError *error = NULL;
	gchar **package_ids;
	GConfClient *client;
	gboolean precache;

	client = gconf_client_get_default ();
	precache = gconf_client_get_bool (client, GPK_CONF_UPDATE_VIEWER_PRECACHE_DETAILS, NULL);
	g_object_unref (client);

	if (!precache)
		return FALSE;

	egg_debug ("doing precache");

	/* reset */
	ret = pk_client_reset (client_query, &error);
	if (!ret) {
		egg_warning ("failed to reset: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* pre-cache the update detail if we can */
	package_ids = pk_package_list_to_strv (list);
	ret = pk_client_get_update_detail (client_query, package_ids, &error);
	g_strfreev (package_ids);
	if (!ret) {
		egg_warning ("failed to cache update detail: %s", error->message);
		g_error_free (error);
		return FALSE;
	}
	return TRUE;
}

/**
 * gpk_update_viewer_get_new_update_list:
 **/
static void
gpk_update_viewer_get_new_update_list (void)
{
	GError *error = NULL;
	PkPackageList *list;
	const PkPackageObj *obj;
	GtkWidget *widget;
	guint length;
	guint i;
	gchar *text;
	gchar *package_id;
	const gchar *icon_name;
	GtkTreeIter iter;
	gboolean selected;

	/* spin */
	gpk_update_viewer_description_animation_start ();

	/* clear existing list */
	gtk_list_store_clear (list_store_details);

	gpk_client_set_interaction (gclient, GPK_CLIENT_INTERACT_NEVER);
	list = gpk_client_get_updates (gclient, &error);
	if (list == NULL) {
		gpk_update_viewer_description_animation_stop ();
		egg_warning ("failed: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* sort by priority, then by package_id (reversed) */
	pk_package_list_sort (list);
	pk_package_list_sort_info (list);

	/* do we have updates? */
	length = pk_package_list_get_size (list);
	if (length == 0)
		are_updates_available = FALSE;
	else
		are_updates_available = TRUE;

	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		text = gpk_package_id_format_twoline (obj->id, obj->summary);
		icon_name = gpk_info_enum_to_icon_name (obj->info);
		gtk_list_store_append (list_store_details, &iter);
		package_id = pk_package_id_to_string (obj->id);
		selected = (obj->info != PK_INFO_ENUM_BLOCKED);
		gtk_list_store_set (list_store_details, &iter,
				    PACKAGES_COLUMN_TEXT, text,
				    PACKAGES_COLUMN_ID, package_id,
				    PACKAGES_COLUMN_ICON, icon_name,
				    PACKAGES_COLUMN_INFO, obj->info,
				    PACKAGES_COLUMN_SELECT, selected,
				    PACKAGES_COLUMN_SENSITIVE, selected,
				    PACKAGES_COLUMN_CLICKABLE, selected,
				    -1);
		g_free (package_id);
		g_free (text);
	}


	/* make the buttons non-clickable until we get completion */
	polkit_gnome_action_set_sensitive (update_system_action, are_updates_available);
	polkit_gnome_action_set_sensitive (update_packages_action, are_updates_available);
	widget = glade_xml_get_widget (glade_xml, "button_review");
	gtk_widget_set_sensitive (widget, are_updates_available);

	gpk_update_viewer_populate_preview (list);
	gpk_update_viewer_do_precache (list);

	/* don't spin */
	gpk_update_viewer_description_animation_stop ();

out:
	if (list != NULL)
		g_object_unref (list);
}

/**
 * gpk_update_viewer_overview_cb:
 **/
static void
gpk_update_viewer_overview_cb (GtkWidget *widget, gpointer data)
{
	/* set correct view */
	gpk_update_viewer_set_page (PAGE_PREVIEW);

	/* get the new update list */
	gpk_update_viewer_get_new_update_list ();
}

/**
 * gpk_update_viewer_package_cb:
 **/
static void
gpk_update_viewer_package_cb (PkClient *client, const PkPackageObj *obj, gpointer data)
{
	PkRoleEnum role;

	pk_client_get_role (client, &role, NULL, NULL);
	egg_debug ("role = %s, package = %s:%s:%s", pk_role_enum_to_text (role),
		  pk_info_enum_to_text (obj->info), obj->id->name, obj->summary);
}

/**
 * gpk_update_viewer_add_description_item:
 **/
static void
gpk_update_viewer_add_description_item (const gchar *title, const gchar *text, const gchar *uri)
{
	gchar *markup;
	GtkWidget *tree_view;
	GtkTreeIter iter;
	GtkTreeSelection *selection;

	/* format */
	markup = g_strdup_printf ("<b>%s:</b>", title);

	egg_debug ("%s %s %s", markup, text, uri);
	gtk_list_store_append (list_store_description, &iter);
	gtk_list_store_set (list_store_description, &iter,
			    DESC_COLUMN_TITLE, markup,
			    DESC_COLUMN_TEXT, text,
			    DESC_COLUMN_URI, uri,
			    -1);

	g_free (markup);

	tree_view = glade_xml_get_widget (glade_xml, "treeview_description");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_NONE);
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (tree_view));
}

/**
 * gpk_update_viewer_add_description_link_item:
 **/
static void
gpk_update_viewer_add_description_link_item (const gchar *title, const gchar *url_string)
{
	const gchar *text;
	const gchar *uri;
	gchar *title_num;
	gchar **urls;
	guint length;
	gint i;

	urls = g_strsplit (url_string, ";", 0);
	length = g_strv_length (urls);

	/* could we have malformed descriptions with ';' in them? */
	if (length % 2 != 0) {
		egg_warning ("length not correct, correcting");
		length--;
	}

	for (i=0; i<length; i+=2) {
		uri = urls[i];
		text = urls[i+1];
		if (egg_strzero (text)) {
			text = uri;
		}
		/* no suffix needed */
		if (length == 2) {
			gpk_update_viewer_add_description_item (title, text, uri);
		} else {
			title_num = g_strdup_printf ("%s (%i)", title, (i/2) + 1);
			gpk_update_viewer_add_description_item (title_num, text, uri);
			g_free (title_num);
		}
	}
	g_strfreev (urls);
}

/**
 * gpk_update_viewer_get_pretty_from_composite:
 **/
static gchar *
gpk_update_viewer_get_pretty_from_composite (const gchar *package_ids_delimit)
{
	guint i;
	guint length;
	gchar **package_ids;
	gchar *pretty = NULL;
	GString *string;
	PkPackageId *id;

	/* do we have any data? */
	if (egg_strzero (package_ids_delimit))
		goto out;

	string = g_string_new ("");
	package_ids = pk_package_ids_from_text (package_ids_delimit);
	length = g_strv_length (package_ids);
	for (i=0; i<length; i++) {
		id = pk_package_id_new_from_string (package_ids[i]);
		pretty = gpk_package_id_name_version (id);
		pk_package_id_free (id);
		g_string_append (string, pretty);
		g_string_append_c (string, '\n');
		g_free (pretty);
	}

	/* remove trailing \n */
	g_string_set_size (string, string->len - 1);
	pretty = g_string_free (string, FALSE);
	g_strfreev (package_ids);
out:
	return pretty;
}

/**
 * gpk_update_viewer_pretty_description:
 **/
static gchar *
gpk_update_viewer_pretty_description (const gchar *description)
{
	gchar **lines;
	GString *string;
	gchar *line;
	gchar *line2;
	guint len;
	guint i;

	/* process each line */
	lines = g_strsplit (description, "\n", 0);
	string = g_string_new ("");
	len = g_strv_length (lines);
	for (i=0; i<len; i++) {
		/* do not free this */
		line = g_strstrip (lines[i]);

		/* common prefixes */
		if (g_str_has_prefix (line, "- ") ||
		    g_str_has_prefix (line, "* "))
			line2 = g_strdup_printf ("• %s", line+2);
		else
			line2 = g_strdup (line);

		/* if not null then append back */
		if (!egg_strzero (line2))
			g_string_append_printf (string, "%s\n", line2);
		g_free (line2);
	}
	/* remove trailing \n */
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);
	line = g_string_free (string, FALSE);
	g_strfreev (lines);

	return line;
}

/**
 * gpk_update_viewer_update_detail_cb:
 **/
static void
gpk_update_viewer_update_detail_cb (PkClient *client, const PkUpdateDetailObj *obj, gpointer data)
{
	GtkWidget *widget;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter treeiter;
	gchar *package_pretty;
	const gchar *info_text;
	PkInfoEnum info;
	gchar *line;

	/* clear existing list */
	gpk_update_viewer_description_animation_stop ();
	gtk_list_store_clear (list_store_description);

	/* initially we are hidden */
	widget = glade_xml_get_widget (glade_xml, "scrolledwindow_description");
	gtk_widget_show (widget);

	/* get info  */
	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	if (gtk_tree_selection_get_selected (selection, &model, &treeiter))
		gtk_tree_model_get (model, &treeiter,
				    PACKAGES_COLUMN_INFO, &info, -1);
	else
		info = PK_INFO_ENUM_NORMAL;

	info_text = gpk_info_enum_to_localised_text (info);
	/* TRANSLATORS: this is the update type, e.g. security */
	gpk_update_viewer_add_description_item (_("Type"), info_text, NULL);

	/* state */
	if (obj->state != PK_UPDATE_STATE_ENUM_UNKNOWN) {
		info_text = gpk_update_state_enum_to_localised_text (obj->state);
		/* TRANSLATORS: this is the stability status of the update */
		gpk_update_viewer_add_description_item (_("State"), info_text, NULL);
	}

	/* issued */
	if (obj->issued != NULL) {
		line = pk_iso8601_from_date (obj->issued);
		/* TRANSLATORS: this is when the update was issued */
		gpk_update_viewer_add_description_item (_("Issued"), line, NULL);
		g_free (line);
	}

	/* updated */
	if (obj->updated != NULL) {
		line = pk_iso8601_from_date (obj->updated);
		/* TRANSLATORS: this is when (if?) the update was updated */
		gpk_update_viewer_add_description_item (_("Updated"), line, NULL);
		g_free (line);
	}

	package_pretty = gpk_package_id_name_version (obj->id);
	/* TRANSLATORS: this is the package version */
	gpk_update_viewer_add_description_item (_("New version"), package_pretty, NULL);
	g_free (package_pretty);

	/* split and add */
	package_pretty = gpk_update_viewer_get_pretty_from_composite (obj->updates);
	if (package_pretty != NULL) {
		/* TRANSLATORS: this is a list of packages that are updated */
		gpk_update_viewer_add_description_item (_("Updates"), package_pretty, NULL);
	}
	g_free (package_pretty);

	/* split and add */
	package_pretty = gpk_update_viewer_get_pretty_from_composite (obj->obsoletes);
	if (package_pretty != NULL) {
		/* TRANSLATORS: this is a list of packages that are obsoleted */
		gpk_update_viewer_add_description_item (_("Obsoletes"), package_pretty, NULL);
	}
	g_free (package_pretty);

	/* TRANSLATORS: this is the repository the package has come from */
	gpk_update_viewer_add_description_item (_("Repository"), obj->id->data, NULL);

	if (!egg_strzero (obj->update_text)) {
		/* convert the bullets */
		line = gpk_update_viewer_pretty_description (obj->update_text);
		/* TRANSLATORS: this is the package description */
		gpk_update_viewer_add_description_item (_("Description"), line, NULL);
		g_free (line);
	}

	/* changelog */
	if (!egg_strzero (obj->changelog)) {
		/* TRANSLATORS: this is a list of CVE (security) URLs */
		gpk_update_viewer_add_description_item (_("Changes"), obj->changelog, NULL);
	}

	/* add all the links */
	if (!egg_strzero (obj->vendor_url)) {
		/* TRANSLATORS: this is a list of vendor URLs */
		gpk_update_viewer_add_description_link_item (_("Vendor"), obj->vendor_url);
	}
	if (!egg_strzero (obj->bugzilla_url)) {
		/* TRANSLATORS: this is a list of bugzilla URLs */
		gpk_update_viewer_add_description_link_item (_("Bugzilla"), obj->bugzilla_url);
	}
	if (!egg_strzero (obj->cve_url)) {
		/* TRANSLATORS: this is a list of CVE (security) URLs */
		gpk_update_viewer_add_description_link_item (_("CVE"), obj->cve_url);
	}

	/* reboot */
	if (obj->restart == PK_RESTART_ENUM_SESSION ||
	    obj->restart == PK_RESTART_ENUM_SYSTEM) {
		info_text = gpk_restart_enum_to_localised_text (obj->restart);
		/* TRANSLATORS: this is a notice a restart might be required */
		gpk_update_viewer_add_description_item (_("Notice"), info_text, NULL);
	}
}

/**
 * gpk_update_viewer_status_changed_cb:
 **/
static void
gpk_update_viewer_status_changed_cb (PkClient *client, PkStatusEnum status, gpointer data)
{
	GtkWidget *widget;

	/* when we are testing the transaction, no package should be displayed */
	if (status == PK_STATUS_ENUM_TEST_COMMIT) {
		widget = glade_xml_get_widget (glade_xml, "progress_package_label");
		gtk_label_set_label (GTK_LABEL (widget), "");
	}
}

/**
 * gpk_update_viewer_treeview_update_toggled:
 **/
static void
gpk_update_viewer_treeview_update_toggled (GtkCellRendererToggle *cell, gchar *path_str, gpointer data)
{
	GtkTreeModel *model = (GtkTreeModel *) data;
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	gboolean update;
	gchar *package_id;

	/* get toggled iter */
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_SELECT, &update,
			    PACKAGES_COLUMN_ID, &package_id, -1);

	/* unstage */
	update ^= 1;

	egg_debug ("update %s[%i]", package_id, update);
	g_free (package_id);

	/* set new value */
	gtk_list_store_set (GTK_LIST_STORE (model), &iter, PACKAGES_COLUMN_SELECT, update, -1);

	/* clean up */
	gtk_tree_path_free (path);
}

/**
 * gpk_update_viewer_treeview_add_columns:
 **/
static void
gpk_update_viewer_treeview_add_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* image */
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_DIALOG, NULL);
	/* TRANSLATORS: a column that has how serious the update is */
	column = gtk_tree_view_column_new_with_attributes (_("Severity"), renderer,
							   "icon-name", PACKAGES_COLUMN_ICON, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_INFO);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	/* TRANSLATORS: a column that has name of the package that will be updated */
	column = gtk_tree_view_column_new_with_attributes (_("Software"), renderer,
							   "markup", PACKAGES_COLUMN_TEXT, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PACKAGES_COLUMN_TEXT);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * gpk_update_viewer_treeview_renderer_clicked:
 **/
static void
gpk_update_viewer_treeview_renderer_clicked (GtkCellRendererToggle *cell, gchar *uri, gpointer data)
{
	egg_debug ("clicked %s", uri);
	gpk_gnome_open (uri);
}

/**
 * gpk_update_viewer_treeview_add_columns_description:
 **/
static void
gpk_update_viewer_treeview_add_columns_description (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* title */
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "yalign", 0.0, NULL);
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer, "markup", DESC_COLUMN_TITLE);
	gtk_tree_view_append_column (treeview, column);

	/* column for uris */
	renderer = gpk_cell_renderer_uri_new ();
	g_object_set (renderer, "yalign", 0.0, NULL);
	g_signal_connect (renderer, "clicked", G_CALLBACK (gpk_update_viewer_treeview_renderer_clicked), NULL);
	/* TRANSLATORS: The information about the update, not currently shown */
	column = gtk_tree_view_column_new_with_attributes (_("Text"), renderer,
							   "text", DESC_COLUMN_TEXT,
							   "uri", DESC_COLUMN_URI, NULL);
	gtk_tree_view_append_column (treeview, column);
}

/**
 * gpk_update_viewer_treeview_add_columns_update:
 **/
static void
gpk_update_viewer_treeview_add_columns_update (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeModel *model;

	/* column for select toggle */
	renderer = gtk_cell_renderer_toggle_new ();
	model = gtk_tree_view_get_model (treeview);
	g_signal_connect (renderer, "toggled", G_CALLBACK (gpk_update_viewer_treeview_update_toggled), model);
	column = gtk_tree_view_column_new_with_attributes ("Update", renderer,
							   "active", PACKAGES_COLUMN_SELECT,
							   "activatable", PACKAGES_COLUMN_CLICKABLE,
							   "sensitive", PACKAGES_COLUMN_SENSITIVE, NULL);

	/* set this column to a fixed sizing (of 50 pixels) */
	gtk_tree_view_column_set_sizing (GTK_TREE_VIEW_COLUMN (column), GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_fixed_width (GTK_TREE_VIEW_COLUMN (column), 20);
	gtk_tree_view_append_column (treeview, column);

	/* usual suspects */
	gpk_update_viewer_treeview_add_columns (treeview);
}

/**
 * pk_packages_treeview_clicked_cb:
 **/
static void
pk_packages_treeview_clicked_cb (GtkTreeSelection *selection, gpointer data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *package_id;
	GError *error = NULL;
	gboolean ret;
	gchar **package_ids;

	/* This will only work in single or browse selection mode! */
	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    PACKAGES_COLUMN_ID, &package_id, -1);

		if (cached_package_id && package_id &&
		    strcmp (cached_package_id, package_id) == 0) {
			g_free (package_id);
			return;
		}
		g_free (cached_package_id);
		/* make back into package ID */
		cached_package_id = g_strdup (package_id);
		g_free (package_id);

		/* clear and display animation until new details come in */
		gpk_update_viewer_description_animation_start ();

		egg_debug ("selected row is: %s", cached_package_id);

		/* reset */
		ret = pk_client_reset (client_query, &error);
		if (!ret) {
			egg_warning ("failed to reset: %s", error->message);
			g_error_free (error);
		}

		/* get the description */
		error = NULL;
		package_ids = pk_package_ids_from_id (cached_package_id);
		ret = pk_client_get_update_detail (client_query, package_ids, &error);
		g_strfreev (package_ids);
		if (!ret) {
			egg_warning ("failed to get update detail: %s", error->message);
			g_error_free (error);
		}
	} else {
		egg_debug ("no row selected");
	}
}

/**
 * gpk_update_viewer_add_preview_item:
 **/
static void
gpk_update_viewer_add_preview_item (const gchar *icon, const gchar *message, gboolean clear)
{
	GtkWidget *tree_view;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	gchar *markup;

	/* clear existing list */
	if (clear) {
		gtk_list_store_clear (list_store_preview);
	}

	markup = g_strdup_printf ("<b>%s</b>", message);
	gtk_list_store_append (list_store_preview, &iter);
	gtk_list_store_set (list_store_preview, &iter,
			    PACKAGES_COLUMN_TEXT, markup,
			    PACKAGES_COLUMN_ICON, icon,
			    -1);
	g_free (markup);

	tree_view = glade_xml_get_widget (glade_xml, "treeview_preview");
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_NONE);
}

/**
 * pk_update_get_approx_time:
 **/
static const gchar *
pk_update_get_approx_time (guint time)
{
	if (time < 60)
		/* TRANSLATORS: less than 60 seconds, a short time */
		return _("Less than a minute ago");
	else if (time < 60*60)
		return _("Less than an hour ago");
	else if (time < 24*60*60)
		return _("A few hours ago");
	else if (time < 7*24*60*60)
		return _("A few days ago");
	return _("Over a week ago");
}

/**
 * pk_update_viewer_set_last_refreshed_time:
 **/
static gboolean
pk_update_viewer_set_last_refreshed_time (void)
{
	GtkWidget *widget;
	guint time;
	const gchar *time_text;

	/* get times from the daemon */
	pk_control_get_time_since_action (control, PK_ROLE_ENUM_REFRESH_CACHE, &time, NULL);
	if (time < 60*60*24) {
		widget = glade_xml_get_widget (glade_xml, "label_last_refresh_title");
		gtk_widget_hide (widget);
		widget = glade_xml_get_widget (glade_xml, "label_last_refresh");
		gtk_widget_hide (widget);
		return FALSE;
	}

	widget = glade_xml_get_widget (glade_xml, "label_last_refresh_title");
	gtk_widget_show (widget);
	time_text = pk_update_get_approx_time (time);
	widget = glade_xml_get_widget (glade_xml, "label_last_refresh");
	gtk_label_set_label (GTK_LABEL (widget), time_text);
	gtk_widget_show (widget);
	return TRUE;
}

/**
 * pk_update_viewer_set_last_updated_time:
 **/
static gboolean
pk_update_viewer_set_last_updated_time (void)
{
	GtkWidget *widget;
	guint time;
	guint time_new;
	const gchar *time_text;

	/* get times from the daemon */
	pk_control_get_time_since_action (control, PK_ROLE_ENUM_UPDATE_SYSTEM, &time, NULL);
	pk_control_get_time_since_action (control, PK_ROLE_ENUM_UPDATE_PACKAGES, &time_new, NULL);

	/* always use the shortest time */
	if (time_new < time)
		time = time_new;
	time_text = pk_update_get_approx_time (time);
	widget = glade_xml_get_widget (glade_xml, "label_last_update");
	gtk_label_set_label (GTK_LABEL (widget), time_text);
	return TRUE;
}

static void
gpk_update_viewer_restart_cb (GtkWidget *widget, gpointer data)
{
	gpk_restart_system ();
}

static void gpk_update_viewer_populate_preview (PkPackageList *list);

/**
 * gpk_update_viewer_check_blocked_packages:
 **/
static void
gpk_update_viewer_check_blocked_packages (PkPackageList *list)
{
	guint i;
	guint length;
	const PkPackageObj *obj;
	GString *string;
	gboolean exists = FALSE;
	gchar *text;
	gchar *title_bold;
	GtkWidget *widget;

	string = g_string_new ("");

	/* find any that are blocked */
	length = pk_package_list_get_size (list);
	for (i=0;i<length;i++) {
		obj = pk_package_list_get_obj (list, i);
		if (obj->info == PK_INFO_ENUM_BLOCKED) {
			text = gpk_package_id_format_oneline (obj->id, obj->summary);
			g_string_append_printf (string, "%s\n", text);
			g_free (text);
			exists = TRUE;
		}
	}

	/* trim off extra newlines */
	if (string->len != 0)
		g_string_set_size (string, string->len-1);

	/* convert to a normal gchar */
	text = g_string_free (string, FALSE);

	/* set the widget text */
	if (exists) {
		widget = glade_xml_get_widget (glade_xml, "label_update_title");
		/* TRANSLATORS: some updates the user selected could not be installed */
		title_bold = g_strdup_printf ("<b>%s</b>", _("Some updates were not installed"));
		gtk_label_set_markup (GTK_LABEL (widget), title_bold);
		g_free (title_bold);

		widget = glade_xml_get_widget (glade_xml, "label_update_notice");
		gtk_label_set_markup (GTK_LABEL (widget), text);
		gtk_widget_show (widget);
	} else {
		widget = glade_xml_get_widget (glade_xml, "label_update_title");
		/* TRANSLATORS: everything updates okay */
		title_bold = g_strdup_printf ("<b>%s</b>", _("System update completed"));
		gtk_label_set_markup (GTK_LABEL (widget), title_bold);
		g_free (title_bold);

		widget = glade_xml_get_widget (glade_xml, "label_update_notice");
		gtk_widget_hide (widget);
	}

	g_free (text);
}

/**
 * gpk_update_viewer_finished_cb:
 **/
static void
gpk_update_viewer_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, gpointer data)
{
	GtkWidget *widget;
	GtkTreePath *path;
	GtkTreeSelection *selection;
	PkRoleEnum role;
	PkRestartEnum restart;
	PkPackageList *list;

	pk_client_get_role (client, &role, NULL, NULL);

	/* we don't need to do anything here */
	if (role == PK_ROLE_ENUM_GET_UPDATE_DETAIL ||
	    role == PK_ROLE_ENUM_REFRESH_CACHE) {
		gpk_update_viewer_description_animation_stop ();
	}

	/* select the first entry in the updates list */
	if (role == PK_ROLE_ENUM_GET_UPDATES) {
		widget = glade_xml_get_widget (glade_xml, "treeview_updates");
		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
		gtk_tree_selection_unselect_all (selection);
		path = gtk_tree_path_new_first ();
		gtk_tree_selection_select_path (selection, path);
		gtk_tree_path_free (path);
	}

	/* stop the throbber */
	gpk_update_viewer_preview_animation_stop ();

	/* check if we need to display infomation about blocked packages */
	if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		//TODO: this has to be moved to GpkClient
		list = pk_client_get_package_list (client);
		gpk_update_viewer_check_blocked_packages (list);
		g_object_unref (list);
	}

	/* hide the cancel */
	if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		widget = glade_xml_get_widget (glade_xml, "button_cancel");
		gtk_widget_hide (widget);

		/* go onto the success page */
		if (exit == PK_EXIT_ENUM_SUCCESS) {

			/* do we have to show any widgets? */
			restart = pk_client_get_require_restart (client);
			if (restart == PK_RESTART_ENUM_SYSTEM ||
			    restart == PK_RESTART_ENUM_SESSION) {
				egg_debug ("showing reboot widgets");
				widget = glade_xml_get_widget (glade_xml, "hbox_restart");
				gtk_widget_show (widget);
				polkit_gnome_action_set_visible (restart_action, TRUE);
			}

			widget = glade_xml_get_widget (glade_xml, "button_close4");
			gtk_widget_grab_default (widget);

			/* set correct view */
			gpk_update_viewer_set_page (PAGE_CONFIRM);
		}
	}

//	gpk_update_viewer_populate_preview (list);
}

static void
pk_button_more_installs_cb (GtkWidget *button, gpointer data)
{
	/* set correct view */
	gpk_update_viewer_set_page (PAGE_DETAILS);
	gpk_update_viewer_get_new_update_list ();
}

/**
 * gpk_update_viewer_progress_changed_cb:
 **/
static void
gpk_update_viewer_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				guint elapsed, guint remaining, gpointer data)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (glade_xml, "progressbar_percent");

	if (percentage != PK_CLIENT_PERCENTAGE_INVALID)
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget), (gfloat) percentage / 100.0);
}

/**
 * gpk_update_viewer_preview_set_animation:
 **/
static void
gpk_update_viewer_preview_set_animation (const gchar *text)
{
	GtkWidget *widget;

	/* hide apply, review and refresh */
	polkit_gnome_action_set_sensitive (update_system_action, FALSE);
	polkit_gnome_action_set_sensitive (update_packages_action, FALSE);
	widget = glade_xml_get_widget (glade_xml, "button_review");
	gtk_widget_set_sensitive (widget, FALSE);

	/* start the spinning preview */
	gpk_update_viewer_preview_animation_start (text);
}

/**
 * gpk_update_viewer_task_list_changed_cb:
 **/
static void
gpk_update_viewer_task_list_changed_cb (PkTaskList *tlist, gpointer data)
{
	GtkWidget *widget;

	/* hide buttons if we are updating */
	if (pk_task_list_contains_role (tlist, PK_ROLE_ENUM_UPDATE_SYSTEM) ||
	    pk_task_list_contains_role (tlist, PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		/* TRANSLATORS: either this current user, or another user is already updating the system */
		gpk_update_viewer_preview_set_animation (_("A system update is already in progress"));

	} else if (pk_task_list_contains_role (tlist, PK_ROLE_ENUM_GET_UPDATES)) {
		/* TRANSLATORS: we are getting the list of updates from the server */
		gpk_update_viewer_preview_set_animation (_("Getting updates"));

	} else if (pk_task_list_contains_role (tlist, PK_ROLE_ENUM_REFRESH_CACHE)) {
		/* TRANSLATORS: we are refreshing the package lists and the update lists */
		gpk_update_viewer_preview_set_animation (_("Refreshing package cache"));

	} else {
		gpk_update_viewer_preview_animation_stop ();

		/* show apply, review and refresh */
		polkit_gnome_action_set_sensitive (update_system_action, are_updates_available);
		polkit_gnome_action_set_sensitive (update_packages_action, are_updates_available);
		widget = glade_xml_get_widget (glade_xml, "button_review");
		gtk_widget_set_sensitive (widget, are_updates_available);
	}
}

/**
 * gpk_update_viewer_error_code_cb:
 **/
static void
gpk_update_viewer_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, gpointer data)
{
	GtkWidget *widget;

	/* ignore some errors */
	if (code == PK_ERROR_ENUM_PROCESS_KILL ||
	    code == PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
		egg_debug ("error ignored %s\n%s", pk_error_enum_to_text (code), details);
		return;
	}

	widget = glade_xml_get_widget (glade_xml, "window_updates");
	gpk_error_dialog_modal (GTK_WINDOW (widget), gpk_error_enum_to_localised_text (code),
				gpk_error_enum_to_localised_message (code), details);
}

/**
 * gpk_update_viewer_repo_list_changed_cb:
 **/
static void
gpk_update_viewer_repo_list_changed_cb (PkClient *client, gpointer data)
{
	gpk_update_viewer_get_new_update_list ();
}

/**
 * gpk_update_viewer_detail_popup_menu_select_all:
 **/
void
gpk_update_viewer_detail_popup_menu_select_all (GtkWidget *menuitem, gpointer userdata)
{
	GtkTreeView *treeview = GTK_TREE_VIEW (userdata);
	gboolean valid;
	GtkTreeIter iter;
	GtkTreeModel *model;
	PkStatusEnum info;

	/* get the first iter in the list */
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_INFO, &info, -1);
		if (info != PK_INFO_ENUM_BLOCKED)
			gtk_list_store_set (GTK_LIST_STORE (model), &iter,
					    PACKAGES_COLUMN_SELECT, TRUE, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * gpk_update_viewer_detail_popup_menu_select_none:
 **/
void
gpk_update_viewer_detail_popup_menu_select_none (GtkWidget *menuitem, gpointer userdata)
{
	GtkTreeView *treeview = GTK_TREE_VIEW (userdata);
	gboolean valid;
	GtkTreeIter iter;
	GtkTreeModel *model;

	/* get the first iter in the list */
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	while (valid) {
		gtk_tree_model_get (model, &iter, -1);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    PACKAGES_COLUMN_SELECT, FALSE, -1);
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * gpk_update_viewer_get_checked_status:
 **/
void
gpk_update_viewer_get_checked_status (gboolean *all_checked, gboolean *none_checked)
{
	GtkTreeView *treeview;
	gboolean valid;
	gboolean update;
	GtkTreeIter iter;
	GtkTreeModel *model;

	/* get the first iter in the list */
	treeview = GTK_TREE_VIEW (glade_xml_get_widget (glade_xml, "treeview_updates"));
	model = gtk_tree_view_get_model (treeview);
	valid = gtk_tree_model_get_iter_first (model, &iter);
	*all_checked = TRUE;
	*none_checked = TRUE;
	while (valid) {
		gtk_tree_model_get (model, &iter, PACKAGES_COLUMN_SELECT, &update, -1);
		if (update)
			*none_checked = FALSE;
		else
			*all_checked = FALSE;
		valid = gtk_tree_model_iter_next (model, &iter);
	}
}

/**
 * gpk_update_viewer_detail_popup_menu_create:
 **/
void
gpk_update_viewer_detail_popup_menu_create (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
	GtkWidget *menu;
	GtkWidget *menuitem;
	gboolean all_checked;
	gboolean none_checked;

	menu = gtk_menu_new();

	/* we don't want to show 'Select all' if they are all checked */
	gpk_update_viewer_get_checked_status (&all_checked, &none_checked);

	if (!all_checked) {
		/* TRANSLATORS: right click menu, select all the updates */
		menuitem = gtk_menu_item_new_with_label (_("Select all"));
		g_signal_connect (menuitem, "activate",
				  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_all), treeview);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	}

	if (!none_checked) {
		/* TRANSLATORS: right click menu, unselect all the updates */
		menuitem = gtk_menu_item_new_with_label (_("Unselect all"));
		g_signal_connect (menuitem, "activate",
				  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_none), treeview);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
	}

	/* TRANSLATORS: right click option, ignore this package name, not currently used */
	menuitem = gtk_menu_item_new_with_label (_("Ignore this package"));
	gtk_widget_set_sensitive (GTK_WIDGET (menuitem), FALSE);
	g_signal_connect (menuitem, "activate",
			  G_CALLBACK (gpk_update_viewer_detail_popup_menu_select_all), treeview);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);

	gtk_widget_show_all (menu);
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
		        (event != NULL) ? event->button : 0,
		        gdk_event_get_time((GdkEvent*)event));
}

/**
 * gpk_update_viewer_detail_button_pressed:
 **/
gboolean
gpk_update_viewer_detail_button_pressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
	GtkTreeSelection *selection;

	/* single click with the right mouse button? */
	if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
		/* we did not handle this */
		return FALSE;
	}

	egg_debug ("Single right click on the tree view");

	/* select the row */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	if (gtk_tree_selection_count_selected_rows (selection) <= 1) {
		GtkTreePath *path;
		/* Get tree path for row that was clicked */
		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview),
						   (gint) event->x, (gint) event->y, &path,
						   NULL, NULL, NULL)) {
			gtk_tree_selection_unselect_all (selection);
			gtk_tree_selection_select_path (selection, path);
			gtk_tree_path_free (path);
		}
	}

	/* create */
	gpk_update_viewer_detail_popup_menu_create (treeview, event, userdata);
	return TRUE;
}

/**
 * gpk_update_viewer_detail_popup_menu:
 **/
gboolean
gpk_update_viewer_detail_popup_menu (GtkWidget *treeview, gpointer userdata)
{
	gpk_update_viewer_detail_popup_menu_create (treeview, NULL, userdata);
	return TRUE;
}

/**
 * gpk_update_viewer_task_list_finished_cb:
 **/
static void
gpk_update_viewer_task_list_finished_cb (PkTaskList *tlist, PkClient *client, PkExitEnum exit,
					guint runtime, gpointer userdata)
{
	PkRoleEnum role;
	gboolean ret;

	/* get the role */
	ret = pk_client_get_role (client, &role, NULL, NULL);
	if (!ret) {
		egg_warning ("cannot get role");
		return;
	}
	egg_debug ("%s", pk_role_enum_to_text (role));

	/* update last time in the UI */
	if (role == PK_ROLE_ENUM_REFRESH_CACHE) {
		pk_update_viewer_set_last_refreshed_time ();
	} else if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
		   role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		pk_update_viewer_set_last_updated_time ();
	}

	/* do we need to repopulate the preview widget */
	if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    role == PK_ROLE_ENUM_REFRESH_CACHE) {
		egg_debug ("getting new");
		//gpk_update_viewer_get_new_update_list ();
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
	if (egg_strequal (name, "button_refresh"))
		return polkit_gnome_action_create_button (refresh_action);
	if (egg_strequal (name, "button_restart"))
		return polkit_gnome_action_create_button (restart_action);
	if (egg_strequal (name, "button_update_system"))
		return polkit_gnome_action_create_button (update_system_action);
	if (egg_strequal (name, "button_update_packages"))
		return polkit_gnome_action_create_button (update_packages_action);
	if (egg_strequal (name, "image_animation_preview"))
		return gpk_animated_icon_new ();
	if (egg_strequal (name, "image_animation_description"))
		return gpk_animated_icon_new ();
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

	/* refresh */
	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.system-sources-refresh");
	refresh_action = polkit_gnome_action_new_default ("refresh", pk_action,
							  /* TRANSLATORS: button label, refresh the package lists */
							  _("Refresh"),
							  /* TRANSLATORS: button tooltip */
							  _("Refreshing is not normally required but will retrieve the latest application and update lists"));
	g_object_set (refresh_action, "auth-icon-name", NULL, NULL);
	polkit_action_unref (pk_action);

	/* restart */
	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.consolekit.system.restart");
	restart_action = polkit_gnome_action_new_default ("restart-system", pk_action,
							  /* TRANSLATORS: button label */
							  _("_Restart computer now"), NULL);
	g_object_set (restart_action,
		      "no-icon-name", GTK_STOCK_REFRESH,
		      "auth-icon-name", GTK_STOCK_REFRESH,
		      "yes-icon-name", GTK_STOCK_REFRESH,
		      "self-blocked-icon-name", GTK_STOCK_REFRESH,
		      "no-visible", FALSE,
		      "master-visible", FALSE,
		      NULL);
	polkit_action_unref (pk_action);

	/* update-package */
	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.system-update");
	update_packages_action = polkit_gnome_action_new_default ("update-package", pk_action,
								  /* TRANSLATORS: button label, apply all pending updates the user has selected */
								  _("_Apply Updates"),
								  /* TRANSLATORS: button tooltip */
								  _("Apply the selected updates"));
	g_object_set (update_packages_action,
		      "no-icon-name", GTK_STOCK_APPLY,
		      "auth-icon-name", GTK_STOCK_APPLY,
		      "yes-icon-name", GTK_STOCK_APPLY,
		      "self-blocked-icon-name", GTK_STOCK_APPLY,
		      NULL);
	polkit_action_unref (pk_action);

	/* update-system */
	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, "org.freedesktop.packagekit.system-update");
	update_system_action = polkit_gnome_action_new_default ("update-system", pk_action,
								/* TRANSLATORS: button label, update all packages pending */
								_("_Update System"),
								/* TRANSLATORS: button tooltip */
								_("Apply all updates"));
	g_object_set (update_system_action,
		      "no-icon-name", GTK_STOCK_APPLY,
		      "auth-icon-name", GTK_STOCK_APPLY,
		      "yes-icon-name", GTK_STOCK_APPLY,
		      "self-blocked-icon-name", GTK_STOCK_APPLY,
		      NULL);
	polkit_action_unref (pk_action);
}

/**
 * gpk_update_viewer_activated_cb
 **/
static void
gpk_update_viewer_activated_cb (EggUnique *egg_unique, gpointer data)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (glade_xml, "window_updates");
	gtk_window_present (GTK_WINDOW (widget));
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	GOptionContext *context;
	GtkWidget *main_window;
	GtkWidget *widget;
	GtkTreeSelection *selection;
	PkBitfield roles;
	gboolean ret;
	GError *error = NULL;
	EggUnique *egg_unique;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
		  _("Show the program version and exit"), NULL },
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
	/* TRANSLATORS: program name, a simple app to view pending updates */
	g_option_context_set_summary (context, _("Software Update Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	egg_debug_init (verbose);
	notify_init ("gpk-update-viewer");
	gtk_init (&argc, &argv);

	/* TRANSLATORS: title to pass to to the user if there are not enough privs */
	ret = gpk_check_privileged_user (_("Software Update Viewer"), TRUE);
	if (!ret)
		return 1;

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* are we already activated? */
	egg_unique = egg_unique_new ();
	ret = egg_unique_assign (egg_unique, "org.freedesktop.PackageKit.UpdateViewer");
	if (!ret)
		goto unique_out;

	g_signal_connect (egg_unique, "activated",
			  G_CALLBACK (gpk_update_viewer_activated_cb), NULL);

	/* we have to do this before we connect up the glade file */
	gpk_update_viewer_setup_policykit ();

	control = pk_control_new ();
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (gpk_update_viewer_repo_list_changed_cb), NULL);

	/* this is stuff we don't care about */
	client_query = pk_client_new ();
	pk_client_set_use_buffer (client_query, TRUE, NULL);
	g_signal_connect (client_query, "package",
			  G_CALLBACK (gpk_update_viewer_package_cb), NULL);
	g_signal_connect (client_query, "finished",
			  G_CALLBACK (gpk_update_viewer_finished_cb), NULL);
	g_signal_connect (client_query, "progress-changed",
			  G_CALLBACK (gpk_update_viewer_progress_changed_cb), NULL);
	g_signal_connect (client_query, "update-detail",
			  G_CALLBACK (gpk_update_viewer_update_detail_cb), NULL);
	g_signal_connect (client_query, "status-changed",
			  G_CALLBACK (gpk_update_viewer_status_changed_cb), NULL);
	g_signal_connect (client_query, "error-code",
			  G_CALLBACK (gpk_update_viewer_error_code_cb), NULL);

	client_action = pk_client_new ();
	pk_client_set_use_buffer (client_action, TRUE, NULL);
	g_signal_connect (client_action, "package",
			  G_CALLBACK (gpk_update_viewer_package_cb), NULL);
	g_signal_connect (client_action, "finished",
			  G_CALLBACK (gpk_update_viewer_finished_cb), NULL);
	g_signal_connect (client_action, "progress-changed",
			  G_CALLBACK (gpk_update_viewer_progress_changed_cb), NULL);
	g_signal_connect (client_action, "status-changed",
			  G_CALLBACK (gpk_update_viewer_status_changed_cb), NULL);
	g_signal_connect (client_action, "error-code",
			  G_CALLBACK (gpk_update_viewer_error_code_cb), NULL);

	/* get actions */
	roles = pk_control_get_actions (control, NULL);

	/* monitor for other updates in progress */
	tlist = pk_task_list_new ();

	/* install stuff using the gnome helpers */
	gclient = gpk_client_new ();

	/* use custom widgets */
	glade_set_custom_handler (gpk_update_viewer_create_custom_widget, NULL);

	glade_xml = glade_xml_new (GPK_DATA "/gpk-update-viewer.glade", NULL, NULL);
	main_window = glade_xml_get_widget (glade_xml, "window_updates");

	/* make GpkClient windows modal */
	gtk_widget_realize (main_window);
	gpk_client_set_parent (gclient, GTK_WINDOW (main_window));

	/* hide from finished page until we have updates */
	widget = glade_xml_get_widget (glade_xml, "hbox_restart");
	gtk_widget_hide (widget);

	/* Get the main window quit */
	g_signal_connect_swapped (main_window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);

	/* button_close2 and button_close3 are on the overview/review
	 * screens, where we want to cancel transactions when closing
	 */
	widget = glade_xml_get_widget (glade_xml, "button_close2");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_close_and_cancel_cb), NULL);
	widget = glade_xml_get_widget (glade_xml, "button_close3");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_button_close_and_cancel_cb), NULL);

	/* normal close buttons */
	widget = glade_xml_get_widget (glade_xml, "button_close4");
	g_signal_connect_swapped (widget, "clicked", G_CALLBACK (gtk_main_quit), NULL);

	/* connect up PolicyKit actions */
	g_signal_connect (refresh_action, "activate",
			  G_CALLBACK (gpk_update_viewer_refresh_cb), NULL);
	g_signal_connect (restart_action, "activate",
			  G_CALLBACK (gpk_update_viewer_restart_cb), NULL);
	g_signal_connect (update_packages_action, "activate",
			  G_CALLBACK (gpk_update_viewer_apply_cb), NULL);
	g_signal_connect (update_system_action, "activate",
			  G_CALLBACK (gpk_update_viewer_update_system_cb), NULL);

	widget = glade_xml_get_widget (glade_xml, "button_review");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_review_cb), NULL);
	/* TRANSLATORS: tooltip on the review button */
	gtk_widget_set_tooltip_text(widget, _("Review the update list"));

	widget = glade_xml_get_widget (glade_xml, "button_overview");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_update_viewer_overview_cb), NULL);
	/* TRANSLATORS: tooltip on the back button (from the detailed view) */
	gtk_widget_set_tooltip_text(widget, _("Back to overview"));

	widget = glade_xml_get_widget (glade_xml, "button_overview2");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_more_installs_cb), NULL);
	/* TRANSLATORS: tooltip on the back button (from finished view) */
	gtk_widget_set_tooltip_text (widget, _("Back to overview"));

	widget = glade_xml_get_widget (glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_help_cb), "update-viewer");

	widget = glade_xml_get_widget (glade_xml, "button_help2");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_button_help_cb), "update-viewer-details");

	/* create list stores */
	list_store_details = gtk_list_store_new (PACKAGES_COLUMN_LAST, G_TYPE_STRING,
						 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT,
						 G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
	list_store_preview = gtk_list_store_new (PREVIEW_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF);
	list_store_description = gtk_list_store_new (DESC_COLUMN_LAST, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* create preview tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_preview");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store_preview));

	/* add columns to the tree view */
	gpk_update_viewer_treeview_add_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* create description tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_description");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store_description));

	/* add columns to the tree view */
	gpk_update_viewer_treeview_add_columns_description (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* create package tree view */
	widget = glade_xml_get_widget (glade_xml, "treeview_updates");
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store_details));
	g_signal_connect (widget, "popup-menu",
			  G_CALLBACK (gpk_update_viewer_detail_popup_menu), NULL);
	g_signal_connect (widget, "button-press-event",
			  G_CALLBACK (gpk_update_viewer_detail_button_pressed), NULL);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	g_signal_connect (selection, "changed",
			  G_CALLBACK (pk_packages_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	gpk_update_viewer_treeview_add_columns_update (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget));

	/* make the buttons non-clickable until we get completion */
	polkit_gnome_action_set_sensitive (update_system_action, FALSE);
	polkit_gnome_action_set_sensitive (update_packages_action, FALSE);
	widget = glade_xml_get_widget (glade_xml, "button_review");
	gtk_widget_set_sensitive (widget, FALSE);

	/* set the last updated text */
	pk_update_viewer_set_last_refreshed_time ();
	pk_update_viewer_set_last_updated_time ();

	/* we need to grey out all the buttons if we are in progress */
	g_signal_connect (tlist, "changed",
			  G_CALLBACK (gpk_update_viewer_task_list_changed_cb), NULL);
	gpk_update_viewer_task_list_changed_cb (tlist, NULL);
	g_signal_connect (tlist, "finished",
			  G_CALLBACK (gpk_update_viewer_task_list_finished_cb), NULL);

	/* show window */
	gtk_widget_show (main_window);

	/* coldplug */
	gpk_update_viewer_get_new_update_list ();

	/* wait */
	gtk_main ();

	/* we might have visual stuff running, close it down */
	ret = pk_client_cancel (client_query, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (glade_xml);
	g_object_unref (list_store_preview);
	g_object_unref (list_store_description);
	g_object_unref (list_store_details);
	g_object_unref (gclient);
	g_object_unref (control);
	g_object_unref (client_query);
	g_object_unref (client_action);
	g_free (cached_package_id);
unique_out:
	g_object_unref (egg_unique);

	return 0;
}
