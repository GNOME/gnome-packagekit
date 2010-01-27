/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include "gpk-helper-deps-install.h"
#include "gpk-marshal.h"
#include "gpk-gnome.h"
#include "gpk-common.h"
#include "gpk-dialog.h"

#include "egg-debug.h"

static void     gpk_helper_deps_install_finalize	(GObject	  *object);

#define GPK_HELPER_DEPS_INSTALL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_HELPER_DEPS_INSTALL, GpkHelperDepsInstallPrivate))

struct GpkHelperDepsInstallPrivate
{
	GtkWindow		*window;
	GConfClient		*gconf_client;
};

enum {
	GPK_HELPER_DEPS_INSTALL_EVENT,
	GPK_HELPER_DEPS_INSTALL_LAST_SIGNAL
};

static guint signals [GPK_HELPER_DEPS_INSTALL_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkHelperDepsInstall, gpk_helper_deps_install, G_TYPE_OBJECT)

/**
 * gpk_helper_deps_install_show:
 *
 * Return value: if we agreed
 **/
gboolean
gpk_helper_deps_install_show (GpkHelperDepsInstall *helper, PkPackageList *packages, PkPackageList *deps_list)
{
	gchar *name = NULL;
	gchar *title = NULL;
	gchar *message = NULL;
	gchar **package_ids = NULL;
	guint length;
	gboolean ret;
	GtkWidget *dialog;
	GtkResponseType response;
	gchar *package_id;
	const PkPackageObj *obj;
	const PkPackageObj *obj_tmp;
	guint i = 0, j;

	/* remove cleanup packages */
	while (i<pk_package_list_get_size (deps_list)) {
		obj = pk_package_list_get_obj (deps_list, i);
		if (obj->info == PK_INFO_ENUM_CLEANUP ||
		    obj->info == PK_INFO_ENUM_FINISHED) {
			package_id = pk_package_id_to_string (obj->id);
			pk_package_list_remove (deps_list, package_id);
			g_free (package_id);
			continue;
		}

		/* remove original packages */
		ret = FALSE;
		length = pk_package_list_get_size (packages);
		for (j=0; j<length; j++) {
			obj_tmp = pk_package_list_get_obj (packages, j);
			if (pk_package_id_equal (obj_tmp->id, obj->id)) {
				package_id = pk_package_id_to_string (obj->id);
				pk_package_list_remove (deps_list, package_id);
				g_free (package_id);
				ret = TRUE;
			}
		}
		if (ret)
			continue;

		/* only increment if we didn't remove a package */
		i++;
	}

	/* empty list */
	length = pk_package_list_get_size (deps_list);
	if (length == 0) {
		g_signal_emit (helper, signals [GPK_HELPER_DEPS_INSTALL_EVENT], 0, GTK_RESPONSE_YES);
		goto out;
	}

	/* have we previously said we don't want to be shown the confirmation */
	ret = gconf_client_get_bool (helper->priv->gconf_client, GPK_CONF_SHOW_DEPENDS, NULL);
	if (!ret) {
		egg_debug ("we've said we don't want the dep dialog");
		g_signal_emit (helper, signals [GPK_HELPER_DEPS_INSTALL_EVENT], 0, GTK_RESPONSE_YES);
		goto out;
	}

	/* TRANSLATORS: title: tell the user we have to install additional packages */
	title = g_strdup_printf (ngettext ("%i additional package also has to be installed",
					   "%i additional packages also have to be installed",
					   length), length);

	package_ids = pk_package_list_to_strv (packages);
	name = gpk_dialog_package_id_name_join_locale (package_ids);

	/* TRANSLATORS: message: describe in detail why it must happen */
	message = g_strdup_printf (ngettext ("To install %s, an additional package also has to be downloaded.",
					     "To install %s, additional packages also have to be downloaded.",
					     length), name);

	dialog = gtk_message_dialog_new (helper->priv->window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_CANCEL, "%s", title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gpk_dialog_embed_package_list_widget (GTK_DIALOG (dialog), deps_list);
	gpk_dialog_embed_do_not_show_widget (GTK_DIALOG (dialog), GPK_CONF_SHOW_DEPENDS);
//	gtk_dialog_add_button (GTK_DIALOG (dialog), "help", GTK_RESPONSE_HELP);
	/* TRANSLATORS: this is button text */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Install"), GTK_RESPONSE_YES);

	/* set icon name */
	gtk_window_set_icon_name (GTK_WINDOW (dialog), GPK_ICON_SOFTWARE_INSTALLER);

	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));

	/* yes / no */
	if (response == GTK_RESPONSE_YES) {
		g_signal_emit (helper, signals [GPK_HELPER_DEPS_INSTALL_EVENT], 0, response);
//	} else if (response == GTK_RESPONSE_HELP) {
//		gpk_gnome_help ("dialog-install-other-packages");
	} else {
		g_signal_emit (helper, signals [GPK_HELPER_DEPS_INSTALL_EVENT], 0, GTK_RESPONSE_NO);
	}
out:
	g_strfreev (package_ids);
	g_free (name);
	g_free (title);
	g_free (message);
	return TRUE;
}

/**
 * gpk_helper_deps_install_set_parent:
 **/
gboolean
gpk_helper_deps_install_set_parent (GpkHelperDepsInstall *helper, GtkWindow *window)
{
	g_return_val_if_fail (GPK_IS_HELPER_DEPS_INSTALL (helper), FALSE);
	g_return_val_if_fail (window != NULL, FALSE);

	helper->priv->window = window;
	return TRUE;
}

/**
 * gpk_helper_deps_install_class_init:
 * @klass: The GpkHelperDepsInstallClass
 **/
static void
gpk_helper_deps_install_class_init (GpkHelperDepsInstallClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_helper_deps_install_finalize;
	g_type_class_add_private (klass, sizeof (GpkHelperDepsInstallPrivate));
	signals [GPK_HELPER_DEPS_INSTALL_EVENT] =
		g_signal_new ("event",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkHelperDepsInstallClass, event),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * gpk_helper_deps_install_init:
 **/
static void
gpk_helper_deps_install_init (GpkHelperDepsInstall *helper)
{
	helper->priv = GPK_HELPER_DEPS_INSTALL_GET_PRIVATE (helper);
	helper->priv->window = NULL;
	helper->priv->gconf_client = gconf_client_get_default ();
}

/**
 * gpk_helper_deps_install_finalize:
 **/
static void
gpk_helper_deps_install_finalize (GObject *object)
{
	GpkHelperDepsInstall *helper;

	g_return_if_fail (GPK_IS_HELPER_DEPS_INSTALL (object));

	helper = GPK_HELPER_DEPS_INSTALL (object);
	g_object_unref (helper->priv->gconf_client);

	G_OBJECT_CLASS (gpk_helper_deps_install_parent_class)->finalize (object);
}

/**
 * gpk_helper_deps_install_new:
 **/
GpkHelperDepsInstall *
gpk_helper_deps_install_new (void)
{
	GpkHelperDepsInstall *helper;
	helper = g_object_new (GPK_TYPE_HELPER_DEPS_INSTALL, NULL);
	return GPK_HELPER_DEPS_INSTALL (helper);
}

