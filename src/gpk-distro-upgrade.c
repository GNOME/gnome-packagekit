/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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
#include <locale.h>
#include <packagekit-glib2/packagekit.h>

#ifdef HAVE_SYSTEMD
#include "systemd-proxy.h"
#else
#include "egg-console-kit.h"
#endif

#include "gpk-animated-icon.h"
#include "gpk-common.h"
#include "gpk-debug.h"
#include "gpk-enum.h"
#include "gpk-error.h"

enum {
	GPK_DISTRO_UPGRADE_COMBO_COLUMN_TEXT,
	GPK_DISTRO_UPGRADE_COMBO_COLUMN_ID,
	GPK_DISTRO_UPGRADE_COMBO_COLUMN_LAST
};

typedef struct {
#ifdef HAVE_SYSTEMD
        SystemdProxy    *systemd_proxy;
#else
	EggConsoleKit	*console_kit;
#endif
	GCancellable	*cancellable;
	GtkListStore	*distro_upgrade_store;
	GtkWidget	*assistant;
	GtkWidget	*checkbutton;
	GtkWidget	*radio_minimal;
	GtkWidget	*radio_default;
	GtkWidget	*radio_complete;
	GtkWidget	*combobox;
	GtkWidget	*page_choose_vbox;
	GtkWidget	*progress_bar;
	GtkWidget	*status_icon;
	GtkWidget	*status_label;
	GtkWidget	*info_bar;
	PkBitfield	 roles;
	PkClient	*client;
} GpkDistroUpgradePrivate;

enum {
	GPK_DISTRO_UPGRADE_PAGE_INTRODUCTION,
	GPK_DISTRO_UPGRADE_PAGE_CHOOSE,
	GPK_DISTRO_UPGRADE_PAGE_KIND,
	GPK_DISTRO_UPGRADE_PAGE_CONFIRM,
	GPK_DISTRO_UPGRADE_PAGE_PROGRESS,
};

/**
 * gpk_distro_upgrade_progress_cb:
 **/
static void
gpk_distro_upgrade_progress_cb (PkProgress *progress, PkProgressType type, GpkDistroUpgradePrivate *priv)
{
	gint percentage;
	PkStatusEnum status;

	if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		g_object_get (progress,
			      "percentage", &percentage,
			      NULL);
		g_debug ("percentage=%i", percentage);
		if (percentage > 0)
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->progress_bar),
						       (gfloat)percentage / 100.0f);
		return;
	}
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
			      "status", &status,
			      NULL);
		g_debug ("status=%s", pk_status_enum_to_string (status));
		gtk_label_set_label (GTK_LABEL (priv->status_label), gpk_status_enum_to_localised_text (status));
		gpk_animated_icon_set_filename_tile (GPK_ANIMATED_ICON (priv->status_icon),
						     GTK_ICON_SIZE_DIALOG,
						     gpk_status_enum_to_animation (status));
	}
}

/**
 * gpk_distro_upgrade_restart_response_cb:
 **/
static void
gpk_distro_upgrade_restart_response_cb (GtkDialog *dialog, gint response_id, GpkDistroUpgradePrivate *priv)
{
	gboolean ret;
	GError *error = NULL;

	/* restart */
	if (response_id == GTK_RESPONSE_OK) {
#ifdef HAVE_SYSTEMD
                ret = systemd_proxy_restart (priv->systemd_proxy, &error);
#else
		ret = egg_console_kit_restart (priv->console_kit, &error);
#endif
		if (!ret) {
			g_warning ("Cannot restart: %s", error->message);
			g_error_free (error);
		}
	}

	/* close  */
	gtk_widget_destroy (priv->assistant);
}

/**
 * gpk_distro_upgrade_upgrade_system_cb:
 **/
