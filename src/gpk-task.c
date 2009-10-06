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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <glib/gi18n.h>
#include <packagekit-glib2/packagekit.h>
#include <gconf/gconf-client.h>

#include "egg-debug.h"

#include "gpk-task.h"
#include "gpk-gnome.h"
#include "gpk-common.h"
#include "gpk-enum.h"
#include "gpk-dialog.h"

static void     gpk_task_finalize	(GObject     *object);

#define GPK_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_TASK, GpkTaskPrivate))

/**
 * GpkTaskPrivate:
 *
 * Private #GpkTask data
 **/
struct _GpkTaskPrivate
{
	gpointer		 user_data;
	GConfClient		*gconf_client;
	GtkWindow		*parent_window;
	GtkWindow		*current_window;
	GtkBuilder		*builder_untrusted;
	GtkBuilder		*builder_signature;
	GtkBuilder		*builder_eula;
	guint			 request;
	const gchar		*help_id;
};

G_DEFINE_TYPE (GpkTask, gpk_task, PK_TYPE_TASK)

/**
 * gpk_task_set_parent_window:
 **/
gboolean
gpk_task_set_parent_window (GpkTask *task, GtkWindow *parent_window)
{
	g_return_val_if_fail (GPK_IS_TASK (task), FALSE);
	g_return_val_if_fail (parent_window != NULL, FALSE);
	task->priv->parent_window = parent_window;
	return TRUE;
}

/**
 * gpk_task_button_accept_cb:
 **/
static void
gpk_task_button_accept_cb (GtkWidget *widget, GpkTask *task)
{
	gtk_widget_hide (GTK_WIDGET(task->priv->current_window));
	pk_task_user_accepted (PK_TASK(task), task->priv->request);
	task->priv->request = 0;
	task->priv->current_window = NULL;
}

/**
 * gpk_task_button_decline_cb:
 **/
static void
gpk_task_button_decline_cb (GtkWidget *widget, GpkTask *task)
{
	gtk_widget_hide (GTK_WIDGET(task->priv->current_window));
	pk_task_user_declined (PK_TASK(task), task->priv->request);
	task->priv->request = 0;
	task->priv->current_window = NULL;
}

/**
 * gpk_task_dialog_response_cb:
 **/
static void
gpk_task_dialog_response_cb (GtkDialog *dialog, gint response_id, GpkTask *task)
{
	if (response_id == GTK_RESPONSE_YES) {
		gpk_task_button_accept_cb (GTK_WIDGET(dialog), task);
		return;
	}
	/* all other options */
	gpk_task_button_decline_cb (GTK_WIDGET(dialog), task);
}

/**
 * gpk_task_button_help_cb:
 **/
static void
gpk_task_button_help_cb (GtkWidget *widget, GpkTask *task)
{
	/* show the help */
	gpk_gnome_help (task->priv->help_id);
}

/**
 * gpk_task_untrusted_question:
 **/