static void
gpk_distro_upgrade_upgrade_system_cb (PkClient *client, GAsyncResult *res, GpkDistroUpgradePrivate *priv)
{
	PkResults *results;
	GError *error = NULL;
	PkError *error_code = NULL;
	GtkWidget *dialog;
	gboolean ret;
	gboolean can_restart = FALSE;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		/* TRANSLATORS: the PackageKit request did not complete, and it did not send an error */
		gpk_error_dialog_modal (GTK_WINDOW (priv->assistant), _("Could not upgrade the system"), "", error->message);
		g_error_free (error);
		gtk_widget_destroy (priv->assistant);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to upgrade: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		gpk_error_dialog_modal (GTK_WINDOW (priv->assistant), gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
					gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		gtk_widget_destroy (priv->assistant);
		goto out;
	}

	/* modal restart dialog */
	dialog = gtk_message_dialog_new (GTK_WINDOW (priv->assistant),
					 GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_CLOSE,
					 "%s", _("The upgrade completed successfully"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s\n\n%s %s",
						  _("Your system now has the required software needed to complete the operating system upgrade."),
						  _("When you are ready, you can restart your system and continue the upgrade process."),
						  _("Make sure you have saved any unsaved work before restarting."));

#ifdef HAVE_SYSTEMD
        ret = systemd_proxy_can_restart (priv->systemd_proxy, &can_restart, &error);
#else
	/* check with ConsoleKit we can restart */
	ret = egg_console_kit_can_restart (priv->console_kit, &can_restart, &error);
#endif
	if (!ret) {
		g_warning ("cannot get consolekit CanRestart data: %s", error->message);
		g_error_free (error);
	}

	/* only add button if possible */
	if (can_restart) {
		/* TRANSLATORS: this is button text */
		gtk_dialog_add_button (GTK_DIALOG (dialog), _("Restart Now"), GTK_RESPONSE_OK);
	}
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gpk_distro_upgrade_restart_response_cb), priv);
	gtk_widget_show (dialog);
out:
	/* make the first entry highlighted */
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_distro_upgrade_assistant_apply_cb:
 **/
static void
gpk_distro_upgrade_assistant_apply_cb (GtkWidget *widget, GpkDistroUpgradePrivate *priv)
{
	GtkTreeIter iter;
	gchar *id;
	PkUpgradeKindEnum upgrade_kind = PK_UPGRADE_KIND_ENUM_MINIMAL;

	if (!pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_UPGRADE_SYSTEM)) {
		g_debug ("no support");
		return;
	}

	/* get the id we should update to */
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (priv->combobox), &iter);
	gtk_tree_model_get (GTK_TREE_MODEL (priv->distro_upgrade_store), &iter,
			    GPK_DISTRO_UPGRADE_COMBO_COLUMN_ID, &id,
			    -1);

	/* get the kind of upgrade */
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->radio_default)))
		upgrade_kind = PK_UPGRADE_KIND_ENUM_DEFAULT;
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->radio_complete)))
		upgrade_kind = PK_UPGRADE_KIND_ENUM_COMPLETE;

	/* upgrade */
	g_debug ("upgrade to %s", id);
	pk_client_upgrade_system_async (priv->client, id, upgrade_kind, priv->cancellable,
					(PkProgressCallback) gpk_distro_upgrade_progress_cb, priv,
					(GAsyncReadyCallback) gpk_distro_upgrade_upgrade_system_cb, priv);
	g_free (id);
}

/**
 * gpk_distro_upgrade_assistant_close_cancel_cb:
 **/
static void
gpk_distro_upgrade_assistant_close_cancel_cb (GtkWidget *widget, GpkDistroUpgradePrivate *priv)
{
	gtk_widget_destroy (priv->assistant);
	priv->assistant = NULL;
}

/**
 * gpk_distro_upgrade_add_item_to_list:
 **/
static void
gpk_distro_upgrade_add_item_to_list (GpkDistroUpgradePrivate *priv, PkDistroUpgrade *distro_upgrade)
{
	GtkTreeIter iter;
	const gchar *id = NULL;
	const gchar *summary = NULL;

	id = pk_distro_upgrade_get_id (distro_upgrade);
	summary = pk_distro_upgrade_get_summary (distro_upgrade);

	/* add item */
	gtk_list_store_append (priv->distro_upgrade_store, &iter);
	gtk_list_store_set (priv->distro_upgrade_store, &iter,
			    GPK_DISTRO_UPGRADE_COMBO_COLUMN_TEXT, summary,
			    GPK_DISTRO_UPGRADE_COMBO_COLUMN_ID, id,
			    -1);
}

/**
 * gpk_distro_upgrade_set_combobox_text:
 **/
static void
gpk_distro_upgrade_set_combobox_text (GpkDistroUpgradePrivate *priv, const gchar *text)
{
	GtkTreeIter iter;

	/* add item */
	gtk_list_store_append (priv->distro_upgrade_store, &iter);
	gtk_list_store_set (priv->distro_upgrade_store, &iter,
			    GPK_DISTRO_UPGRADE_COMBO_COLUMN_TEXT, text,
			    GPK_DISTRO_UPGRADE_COMBO_COLUMN_ID, NULL,
			    -1);
}

/**
 * gpk_distro_upgrade_get_distro_upgrades_cb:
 **/
static void
gpk_distro_upgrade_get_distro_upgrades_cb (PkClient *client, GAsyncResult *res, GpkDistroUpgradePrivate *priv)
{
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array = NULL;
	PkError *error_code = NULL;
	guint i;
	PkDistroUpgrade *distro_upgrade;
	gboolean show_unstable;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		/* TRANSLATORS: the PackageKit request did not complete, and it did not send an error */
		gpk_error_dialog_modal (GTK_WINDOW (priv->assistant), _("Could not get distribution upgrades"), "", error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_warning ("failed to get upgrades: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		gpk_error_dialog_modal (GTK_WINDOW (priv->assistant), gpk_error_enum_to_localised_text (pk_error_get_code (error_code)),
					gpk_error_enum_to_localised_message (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* remove loading entry */
	gtk_list_store_clear (priv->distro_upgrade_store);

	/* get data */
	array = pk_results_get_distro_upgrade_array (results);
	if (array->len == 0) {
		/* TRANSLATORS: nothing to do */
		gpk_distro_upgrade_set_combobox_text (priv, _("No releases available for upgrade"));
		gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant), priv->page_choose_vbox, FALSE);
		goto out;
	}

	/* only add relevant entries */
	show_unstable = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->checkbutton));
	for (i=0; i<array->len; i++) {
		distro_upgrade = g_ptr_array_index (array, i);
		if (show_unstable ||
		    pk_distro_upgrade_get_state (distro_upgrade) == PK_DISTRO_UPGRADE_ENUM_STABLE) {
			gpk_distro_upgrade_add_item_to_list (priv, distro_upgrade);
		}
	}
	gtk_widget_set_sensitive (GTK_WIDGET (priv->combobox), TRUE);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant), priv->page_choose_vbox, TRUE);
out:
	/* make the first entry highlighted */
	gtk_combo_box_set_active (GTK_COMBO_BOX (priv->combobox), 0);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * gpk_distro_upgrade_get_distro_upgrades:
 **/
static void
gpk_distro_upgrade_get_distro_upgrades (GpkDistroUpgradePrivate *priv)
{
	if (!pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		g_debug ("no support");
		return;
	}

	g_debug ("getting distro upgrades");

	/* get the details of all the packages */
	pk_client_get_distro_upgrades_async (priv->client, priv->cancellable,
					     (PkProgressCallback) gpk_distro_upgrade_progress_cb, priv,
					     (GAsyncReadyCallback) gpk_distro_upgrade_get_distro_upgrades_cb, priv);
}

/**
 * gpk_distro_upgrade_assistant_page_prepare_cb:
 **/
static void
gpk_distro_upgrade_assistant_page_prepare_cb (GtkWidget *widget, GtkWidget *page, GpkDistroUpgradePrivate *priv)
{
	gint current_page, n_pages;
	gchar *title;

	current_page = gtk_assistant_get_current_page (GTK_ASSISTANT (widget));
	n_pages = gtk_assistant_get_n_pages (GTK_ASSISTANT (widget));

	/* TRANSLATORS: this is the window title */
	title = g_strdup_printf (_("Upgrade your system (%d of %d)"), current_page + 1, n_pages);
	gtk_window_set_title (GTK_WINDOW (widget), title);
	g_free (title);

	if (current_page == GPK_DISTRO_UPGRADE_PAGE_CHOOSE) {
		/* reset to false */
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->checkbutton), FALSE);
		gpk_distro_upgrade_get_distro_upgrades (priv);
	}

	/* progress page */
	if (current_page == 3)
		gtk_assistant_commit (GTK_ASSISTANT (widget));
}