static void
gpk_task_untrusted_question (PkTask *task, guint request, PkResults *results)
{
	GtkWidget *widget;
	gchar *message;
	PkRoleEnum role;
	GpkTaskPrivate *priv = GPK_TASK(task)->priv;

	/* save the current request */
	priv->request = request;

	/* title */
	widget = GTK_WIDGET(gtk_builder_get_object (priv->builder_untrusted, "label_title"));
	gtk_widget_hide (widget);

	/* message */
	g_object_get (results, "role", &role, NULL);
	if (role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		message = g_strdup_printf ("%s\n%s\n\n%s\n%s",
					   /* TRANSLATORS: is not GPG signed */
					   _("The software is not signed by a trusted provider."),
					   /* TRANSLATORS: user has to trust provider -- I know, this sucks */
					   _("Do not update this package unless you are sure it is safe to do so."),
					   /* TRANSLATORS: warn the user that all bets are off */
					   _("Malicious software can damage your computer or cause other harm."),
					   /* TRANSLATORS: ask if they are absolutely sure they want to do this */
					   _("Are you <b>sure</b> you want to update this package?"));
	} else {
		message = g_strdup_printf ("%s\n%s\n\n%s\n%s",
					   /* TRANSLATORS: is not GPG signed */
					   _("The software is not signed by a trusted provider."),
					   /* TRANSLATORS: user has to trust provider -- I know, this sucks */
					   _("Do not install this package unless you are sure it is safe to do so."),
					   /* TRANSLATORS: warn the user that all bets are off */
					   _("Malicious software can damage your computer or cause other harm."),
					   /* TRANSLATORS: ask if they are absolutely sure they want to do this */
					   _("Are you <b>sure</b> you want to install this package?"));
	}
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder_untrusted, "label_message"));
	gtk_label_set_markup (GTK_LABEL (widget), message);
	g_free (message);

	/* show window */
	priv->current_window = GTK_WINDOW(gtk_builder_get_object (priv->builder_untrusted, "dialog_error"));
	if (priv->parent_window != NULL) {
		gtk_window_set_transient_for (priv->current_window, priv->parent_window);
		gtk_window_set_modal (priv->current_window, TRUE);
		/* this is a modal popup, so don't show a window title */
		gtk_window_set_title (priv->current_window, "");
	}
	gtk_window_set_title (priv->current_window, _("The software is not signed by a trusted provider."));
	gtk_widget_show (GTK_WIDGET(priv->current_window));
}

/**
 * gpk_task_key_question:
 **/
static void
gpk_task_key_question (PkTask *task, guint request, PkResults *results)
{
	GPtrArray *array;
	const PkItemRepoSignatureRequired *item;
	GpkTaskPrivate *priv = GPK_TASK(task)->priv;
	GtkWidget *widget;
	gchar *text;

	/* save the current request */
	priv->request = request;

	/* get data */
	array = pk_results_get_repo_signature_required_array (results);
	if (array->len != 1) {
		egg_warning ("array length %i, aborting", array->len);
		goto out;
	}

	/* only one item supported */
	item = g_ptr_array_index (array, 0);

	/* show correct text */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder_signature, "label_name"));
	gtk_label_set_label (GTK_LABEL (widget), item->repository_name);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder_signature, "label_url"));
	gtk_label_set_label (GTK_LABEL (widget), item->key_url);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder_signature, "label_user"));
	gtk_label_set_label (GTK_LABEL (widget), item->key_userid);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder_signature, "label_id"));
	gtk_label_set_label (GTK_LABEL (widget), item->key_id);

	text = pk_package_id_to_printable (item->package_id);
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder_signature, "label_package"));
	gtk_label_set_label (GTK_LABEL (widget), text);
	g_free (text);

	/* show window */
	priv->current_window = GTK_WINDOW(gtk_builder_get_object (priv->builder_signature, "dialog_gpg"));
	if (priv->parent_window != NULL) {
		gtk_window_set_transient_for (priv->current_window, priv->parent_window);
		gtk_window_set_modal (priv->current_window, TRUE);
		/* this is a modal popup, so don't show a window title */
		gtk_window_set_title (priv->current_window, "");
	}
	priv->help_id = "gpg-signature";
	gtk_widget_show (GTK_WIDGET(priv->current_window));
out:
	g_ptr_array_unref (array);
}

/**
 * gpk_task_eula_question:
 **/