/**
 * gpk_distro_upgrade_create_page_introduction:
 **/
static void
gpk_distro_upgrade_create_page_introduction (GpkDistroUpgradePrivate *priv)
{
	GtkWidget *vbox, *box, *label;
	gchar *text;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

	/* TRANSLATORS: this is a intro page title */
	text = g_strdup_printf ("%s %s\n\n%s %s",
				_("This assistant will guide you through upgrading your currently installed operating system to a newer release."),
				_("This process may take several hours to complete, depending on the speed of your internet connection and the options selected."),
				_("You will be able to continue using your system while this assistant downloads the packages needed to upgrade your system."),
				_("When the download has completed, you will be prompted to restart your system in order to complete the upgrade process."));
	label = gtk_label_new (text);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	box = gtk_box_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), box, FALSE, FALSE, 0);
	g_free (text);

	gtk_widget_show_all (vbox);
	gtk_assistant_append_page (GTK_ASSISTANT (priv->assistant), vbox);

	/* TRANSLATORS: this is a intro page title */
	gtk_assistant_set_page_title (GTK_ASSISTANT (priv->assistant), vbox, _("Upgrade your system"));
	gtk_assistant_set_page_type (GTK_ASSISTANT (priv->assistant), vbox, GTK_ASSISTANT_PAGE_INTRO);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant), vbox, TRUE);
}

/**
 * gpk_distro_upgrade_set_combo_model:
 **/
static void
gpk_distro_upgrade_set_combo_model (GpkDistroUpgradePrivate *priv, GtkWidget *combo_box)
{
	GtkCellRenderer *renderer;

	priv->distro_upgrade_store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->distro_upgrade_store), GPK_DISTRO_UPGRADE_COMBO_COLUMN_ID, GTK_SORT_ASCENDING);
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo_box), GTK_TREE_MODEL (priv->distro_upgrade_store));
	g_object_unref (priv->distro_upgrade_store);

	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer,
		      "ellipsize", PANGO_ELLIPSIZE_NONE,
		      NULL);
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), renderer,
					"text", GPK_DISTRO_UPGRADE_COMBO_COLUMN_TEXT,
					NULL);
}

/**
 * gpk_distro_upgrade_unstable_checkbox_toggled_cb:
 **/
static void
gpk_distro_upgrade_unstable_checkbox_toggled_cb (GtkToggleButton *toggle_button, GpkDistroUpgradePrivate *priv)
{
	gpk_distro_upgrade_get_distro_upgrades (priv);
}

/**
 * gpk_distro_upgrade_create_page_choose:
 **/
static void
gpk_distro_upgrade_create_page_choose (GpkDistroUpgradePrivate *priv)
{
	GtkWidget *vbox, *subvbox, *box, *label;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

	/* label and combobox */
	subvbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	label = gtk_label_new_with_mnemonic (_("Available operating system _releases:"));
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	box = gtk_box_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (subvbox), box, FALSE, FALSE, 0);

	priv->combobox = gtk_combo_box_new ();
	gpk_distro_upgrade_set_combo_model (priv, priv->combobox);
	/* TRANSLATORS: this is in the combobox */
	gpk_distro_upgrade_set_combobox_text (priv, _("Loading list of upgrades"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (priv->combobox), 0);
	gtk_box_pack_start (GTK_BOX (subvbox), priv->combobox, TRUE, FALSE, 0);
	gtk_widget_set_sensitive (priv->combobox, FALSE);

	/* add both */
	gtk_box_pack_start (GTK_BOX (vbox), subvbox, FALSE, FALSE, 0);

	/* TRANSLATORS: this is a checkbox */
	priv->checkbutton = gtk_check_button_new_with_mnemonic ("Display _unstable test releases (e.g. alpha and beta releases)");
	gtk_box_pack_end (GTK_BOX (vbox), priv->checkbutton, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (priv->checkbutton), "toggled",
			  G_CALLBACK (gpk_distro_upgrade_unstable_checkbox_toggled_cb), priv);

	gtk_widget_show_all (vbox);
	gtk_assistant_append_page (GTK_ASSISTANT (priv->assistant), vbox);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant), vbox, FALSE);
	/* TRANSLATORS: this is a choose page title */
	gtk_assistant_set_page_title (GTK_ASSISTANT (priv->assistant), vbox, _("Choose desired operating system version"));

	/* we need this for later */
	priv->page_choose_vbox = vbox;
}

/**
 * gpk_distro_upgrade_additional_download_toggled_cb:
 **/
static void
gpk_distro_upgrade_additional_download_toggled_cb (GtkToggleButton *toggle_button, GpkDistroUpgradePrivate *priv)
{
	gtk_widget_set_visible (priv->info_bar,
				!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->radio_complete)));
}

/**
 * gpk_distro_upgrade_create_page_kind:
 **/
static void
gpk_distro_upgrade_create_page_kind (GpkDistroUpgradePrivate *priv)
{
	GtkWidget *vbox, *box, *label, *content_area, *message_label;
	gchar *text;
	GSList *group = NULL;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

	/* label and combobox */
	label = gtk_label_new (_("The upgrade tool can operate with three different modes:"));
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	box = gtk_box_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), box, FALSE, FALSE, 0);

	/* TRANSLATORS: this is a radio button */
	priv->radio_minimal = gtk_radio_button_new_with_mnemonic (group, "Download the smallest amount of data now.");
	g_signal_connect (G_OBJECT (priv->radio_minimal), "toggled",
			  G_CALLBACK (gpk_distro_upgrade_additional_download_toggled_cb), priv);
	gtk_box_pack_start (GTK_BOX (vbox), priv->radio_minimal, FALSE, FALSE, 0);
	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (priv->radio_minimal));

	/* TRANSLATORS: this is a radio button */
	priv->radio_default = gtk_radio_button_new_with_mnemonic (group, "Download the default amount of data.");
	g_signal_connect (G_OBJECT (priv->radio_default), "toggled",
			  G_CALLBACK (gpk_distro_upgrade_additional_download_toggled_cb), priv);
	gtk_box_pack_start (GTK_BOX (vbox), priv->radio_default, FALSE, FALSE, 0);
	group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (priv->radio_minimal));

	/* TRANSLATORS: this is a radio button */
	priv->radio_complete = gtk_radio_button_new_with_mnemonic (group, "Download all of the data the upgrade process is going to need.");
	g_signal_connect (G_OBJECT (priv->radio_complete), "toggled",
			  G_CALLBACK (gpk_distro_upgrade_additional_download_toggled_cb), priv);
	gtk_box_pack_start (GTK_BOX (vbox), priv->radio_complete, FALSE, FALSE, 0);

	/* TRANSLATORS: this is a checkbox */
	priv->info_bar = gtk_info_bar_new ();
	gtk_info_bar_set_message_type (GTK_INFO_BAR (priv->info_bar), GTK_MESSAGE_INFO);
	text = g_strdup_printf ("%s\n%s",
				_("The selected option will require the installer to download additional data."),
				_("Do not continue with this option if the network will not be available at upgrade time."));
	message_label = gtk_label_new (text);
	content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (priv->info_bar));
	gtk_container_add (GTK_CONTAINER (content_area), message_label);
	gtk_widget_show (message_label);
	g_free (text);
	gtk_box_pack_start (GTK_BOX (vbox), priv->info_bar, FALSE, FALSE, 0);

	/* use the default */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->radio_default), TRUE);

	gtk_widget_show_all (vbox);
	gtk_assistant_append_page (GTK_ASSISTANT (priv->assistant), vbox);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant), vbox, TRUE);
	/* TRANSLATORS: this is a choose page title */
	gtk_assistant_set_page_title (GTK_ASSISTANT (priv->assistant), vbox, _("Choose desired download options"));
}