static void
gpk_task_eula_question (PkTask *task, guint request, PkResults *results)
{
	GPtrArray *array;
	const PkItemEulaRequired *item;
	GpkTaskPrivate *priv = GPK_TASK(task)->priv;
	GtkWidget *widget;
	GtkTextBuffer *buffer;
	gchar *text;
	gchar **split;

	/* save the current request */
	priv->request = request;

	/* get data */
	array = pk_results_get_eula_required_array (results);
	if (array->len != 1) {
		egg_warning ("array length %i, aborting", array->len);
		goto out;
	}

	/* only one item supported */
	item = g_ptr_array_index (array, 0);

	/* title */
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder_eula, "label_title"));

	split = pk_package_id_split (item->package_id);
	text = g_strdup_printf ("<b><big>License required for %s by %s</big></b>", split[0], item->vendor_name);
	gtk_label_set_label (GTK_LABEL (widget), text);
	g_free (text);

	buffer = gtk_text_buffer_new (NULL);
	gtk_text_buffer_insert_at_cursor (buffer, item->license_agreement, strlen (item->license_agreement));
	widget = GTK_WIDGET (gtk_builder_get_object (priv->builder_eula, "textview_details"));
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (widget), buffer);

	/* set minimum size a bit bigger */
	gtk_widget_set_size_request (widget, 100, 200);

	/* show window */
	priv->current_window = GTK_WINDOW(gtk_builder_get_object (priv->builder_eula, "dialog_eula"));
	if (priv->parent_window != NULL) {
		gtk_window_set_transient_for (priv->current_window, priv->parent_window);
		gtk_window_set_modal (priv->current_window, TRUE);
		/* this is a modal popup, so don't show a window title */
		gtk_window_set_title (priv->current_window, "");
	}
	priv->help_id = "eula";
	gtk_widget_show (GTK_WIDGET(priv->current_window));

	g_object_unref (buffer);
	g_strfreev (split);
out:
	g_ptr_array_unref (array);
}

/**
 * gpk_task_media_change_question:
 **/
static void
gpk_task_media_change_question (PkTask *task, guint request, PkResults *results)
{
	GPtrArray *array;
	const PkItemMediaChangeRequired *item;
	GpkTaskPrivate *priv = GPK_TASK(task)->priv;
	const gchar *name;
	gchar *message = NULL;

	/* save the current request */
	priv->request = request;

	/* get data */
	array = pk_results_get_media_change_required_array (results);
	if (array->len != 1) {
		egg_warning ("array length %i, aborting", array->len);
		goto out;
	}

	/* only one item supported */
	item = g_ptr_array_index (array, 0);

	name = gpk_media_type_enum_to_localised_text (item->media_type);
	/* TRANSLATORS: dialog body, explains to the user that they need to insert a disk to continue. The first replacement is DVD, CD etc */
	message = g_strdup_printf (_("Additional media is required. Please insert the %s labeled '%s' to continue."), name, item->media_text);

	priv->current_window = GTK_WINDOW (gtk_message_dialog_new (priv->parent_window, GTK_DIALOG_DESTROY_WITH_PARENT,
								   /* TRANSLATORS: this is the window title when a new cd or dvd is required */
								   GTK_MESSAGE_INFO, GTK_BUTTONS_CANCEL, _("A media change is required")));
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG(priv->current_window), "%s", message);

	/* TRANSLATORS: this is button text */
	gtk_dialog_add_button (GTK_DIALOG(priv->current_window), _("Continue"), GTK_RESPONSE_YES);

	/* set icon name */
	gtk_window_set_icon_name (priv->current_window, GPK_ICON_SOFTWARE_INSTALLER);

	g_signal_connect (priv->current_window, "response", G_CALLBACK (gpk_task_dialog_response_cb), task);
	gtk_widget_show_all (GTK_WIDGET(priv->current_window));
out:
	g_free (message);
	g_ptr_array_unref (array);
}

/**
 * gpk_task_simulate_question:
 **/
static void
gpk_task_simulate_question (PkTask *task, guint request, PkResults *results)
{
	gboolean ret;
	GPtrArray *array = NULL;
	GpkTaskPrivate *priv = GPK_TASK(task)->priv;
	PkRoleEnum role;
	guint inputs;
	const gchar *title;
	const gchar *message = NULL;

	/* get data about the transaction */
	g_object_get (results,
		      "role", &role,
		      "inputs", &inputs,
		      NULL);

	/* allow skipping of deps except when we remove other packages */
	if (role != PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES) {
		/* have we previously said we don't want to be shown the confirmation */
		ret = gconf_client_get_bool (priv->gconf_client, GPK_CONF_SHOW_DEPENDS, NULL);
		if (!ret) {
			egg_debug ("we've said we don't want the dep dialog");
			pk_task_user_accepted (PK_TASK(task), priv->request);
			goto out;
		}
	}

	/* per-role messages */
	if (role == PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES) {
		/* TRANSLATORS: title of a dependency dialog */
		title = _("Additional software will be installed");

		/* TRANSLATORS: message text of a dependency dialog */
		message = ngettext ("To install this package, additional software also has to be installed.",
				    "To install these packages, additional software also has to be installed.", inputs);
	} else if (role == PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES) {
		/* TRANSLATORS: title of a dependency dialog */
		title = _("Additional software will be removed");

		/* TRANSLATORS: message text of a dependency dialog */
		message = ngettext ("To remove this package, additional software also has to be removed.",
				    "To remove these packages, additional software also has to be removed.", inputs);
	} else if (role == PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES) {
		/* TRANSLATORS: title of a dependency dialog */
		title = _("Additional software will be installed");

		/* TRANSLATORS: message text of a dependency dialog */
		message = ngettext ("To update this package, additional software also has to be installed.",
				    "To update these packages, additional software also has to be installed.", inputs);
	} else if (role == PK_ROLE_ENUM_SIMULATE_INSTALL_FILES) {
		/* TRANSLATORS: title of a dependency dialog */
		title = _("Additional software will be installed");

		/* TRANSLATORS: message text of a dependency dialog */
		message = ngettext ("To install this file, additional software also has to be installed.",
				    "To install these file, additional software also has to be installed.", inputs);
	} else {
		/* TRANSLATORS: title of a dependency dialog */
		title = _("Additional software required");

		/* TRANSLATORS: message text of a dependency dialog */
		message = _("To process this transaction, additional software is required.");
	}

	/* save the current request */
	priv->request = request;

	/* get data */
	array = pk_results_get_package_array (results);

	priv->current_window = GTK_WINDOW (gtk_message_dialog_new (priv->parent_window, GTK_DIALOG_DESTROY_WITH_PARENT,
								   GTK_MESSAGE_INFO, GTK_BUTTONS_CANCEL, "%s", title));
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (priv->current_window), "%s", message);
	gpk_dialog_embed_package_list_widget (GTK_DIALOG(priv->current_window), array);
	gpk_dialog_embed_do_not_show_widget (GTK_DIALOG(priv->current_window), GPK_CONF_SHOW_DEPENDS);
	/* TRANSLATORS: this is button text */
	gtk_dialog_add_button (GTK_DIALOG(priv->current_window), _("Continue"), GTK_RESPONSE_YES);

	/* set icon name */
	gtk_window_set_icon_name (priv->current_window, GPK_ICON_SOFTWARE_INSTALLER);

	g_signal_connect (priv->current_window, "response", G_CALLBACK (gpk_task_dialog_response_cb), task);
	gtk_widget_show_all (GTK_WIDGET(priv->current_window));
out:
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * gpk_task_setup_dialog_untrusted:
 **/