/**
 * gpk_distro_upgrade_create_page_confirmation:
 **/
static void
gpk_distro_upgrade_create_page_confirmation (GpkDistroUpgradePrivate *priv)
{
	GtkWidget *vbox, *box, *label;
	gchar *text;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

	/* TRANSLATORS: this is the "are you sure" message */
	text = g_strdup_printf ("%s\n\n• %s\n• %s\n• %s\n• %s\n\n%s\n\n<b>%s</b>",
				_("The operating system upgrade tool will now perform the following actions:"),
				_("Request authentication from a privileged user"),
				_("Download installer images"),
				_("Download packages"),
				_("Prepare and test the upgrade"),
				_("You will have to restart your computer at the end of the upgrade."),
				_("Press 'Apply' to apply changes."));
	label = gtk_label_new (NULL);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_markup (GTK_LABEL (label), text);
	box = gtk_box_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), box, FALSE, FALSE, 0);
	g_free (text);

	gtk_widget_show_all (vbox);
	gtk_assistant_append_page (GTK_ASSISTANT (priv->assistant), vbox);

	/* TRANSLATORS: button text */
	gtk_assistant_set_page_title (GTK_ASSISTANT (priv->assistant), vbox, _("Confirmation"));
	gtk_assistant_set_page_type (GTK_ASSISTANT (priv->assistant), vbox, GTK_ASSISTANT_PAGE_CONFIRM);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant), vbox, TRUE);
}

/**
 * gpk_distro_upgrade_create_page_action:
 **/
static void
gpk_distro_upgrade_create_page_action (GpkDistroUpgradePrivate *priv)
{
	GtkWidget *vbox, *box;

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

	priv->status_icon = gpk_animated_icon_new ();
	priv->status_label = gtk_label_new ("This is where the status goes");
	gtk_label_set_line_wrap (GTK_LABEL (priv->status_label), TRUE);
	box = gtk_box_new (FALSE, 18);
	gtk_box_pack_start (GTK_BOX (box), priv->status_icon, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), priv->status_label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), box, FALSE, FALSE, 0);

	/* progress bar */
	priv->progress_bar = gtk_progress_bar_new ();
	gtk_box_pack_start (GTK_BOX (vbox), priv->progress_bar, FALSE, FALSE, 0);

	gtk_widget_show_all (vbox);
	gtk_assistant_append_page (GTK_ASSISTANT (priv->assistant), vbox);

	/* TRANSLATORS: title text */
	gtk_assistant_set_page_title (GTK_ASSISTANT (priv->assistant), vbox, _("Applying changes"));
	gtk_assistant_set_page_type (GTK_ASSISTANT (priv->assistant), vbox, GTK_ASSISTANT_PAGE_PROGRESS);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant), vbox, TRUE);

	/* prevent the assistant window from being closed while we're applying changes */
	gtk_assistant_set_page_complete (GTK_ASSISTANT (priv->assistant), vbox, FALSE);
}

/**
 * gpk_distro_upgrade_startup_cb:
 **/
static void
gpk_distro_upgrade_startup_cb (GtkApplication *application, GpkDistroUpgradePrivate *priv)
{
	/* create new objects */
	priv->assistant = gtk_assistant_new ();
	gtk_window_set_default_size (GTK_WINDOW (priv->assistant), 200, 100);
	gtk_window_set_icon_name (GTK_WINDOW (priv->assistant), GTK_STOCK_REFRESH);
	gpk_distro_upgrade_create_page_introduction (priv);
	gpk_distro_upgrade_create_page_choose (priv);
	gpk_distro_upgrade_create_page_kind (priv);
	gpk_distro_upgrade_create_page_confirmation (priv);
	gpk_distro_upgrade_create_page_action (priv);

	gtk_application_add_window (application, GTK_WINDOW (priv->assistant));

	g_signal_connect (G_OBJECT (priv->assistant), "cancel",
			  G_CALLBACK (gpk_distro_upgrade_assistant_close_cancel_cb), priv);
	g_signal_connect (G_OBJECT (priv->assistant), "close",
			  G_CALLBACK (gpk_distro_upgrade_assistant_close_cancel_cb), priv);
	g_signal_connect (G_OBJECT (priv->assistant), "apply",
			  G_CALLBACK (gpk_distro_upgrade_assistant_apply_cb), priv);
	g_signal_connect (G_OBJECT (priv->assistant), "prepare",
			  G_CALLBACK (gpk_distro_upgrade_assistant_page_prepare_cb), priv);
}