static void
gpk_task_setup_dialog_untrusted (GpkTask *task)
{
	GtkWidget *widget;
	GtkWidget *button;
	guint retval;
	GError *error = NULL;

	/* get UI */
	task->priv->builder_untrusted = gtk_builder_new ();
	retval = gtk_builder_add_from_file (task->priv->builder_untrusted, GPK_DATA "/gpk-error.ui", &error);
	if (retval == 0) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
	}

	/* connect up default actions */
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_untrusted, "dialog_error"));
	g_signal_connect (widget, "delete_event", G_CALLBACK (gpk_task_button_decline_cb), task);

	/* set icon name */
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_untrusted, "dialog_error"));
	gtk_window_set_icon_name (GTK_WINDOW(widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* connect up buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_untrusted, "button_close"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_task_button_decline_cb), task);

	/* don't show text in the expander */
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_untrusted, "expander_details"));
	gtk_widget_hide (widget);

	/* TRANSLATORS: button label, force the install, even though it's untrusted */
	button = gtk_button_new_with_mnemonic (_("_Force install"));
	g_signal_connect (button, "clicked", G_CALLBACK (gpk_task_button_accept_cb), task);

	/* TRANSLATORS: button tooltip */
	gtk_widget_set_tooltip_text (button, _("Force installing package"));

	/* add to box */
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_untrusted, "dialog_error"));
	widget = gtk_dialog_get_action_area (GTK_DIALOG(widget));
	gtk_box_pack_start (GTK_BOX (widget), button, FALSE, FALSE, 0);
	gtk_widget_show (button);
}

/**
 * gpk_task_setup_dialog_signature:
 **/
static void
gpk_task_setup_dialog_signature (GpkTask *task)
{
	GtkWidget *widget;
	guint retval;
	GError *error = NULL;

	/* get UI */
	task->priv->builder_signature = gtk_builder_new ();
	retval = gtk_builder_add_from_file (task->priv->builder_signature, GPK_DATA "/gpk-signature.ui", &error);
	if (retval == 0) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
	}

	/* connect up default actions */
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_signature, "dialog_gpg"));
	g_signal_connect (widget, "delete_event", G_CALLBACK (gpk_task_button_decline_cb), task);

	/* set icon name */
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_signature, "dialog_gpg"));
	gtk_window_set_icon_name (GTK_WINDOW(widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* connect up buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_signature, "button_yes"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_task_button_accept_cb), task);
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_signature, "button_help"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_task_button_help_cb), task);
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_signature, "button_no"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_task_button_decline_cb), task);
}

/**
 * gpk_task_setup_dialog_eula:
 **/
static void
gpk_task_setup_dialog_eula (GpkTask *task)
{
	GtkWidget *widget;
	guint retval;
	GError *error = NULL;

	/* get UI */
	task->priv->builder_eula = gtk_builder_new ();
	retval = gtk_builder_add_from_file (task->priv->builder_eula, GPK_DATA "/gpk-eula.ui", &error);
	if (retval == 0) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
	}

	/* connect up default actions */
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_eula, "dialog_eula"));
	g_signal_connect (widget, "delete_event", G_CALLBACK (gpk_task_button_decline_cb), task);

	/* set icon name */
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_eula, "dialog_eula"));
	gtk_window_set_icon_name (GTK_WINDOW(widget), GPK_ICON_SOFTWARE_INSTALLER);

	/* connect up buttons */
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_eula, "button_agree"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_task_button_accept_cb), task);
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_eula, "button_help"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_task_button_help_cb), task);
	widget = GTK_WIDGET (gtk_builder_get_object (task->priv->builder_eula, "button_cancel"));
	g_signal_connect (widget, "clicked", G_CALLBACK (gpk_task_button_decline_cb), task);
}

/**
 * gpk_task_class_init:
 **/
static void
gpk_task_class_init (GpkTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	PkTaskClass *task_class = PK_TASK_CLASS (klass);

	object_class->finalize = gpk_task_finalize;
	task_class->untrusted_question = gpk_task_untrusted_question;
	task_class->key_question = gpk_task_key_question;
	task_class->eula_question = gpk_task_eula_question;
	task_class->media_change_question = gpk_task_media_change_question;
	task_class->simulate_question = gpk_task_simulate_question;

	g_type_class_add_private (klass, sizeof (GpkTaskPrivate));
}

/**
 * gpk_task_init:
 * @task: This class instance
 **/
static void
gpk_task_init (GpkTask *task)
{
	task->priv = GPK_TASK_GET_PRIVATE (task);
	task->priv->request = 0;
	task->priv->parent_window = NULL;
	task->priv->current_window = NULL;
	task->priv->gconf_client = gconf_client_get_default ();

	/* setup dialogs ahead of time */
	gpk_task_setup_dialog_untrusted (task);
	gpk_task_setup_dialog_eula (task);
	gpk_task_setup_dialog_signature (task);
}

/**
 * gpk_task_finalize:
 * @object: The object to finalize
 **/
static void
gpk_task_finalize (GObject *object)
{
	GpkTask *task = GPK_TASK (object);

	g_object_unref (task->priv->builder_untrusted);
	g_object_unref (task->priv->builder_signature);
	g_object_unref (task->priv->builder_eula);
	g_object_unref (task->priv->gconf_client);

	G_OBJECT_CLASS (gpk_task_parent_class)->finalize (object);
}

/**
 * gpk_task_new:
 *
 * Return value: a new GpkTask object.
 **/
GpkTask *
gpk_task_new (void)
{
	GpkTask *task;
	task = g_object_new (GPK_TYPE_TASK, NULL);
	return GPK_TASK (task);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static void
gpk_task_test_install_packages_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	GpkTask *task = GPK_TASK (object);
	GError *error = NULL;
	PkResults *results;
	GPtrArray *packages;
	const PkItemPackage *item;
	guint i;
	PkItemErrorCode *error_item = NULL;

	/* get the results */
	results = pk_task_generic_finish (PK_TASK(task), res, &error);
	if (results == NULL) {
		egg_test_failed (test, "failed to resolve: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_item = pk_results_get_error_code (results);
	if (error_item != NULL)
		egg_test_failed (test, "failed to resolve success: %s", error_item->details);

	packages = pk_results_get_package_array (results);
	if (packages == NULL)
		egg_test_failed (test, "no packages!");

	/* list, just for shits and giggles */
	for (i=0; i<packages->len; i++) {
		item = g_ptr_array_index (packages, i);
		egg_debug ("%s\t%s\t%s", pk_info_enum_to_text (item->info), item->package_id, item->summary);
	}

	if (packages->len != 3)
		egg_test_failed (test, "invalid number of packages: %i", packages->len);

	g_ptr_array_unref (packages);
out:
	if (error_item != NULL)
		pk_item_error_code_unref (error_item);
	if (results != NULL)
		g_object_unref (results);
	egg_test_loop_quit (test);
}

static void
gpk_task_test_progress_cb (PkProgress *progress, PkProgressType type, EggTest *test)
{
	PkStatusEnum status;
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
			      "status", &status,
			      NULL);
		egg_debug ("now %s", pk_status_enum_to_text (status));
	}
}

void
gpk_task_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	GpkTask *task;
	gchar **package_ids;

	if (!egg_test_start (test, "GpkTask"))
		return;

	/************************************************************/
	egg_test_title (test, "get task");
	task = gpk_task_new ();
	egg_test_assert (test, task != NULL);

	/* For testing, you will need to manually do:
	pkcon repo-set-data dummy use-gpg 1
	pkcon repo-set-data dummy use-eula 1
	pkcon repo-set-data dummy use-media 1
	*/

	/************************************************************/
	egg_test_title (test, "install package");
	package_ids = pk_package_ids_from_id ("vips-doc;7.12.4-2.fc8;noarch;linva");
	pk_task_install_packages_async (PK_TASK(task), package_ids, NULL,
				        (PkProgressCallback) gpk_task_test_progress_cb, test,
				        (GAsyncReadyCallback) gpk_task_test_install_packages_cb, test);
	g_strfreev (package_ids);
	egg_test_loop_wait (test, 150000);
	egg_test_success (test, "installed in %i", egg_test_elapsed (test));

	g_object_unref (task);
	egg_test_end (test);
}
#endif