/**
 * gpk_distro_upgrade_activate_cb:
 **/
static void
gpk_distro_upgrade_activate_cb (GApplication *application, GpkDistroUpgradePrivate *priv)
{
	PkControl *control;
	gboolean ret;
	const gchar *title = NULL;
	const gchar *message = NULL;
	GError *error = NULL;

	/* just show the window */
	g_debug ("activated");
	gtk_widget_show (priv->assistant);

	/* get the properties of the daemon */
	control = pk_control_new ();
	ret = pk_control_get_properties (control, NULL, &error);
	if (!ret) {
		g_error ("Failed to contact PackageKit: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* TRANSLATORS: title, we're unable to do this action */
	title = _("Cannot perform operating system upgrade");

	/* check we can do the upgrade */
	g_object_get (control,
		      "roles", &priv->roles,
		      NULL);
	ret = pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);
	if (ret) {
		ret = pk_bitfield_contain (priv->roles, PK_ROLE_ENUM_UPGRADE_SYSTEM);
		if (!ret) {
			/* TRANSLATORS: message, we're unable to do this action */
			message = _("Upgrading the operating system is not supported.");
		}
	} else {
		/* TRANSLATORS: message, we're unable to do this action as PackageKit is too old */
		message = _("Cannot get operating system upgrade information.");
	}
	if (!ret) {
		GtkWidget *dialog;
		dialog = gtk_message_dialog_new (GTK_WINDOW (priv->assistant),
						 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_WARNING,
						 GTK_BUTTONS_CLOSE,
						 "%s", title);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  "%s", message);
		g_signal_connect_swapped (dialog,
					  "response",
					  G_CALLBACK (g_application_release),
					  application);
		gtk_widget_show (dialog);
	}
out:
	return;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GtkApplication *application = NULL;
	gint status = 0;
	gboolean ret;
	GpkDistroUpgradePrivate *priv;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	g_type_init ();
	gtk_init (&argc, &argv);
	priv = g_new0 (GpkDistroUpgradePrivate, 1);
#ifdef HAVE_SYSTEMD
        priv->systemd_proxy = systemd_proxy_new ();
#else
	priv->console_kit = egg_console_kit_new ();
#endif
	priv->cancellable = g_cancellable_new ();
	priv->client = pk_client_new ();
	g_object_set (priv->client,
		      "background", FALSE,
		      NULL);

	/* TRANSLATORS: program name, a session wide daemon to watch for updates and changing system state */
	g_set_application_name (_("Distribution Upgrade Tool"));

	/* TRANSLATORS: title to pass to to the user if there are not enough privs */
	ret = gpk_check_privileged_user (_("Distribution Upgrade Tool"), FALSE);
	if (!ret) {
		status = 1;
		g_warning ("Exit: gpk_check_privileged_user returned FALSE");
		goto out;
	}

	/* are we already activated? */
	application = gtk_application_new ("org.freedesktop.PackageKit.DistroUpgrade",
					   0);
	g_signal_connect (application, "startup",
			  G_CALLBACK (gpk_distro_upgrade_startup_cb), priv);
	g_signal_connect (application, "activate",
			  G_CALLBACK (gpk_distro_upgrade_activate_cb), priv);

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* run */
	status = g_application_run (G_APPLICATION (application), argc, argv);
out:
	g_object_unref (priv->cancellable);
	g_object_unref (priv->client);
#ifdef HAVE_SYSTEMD
        systemd_proxy_free (priv->systemd_proxy);
#else
	g_object_unref (priv->console_kit);
#endif
	g_free (priv);
        if (application)
                g_object_unref (application);
	return status;
}

