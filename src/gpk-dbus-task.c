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

#include <unistd.h>
#include <string.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <fontconfig/fontconfig.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <libnotify/notify.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "gpk-dbus.h"
#include "gpk-dbus-task.h"
#include "gpk-modal-dialog.h"
#include "gpk-common.h"
#include "gpk-gnome.h"
#include "gpk-error.h"
#include "gpk-language.h"
#include "gpk-dialog.h"
#include "gpk-vendor.h"
#include "gpk-enum.h"
#include "gpk-x11.h"
#include "gpk-desktop.h"
#include "gpk-helper-repo-signature.h"
#include "gpk-helper-eula.h"
#include "gpk-helper-run.h"
#include "gpk-helper-untrusted.h"
#include "gpk-helper-chooser.h"

static void     gpk_dbus_task_finalize	(GObject	*object);

#define GPK_DBUS_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_DBUS_TASK, GpkDbusTaskPrivate))
#define GPK_DBUS_TASK_FINISHED_AUTOCLOSE_DELAY	10 /* seconds */

typedef enum {
	GPK_DBUS_TASK_ROLE_IS_INSTALLED,
	GPK_DBUS_TASK_ROLE_SEARCH_FILE,
	GPK_DBUS_TASK_ROLE_INSTALL_PACKAGE_FILES,
	GPK_DBUS_TASK_ROLE_INSTALL_PROVIDE_FILES,
	GPK_DBUS_TASK_ROLE_INSTALL_MIME_TYPES,
	GPK_DBUS_TASK_ROLE_INSTALL_GSTREAMER_RESOURCES,
	GPK_DBUS_TASK_ROLE_INSTALL_FONTCONFIG_RESOURCES,
	GPK_DBUS_TASK_ROLE_INSTALL_PACKAGE_NAMES,
	GPK_DBUS_TASK_ROLE_INSTALL_CATALOGS,
	GPK_DBUS_TASK_ROLE_UNKNOWN
} GpkDbusTaskRole;

/**
 * GpkDbusTaskPrivate:
 *
 * Private #GpkDbusTask data
 **/
struct _GpkDbusTaskPrivate
{
	GdkWindow		*parent_window;
	GConfClient		*gconf_client;
	PkClient		*client_primary;
	PkClient		*client_secondary;
	PkDesktop		*desktop;
	PkControl		*control;
	PkExitEnum		 exit;
	PkBitfield		 roles;
	GpkLanguage		*language;
	GpkModalDialog		*dialog;
	GpkVendor		*vendor;
	gboolean		 show_confirm_search;
	gboolean		 show_confirm_deps;
	gboolean		 show_confirm_install;
	gboolean		 show_progress;
	gboolean		 show_finished;
	gboolean		 show_warning;
	guint			 timestamp;
	gchar			*parent_title;
	gchar			*parent_icon_name;
	gchar			*error_details;
	gint			 timeout;
	GpkHelperRepoSignature	*helper_repo_signature;
	GpkHelperEula		*helper_eula;
	GpkHelperRun		*helper_run;
#if (!PK_CHECK_VERSION(0,5,0))
	GpkHelperUntrusted	*helper_untrusted;
#endif
	GpkHelperChooser	*helper_chooser;
	DBusGMethodInvocation	*context;
	GpkDbusTaskRole		 role;
	gchar			**package_ids;
	gchar			**files;
	PkErrorCodeEnum		 last_exit_code;
};

G_DEFINE_TYPE (GpkDbusTask, gpk_dbus_task, G_TYPE_OBJECT)

/**
 * gpk_dbus_task_set_interaction:
 **/
gboolean
gpk_dbus_task_set_interaction (GpkDbusTask *task, PkBitfield interact)
{
	g_return_val_if_fail (GPK_IS_DBUS_TASK (task), FALSE);

	task->priv->show_confirm_search = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_CONFIRM_SEARCH);
	task->priv->show_confirm_deps = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_CONFIRM_DEPS);
	task->priv->show_confirm_install = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_CONFIRM_INSTALL);
	task->priv->show_progress = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_PROGRESS);
	task->priv->show_finished = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_FINISHED);
	task->priv->show_warning = pk_bitfield_contain (interact, GPK_CLIENT_INTERACT_WARNING);

	/* debug */
	egg_debug ("confirm_search:%i, confirm_deps:%i, confirm_install:%i, progress:%i, finished:%i, warning:%i",
		   task->priv->show_confirm_search, task->priv->show_confirm_deps,
		   task->priv->show_confirm_install, task->priv->show_progress,
		   task->priv->show_finished, task->priv->show_warning);

	return TRUE;
}

/**
 * gpk_dbus_task_set_context:
 **/
gboolean
gpk_dbus_task_set_context (GpkDbusTask *task, DBusGMethodInvocation *context)
{
	g_return_val_if_fail (GPK_IS_DBUS_TASK (task), FALSE);
	g_return_val_if_fail (context != NULL, FALSE);

	task->priv->context = context;
	return TRUE;
}

/**
 * gpk_dbus_task_set_timeout:
 **/
gboolean
gpk_dbus_task_set_timeout (GpkDbusTask *task, gint timeout)
{
	g_return_val_if_fail (GPK_IS_DBUS_TASK (task), FALSE);

	task->priv->timeout = timeout;
	return TRUE;
}

/**
 * gpk_dbus_task_set_xid:
 **/
gboolean
gpk_dbus_task_set_xid (GpkDbusTask *task, guint32 xid)
{
	GdkDisplay *display;
	g_return_val_if_fail (GPK_IS_DBUS_TASK (task), FALSE);

	display = gdk_display_get_default ();
	task->priv->parent_window = gdk_window_foreign_new_for_display (display, xid);
	egg_debug ("parent_window=%p", task->priv->parent_window);
	gpk_modal_dialog_set_parent (task->priv->dialog, task->priv->parent_window);
	return TRUE;
}

/**
 * gpk_dbus_task_set_timestamp:
 **/
gboolean
gpk_dbus_task_set_timestamp (GpkDbusTask *task, guint32 timestamp)
{
	g_return_val_if_fail (GPK_IS_DBUS_TASK (task), FALSE);
	task->priv->timestamp = timestamp;
	return TRUE;
}

/**
 * gpk_dbus_task_repo_signature_event_cb:
 **/
static void
gpk_dbus_task_repo_signature_event_cb (GpkHelperRepoSignature *helper_repo_signature, GtkResponseType type, const gchar *key_id, const gchar *package_id, GpkDbusTask *task)
{
	gboolean ret;
	GError *error = NULL;

	if (type != GTK_RESPONSE_YES) {
		goto out;
	}

	/* reset client */
	ret = pk_client_reset (task->priv->client_secondary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* install signature */
	ret = pk_client_install_signature (task->priv->client_secondary, PK_SIGTYPE_ENUM_GPG, key_id, package_id, &error);
	if (!ret) {
		egg_warning ("cannot install signature: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return;
}

/**
 * gpk_dbus_task_eula_event_cb:
 **/
static void
gpk_dbus_task_eula_event_cb (GpkHelperEula *helper_eula, GtkResponseType type, const gchar *eula_id, GpkDbusTask *task)
{
	gboolean ret;
	GError *error = NULL;

	if (type != GTK_RESPONSE_YES) {
		goto out;
	}

	/* reset client */
	ret = pk_client_reset (task->priv->client_secondary, &error);
	if (!ret) {
		egg_warning ("cannot reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* install signature */
	ret = pk_client_accept_eula (task->priv->client_secondary, eula_id, &error);
	if (!ret) {
		egg_warning ("cannot accept eula: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return;
}

/**
 * gpk_dbus_task_eula_cb:
 **/
static void
gpk_dbus_task_eula_required_cb (PkClient *client, const gchar *eula_id, const gchar *package_id,
				    const gchar *vendor_name, const gchar *license_agreement, GpkDbusTask *task)
{
	g_return_if_fail (GPK_IS_DBUS_TASK (task));

	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_CUSTOM, pk_bitfield_value (GPK_MODAL_DIALOG_WIDGET_PROGRESS_BAR));
	gpk_modal_dialog_set_title (task->priv->dialog, _("EULA required"));
	gpk_modal_dialog_set_image_status (task->priv->dialog, PK_STATUS_ENUM_SIG_CHECK);
	gpk_modal_dialog_set_percentage (task->priv->dialog, 101);
	if (task->priv->show_progress)
		gpk_modal_dialog_present (task->priv->dialog);

	/* use the helper */
	gpk_helper_eula_show (task->priv->helper_eula, eula_id, package_id, vendor_name, license_agreement);
}

/**
 * gpk_dbus_task_repo_signature_required_cb:
 **/
static void
gpk_dbus_task_repo_signature_required_cb (PkClient *client, const gchar *package_id, const gchar *repository_name,
					      const gchar *key_url, const gchar *key_userid, const gchar *key_id,
					      const gchar *key_fingerprint, const gchar *key_timestamp,
					      PkSigTypeEnum type, GpkDbusTask *task)
{
	g_return_if_fail (GPK_IS_DBUS_TASK (task));

	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_CUSTOM, pk_bitfield_value (GPK_MODAL_DIALOG_WIDGET_PROGRESS_BAR));
	gpk_modal_dialog_set_title (task->priv->dialog, _("Signature required"));
	gpk_modal_dialog_set_image_status (task->priv->dialog, PK_STATUS_ENUM_SIG_CHECK);
	gpk_modal_dialog_set_percentage (task->priv->dialog, 101);
	if (task->priv->show_progress)
		gpk_modal_dialog_present (task->priv->dialog);

	/* use the helper */
	gpk_helper_repo_signature_show (task->priv->helper_repo_signature, package_id, repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp);
}

static void gpk_dbus_task_install_package_files_internal (GpkDbusTask *task, gboolean trusted);

#if (!PK_CHECK_VERSION(0,5,0))
/**
 * gpk_dbus_task_untrusted_event_cb:
 **/
static void
gpk_dbus_task_untrusted_event_cb (GpkHelperUntrusted *helper_untrusted, GtkResponseType type, GpkDbusTask *task)
{
	GError *error = NULL;
	const gchar *title;

	if (type != GTK_RESPONSE_YES) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to download");
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	title = _("Install untrusted");
	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	gpk_modal_dialog_set_title (task->priv->dialog, title);
	if (task->priv->show_progress)
		gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);

	/* install trusted */
	gpk_dbus_task_install_package_files_internal (task, FALSE);
out:
	if (error != NULL)
		g_error_free (error);
	return;
}
#endif

static void gpk_dbus_task_install_package_ids_dep_check (GpkDbusTask *task);

/**
 * gpk_dbus_task_chooser_event_cb:
 **/
static void
gpk_dbus_task_chooser_event_cb (GpkHelperChooser *helper_chooser, GtkResponseType type, const gchar *package_id, GpkDbusTask *task)
{
	GError *error = NULL;

	/* selected nothing */
	if (type != GTK_RESPONSE_YES || package_id == NULL) {

		/* failed */
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not choose anything to install");
		dbus_g_method_return_error (task->priv->context, error);

		if (task->priv->show_warning) {
			gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			/* TRANSLATORS: we failed to install */
			gpk_modal_dialog_set_title (task->priv->dialog, _("Failed to install software"));
			/* TRANSLATORS: we didn't select any applications that were returned */
			gpk_modal_dialog_set_message (task->priv->dialog, _("No applications were chosen to be installed"));
			gpk_modal_dialog_present (task->priv->dialog);
			gpk_modal_dialog_run (task->priv->dialog);
		}
		goto out;
	}

	/* install this specific package */
	task->priv->package_ids = pk_package_ids_from_id (package_id);

	/* install these packages with deps */
	gpk_dbus_task_install_package_ids_dep_check (task);

out:
	if (error != NULL)
		g_error_free (error);
	return;
}

/**
 * gpk_dbus_task_libnotify_cb:
 **/
static void
gpk_dbus_task_libnotify_cb (NotifyNotification *notification, gchar *action, gpointer data)
{
	GpkDbusTask *task = GPK_DBUS_TASK (data);

	if (egg_strequal (action, "show-error-details")) {
		/* TRANSLATORS: detailed text about the error */
		gpk_error_dialog (_("Error details"), _("Package Manager error details"), task->priv->error_details);
	} else {
		egg_warning ("unknown action id: %s", action);
	}
}

/**
 * gpk_dbus_task_error_msg:
 **/
static void
gpk_dbus_task_error_msg (GpkDbusTask *task, const gchar *title, GError *error)
{
	GtkWindow *window;
	/* TRANSLATORS: default fallback error -- this should never happen */
	const gchar *message = _("Unknown error. Please refer to the detailed report and report in your distribution bugtracker.");
	const gchar *details = NULL;

	if (!task->priv->show_warning)
		return;

	/* setup UI */
	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);

	/* print a proper error if we have it */
	if (error != NULL) {
		if (error->code == PK_CLIENT_ERROR_FAILED_AUTH ||
		    g_str_has_prefix (error->message, "org.freedesktop.packagekit.")) {
			/* TRANSLATORS: failed authentication */
			message = _("You don't have the necessary privileges to perform this action.");
			gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-permissions");
		} else if (error->code == PK_CLIENT_ERROR_CANNOT_START_DAEMON) {
			/* TRANSLATORS: could not start system service */
			message = _("The packagekitd service could not be started.");
			gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-no-service");
		} else if (error->code == PK_CLIENT_ERROR_INVALID_INPUT) {
			/* TRANSLATORS: the user tried to query for something invalid */
			message = _("The query is not valid.");
			details = error->message;
		} else if (error->code == PK_CLIENT_ERROR_INVALID_FILE) {
			/* TRANSLATORS: the user tried to install a file that was not compatable or broken */
			message = _("The file is not valid.");
			details = error->message;
		} else {
			details = error->message;
		}
	}

	/* it's a normal UI, not a backtrace so keep in the UI */
	if (details == NULL) {
		gpk_modal_dialog_set_title (task->priv->dialog, title);
		gpk_modal_dialog_set_message (task->priv->dialog, message);
		gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
		gpk_modal_dialog_run (task->priv->dialog);
		return;
	}

	/* hide the main window */
	window = gpk_modal_dialog_get_window (task->priv->dialog);
	gpk_error_dialog_modal_with_time (window, title, message, details, task->priv->timestamp);
}

/**
 * gpk_dbus_task_install_package_ids:
 * @task: a valid #GpkDbusTask instance
 * @package_id: a package_id such as <literal>hal-info;0.20;i386;fedora</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Return value: %TRUE if the method succeeded
 **/
static void
gpk_dbus_task_install_package_ids (GpkDbusTask *task)
{
	gboolean ret;
	GError *error = NULL;
	GError *error_local = NULL;

	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, GPK_MODAL_DIALOG_PACKAGE_PADDING);
	/* TRANSLATORS: title: installing packages */
	gpk_modal_dialog_set_title (task->priv->dialog, _("Installing packages"));
	if (task->priv->show_progress)
		gpk_modal_dialog_present (task->priv->dialog);

	/* reset */
	ret = pk_client_reset (task->priv->client_primary, &error_local);
	if (!ret) {
		/* TRANSLATORS: this should never happen, low level failure */
		gpk_dbus_task_error_msg (task, _("Failed to reset client to perform action"), error_local);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* set timeout */
	pk_client_set_timeout (task->priv->client_primary, task->priv->timeout, NULL);

#if PK_CHECK_VERSION(0,5,0)
	ret = pk_client_install_packages (task->priv->client_primary, TRUE, task->priv->package_ids, &error_local);
#else
	ret = pk_client_install_packages (task->priv->client_primary, task->priv->package_ids, &error_local);
#endif
	if (!ret) {
		/* TRANSLATORS: error: failed to install, detailed error follows */
		gpk_dbus_task_error_msg (task, _("Failed to install package"), error_local);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}
out:
	if (error != NULL)
		g_error_free (error);
	if (error_local != NULL)
		g_error_free (error_local);
}

/**
 * gpk_dbus_task_install_package_ids_dep_check:
 * @task: a valid #GpkDbusTask instance
 * @package_id: a package_id such as <literal>hal-info;0.20;i386;fedora</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Return value: %TRUE if the method succeeded
 **/
static void
gpk_dbus_task_install_package_ids_dep_check (GpkDbusTask *task)
{
	gboolean ret;
	GError *error = NULL;
	GError *error_local = NULL;

	g_return_if_fail (GPK_IS_DBUS_TASK (task));
	g_return_if_fail (task->priv->package_ids != NULL);

	/* are we dumb and can't check for depends? */
	if (!pk_bitfield_contain (task->priv->roles, PK_ROLE_ENUM_GET_DEPENDS)) {
		egg_warning ("skipping depends check");
		gpk_dbus_task_install_package_ids (task);
		goto out;
	}

	/* have we previously said we don't want to be shown the confirmation */
	ret = gconf_client_get_bool (task->priv->gconf_client, GPK_CONF_SHOW_DEPENDS, NULL);
	if (!ret) {
		egg_warning ("we've said we don't want the dep dialog");
		gpk_dbus_task_install_package_ids (task);
		goto out;
	}

	/* optional */
	if (!task->priv->show_confirm_deps) {
		egg_warning ("skip confirm as not allowed to interact with user");
		gpk_dbus_task_install_package_ids (task);
		goto out;
	}

	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, GPK_MODAL_DIALOG_PACKAGE_PADDING);
	/* TRANSLATORS: finding a list of packages that we would also need to download */
	gpk_modal_dialog_set_title (task->priv->dialog, _("Finding other packages we require"));
	gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-finding-depends");

	/* setup the UI */
	if (task->priv->show_progress)
		gpk_modal_dialog_present (task->priv->dialog);

	/* reset */
	ret = pk_client_reset (task->priv->client_primary, &error_local);
	if (!ret) {
		/* TRANSLATORS: this is an internal error, and should not be seen */
		gpk_dbus_task_error_msg (task, _("Failed to reset client"), error_local);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* set timeout */
	pk_client_set_timeout (task->priv->client_primary, task->priv->timeout, NULL);

	/* find out if this would drag in other packages */
	ret = pk_client_get_depends (task->priv->client_primary, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), task->priv->package_ids, TRUE, &error_local);
	if (!ret) {
		/* TRANSLATORS: error: could not get the extra package list when installing a package */
		gpk_dbus_task_error_msg (task, _("Could not work out what packages would be also installed"), error_local);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* wait for async reply */

out:
	if (error != NULL)
		g_error_free (error);
	if (error_local != NULL)
		g_error_free (error_local);
}

/**
 * gpk_dbus_task_error_from_exit_enum:
 **/
static GError *
gpk_dbus_task_error_from_exit_enum (PkExitEnum exit)
{
	GError *error = NULL;

	/* trivial case */
	if (exit == PK_EXIT_ENUM_SUCCESS)
		goto out;

	/* set the correct error type */
	if (exit == PK_EXIT_ENUM_FAILED)
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "Unspecified failure");
	else if (exit == PK_EXIT_ENUM_CANCELLED)
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "Transaction was cancelled");
	else if (exit == PK_EXIT_ENUM_KEY_REQUIRED)
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "A key was required but not provided");
	else if (exit == PK_EXIT_ENUM_EULA_REQUIRED)
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "A EULA was not agreed to");
	else if (exit == PK_EXIT_ENUM_KILLED)
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "The transaction was killed");
	else
		egg_error ("unknown exit code");
out:
	return error;
}

/**
 * gpk_dbus_task_install_package_ids_dep_check_idle_cb:
 **/
static gboolean
gpk_dbus_task_install_package_ids_dep_check_idle_cb (GpkDbusTask *task)
{
	egg_warning ("idle add install package ids dep check");
	gpk_dbus_task_install_package_ids_dep_check (task);
	return FALSE;
}

/**
 * gpk_dbus_task_install_package_ids_idle_cb:
 **/
static gboolean
gpk_dbus_task_install_package_ids_idle_cb (GpkDbusTask *task)
{
	egg_warning ("idle add install package ids");
	gpk_dbus_task_install_package_ids (task);
	return FALSE;
}

/**
 * gpk_dbus_task_finished_cb:
 **/
static void
gpk_dbus_task_finished_cb (PkClient *client, PkExitEnum exit_enum, guint runtime, GpkDbusTask *task)
{
	PkRoleEnum role = PK_ROLE_ENUM_UNKNOWN;
	gboolean ret;
	guint len;
	guint i;
	const gchar *name = NULL;
	GError *error = NULL;
	GError *error_local = NULL;
	PkPackageList *list = NULL;
	const PkPackageObj *obj;
	gboolean already_installed = FALSE;
	gchar *title = NULL;
	gchar *message = NULL;
	gchar *text = NULL;
	PkPackageId *id = NULL;
	gchar *info_url = NULL;
	GtkResponseType button;
	gchar *package_id = NULL;

	/* get role */
	pk_client_get_role (client, &role, NULL, NULL);
	egg_debug ("role: %s, exit: %s", pk_role_enum_to_text (role), pk_exit_enum_to_text (exit_enum));

	/* stop spinning */
	gpk_modal_dialog_set_percentage (task->priv->dialog, 100);

	/* we failed because we're handling the auth, just ignore */
	if (client == task->priv->client_primary &&
	    (exit_enum == PK_EXIT_ENUM_KEY_REQUIRED ||
	     exit_enum == PK_EXIT_ENUM_EULA_REQUIRED)) {
		egg_debug ("ignoring primary sig-required or eula");
		goto out;
	}

	/* need to handle retry with only_trusted=FALSE */
	if (client == task->priv->client_primary &&
	    exit_enum == PK_EXIT_ENUM_NEED_UNTRUSTED) {
		egg_debug ("need to handle untrusted");
		pk_client_set_only_trusted (client, FALSE);

		/* try again */
		ret = pk_client_requeue (task->priv->client_primary, &error_local);
		if (!ret) {
			egg_warning ("Failed to requeue: %s", error_local->message);
			error = g_error_new (GPK_DBUS_ERROR, PK_ERROR_ENUM_INTERNAL_ERROR, "cannot requeue: %s", error_local->message);
			dbus_g_method_return_error (task->priv->context, error);
		}
		goto out;
	}

	/* EULA or GPG key auth done */
	if (client == task->priv->client_secondary &&
	    exit_enum == PK_EXIT_ENUM_SUCCESS) {

		/* try again */
		ret = pk_client_requeue (task->priv->client_primary, &error_local);
		if (!ret) {
			egg_warning ("Failed to requeue: %s", error_local->message);
			error = g_error_new (GPK_DBUS_ERROR, PK_ERROR_ENUM_INTERNAL_ERROR, "cannot requeue: %s", error_local->message);
			dbus_g_method_return_error (task->priv->context, error);
		}
		goto out;
	}

	if (exit_enum != PK_EXIT_ENUM_SUCCESS) {

#if (!PK_CHECK_VERSION(0,5,0))
		/* we failed because of failed exit code */
		ret = pk_error_code_is_need_untrusted (task->priv->last_exit_code);
		if (ret) {
			egg_debug ("showing untrusted ui");
			gpk_helper_untrusted_show (task->priv->helper_untrusted, task->priv->last_exit_code);
			goto out;
		}
#endif

		/* show finished? */
		if (!task->priv->show_finished)
			gpk_modal_dialog_close (task->priv->dialog);

		/* fail the transaction and set the correct error */
		error = gpk_dbus_task_error_from_exit_enum (exit_enum);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* from InstallMimeTypes */
	if (task->priv->role == GPK_DBUS_TASK_ROLE_INSTALL_MIME_TYPES &&
	    role == PK_ROLE_ENUM_WHAT_PROVIDES) {

		/* found nothing? */
		list = pk_client_get_package_list (task->priv->client_primary);
		len = pk_package_list_get_size (list);
		if (len == 0) {
			if (task->priv->show_warning) {
				info_url = gpk_vendor_get_not_found_url (task->priv->vendor, GPK_VENDOR_URL_TYPE_MIME);
				/* only show the "more info" button if there is a valid link */
				if (info_url != NULL)
					gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, GPK_MODAL_DIALOG_BUTTON_ACTION);
				else
					gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
				/* TRANSLATORS: title */
				gpk_modal_dialog_set_title (task->priv->dialog, _("Failed to find software"));
				/* TRANSLATORS: nothing found in the software sources that helps */
				gpk_modal_dialog_set_message (task->priv->dialog, _("No new applications can be found to handle this type of file"));
				gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-package-not-found");
				/* TRANSLATORS: button: show the user a button to get more help finding stuff */
				gpk_modal_dialog_set_action (task->priv->dialog, _("More information"));
				gpk_modal_dialog_present (task->priv->dialog);
				button = gpk_modal_dialog_run (task->priv->dialog);
				if (button == GTK_RESPONSE_OK)
					gpk_gnome_open (info_url);
				g_free (info_url);
			}
			error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_NO_PACKAGES_FOUND, "nothing was found to handle mime type");
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}

		/* populate a chooser */
		gpk_helper_chooser_show (task->priv->helper_chooser, list);
		goto out;
	}

	/* from InstallProvideFiles */
	if (task->priv->role == GPK_DBUS_TASK_ROLE_INSTALL_PROVIDE_FILES &&
	    role == PK_ROLE_ENUM_SEARCH_FILE) {
		/* found nothing? */
		list = pk_client_get_package_list (task->priv->client_primary);
		len = pk_package_list_get_size (list);
		if (len == 0) {
			if (task->priv->show_warning) {
				info_url = gpk_vendor_get_not_found_url (task->priv->vendor, GPK_VENDOR_URL_TYPE_DEFAULT);
				/* only show the "more info" button if there is a valid link */
				if (info_url != NULL)
					gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, GPK_MODAL_DIALOG_BUTTON_ACTION);
				else
					gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
				/* TRANSLATORS: failed to fild the package for thefile */
				gpk_modal_dialog_set_title (task->priv->dialog, _("Failed to find package"));
				/* TRANSLATORS: nothing found */
				gpk_modal_dialog_set_message (task->priv->dialog, _("The file could not be found in any packages"));
				gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-package-not-found");
				/* TRANSLATORS: button: show the user a button to get more help finding stuff */
				gpk_modal_dialog_set_action (task->priv->dialog, _("More information"));
				gpk_modal_dialog_present (task->priv->dialog);
				button = gpk_modal_dialog_run (task->priv->dialog);
				if (button == GTK_RESPONSE_OK)
					gpk_gnome_open (info_url);
				g_free (info_url);
			}
			error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_NO_PACKAGES_FOUND, "no files found");
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}

		/* see what we've got already */
		for (i=0; i<len; i++) {
			obj = pk_package_list_get_obj (list, i);
			if (obj->info == PK_INFO_ENUM_INSTALLED) {
				already_installed = TRUE;
				id = obj->id;
			} else if (obj->info == PK_INFO_ENUM_AVAILABLE) {
				egg_debug ("package '%s' resolved to:", obj->id->name);
				id = obj->id;
			}
		}

		/* already installed? */
		if (already_installed) {
			if (task->priv->show_warning) {
				/* TRANSLATORS: we've already got a package that provides this file */
				text = g_strdup_printf (_("The %s package already provides this file"), id->name);
				gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
				/* TRANSLATORS: title */
				gpk_modal_dialog_set_title (task->priv->dialog, _("Failed to install file"));
				gpk_modal_dialog_set_message (task->priv->dialog, text);
				gpk_modal_dialog_present (task->priv->dialog);
				gpk_modal_dialog_run (task->priv->dialog);
				g_free (text);
			}
			error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "already provided");
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}

		/* install this specific package */
		package_id = pk_package_id_to_string (id);
		/* convert to data */
		task->priv->package_ids = pk_package_ids_from_id (package_id);
		g_free (package_id);

		/* install these packages with deps */
		g_idle_add ((GSourceFunc) gpk_dbus_task_install_package_ids_dep_check_idle_cb, task);
		goto out;
	}

	/* from InstallPackageIds */
	if (role == PK_ROLE_ENUM_GET_DEPENDS) {
		/* these are the new packages */
		list = pk_client_get_package_list (task->priv->client_primary);
		len = pk_package_list_get_size (list);
		if (len == 0) {
			egg_debug ("no deps");
			goto skip_checks;
		}

		/* TRANSLATORS: title: tell the user we have to install additional packages */
		title = g_strdup_printf (ngettext ("%i additional package also has to be installed",
						   "%i additional packages also have to be installed",
						   len), len);

		/* message */
		text = gpk_dialog_package_id_name_join_locale (task->priv->package_ids);
		/* TRANSLATORS: message: explain to the user what we are doing in more detail */
		message = g_strdup_printf (ngettext ("To install %s, an additional package also has to be downloaded.",
						     "To install %s, additional packages also have to be downloaded.",
						     len), text);
		g_free (text);

		gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, GPK_MODAL_DIALOG_PACKAGE_LIST);
		gpk_modal_dialog_set_package_list (task->priv->dialog, list);
		gpk_modal_dialog_set_title (task->priv->dialog, title);
		gpk_modal_dialog_set_message (task->priv->dialog, message);
		/* TRANSLATORS: title: installing package */
		gpk_modal_dialog_set_action (task->priv->dialog, _("Install"));
		gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-install-other-packages");
		gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
		button = gpk_modal_dialog_run (task->priv->dialog);

		/* did we click no or exit the window? */
		if (button != GTK_RESPONSE_OK) {
			gpk_modal_dialog_close (task->priv->dialog);
			error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to additional deps");
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}
skip_checks:
		g_idle_add ((GSourceFunc) gpk_dbus_task_install_package_ids_idle_cb, task);
	}

	/* from InstallPackageIds */
	if (role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    role == PK_ROLE_ENUM_INSTALL_FILES) {

		/* show summary? */
		if (task->priv->show_finished) {
			list = pk_client_get_package_list (client);
			/* TRANSLATORS: list the packages we just installed */
			gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_FINISHED, GPK_MODAL_DIALOG_PACKAGE_LIST);
			gpk_modal_dialog_set_message (task->priv->dialog, _("The following packages were installed:"));

			/* filter out installed */
			for (i=0; i<PK_OBJ_LIST(list)->len; i++) {
				obj = pk_obj_list_index (PK_OBJ_LIST (list), i);
				if (obj->info != PK_INFO_ENUM_INSTALLING) {
					pk_obj_list_remove_index (PK_OBJ_LIST (list), i);
					i--;
				}
			}
			gpk_modal_dialog_set_package_list (task->priv->dialog, list);
			gpk_modal_dialog_present (task->priv->dialog);
			g_object_unref (list);
		} else {
			gpk_modal_dialog_close (task->priv->dialog);
		}

		/* done! */
		egg_warning ("doing async return");
		dbus_g_method_return (task->priv->context, TRUE); /* FIXME: we send true? */
		goto out;
	}

	/* IsInstalled */
	if (task->priv->role == GPK_DBUS_TASK_ROLE_IS_INSTALLED) {
		list = pk_client_get_package_list (task->priv->client_primary);

		/* one or more entry? */
		ret = (PK_OBJ_LIST(list)->len > 0);
		egg_warning ("doing async return");
		dbus_g_method_return (task->priv->context, ret);
		goto out;
	}

	/* SearchFile */
	if (task->priv->role == GPK_DBUS_TASK_ROLE_SEARCH_FILE) {
		list = pk_client_get_package_list (task->priv->client_primary);

		/* one or more entry? */
		len = PK_OBJ_LIST(list)->len;
		if (len > 0)
			name = pk_package_list_get_obj (list, 0)->id->name;
		egg_warning ("doing async return");
		dbus_g_method_return (task->priv->context, (len > 0), name);
		goto out;
	}

	/* InstallPackageNames */
	if (task->priv->role == GPK_DBUS_TASK_ROLE_INSTALL_PACKAGE_NAMES &&
	    role == PK_ROLE_ENUM_RESOLVE) {

		/* found nothing? */
		list = pk_client_get_package_list (task->priv->client_primary);
		len = pk_package_list_get_size (list);
		if (len == 0) {
			if (task->priv->show_warning) {
				//FIXME: shows package_id in UI
				/* TRANSLATORS: couldn't resolve name to package */
				title = g_strdup_printf (_("Could not find packages"));
				info_url = gpk_vendor_get_not_found_url (task->priv->vendor, GPK_VENDOR_URL_TYPE_DEFAULT);
				/* only show the "more info" button if there is a valid link */
				if (info_url != NULL)
					gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, GPK_MODAL_DIALOG_BUTTON_ACTION);
				else
					gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
				gpk_modal_dialog_set_title (task->priv->dialog, title);
				/* TRANSLATORS: message: could not find */
				gpk_modal_dialog_set_message (task->priv->dialog, _("The packages could not be found in any software source"));
				gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-package-not-found");
				/* TRANSLATORS: button: a link to the help file */
				gpk_modal_dialog_set_action (task->priv->dialog, _("More information"));
				gpk_modal_dialog_present (task->priv->dialog);
				button = gpk_modal_dialog_run (task->priv->dialog);
				if (button == GTK_RESPONSE_OK)
					gpk_gnome_open (info_url);
				g_free (info_url);
				g_free (title);
			}
			error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_NO_PACKAGES_FOUND, "no package found");
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}

		/* see what we've got already */
		for (i=0; i<len; i++) {
			obj = pk_package_list_get_obj (list, i);
			if (obj->info == PK_INFO_ENUM_INSTALLED) {
				already_installed = TRUE;
			} else if (obj->info == PK_INFO_ENUM_AVAILABLE) {
				egg_debug ("package '%s' resolved", obj->id->name);
				id = obj->id;
				//TODO: we need to list these in a gpk-dbus_task-chooser
			}
		}

		/* already installed? */
		if (already_installed) {
			if (task->priv->show_warning) {
				//FIXME: shows package_id in UI
				/* TRANSLATORS: title: package is already installed */
				gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
				gpk_modal_dialog_set_title (task->priv->dialog, _("Failed to install packages"));
				/* TRANSLATORS: message: package is already installed */
				gpk_modal_dialog_set_message (task->priv->dialog, _("The package is already installed"));
				gpk_modal_dialog_present (task->priv->dialog);
				gpk_modal_dialog_run (task->priv->dialog);
			}
			error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "package already found");
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}

		/* got junk? */
		if (id == NULL) {
			if (task->priv->show_warning) {
				gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
				/* TRANSLATORS: failed to install, shouldn't be shown */
				gpk_modal_dialog_set_title (task->priv->dialog, _("Failed to install package"));
				/* TRANSLATORS: the search gave us the wrong result. internal error. barf. */
				gpk_modal_dialog_set_message (task->priv->dialog, _("Incorrect response from search"));
				gpk_modal_dialog_present (task->priv->dialog);
				gpk_modal_dialog_run (task->priv->dialog);
			}
			error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "incorrect response from search");
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}

		/* convert to data */
		task->priv->package_ids = pk_package_list_to_strv (list);

		/* install these packages with deps */
		g_idle_add ((GSourceFunc) gpk_dbus_task_install_package_ids_dep_check_idle_cb, task);
		goto out;
	}

out:
	if (error != NULL)
		g_error_free (error);
	if (error_local != NULL)
		g_error_free (error_local);
	if (list != NULL)
		g_object_unref (list);
}

/**
 * gpk_dbus_task_set_status:
 **/
static gboolean
gpk_dbus_task_set_status (GpkDbusTask *task, PkStatusEnum status)
{
	g_return_val_if_fail (GPK_IS_DBUS_TASK (task), FALSE);

	/* do we force progress? */
	if (!task->priv->show_progress) {
		if (status == PK_STATUS_ENUM_DOWNLOAD_REPOSITORY ||
		    status == PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST ||
		    status == PK_STATUS_ENUM_DOWNLOAD_FILELIST ||
		    status == PK_STATUS_ENUM_DOWNLOAD_CHANGELOG ||
		    status == PK_STATUS_ENUM_DOWNLOAD_GROUP ||
		    status == PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO ||
		    status == PK_STATUS_ENUM_REFRESH_CACHE) {
			gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
			gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-progress");
			gpk_modal_dialog_present_with_time (task->priv->dialog, 0);
		}
	}

	/* ignore */
	if (!task->priv->show_progress) {
		egg_warning ("not showing progress");
		return FALSE;
	}

	/* set icon */
	gpk_modal_dialog_set_image_status (task->priv->dialog, status);

	/* set label */
	gpk_modal_dialog_set_title (task->priv->dialog, gpk_status_enum_to_localised_text (status));

	/* spin */
	if (status == PK_STATUS_ENUM_WAIT)
		gpk_modal_dialog_set_percentage (task->priv->dialog, PK_CLIENT_PERCENTAGE_INVALID);

	/* do visual stuff when finished */
	if (status == PK_STATUS_ENUM_FINISHED) {
		/* make insensitive */
		gpk_modal_dialog_set_allow_cancel (task->priv->dialog, FALSE);

		/* stop spinning */
		gpk_modal_dialog_set_percentage (task->priv->dialog, 100);
	}
	return TRUE;
}

/**
 * gpk_dbus_task_progress_changed_cb:
 **/
static void
gpk_dbus_task_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				guint elapsed, guint remaining, GpkDbusTask *task)
{
	/* ignore */
	gpk_modal_dialog_set_percentage (task->priv->dialog, percentage);
	gpk_modal_dialog_set_remaining (task->priv->dialog, remaining);
}

/**
 * gpk_dbus_task_status_changed_cb:
 **/
static void
gpk_dbus_task_status_changed_cb (PkClient *client, PkStatusEnum status, GpkDbusTask *task)
{
	gpk_dbus_task_set_status (task, status);
}

/**
 * gpk_dbus_task_error_code_cb:
 **/
static void
gpk_dbus_task_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, GpkDbusTask *task)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *title;
	const gchar *message;
	NotifyNotification *notification;
	GtkWidget *widget;

	g_return_if_fail (GPK_IS_DBUS_TASK (task));

	/* save for later */
	task->priv->last_exit_code = code;

	/* have we handled? */
	if (code == PK_ERROR_ENUM_NO_LICENSE_AGREEMENT) {
		egg_warning ("did not auth, but should be already handled");
		return;
	}

	/* have we handled? */
	if (pk_error_code_is_need_untrusted (code)) {
		egg_warning ("will handled in finished");
		return;
	}

	/* ignore some errors */
	if (code == PK_ERROR_ENUM_PROCESS_KILL ||
	    code == PK_ERROR_ENUM_TRANSACTION_CANCELLED) {
		egg_debug ("error ignored %s\n%s", pk_error_enum_to_text (code), details);
		return;
	}

	egg_debug ("code was %s", pk_error_enum_to_text (code));

	/* use a modal dialog if showing progress, else use libnotify */
	title = gpk_error_enum_to_localised_text (code);
	message = gpk_error_enum_to_localised_message (code);
	if (task->priv->show_progress) {
		widget = GTK_WIDGET (gpk_modal_dialog_get_window (task->priv->dialog));
		gpk_error_dialog_modal (GTK_WINDOW (widget), title, message, details);
		return;
	}

	/* save this globally */
	g_free (task->priv->error_details);
	task->priv->error_details = g_markup_escape_text (details, -1);

	/* do the bubble */
	notification = notify_notification_new (title, message, "help-browser", NULL);
	notify_notification_set_timeout (notification, 15000);
	notify_notification_set_urgency (notification, NOTIFY_URGENCY_LOW);
	notify_notification_add_action (notification, "show-error-details",
					/* TRANSLATORS: button: show details about the error */
					_("Show details"), gpk_dbus_task_libnotify_cb, task, NULL);
	ret = notify_notification_show (notification, &error);
	if (!ret) {
		egg_warning ("error: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_dbus_task_package_cb:
 **/
static void
gpk_dbus_task_package_cb (PkClient *client, const PkPackageObj *obj, GpkDbusTask *task)
{
	gchar *text;
	g_return_if_fail (GPK_IS_DBUS_TASK (task));

	if (!task->priv->show_progress)
		return;

	text = gpk_package_id_format_twoline (obj->id, obj->summary);
	gpk_modal_dialog_set_message (task->priv->dialog, text);
	g_free (text);
}

/**
 * gpk_dbus_task_allow_cancel_cb:
 **/
static void
gpk_dbus_task_allow_cancel_cb (PkClient *client, gboolean allow_cancel, GpkDbusTask *task)
{
	gpk_modal_dialog_set_allow_cancel (task->priv->dialog, allow_cancel);
}

/**
 * gpk_dbus_task_button_close_cb:
 **/
static void
gpk_dbus_task_button_close_cb (GtkWidget *widget, GpkDbusTask *task)
{
	/* close, don't abort */
	gpk_modal_dialog_close (task->priv->dialog);
}

/**
 * gpk_dbus_task_button_cancel_cb:
 **/
static void
gpk_dbus_task_button_cancel_cb (GtkWidget *widget, GpkDbusTask *task)
{
	gboolean ret;
	GError *error = NULL;

	/* we might have a transaction running */
	ret = pk_client_cancel (task->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	ret = pk_client_cancel (task->priv->client_secondary, &error);
	if (!ret) {
		egg_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return;
}

/**
 * gpk_dbus_task_install_package_files_internal:
 **/
static void
gpk_dbus_task_install_package_files_internal (GpkDbusTask *task, gboolean trusted)
{
	gboolean ret;
	GError *error = NULL;
	GError *error_local = NULL;
	guint length;
	const gchar *title;

	/* FIXME: we need to move this into PkClient sooner or later */
	task->priv->last_exit_code = PK_ERROR_ENUM_UNKNOWN;

	/* reset */
	ret = pk_client_reset (task->priv->client_primary, &error_local);
	if (!ret) {
		/* TRANSLATORS: this should never happen, low level failure */
		gpk_dbus_task_error_msg (task, _("Failed to reset client to perform action"), error_local);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* set timeout */
	pk_client_set_timeout (task->priv->client_primary, task->priv->timeout, NULL);

	/* install local file */
	ret = pk_client_install_files (task->priv->client_primary, trusted, task->priv->files, &error_local);
	if (!ret) {
		length = g_strv_length (task->priv->files);
		/* TRANSLATORS: title: detailed internal error why the file install failed */
		title = ngettext ("Failed to install file", "Failed to install files", length);
		gpk_dbus_task_error_msg (task, title, error_local);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}
out:
	if (error != NULL)
		g_error_free (error);
	if (error_local != NULL)
		g_error_free (error_local);
}

/**
 * gpk_dbus_task_ptr_array_to_bullets:
 *
 * splits the strings up nicely
 *
 * Return value: a newly allocated string
 **/
static gchar *
gpk_dbus_task_ptr_array_to_bullets (GPtrArray *array, const gchar *prefix)
{
	GString *string;
	guint i;
	gchar *text;

	/* don't use a bullet for one item */
	if (array->len == 1) {
		if (prefix != NULL)
			return g_strdup_printf ("%s\n\n%s", prefix, (const gchar *) g_ptr_array_index (array, 0));
		else
			return g_strdup (g_ptr_array_index (array, 0));
	}

	string = g_string_new (prefix);
	if (prefix != NULL)
		g_string_append (string, "\n\n");

	/* prefix with bullet and suffix with newline */
	for (i=0; i<array->len; i++) {
		text = (gchar *) g_ptr_array_index (array, i);
		g_string_append_printf (string, "• %s\n", text);
	}

	/* remove last \n */
	g_string_set_size (string, string->len - 1);

	text = g_string_free (string, FALSE);
	return text;
}

/**
 * gpk_dbus_task_install_package_files_get_user_temp:
 *
 * Return (and create if does not exist) a temporary directory
 * that is writable only by the user, and readable by root.
 *
 * Return value: the temp directory, or %NULL for create error
 **/
static gchar *
gpk_dbus_task_install_package_files_get_user_temp (GpkDbusTask *task, const gchar *subfolder, GError **error)
{
	GFile *file;
	gboolean ret;
	gchar *path = NULL;

	/* build path in home folder */
	path = g_build_filename (g_get_home_dir (), ".PackageKit", subfolder, NULL);

	/* find if exists */
	file = g_file_new_for_path (path);
	ret = g_file_query_exists (file, NULL);
	if (ret)
		goto out;

	/* create as does not exist */
	ret = g_file_make_directory_with_parents (file, NULL, error);
	g_object_unref (file);
	if (!ret) {
		/* return nothing.. */
		g_free (path);
		path = NULL;
	}
out:
	return path;
}

GMainLoop *_loop = NULL;

/**
 * gpk_dbus_task_install_package_files_ready_callback:
 **/
static void
gpk_dbus_task_install_package_files_ready_callback (GObject *source_object, GAsyncResult *res, GpkDbusTask *task)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_if_fail (GPK_IS_DBUS_TASK (task));

	ret = g_file_copy_finish (G_FILE (source_object), res, &error_local);
	if (!ret) {
		egg_warning ("failed to copy file: %s", error_local->message);
		g_error_free (error_local);
	}

	g_main_loop_quit (_loop);
}

/**
 * gpk_dbus_task_install_package_files_progress_callback:
 **/
static void
gpk_dbus_task_install_package_files_progress_callback (goffset current_num_bytes, goffset total_num_bytes, GpkDbusTask *task)
{
	guint percentage;
	g_return_if_fail (GPK_IS_DBUS_TASK (task));
	percentage = (current_num_bytes * 100) / total_num_bytes;
	gpk_modal_dialog_set_percentage (task->priv->dialog, percentage);
}

/**
 * gpk_dbus_task_install_package_files_copy_non_native:
 *
 * Copy the new file into a new file that can be read by packagekitd, and
 * that can't be written into by other users.
 *
 * Return value: the new file path, or %NULL for copy error
 **/
static gchar *
gpk_dbus_task_install_package_files_copy_non_native (GpkDbusTask *task, const gchar *filename, GError **error)
{
	GFile *file = NULL;
	GFile *dest = NULL;
	gchar *basename = NULL;
	gchar *dest_path = NULL;
	gchar *new_path = NULL;
	gchar *cache_path = NULL;
	GError *error_local = NULL;

	/* create the non FUSE temp directory */
	cache_path = gpk_dbus_task_install_package_files_get_user_temp (task, "native-cache", &error_local);
	if (cache_path == NULL) {
		*error = g_error_new (1, 0, "failed to create temp directory: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the final location */
	file = g_file_new_for_path (filename);
	basename = g_file_get_basename (file);
	dest_path = g_build_filename (cache_path, basename, NULL);

	/* copy the file */
	dest = g_file_new_for_path (dest_path);
	g_file_copy_async (file, dest, G_FILE_COPY_OVERWRITE, 0, NULL,
			   (GFileProgressCallback) gpk_dbus_task_install_package_files_progress_callback, task,
			   (GAsyncReadyCallback) gpk_dbus_task_install_package_files_ready_callback, task);
	_loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (_loop);
	g_main_loop_unref (_loop);

	/* return the modified file item */
	new_path = g_strdup (dest_path);

out:
	if (file != NULL)
		g_object_unref (file);
	if (dest != NULL)
		g_object_unref (dest);
	g_free (basename);
	g_free (cache_path);
	g_free (dest_path);
	return new_path;
}

/**
 * gpk_dbus_task_install_package_files_native_check:
 *
 * Allow the user to confirm the package copy to ~/.PackageKit/native-cache
 * as we cannot access FUSE mounts as the root user.
 *
 * Return value: %TRUE if the method succeeded
 **/
static gboolean
gpk_dbus_task_install_package_files_native_check (GpkDbusTask *task, GPtrArray *array, GError **error)
{
	guint i;
	const gchar *data;
	gchar *cache_path = NULL;
	gchar *filename;
	gboolean ret;
	gboolean native;
	GPtrArray *array_missing;
	const gchar *message_part;
	const gchar *title;
	gchar *message;
	GtkResponseType button;
	GError *error_local = NULL;
	GFile *file;

	/* check if any files are non-native and need to be copied */
	array_missing = g_ptr_array_new ();
	for (i=0; i<array->len; i++) {
		data = (const gchar *) g_ptr_array_index (array, i);
		/* if file is non-native, it's on a FUSE mount (probably created by GVFS).
		 * See https://bugzilla.redhat.com/show_bug.cgi?id=456094 */
		file = g_file_new_for_path (data);
		native = g_file_is_native (file);
		g_object_unref (file);
		if (!native) {
			egg_debug ("%s is non-native", data);
			g_ptr_array_add (array_missing, g_strdup (data));
		}
	}

	/* optional */
	ret = gconf_client_get_bool (task->priv->gconf_client, GPK_CONF_SHOW_COPY_CONFIRM, NULL);
	if (ret && array_missing->len > 0) {
		/* TRANSLATORS: title: we have to copy the private files to a public location */
		title = ngettext ("Do you want to copy this file?",
				  "Do you want to copy these files?", array_missing->len);
		/* TRANSLATORS: message: explain to the user what we are doing */
		message_part = ngettext ("This package file has to be copied from a private directory so it can be installed:",
					 "Several package files have to be copied from a private directory so they can be installed:",
					 array_missing->len);
		message = gpk_dbus_task_ptr_array_to_bullets (array_missing, message_part);

		/* show UI */
		gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, 0);
		gpk_modal_dialog_set_title (task->priv->dialog, title);
		gpk_modal_dialog_set_message (task->priv->dialog, message);
		gpk_modal_dialog_set_image (task->priv->dialog, "dialog-warning");
		/* TRANSLATORS: button: copy file from one directory to another */
		gpk_modal_dialog_set_action (task->priv->dialog, ngettext ("Copy file", "Copy files", array_missing->len));
		gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-installing-private-files");
		g_free (message);

		gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
		button = gpk_modal_dialog_run (task->priv->dialog);
		/* did we click no or exit the window? */
		if (button != GTK_RESPONSE_OK) {
			*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "Aborted the copy");
			ret = FALSE;
			goto out;
		}
	}

	/* setup UI */
	if (array_missing->len > 0) {
		/* TRANSLATORS: title: we are about to copy files, which may take a few seconds */
		title = ngettext ("Copying file",
				  "Copying files", array_missing->len);
		gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
		gpk_modal_dialog_set_title (task->priv->dialog, title);
		gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-installing-private-files");
		gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
	}

	/* now we have the okay to copy the files, do so */
	ret = TRUE;
	for (i=0; i<array->len; i++) {
		data = (const gchar *) g_ptr_array_index (array, i);

		/* check we are not on FUSE */
		file = g_file_new_for_path (data);
		native = g_file_is_native (file);
		g_object_unref (file);
		if (!native) {
			/* copy the file */
			filename = gpk_dbus_task_install_package_files_copy_non_native (task, data, &error_local);
			if (filename == NULL) {
				*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "failed to copy file %s: %s", data, error_local->message);
				ret = FALSE;
				break;
			}

			/* show progress */
			gpk_modal_dialog_set_message (task->priv->dialog, filename);

			/* swap data in array */
			g_free (array->pdata[i]);
			array->pdata[i] = g_strdup (filename);
			g_free (filename);
		}
	}

	/* did we fail to copy the files */
	if (!ret) {
		/* TRANSLATORS: title: tell the user we failed */
		title = ngettext ("The file could not be copied",
				  "The files could not be copied", array_missing->len);

		/* show UI */
		gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
		gpk_modal_dialog_set_title (task->priv->dialog, title);
		gpk_modal_dialog_set_message (task->priv->dialog, error_local->message);
		gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
		gpk_modal_dialog_run (task->priv->dialog);
		*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "files not copied");
		ret = FALSE;
		g_error_free (error_local);
		goto out;
	}
out:
	g_free (cache_path);
	g_ptr_array_foreach (array_missing, (GFunc) g_free, NULL);
	g_ptr_array_free (array_missing, TRUE);
	return ret;
}

/**
 * gpk_dbus_task_install_package_files_verify:
 *
 * Allow the user to confirm the action
 *
 * Return value: %TRUE if the method succeeded
 **/
static gboolean
gpk_dbus_task_install_package_files_verify (GpkDbusTask *task, GPtrArray *array, GError **error)
{
	GtkResponseType button;
	const gchar *title;
	gchar *message;
	gboolean ret = TRUE;

	/* TRANSLATORS: title: confirm the user want's to install a local file */
	title = ngettext ("Do you want to install this file?",
			  "Do you want to install these files?", array->len);
	message = gpk_dbus_task_ptr_array_to_bullets (array, NULL);

	/* show UI */
	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, 0);
	gpk_modal_dialog_set_title (task->priv->dialog, title);
	gpk_modal_dialog_set_message (task->priv->dialog, message);
	/* TRANSLATORS: title: installing local files */
	gpk_modal_dialog_set_action (task->priv->dialog, _("Install"));
	gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-install-files");
	gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
	button = gpk_modal_dialog_run (task->priv->dialog);
	g_free (message);

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK) {
		/* TRANSLATORS: title: the user cancelled the action */
		title = ngettext ("The file was not installed",
				  "The files were not installed", array->len);
		gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
		gpk_modal_dialog_set_title (task->priv->dialog, title);
		gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
		gpk_modal_dialog_run (task->priv->dialog);
		*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "Aborted");
		ret = FALSE;
		goto out;
	}
out:
	return ret;
}

/**
 * gpk_dbus_task_install_package_files_check_exists:
 *
 * Skip files that are not present
 *
 * Return value: %TRUE if the method succeeded
 **/
static gboolean
gpk_dbus_task_install_package_files_check_exists (GpkDbusTask *task, GPtrArray *array, GError **error)
{
	guint i;
	const gchar *data;
	gboolean ret;
	GPtrArray *array_missing;
	const gchar *message_part;
	const gchar *title;
	gchar *message;

	array_missing = g_ptr_array_new ();

	/* find missing */
	for (i=0; i<array->len; i++) {
		data = (const gchar *) g_ptr_array_index (array, i);
		ret = g_file_test (data, G_FILE_TEST_EXISTS);
		if (!ret)
			g_ptr_array_add (array_missing, g_strdup (data));
	}

	/* warn, set error and quit */
	ret = TRUE;
	if (array_missing->len > 0) {
		/* TRANSLATORS: title: we couldn't find the file -- very hard to get this */
		title = ngettext ("File was not found!",
				  "Files were not found!", array_missing->len);

		/* TRANSLATORS: message: explain what went wrong */
		message_part = ngettext ("The following file was not found:",
					 "The following files were not found:", array_missing->len);
		message = gpk_dbus_task_ptr_array_to_bullets (array_missing, message_part);

		/* show UI */
		gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
		gpk_modal_dialog_set_title (task->priv->dialog, title);
		gpk_modal_dialog_set_message (task->priv->dialog, message);
		gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
		gpk_modal_dialog_run (task->priv->dialog);

		g_free (message);

		ret = FALSE;
		*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "some files did not exist");
		goto out;
	}

out:
	g_ptr_array_foreach (array_missing, (GFunc) g_free, NULL);
	g_ptr_array_free (array_missing, TRUE);
	return ret;
}

/**
 * gpk_dbus_task_install_package_files_check_type_get_content_type:
 **/
static gchar *
gpk_dbus_task_install_package_files_check_type_get_content_type (const gchar *filename, GError **error)
{
	GError *error_local = NULL;
	GFile *file;
	GFileInfo *info;
	gchar *content_type = NULL;

	/* get file info synchronously */
	file = g_file_new_for_path (filename);
	info = g_file_query_info (file, "standard::content-type", G_FILE_QUERY_INFO_NONE, NULL, &error_local);
	if (info == NULL) {
		*error = g_error_new (1, 0, "failed to get file attributes for %s: %s", filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get content type as string */
	content_type = g_file_info_get_attribute_as_string (info, "standard::content-type");
out:
	if (info != NULL)
		g_object_unref (info);
	g_object_unref (file);
	return content_type;
}

/**
 * gpk_dbus_task_install_package_files_check_type:
 *
 * Skip files that are not present
 *
 * Return value: %TRUE if the method succeeded
 **/
static gboolean
gpk_dbus_task_install_package_files_check_type (GpkDbusTask *task, GPtrArray *array, GError **error)
{
	guint i;
	guint j;
	const gchar *data;
	gboolean ret;
	GPtrArray *array_unknown;
	const gchar *message_part;
	const gchar *title;
	gchar *message;
	gchar *content_type;
	GError *error_local = NULL;
	gchar **supported_types;

	array_unknown = g_ptr_array_new ();

	/* get mime types supported by the backend */
	supported_types = pk_control_get_mime_types (task->priv->control, &error_local);
	if (supported_types == NULL) {
		*error = g_error_new (1, 0, "failed to get supported types for the backend: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* find invalid files */
	for (i=0; i<array->len; i++) {
		data = (const gchar *) g_ptr_array_index (array, i);

		/* get content type for this file */
		content_type = gpk_dbus_task_install_package_files_check_type_get_content_type (data, error);
		if (content_type == NULL)
			goto out;
		egg_warning ("content_type=%s", content_type);

		/* can we support this one? */
		ret = FALSE;
		for (j=0; supported_types[j] != NULL; j++) {
			if (g_strcmp0 (supported_types[j], content_type) == 0) {
				ret = TRUE;
				break;
			}
		}
		g_free (content_type);

		/* we can't handle the content type :-( */
		if (!ret)
			g_ptr_array_add (array_unknown, g_strdup (data));
	}

	/* warn, set error and quit */
	ret = TRUE;
	if (array_unknown->len > 0) {
		/* TRANSLATORS: title: we couldn't find the file -- very hard to get this */
		title = ngettext ("File was not recognised!",
				  "Files were not recognised!", array_unknown->len);

		/* TRANSLATORS: message: the backend would not be able to handle the mime-type */
		message_part = ngettext ("The following file is not recognised by the packaging system:",
					 "The following files are not recognised by the packaging system:", array_unknown->len);
		message = gpk_dbus_task_ptr_array_to_bullets (array_unknown, message_part);

		/* show UI */
		gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
		gpk_modal_dialog_set_title (task->priv->dialog, title);
		gpk_modal_dialog_set_message (task->priv->dialog, message);
		gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
		gpk_modal_dialog_run (task->priv->dialog);

		g_free (message);

		ret = FALSE;
		*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "some files were not recognised");
		goto out;
	}

out:
	g_strfreev (supported_types);
	g_ptr_array_foreach (array_unknown, (GFunc) g_free, NULL);
	g_ptr_array_free (array_unknown, TRUE);
	return ret;
}

/**
 * gpk_dbus_task_confirm_action:
 * @task: a valid #GpkDbusTask instance
 **/
static gboolean
gpk_dbus_task_confirm_action (GpkDbusTask *task, const gchar *title, const gchar *message, const gchar *action)
{
	GtkResponseType button;

	/* check the user wanted to call this method */
	if (!task->priv->show_confirm_search)
		return TRUE;

	/* setup UI */
	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, 0);
	gpk_modal_dialog_set_action (task->priv->dialog, action);

	/* set icon */
	if (task->priv->parent_icon_name != NULL)
		gpk_modal_dialog_set_image (task->priv->dialog, task->priv->parent_icon_name);
	else
		gpk_modal_dialog_set_image (task->priv->dialog, "emblem-system");

	gpk_modal_dialog_set_title (task->priv->dialog, title);
	gpk_modal_dialog_set_message (task->priv->dialog, message);
	gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-application-confirm");
	gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
	button = gpk_modal_dialog_run (task->priv->dialog);

	/* close, we're going to fail the method */
	if (button != GTK_RESPONSE_OK) {
		gpk_modal_dialog_close (task->priv->dialog);
		return FALSE;
	}

	return TRUE;
}

/**
 * gpk_dbus_task_is_installed:
 **/
void
gpk_dbus_task_is_installed (GpkDbusTask *task, const gchar *package_name)
{
	gboolean ret;
	GError *error = NULL;
	GError *error_local = NULL;
	gchar **package_names = NULL;

	task->priv->role = GPK_DBUS_TASK_ROLE_IS_INSTALLED;

	/* reset */
	ret = pk_client_reset (task->priv->client_primary, &error_local);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "failed to reset: %s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* set timeout */
	pk_client_set_timeout (task->priv->client_primary, task->priv->timeout, NULL);

	/* get the package list for the installed packages */
	package_names = g_strsplit (package_name, "|", 1);
	egg_warning ("package_name=%s", package_name);
	ret = pk_client_resolve (task->priv->client_primary, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_names, &error_local);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "failed to get installed status: %s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* wait for async reply... */
out:
	g_strfreev (package_names);
	if (error != NULL)
		g_error_free (error);
	if (error_local != NULL)
		g_error_free (error_local);
}

/**
 * gpk_dbus_task_search_file:
 **/
void
gpk_dbus_task_search_file (GpkDbusTask *task, const gchar *search_file)
{
	gboolean ret;
	GError *error = NULL;
	GError *error_local = NULL;

	task->priv->role = GPK_DBUS_TASK_ROLE_SEARCH_FILE;

	/* reset */
	ret = pk_client_reset (task->priv->client_primary, &error_local);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "failed to reset: %s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* set timeout */
	pk_client_set_timeout (task->priv->client_primary, task->priv->timeout, NULL);

	/* get the package list for the installed packages */
	egg_warning ("package_name=%s", search_file);
	ret = pk_client_search_file (task->priv->client_primary, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), search_file, &error_local);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "failed to get installed status: %s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* wait for async reply... */
out:
	if (error != NULL)
		g_error_free (error);
	if (error_local != NULL)
		g_error_free (error_local);
}

/**
 * gpk_dbus_task_install_package_files:
 * @task: a valid #GpkDbusTask instance
 * @file_rel: a file such as <literal>./hal-devel-0.10.0.rpm</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a file locally, and get the deps from the repositories.
 * This is useful for double clicking on a .rpm or .deb file.
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_package_files (GpkDbusTask *task, gchar **files_rel)
{
	GError *error = NULL;
	gboolean ret;
	GPtrArray *array;
//	const gchar *title;

	g_return_if_fail (GPK_IS_DBUS_TASK (task));
	g_return_if_fail (files_rel != NULL);

	array = pk_strv_to_ptr_array (files_rel);

	/* check the user wanted to call this method */
	if (task->priv->show_confirm_search) {
		ret = gpk_dbus_task_install_package_files_verify (task, array, &error);
		if (!ret) {
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}
	}

	/* check all files exist and are readable by the local user */
	ret = gpk_dbus_task_install_package_files_check_exists (task, array, &error);
	if (!ret) {
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* check all files can be handled by the backend */
	ret = gpk_dbus_task_install_package_files_check_type (task, array, &error);
	if (!ret) {
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* check all files exist and are readable by the local user */
	ret = gpk_dbus_task_install_package_files_native_check (task, array, &error);
	if (!ret) {
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}
	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	/* TRANSLATORS: title: installing a local file */
	gpk_modal_dialog_set_title (task->priv->dialog, ngettext ("Install local file", "Install local files", array->len));
	if (task->priv->show_progress)
		gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);

	task->priv->files = pk_ptr_array_to_strv (array);
	gpk_dbus_task_install_package_files_internal (task, TRUE);
out:
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);
	if (error != NULL)
		g_error_free (error);
}

/**
 * gpk_dbus_task_install_package_names:
 * @task: a valid #GpkDbusTask instance
 * @package: a pakage name such as <literal>hal-info</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a package of the newest and most correct version.
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_package_names (GpkDbusTask *task, gchar **packages)
{
	gboolean ret;
	GError *error = NULL;
	GError *error_local = NULL;
	gchar *message;
	gchar *text;
	guint len;
	guint i;
	GString *string;

	g_return_if_fail (GPK_IS_DBUS_TASK (task));
	g_return_if_fail (packages != NULL);

	task->priv->role = GPK_DBUS_TASK_ROLE_INSTALL_PACKAGE_NAMES;

	/* optional */
	if (!task->priv->show_confirm_install) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	string = g_string_new ("");
	len = g_strv_length (packages);

	/* don't use a bullet for one item */
	if (len == 1) {
		g_string_append_printf (string, "%s\n", packages[0]);
	} else {
		for (i=0; i<len; i++)
			g_string_append_printf (string, "• %s\n", packages[i]);
	}
	/* display messagebox  */
	text = g_string_free (string, FALSE);

	/* check user wanted operation */
	message = g_strdup_printf ("%s\n\n%s\n%s",
				   /* TRANSLATORS: a program needs a package, for instance openoffice-clipart */
				   ngettext ("An additional package is required:", "Additional packages are required:", len),
				   text,
				   /* TRANSLATORS: ask the user if it's okay to search */
				   ngettext ("Do you want to search for and install this package now?", "Do you want to search for and install these packages now?", len));
	g_free (text);

	/* make title using application name */
	if (task->priv->parent_title != NULL) {
		/* TRANSLATORS: string is a program name, e.g. "Movie Player" */
		text = g_strdup_printf (ngettext ("%s wants to install a package", "%s wants to install packages", len), task->priv->parent_title);
	} else {
		/* TRANSLATORS: a random program which we can't get the name wants to do something */
		text = g_strdup (ngettext ("A program wants to install a package", "A program wants to install packages", len));
	}

	/* TRANSLATORS: button: confirm to search for packages */
	ret = gpk_dbus_task_confirm_action (task, text, message, _("Install"));
	g_free (text);
	g_free (message);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to search");
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

skip_checks:
	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	/* TRANSLATORS: title, searching */
	gpk_modal_dialog_set_title (task->priv->dialog, _("Searching for packages"));
	gpk_modal_dialog_set_image_status (task->priv->dialog, PK_STATUS_ENUM_WAIT);
	gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-finding-packages");
	if (task->priv->show_progress)
		gpk_modal_dialog_present (task->priv->dialog);

	/* reset */
	ret = pk_client_reset (task->priv->client_primary, &error_local);
	if (!ret) {
		/* TRANSLATORS: this is an internal error, and should not be seen */
		gpk_dbus_task_error_msg (task, _("Failed to reset client"), error_local);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* set timeout */
	pk_client_set_timeout (task->priv->client_primary, task->priv->timeout, NULL);

	/* find out if we can find a package */
	ret = pk_client_resolve (task->priv->client_primary, PK_FILTER_ENUM_NONE, packages, &error_local);
	if (!ret) {
		/* TRANSLATORS: we failed to find the package, this shouldn't happen */
		gpk_dbus_task_error_msg (task, _("Incorrect response from search"), error_local);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}
/*xxx*/
	/* wait for async reply */
out:
	if (error != NULL)
		g_error_free (error);
	if (error_local != NULL)
		g_error_free (error_local);
}

/**
 * gpk_dbus_task_install_provide_files:
 * @task: a valid #GpkDbusTask instance
 * @full_path: a file path name such as <literal>/usr/sbin/packagekitd</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a package which provides a file on the system.
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_provide_files (GpkDbusTask *task, gchar **full_paths)
{
	gboolean ret;
	GError *error = NULL;
	GError *error_local = NULL;
	guint len;
	guint i;
	gchar *text;
	gchar *message;
	GString *string;

	g_return_if_fail (GPK_IS_DBUS_TASK (task));
	g_return_if_fail (full_paths != NULL);

	task->priv->role = GPK_DBUS_TASK_ROLE_INSTALL_PROVIDE_FILES;

	/* optional */
	if (!task->priv->show_confirm_search) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	string = g_string_new ("");
	len = g_strv_length (full_paths);

	/* don't use a bullet for one item */
	if (len == 1) {
		g_string_append_printf (string, "%s\n", full_paths[0]);
	} else {
		for (i=0; i<len; i++)
			g_string_append_printf (string, "• %s\n", full_paths[i]);
	}
	/* display messagebox  */
	text = g_string_free (string, FALSE);

	/* check user wanted operation */
	message = g_strdup_printf ("%s\n\n%s\n\n%s",
				   /* TRANSLATORS: a program wants to install a file, e.g. /lib/moo.so */
				   ngettext ("The following file is required:", "The following files are required:", len),
				   text,
				   /* TRANSLATORS: confirm with the user */
				   ngettext ("Do you want to search for this file now?", "Do you want to search for these files now?", len));

	/* make title using application name */
	if (task->priv->parent_title != NULL) {
		/* TRANSLATORS: string is a program name, e.g. "Movie Player" */
		text = g_strdup_printf (ngettext ("%s wants to install a file", "%s wants to install files", len), task->priv->parent_title);
	} else {
		/* TRANSLATORS: a random program which we can't get the name wants to do something */
		text = g_strdup (ngettext ("A program wants to install a file", "A program wants to install files", len));
	}

	/* TRANSLATORS: button: confirm to search for packages */
	ret = gpk_dbus_task_confirm_action (task, text, message, _("Install"));
	g_free (text);
	g_free (message);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to search");
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

skip_checks:
	/* TRANSLATORS: searching for the package that provides the file */
	gpk_modal_dialog_set_title (task->priv->dialog, _("Searching for file"));
	gpk_modal_dialog_set_image_status (task->priv->dialog, PK_STATUS_ENUM_WAIT);

	/* reset */
	ret = pk_client_reset (task->priv->client_primary, &error_local);
	if (!ret) {
		/* TRANSLATORS: this is an internal error, and should not be seen */
		gpk_dbus_task_error_msg (task, _("Failed to reset client"), error_local);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* set timeout */
	pk_client_set_timeout (task->priv->client_primary, task->priv->timeout, NULL);

	/* do search */
	ret = pk_client_search_file (task->priv->client_primary, PK_FILTER_ENUM_NONE, full_paths[0], &error_local);
	if (!ret) {
		/* TRANSLATORS: we failed to find the package, this shouldn't happen */
		gpk_dbus_task_error_msg (task, _("Failed to search for file"), error_local);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* wait for async reply */
out:
	if (error != NULL)
		g_error_free (error);
	if (error_local != NULL)
		g_error_free (error_local);
}

/**
 * gpk_dbus_task_install_gstreamer_codec_part:
 **/
static PkPackageObj *
gpk_dbus_task_install_gstreamer_codec_part (GpkDbusTask *task, const gchar *codec_name, const gchar *codec_desc, GError **error)
{
	PkPackageList *list = NULL;
	gboolean ret;
	PkPackageObj *new_obj = NULL;
	const PkPackageObj *obj;
	guint len;
	gchar *title;

	/* reset */
	ret = pk_client_reset (task->priv->client_primary, error);
	if (!ret)
		return NULL;

	/* set timeout */
	pk_client_set_timeout (task->priv->client_primary, task->priv->timeout, NULL);

	/* TRANSLATORS: title, searching for codecs */
	title = g_strdup_printf (_("Searching for plugin: %s"), codec_name);
	gpk_modal_dialog_set_message (task->priv->dialog, title);
	g_free (title);

	/* get codec packages, FIXME: synchronous! */
	pk_client_set_synchronous (task->priv->client_primary, TRUE, NULL);
	ret = pk_client_what_provides (task->priv->client_primary, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), PK_PROVIDES_ENUM_CODEC, codec_desc, error);
	pk_client_set_synchronous (task->priv->client_primary, FALSE, NULL);
	if (!ret)
		return NULL;

	list = pk_client_get_package_list (task->priv->client_primary);
	len = pk_package_list_get_size (list);

	/* found nothing? */
	if (len == 0) {
		*error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_NO_PACKAGES_FOUND, "failed to find: %s", codec_desc);
		goto out;
	}

	/* gstreamer-ffmpeg and gstreamer-plugins-ugly both provide mp3 playback, choose one */
	if (len > 1)
		egg_warning ("choosing one of the provides as more than one match");

	/* always use the first one */
	obj = pk_package_list_get_obj (list, 0);
	if (obj == NULL)
		egg_error ("obj cannot be NULL");

	/* copy the object */
	new_obj = pk_package_obj_copy (obj);
out:
	if (list != NULL)
		g_object_unref (list);
	return new_obj;
}

/**
 * gpk_dbus_task_install_gstreamer_resources_confirm:
 **/
static gboolean
gpk_dbus_task_install_gstreamer_resources_confirm (GpkDbusTask *task, gchar **codec_names)
{
	guint i;
	guint len;
	const gchar *text;
	gchar **parts;
	gboolean ret;
	GString *string;
	gchar *title;
	gchar *message;
	gboolean is_decoder = FALSE;
	gboolean is_encoder = FALSE;

	len = g_strv_length (codec_names);

	/* find out what type of request this is */
	for (i=0; i<len; i++) {
		parts = g_strsplit (codec_names[i], "|", 2);
		if (g_str_has_prefix (parts[1], "gstreamer0.10(decoder"))
			is_decoder = TRUE;
		if (g_str_has_prefix (parts[1], "gstreamer0.10(encoder"))
			is_encoder = TRUE;
		g_strfreev (parts);
	}

	/* TRANSLATORS: we are listing the plugins in a box */
	text = ngettext ("The following plugin is required:", "The following plugins are required:", len);
	string = g_string_new ("");
	g_string_append_printf (string, "%s\n\n", text);

	/* don't use a bullet for one item */
	if (len == 1) {
		parts = g_strsplit (codec_names[0], "|", 2);
		g_string_append_printf (string, "%s\n", parts[0]);
		g_strfreev (parts);
	} else {
		for (i=0; i<len; i++) {
			parts = g_strsplit (codec_names[i], "|", 2);
			g_string_append_printf (string, "• %s\n", parts[0]);
			g_strfreev (parts);
		}
	}

	/* TRANSLATORS: ask for confirmation */
	message = ngettext ("Do you want to search for this now?", "Do you want to search for these now?", len);
	g_string_append_printf (string, "\n%s\n", message);

	/* remove last \n */
	g_string_set_size (string, string->len - 1);

	/* display messagebox  */
	message = g_string_free (string, FALSE);

	/* make title using application name */
	if (task->priv->parent_title != NULL) {
		if (is_decoder && !is_encoder) {
			/* TRANSLATORS: a program wants to decode something (unknown) -- string is a program name, e.g. "Movie Player" */
			title = g_strdup_printf (ngettext ("%s requires an additional plugin to decode this file",
							   "%s requires additional plugins to decode this file", len), task->priv->parent_title);
		} else if (!is_decoder && is_encoder) {
			/* TRANSLATORS: a program wants to encode something (unknown) -- string is a program name, e.g. "Movie Player" */
			title = g_strdup_printf (ngettext ("%s requires an additional plugin to encode this file",
							   "%s requires additional plugins to encode this file", len), task->priv->parent_title);
		} else if (!is_decoder && is_encoder) {
			/* TRANSLATORS: a program wants to do something (unknown) -- string is a program name, e.g. "Movie Player" */
			title = g_strdup_printf (ngettext ("%s requires an additional plugin for this operation",
							   "%s requires additional plugins for this operation", len), task->priv->parent_title);
		}
	} else {
		if (is_decoder && !is_encoder) {
			/* TRANSLATORS: a random program which we can't get the name wants to decode something */
			title = g_strdup (ngettext ("A program requires an additional plugin to decode this file",
						    "A program requires additional plugins to decode this file", len));
		} else if (!is_decoder && is_encoder) {
			/* TRANSLATORS: a random program which we can't get the name wants to encode something */
			title = g_strdup (ngettext ("A program requires an additional plugin to encode this file",
						    "A program requires additional plugins to encode this file", len));
		} else if (!is_decoder && is_encoder) {
			/* TRANSLATORS: a random program which we can't get the name wants to do something (unknown) */
			title = g_strdup (ngettext ("A program requires an additional plugin for this operation",
						    "A program requires additional plugins for this operation", len));
		}
	}

	/* TRANSLATORS: button: confirm to search for packages */
	ret = gpk_dbus_task_confirm_action (task, title, message, _("Search"));
	g_free (title);
	g_free (message);

	return ret;
}

/**
 * gpk_dbus_task_install_gstreamer_resources:
 * @task: a valid #GpkDbusTask instance
 * @codecs: a codec_type such as <literal>application/text</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a application to handle a mime type
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_gstreamer_resources (GpkDbusTask *task, gchar **codec_names)
{
	gboolean ret = TRUE;
	GError *error = NULL;
	GError *error_local = NULL;
	guint i;
	guint len;
	PkPackageObj *obj_new;
	gchar **parts;
	GtkResponseType button;
	gchar *info_url;
	PkPackageList *list = NULL;
	const gchar *title;
	const gchar *message;

	/* check it's not session wide banned in gconf */
	ret = gconf_client_get_bool (task->priv->gconf_client, GPK_CONF_ENABLE_CODEC_HELPER, NULL);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FORBIDDEN, "not enabled in GConf : %s", GPK_CONF_ENABLE_CODEC_HELPER);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* optional */
	if (!task->priv->show_confirm_search) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* confirm */
	ret = gpk_dbus_task_install_gstreamer_resources_confirm (task, codec_names);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to search");
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

skip_checks:
	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, GPK_MODAL_DIALOG_PACKAGE_PADDING);
	/* TRANSLATORS: search for codec */
	gpk_modal_dialog_set_title (task->priv->dialog, _("Searching for plugins"));
	gpk_modal_dialog_set_image_status (task->priv->dialog, PK_STATUS_ENUM_WAIT);
	gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-finding-packages");

	/* setup the UI */
	if (task->priv->show_progress)
		gpk_modal_dialog_present (task->priv->dialog);

	/* save the objects to download in a list */
	list = pk_package_list_new ();

	len = g_strv_length (codec_names);
	for (i=0; i<len; i++) {
		parts = g_strsplit (codec_names[i], "|", 2);
		if (g_strv_length (parts) != 2) {
			egg_warning ("invalid line '%s', expecting a | delimiter", codec_names[i]);
			continue;
		}
		obj_new = gpk_dbus_task_install_gstreamer_codec_part (task, parts[0], parts[1], &error_local);
		if (obj_new == NULL) {
			if (task->priv->show_warning) {
				info_url = gpk_vendor_get_not_found_url (task->priv->vendor, GPK_VENDOR_URL_TYPE_CODEC);
				/* only show the "more info" button if there is a valid link */
				if (info_url != NULL)
					gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, GPK_MODAL_DIALOG_BUTTON_ACTION);
				else
					gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
				/* TRANSLATORS: failed to search for codec */
				gpk_modal_dialog_set_title (task->priv->dialog, _("Failed to search for plugin"));
				/* TRANSLATORS: no software sources have the wanted codec */
				gpk_modal_dialog_set_message (task->priv->dialog, _("Could not find plugin in any configured software source"));
				gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-package-not-found");

				/* TRANSLATORS: button text */
				gpk_modal_dialog_set_action (task->priv->dialog, _("More information"));
				gpk_modal_dialog_present (task->priv->dialog);
				button = gpk_modal_dialog_run (task->priv->dialog);
				if (button == GTK_RESPONSE_OK)
					gpk_gnome_open (info_url);
				g_free (info_url);
			}
			error = g_error_new (GPK_DBUS_ERROR, error_local->code, "%s", error_local->message);
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}
		pk_obj_list_add (PK_OBJ_LIST(list), obj_new);
		pk_package_obj_free (obj_new);
		g_strfreev (parts);
	}

	/* optional */
	if (!task->priv->show_confirm_deps) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks2;
	}

	title = ngettext ("Install the following plugin", "Install the following plugins", len);
	message = ngettext ("Do you want to install this package now?", "Do you want to install these packages now?", len);

	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, GPK_MODAL_DIALOG_PACKAGE_LIST);
	gpk_modal_dialog_set_package_list (task->priv->dialog, list);
	gpk_modal_dialog_set_title (task->priv->dialog, title);
	gpk_modal_dialog_set_message (task->priv->dialog, message);
	gpk_modal_dialog_set_image (task->priv->dialog, "dialog-information");
	/* TRANSLATORS: button: install codecs */
	gpk_modal_dialog_set_action (task->priv->dialog, _("Install"));
	gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
	button = gpk_modal_dialog_run (task->priv->dialog);

	/* close, we're going to fail the method */
	if (button != GTK_RESPONSE_OK) {
		gpk_modal_dialog_close (task->priv->dialog);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to download");
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

skip_checks2:
	/* install with deps */
	task->priv->package_ids = pk_package_list_to_strv (list);
	gpk_dbus_task_install_package_ids_dep_check (task);
out:
	if (list != NULL)
		g_object_unref (list);
	if (error != NULL)
		g_error_free (error);
	if (error_local != NULL)
		g_error_free (error_local);
}

/**
 * gpk_dbus_task_install_mime_types:
 * @task: a valid #GpkDbusTask instance
 * @mime_type: a mime_type such as <literal>application/text</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a application to handle a mime type
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_mime_types (GpkDbusTask *task, gchar **mime_types)
{
	gboolean ret;
	GError *error = NULL;
	GError *error_local = NULL;
	guint len;
	gchar *message;
	gchar *text;

	g_return_if_fail (GPK_IS_DBUS_TASK (task));
	g_return_if_fail (mime_types != NULL);

	task->priv->role = GPK_DBUS_TASK_ROLE_INSTALL_MIME_TYPES;

	/* check it's not session wide banned in gconf */
	ret = gconf_client_get_bool (task->priv->gconf_client, GPK_CONF_ENABLE_MIME_TYPE_HELPER, NULL);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FORBIDDEN, "not enabled in GConf : %s", GPK_CONF_ENABLE_MIME_TYPE_HELPER);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* optional */
	if (!task->priv->show_confirm_search) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* make sure the user wants to do action */
	message = g_strdup_printf ("%s\n\n%s\n\n%s",
				    /* TRANSLATORS: message: mime type opener required */
				   _("An additional program is required to open this type of file:"),
				   mime_types[0],
				   /* TRANSLATORS: message: confirm with the user */
				   _("Do you want to search for a program to open this file type now?"));

	/* hardcode for now as we only support one mime type at a time */
	len = 1;

	/* make title using application name */
	if (task->priv->parent_title != NULL) {
		/* TRANSLATORS: string is a program name, e.g. "Movie Player" */
		text = g_strdup_printf (ngettext ("%s requires a new mime type", "%s requires new mime types", len), task->priv->parent_title);
	} else {
		/* TRANSLATORS: a random program which we can't get the name wants to do something */
		text = g_strdup (ngettext ("A program requires a new mime type", "A program requires new mime types", len));
	}

	/* TRANSLATORS: button: confirm to search for packages */
	ret = gpk_dbus_task_confirm_action (task, text, message, _("Search"));
	g_free (text);
	g_free (message);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to search");
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

skip_checks:
	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	/* TRANSLATORS: title: searching for mime type handlers */
	gpk_modal_dialog_set_title (task->priv->dialog, _("Searching for file handlers"));
	gpk_modal_dialog_set_image_status (task->priv->dialog, PK_STATUS_ENUM_WAIT);
	gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-finding-packages");

	/* setup the UI */
	if (task->priv->show_progress)
		gpk_modal_dialog_present (task->priv->dialog);

	/* reset */
	ret = pk_client_reset (task->priv->client_primary, &error_local);
	if (!ret) {
		/* TRANSLATORS: this is an internal error, and should not be seen */
		gpk_dbus_task_error_msg (task, _("Failed to reset client"), error_local);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* set timeout */
	pk_client_set_timeout (task->priv->client_primary, task->priv->timeout, NULL);

	/* action */
	ret = pk_client_what_provides (task->priv->client_primary, 0,//pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED),
				       PK_PROVIDES_ENUM_MIMETYPE, mime_types[0], &error_local);
	if (!ret) {
		/* TRANSLATORS: we failed to find the package, this shouldn't happen */
		gpk_dbus_task_error_msg (task, _("Failed to search for provides"), error_local);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* wait for async reply */
out:
	if (error != NULL)
		g_error_free (error);
	if (error_local != NULL)
		g_error_free (error_local);
}

/**
 * gpk_dbus_task_font_tag_to_lang:
 **/
static gchar *
gpk_dbus_task_font_tag_to_lang (const gchar *tag)
{
	gchar *lang = NULL;
#if 0
	*** We do not yet enable this code due to a few bugs in fontconfig ***
	http://bugs.freedesktop.org/show_bug.cgi?id=18846 and
	http://bugs.freedesktop.org/show_bug.cgi?id=18847

	FcPattern *pat = NULL;
	FcChar8 *fclang;
	FcResult res;

	/* parse the tag */
	pat = FcNameParse ((FcChar8 *) tag);
	if (pat == NULL) {
		egg_warning ("cannot parse: '%s'", tag);
		goto out;
	}
	FcPatternPrint (pat);
	res = FcPatternGetString (pat, FC_LANG, 0, &fclang);
	if (res != FcResultMatch) {
		egg_warning ("failed to get string for: '%s': %i", tag, res);
		goto out;
	}
	lang = g_strdup ((gchar *) fclang);
out:
	if (pat != NULL)
		FcPatternDestroy (pat);
#else
	guint len;

	/* verify we have enough to remove prefix */
	len = strlen (tag);
	if (len < 7)
		goto out;
	/* this is a bodge */
	lang = g_strdup (&tag[6]);
out:
#endif
	return lang;
}


/**
 * gpk_dbus_task_font_tag_to_localised_name:
 **/
static gchar *
gpk_dbus_task_font_tag_to_localised_name (GpkDbusTask *task, const gchar *tag)
{
	gchar *lang;
	gchar *language = NULL;
	gchar *name;

	/* use fontconfig to get the language code */
	lang = gpk_dbus_task_font_tag_to_lang (tag);
	if (lang == NULL) {
		/* TRANSLATORS: we could not parse the ISO639 code from the fontconfig tag name */
		name = g_strdup_printf ("%s: %s", _("Language tag not parsed"), tag);
		goto out;
	}

	/* convert to localisable name */
	language = gpk_language_iso639_to_language (task->priv->language, lang);
	if (language == NULL) {
		/* TRANSLATORS: we could not find en_US string for ISO639 code */
		name = g_strdup_printf ("%s: %s", _("Language code not matched"), lang);
		goto out;
	}

	/* get translation, or return untranslated string */
	name = g_strdup (dgettext("iso_639", language));
	if (name == NULL)
		name = g_strdup (language);
out:
	g_free (lang);
	g_free (language);
	return name;
}

/**
 * gpk_dbus_task_install_fontconfig_resources:
 * @task: a valid #GpkDbusTask instance
 * @fonts: font description such as <literal>lang:fr</literal>
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a application to handle a mime type
 *
 * Return value: %TRUE if the method succeeded
 **/
void
gpk_dbus_task_install_fontconfig_resources (GpkDbusTask *task, gchar **fonts)
{
	gboolean ret;
	PkPackageList *list = NULL;
	PkPackageList *list_tmp = NULL;
	GtkResponseType button;
	gchar *info_url;
	GError *error = NULL;
	GError *error_local = NULL;
	guint i;
	guint len;
	guint size;
	gchar *text;
	gchar *message;
	const gchar *title;
	const gchar *title_part;
	GString *string;

	g_return_if_fail (GPK_IS_DBUS_TASK (task));
	g_return_if_fail (fonts != NULL);

	/* get number of fonts to install */
	len = g_strv_length (fonts);

	/* check it's not session wide banned in gconf */
	ret = gconf_client_get_bool (task->priv->gconf_client, GPK_CONF_ENABLE_FONT_HELPER, NULL);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FORBIDDEN, "not enabled in GConf : %s", GPK_CONF_ENABLE_FONT_HELPER);
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* optional */
	if (!task->priv->show_confirm_search) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* check we got valid data */
	for (i=0; i<len; i++) {
		/* correct prefix */
		if (!g_str_has_prefix (fonts[i], ":lang=")) {
			error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "not recognised prefix: '%s'", fonts[i]);
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}
		/* no lang code */
		size = strlen (fonts[i]);
		if (size < 7 || size > 20) {
			error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "lang tag malformed: '%s'", fonts[i]);
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}
	}

	string = g_string_new ("");

	/* don't use a bullet for one item */
	if (len == 1) {
		text = gpk_dbus_task_font_tag_to_localised_name (task, fonts[0]);
		g_string_append_printf (string, "%s\n", text);
		g_free (text);
	} else {
		for (i=0; i<len; i++) {
			text = gpk_dbus_task_font_tag_to_localised_name (task, fonts[i]);
			g_string_append_printf (string, "• %s\n", text);
			g_free (text);
		}
	}
	/* display messagebox  */
	text = g_string_free (string, FALSE);

	/* TRANSLATORS: we need to download a new font package to display a document */
	title = ngettext ("An additional font is required to view this document correctly.",
			  "Additional fonts are required to view this document correctly.", len);

	/* TRANSLATORS: we need to download a new font package to display a document */
	title_part = ngettext ("Do you want to search for a suitable package now?",
			       "Do you want to search for suitable packages now?", len);

	/* check user wanted operation */
	message = g_strdup_printf ("%s\n\n%s\n%s", title, text, title_part);
	g_free (text);

	/* make title using application name */
	if (task->priv->parent_title != NULL) {
		/* TRANSLATORS: string is a program name, e.g. "Movie Player" */
		text = g_strdup_printf (ngettext ("%s wants to install a font", "%s wants to install fonts", len), task->priv->parent_title);
	} else {
		/* TRANSLATORS: a random program which we can't get the name wants to do something */
		text = g_strdup (ngettext ("A program wants to install a font", "A program wants to install fonts", len));
	}

	/* TRANSLATORS: button: confirm to search for packages */
	ret = gpk_dbus_task_confirm_action (task, text, message, _("Search"));
	g_free (text);
	g_free (message);
	if (!ret) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to search");
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

skip_checks:
	/* TRANSLATORS: title to show when searching for font files */
	title = ngettext ("Searching for font", "Searching for fonts", len);
	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	gpk_modal_dialog_set_title (task->priv->dialog, title);
	gpk_modal_dialog_set_image_status (task->priv->dialog, PK_STATUS_ENUM_WAIT);
	gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-finding-packages");

	/* setup the UI */
	if (task->priv->show_progress)
		gpk_modal_dialog_present (task->priv->dialog);

	/* do each one */
	list = pk_package_list_new ();
	for (i=0; i<len; i++) {

		/* reset */
		ret = pk_client_reset (task->priv->client_primary, &error_local);
		if (!ret) {
			/* TRANSLATORS: this is an internal error, and should not be seen */
			gpk_dbus_task_error_msg (task, _("Failed to reset client"), error_local);
			error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}

		/* set timeout */
		pk_client_set_timeout (task->priv->client_primary, task->priv->timeout, NULL);

		/* action: FIXME: synchronous */
		pk_client_set_synchronous (task->priv->client_primary, TRUE, NULL);
		ret = pk_client_what_provides (task->priv->client_primary, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED),
					       PK_PROVIDES_ENUM_FONT, fonts[i], &error_local);
		pk_client_set_synchronous (task->priv->client_primary, FALSE, NULL);
		if (!ret) {
			/* TRANSLATORS: we failed to find the package, this shouldn't happen */
			gpk_dbus_task_error_msg (task, _("Failed to search for provides"), error_local);
			error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_INTERNAL_ERROR, "%s", error_local->message);
			dbus_g_method_return_error (task->priv->context, error);
			goto out;
		}

		/* add to main list */
		list_tmp = pk_client_get_package_list (task->priv->client_primary);
		pk_obj_list_add_list (PK_OBJ_LIST (list), PK_OBJ_LIST (list_tmp));
		g_object_unref (list_tmp);
	}

	/* found nothing? */
	len = pk_package_list_get_size (list);
	if (len == 0) {
		if (task->priv->show_warning) {
			info_url = gpk_vendor_get_not_found_url (task->priv->vendor, GPK_VENDOR_URL_TYPE_FONT);
			/* TRANSLATORS: title: cannot find in sources */
			title = ngettext ("Failed to find font", "Failed to find fonts", len);
			/* only show the "more info" button if there is a valid link */
			if (info_url != NULL)
				gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, GPK_MODAL_DIALOG_BUTTON_ACTION);
			else
				gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			gpk_modal_dialog_set_title (task->priv->dialog, title);
			/* TRANSLATORS: message: tell the user we suck */
			gpk_modal_dialog_set_message (task->priv->dialog, _("No new fonts can be found for this document"));
			gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-package-not-found");
			/* TRANSLATORS: button: show the user a button to get more help finding stuff */
			gpk_modal_dialog_set_action (task->priv->dialog, _("More information"));
			gpk_modal_dialog_present (task->priv->dialog);
			button = gpk_modal_dialog_run (task->priv->dialog);
			if (button == GTK_RESPONSE_OK)
				gpk_gnome_open (info_url);
			g_free (info_url);
		}
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_NO_PACKAGES_FOUND, "failed to find font");
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* optional */
	if (!task->priv->show_confirm_deps) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks2;
	}

	/* TRANSLATORS: title: show a list of fonts */
	title = ngettext ("Do you want to install this package now?", "Do you want to install these packages now?", len);
	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, GPK_MODAL_DIALOG_PACKAGE_LIST);
	gpk_modal_dialog_set_package_list (task->priv->dialog, list);
	gpk_modal_dialog_set_title (task->priv->dialog, title);
	gpk_modal_dialog_set_message (task->priv->dialog, title);
	gpk_modal_dialog_set_image (task->priv->dialog, "dialog-information");
	/* TRANSLATORS: button: install a font */
	gpk_modal_dialog_set_action (task->priv->dialog, _("Install"));
	gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
	button = gpk_modal_dialog_run (task->priv->dialog);

	/* close, we're going to fail the method */
	if (button != GTK_RESPONSE_OK) {
		gpk_modal_dialog_close (task->priv->dialog);
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to download");
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

skip_checks2:
	/* convert to list of package id's */
	task->priv->package_ids = pk_package_list_to_strv (list);
	gpk_dbus_task_install_package_ids_dep_check (task);

out:
	if (error != NULL)
		g_error_free (error);
	if (error_local != NULL)
		g_error_free (error_local);
	if (list != NULL)
		g_object_unref (list);
}

/**
 * gpk_dbus_task_catalog_progress_cb:
 **/
static void
gpk_dbus_task_catalog_progress_cb (PkCatalog *catalog, PkCatalogProgress mode, const gchar *text, GpkDbusTask *task)
{
	gchar *message = NULL;

	g_return_if_fail (GPK_IS_DBUS_TASK (task));

	if (mode == PK_CATALOG_PROGRESS_PACKAGES) {
		/* TRANSLATORS: finding the package names for a catalog */
		message = g_strdup_printf (_("Finding package name: %s"), text);
	} else if (mode == PK_CATALOG_PROGRESS_FILES) {
		/* TRANSLATORS: finding a package for a file for a catalog */
		message = g_strdup_printf (_("Finding file name: %s"), text);
	} else if (mode == PK_CATALOG_PROGRESS_PROVIDES) {
		/* TRANSLATORS: finding a package which can provide a virtual provide */
		message = g_strdup_printf (_("Finding a package to provide: %s"), text);
	}
	gpk_dbus_task_set_status (task, PK_STATUS_ENUM_QUERY);
	gpk_modal_dialog_set_message (task->priv->dialog, message);
	g_free (message);
}

/**
 * gpk_dbus_task_install_catalogs:
 **/
void
gpk_dbus_task_install_catalogs (GpkDbusTask *task, gchar **filenames)
{
	GError *error = NULL;
	GtkResponseType button;
	gchar *message;
	const gchar *title;
	PkPackageList *list = NULL;
	PkCatalog *catalog;
	guint len;

	len = g_strv_length (filenames);

	/* optional */
	if (!task->priv->show_confirm_search) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks;
	}

	/* TRANSLATORS: title to install package catalogs */
	title = ngettext ("Do you want to install this catalog?",
			  "Do you want to install these catalogs?", len);
	message = g_strjoinv ("\n", filenames);

	/* show UI */
	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, 0);
	gpk_modal_dialog_set_title (task->priv->dialog, title);
	gpk_modal_dialog_set_message (task->priv->dialog, message);
	/* TRANSLATORS: button: install catalog */
	gpk_modal_dialog_set_action (task->priv->dialog, _("Install"));
	gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-install-catalogs");
	gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
	button = gpk_modal_dialog_run (task->priv->dialog);
	g_free (message);

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "did not agree to install");
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

skip_checks:
	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_PROGRESS, 0);
	/* TRANSLATORS: title: install package catalogs, that is, instructions for installing */
	gpk_modal_dialog_set_title (task->priv->dialog, _("Install catalogs"));
	gpk_dbus_task_set_status (task, PK_STATUS_ENUM_WAIT);

	/* setup the UI */
	if (task->priv->show_progress)
		gpk_modal_dialog_present (task->priv->dialog);

	/* get files to be installed */
	catalog = pk_catalog_new ();
	g_signal_connect (catalog, "progress", G_CALLBACK (gpk_dbus_task_catalog_progress_cb), task);
	list = pk_catalog_process_files (catalog, filenames);
	g_object_unref (catalog);

	/* nothing to do? */
	len = pk_package_list_get_size (list);
	if (len == 0) {
		/* show UI */
		if (task->priv->show_warning) {
			/* TRANSLATORS: title: we've already got all these packages installed */
			gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_WARNING, 0);
			gpk_modal_dialog_set_title (task->priv->dialog, _("No packages need to be installed"));
			gpk_modal_dialog_set_help_id (task->priv->dialog, "dialog-catalog-none-required");
			gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
			gpk_modal_dialog_run (task->priv->dialog);
		}
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_FAILED, "No packages need to be installed");
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

	/* optional */
	if (!task->priv->show_confirm_deps) {
		egg_debug ("skip confirm as not allowed to interact with user");
		goto skip_checks2;
	}

	gpk_modal_dialog_setup (task->priv->dialog, GPK_MODAL_DIALOG_PAGE_CONFIRM, GPK_MODAL_DIALOG_PACKAGE_LIST);
	/* TRANSLATORS: title: allow user to confirm */
	gpk_modal_dialog_set_title (task->priv->dialog, _("Install packages in catalog?"));
	/* TRANSLATORS: display a list of packages to install */
	gpk_modal_dialog_set_message (task->priv->dialog, _("The following packages are marked to be installed from the catalog:"));
	gpk_modal_dialog_set_image (task->priv->dialog, "dialog-question");
	gpk_modal_dialog_set_package_list (task->priv->dialog, list);
	/* TRANSLATORS: button: install packages in catalog */
	gpk_modal_dialog_set_action (task->priv->dialog, _("Install"));
	gpk_modal_dialog_present_with_time (task->priv->dialog, task->priv->timestamp);
	button = gpk_modal_dialog_run (task->priv->dialog);

	/* did we click no or exit the window? */
	if (button != GTK_RESPONSE_OK) {
		error = g_error_new (GPK_DBUS_ERROR, GPK_DBUS_ERROR_CANCELLED, "Action was cancelled");
		dbus_g_method_return_error (task->priv->context, error);
		goto out;
	}

skip_checks2:
	/* convert to list of package id's */
	task->priv->package_ids = pk_package_list_to_strv (list);
	gpk_dbus_task_install_package_ids_dep_check (task);

out:
	if (error != NULL)
		g_error_free (error);
	if (list != NULL)
		g_object_unref (list);
}

/**
 * gpk_dbus_task_get_package_for_exec:
 **/
static gchar *
gpk_dbus_task_get_package_for_exec (GpkDbusTask *task, const gchar *exec)
{
	gchar *package = NULL;
	gboolean ret;
	GError *error = NULL;
	guint length;
	PkPackageList *list = NULL;
	const PkPackageObj *obj;

	/* reset dbus_task */
	ret = pk_client_reset (task->priv->client_primary, &error);
	if (!ret) {
		egg_warning ("failed to reset client: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set timeout */
	pk_client_set_timeout (task->priv->client_primary, task->priv->timeout, NULL);

	/* find the package name */
	pk_client_set_synchronous (task->priv->client_primary, TRUE, NULL);
	ret = pk_client_search_file (task->priv->client_primary, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), exec, &error);
	pk_client_set_synchronous (task->priv->client_primary, FALSE, NULL);
	if (!ret) {
		egg_warning ("failed to search file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get the list of packages */
	list = pk_client_get_package_list (task->priv->client_primary);
	length = pk_package_list_get_size (list);

	/* nothing found */
	if (length == 0) {
		egg_debug ("cannot find installed package that provides : %s", exec);
		goto out;
	}

	/* check we have one */
	if (length != 1)
		egg_warning ("not one return, using first");

	/* copy name */
	obj = pk_package_list_get_obj (list, 0);
	package = g_strdup (obj->id->name);
	egg_debug ("got package %s", package);

out:
	/* use the exec name if we can't find an installed package */
	if (list != NULL)
		g_object_unref (list);
	return package;
}

/**
 * gpk_dbus_task_path_is_trusted:
 **/
static gboolean
gpk_dbus_task_path_is_trusted (const gchar *exec)
{
	/* special case the plugin helper -- it's trusted */
	if (egg_strequal (exec, "/usr/libexec/gst-install-plugins-helper") ||
	    egg_strequal (exec, "/usr/libexec/pk-gstreamer-install"))
		return TRUE;
	return FALSE;
}

/**
 * gpk_dbus_task_set_exec:
 *
 * This sets the package name of the application that is trying to install
 * software, e.g. "totem" and is used for the PkDesktop lookup to provide
 * a translated name and icon.
 **/
gboolean
gpk_dbus_task_set_exec (GpkDbusTask *task, const gchar *exec)
{
	GpkX11 *x11;
	gchar *package = NULL;

	g_return_val_if_fail (GPK_IS_DBUS_TASK (task), FALSE);

	/* old values invalid */
	g_free (task->priv->parent_title);
	g_free (task->priv->parent_icon_name);
	task->priv->parent_title = NULL;
	task->priv->parent_icon_name = NULL;

	/* is the binary trusted, i.e. can we probe it's window properties */
	if (gpk_dbus_task_path_is_trusted (exec)) {
		egg_debug ("using application window properties");
		/* get from window properties */
		x11 = gpk_x11_new ();
		gpk_x11_set_window (x11, task->priv->parent_window);
		task->priv->parent_title = gpk_x11_get_title (x11);
		g_object_unref (x11);
		goto out;
	}

	/* get from installed database */
	package = gpk_dbus_task_get_package_for_exec (task, exec);
	egg_debug ("got package %s", package);

	/* try to get from PkDesktop */
	if (package != NULL) {
		task->priv->parent_title = gpk_desktop_guess_localised_name (task->priv->desktop, package);
		task->priv->parent_icon_name = gpk_desktop_guess_icon_name (task->priv->desktop, package);
		/* fallback to package name */
		if (task->priv->parent_title == NULL) {
			egg_debug ("did not get localised description for %s", package);
			task->priv->parent_title = g_strdup (package);
		}
	}

	/* fallback to exec - eugh... */
	if (task->priv->parent_title == NULL) {
		egg_debug ("did not get package for %s, using exec basename", package);
		task->priv->parent_title = g_path_get_basename (exec);
	}
out:
	g_free (package);
	egg_debug ("got name=%s, icon=%s", task->priv->parent_title, task->priv->parent_icon_name);
	return TRUE;
}

/**
 * gpk_dbus_task_class_init:
 * @klass: The #GpkDbusTaskClass
 **/
static void
gpk_dbus_task_class_init (GpkDbusTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_dbus_task_finalize;
	g_type_class_add_private (klass, sizeof (GpkDbusTaskPrivate));
}

/**
 * gpk_dbus_task_init:
 * @task: a valid #GpkDbusTask instance
 **/
static void
gpk_dbus_task_init (GpkDbusTask *task)
{
	gboolean ret;
	GtkWindow *main_window;

	task->priv = GPK_DBUS_TASK_GET_PRIVATE (task);

	task->priv->package_ids = NULL;
	task->priv->files = NULL;
	task->priv->parent_window = NULL;
	task->priv->parent_title = NULL;
	task->priv->parent_icon_name = NULL;
	task->priv->error_details = NULL;
	task->priv->context = NULL;
	task->priv->exit = PK_EXIT_ENUM_FAILED;
	task->priv->show_confirm_search = TRUE;
	task->priv->show_confirm_deps = TRUE;
	task->priv->show_confirm_install = TRUE;
	task->priv->show_progress = TRUE;
	task->priv->show_finished = TRUE;
	task->priv->show_warning = TRUE;
	task->priv->timestamp = 0;
	task->priv->timeout = -1;
	task->priv->last_exit_code = PK_ERROR_ENUM_UNKNOWN;
	task->priv->role = GPK_DBUS_TASK_ROLE_UNKNOWN;

	/* add application specific icons to search path */
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   GPK_DATA G_DIR_SEPARATOR_S "icons");

	/* only initialise if the application didn't do it before */
	if (!notify_is_initted ())
		notify_init ("gpk-dbus_task");

	task->priv->vendor = gpk_vendor_new ();
	task->priv->dialog = gpk_modal_dialog_new ();
	main_window = gpk_modal_dialog_get_window (task->priv->dialog);
	gpk_modal_dialog_set_window_icon (task->priv->dialog, "pk-package-installed");
	g_signal_connect (task->priv->dialog, "cancel",
			  G_CALLBACK (gpk_dbus_task_button_cancel_cb), task);
	g_signal_connect (task->priv->dialog, "close",
			  G_CALLBACK (gpk_dbus_task_button_close_cb), task);

	/* helpers */
	task->priv->helper_repo_signature = gpk_helper_repo_signature_new ();
	g_signal_connect (task->priv->helper_repo_signature, "event", G_CALLBACK (gpk_dbus_task_repo_signature_event_cb), task);
	gpk_helper_repo_signature_set_parent (task->priv->helper_repo_signature, main_window);

	task->priv->helper_eula = gpk_helper_eula_new ();
	g_signal_connect (task->priv->helper_eula, "event", G_CALLBACK (gpk_dbus_task_eula_event_cb), task);
	gpk_helper_eula_set_parent (task->priv->helper_eula, main_window);

	task->priv->helper_run = gpk_helper_run_new ();
	gpk_helper_run_set_parent (task->priv->helper_run, main_window);

#if (!PK_CHECK_VERSION(0,5,0))
	task->priv->helper_untrusted = gpk_helper_untrusted_new ();
	g_signal_connect (task->priv->helper_untrusted, "event", G_CALLBACK (gpk_dbus_task_untrusted_event_cb), task);
	gpk_helper_untrusted_set_parent (task->priv->helper_untrusted, main_window);
#endif

	task->priv->helper_chooser = gpk_helper_chooser_new ();
	g_signal_connect (task->priv->helper_chooser, "event", G_CALLBACK (gpk_dbus_task_chooser_event_cb), task);
	gpk_helper_chooser_set_parent (task->priv->helper_chooser, main_window);

	/* map ISO639 to language names */
	task->priv->language = gpk_language_new ();
	gpk_language_populate (task->priv->language, NULL);

	/* use gconf for session settings */
	task->priv->gconf_client = gconf_client_get_default ();

	/* get actions */
	task->priv->control = pk_control_new ();
	task->priv->roles = pk_control_get_actions (task->priv->control, NULL);

	task->priv->client_primary = pk_client_new ();
	pk_client_set_use_buffer (task->priv->client_primary, TRUE, NULL);
	g_signal_connect (task->priv->client_primary, "finished",
			  G_CALLBACK (gpk_dbus_task_finished_cb), task);
	g_signal_connect (task->priv->client_primary, "progress-changed",
			  G_CALLBACK (gpk_dbus_task_progress_changed_cb), task);
	g_signal_connect (task->priv->client_primary, "status-changed",
			  G_CALLBACK (gpk_dbus_task_status_changed_cb), task);
	g_signal_connect (task->priv->client_primary, "error-code",
			  G_CALLBACK (gpk_dbus_task_error_code_cb), task);
	g_signal_connect (task->priv->client_primary, "package",
			  G_CALLBACK (gpk_dbus_task_package_cb), task);
	g_signal_connect (task->priv->client_primary, "allow-cancel",
			  G_CALLBACK (gpk_dbus_task_allow_cancel_cb), task);
	g_signal_connect (task->priv->client_primary, "repo-signature-required",
			  G_CALLBACK (gpk_dbus_task_repo_signature_required_cb), task);
	g_signal_connect (task->priv->client_primary, "eula-required",
			  G_CALLBACK (gpk_dbus_task_eula_required_cb), task);

	/* this is asynchronous, else we get into livelock */
	task->priv->client_secondary = pk_client_new ();
	g_signal_connect (task->priv->client_secondary, "finished",
			  G_CALLBACK (gpk_dbus_task_finished_cb), task);
	g_signal_connect (task->priv->client_secondary, "error-code",
			  G_CALLBACK (gpk_dbus_task_error_code_cb), task);
	g_signal_connect (task->priv->client_secondary, "status-changed",
			  G_CALLBACK (gpk_dbus_task_status_changed_cb), task);

	/* used for icons and translations */
	task->priv->desktop = pk_desktop_new ();
	ret = pk_desktop_open_database (task->priv->desktop, NULL);
	if (!ret)
		egg_warning ("failed to open desktop database");
}

/**
 * gpk_dbus_task_finalize:
 * @object: The object to finalize
 **/
static void
gpk_dbus_task_finalize (GObject *object)
{
	GpkDbusTask *task;

	g_return_if_fail (GPK_IS_DBUS_TASK (object));

	task = GPK_DBUS_TASK (object);
	g_return_if_fail (task->priv != NULL);

	g_free (task->priv->parent_title);
	g_free (task->priv->parent_icon_name);
	g_free (task->priv->error_details);
	g_strfreev (task->priv->files);
	g_strfreev (task->priv->package_ids);
	g_object_unref (task->priv->client_primary);
	g_object_unref (task->priv->client_secondary);
	g_object_unref (task->priv->control);
	g_object_unref (task->priv->desktop);
	g_object_unref (task->priv->gconf_client);
	g_object_unref (task->priv->dialog);
	g_object_unref (task->priv->vendor);
	g_object_unref (task->priv->language);
	g_object_unref (task->priv->helper_eula);
	g_object_unref (task->priv->helper_run);
#if (!PK_CHECK_VERSION(0,5,0))
	g_object_unref (task->priv->helper_untrusted);
#endif
	g_object_unref (task->priv->helper_chooser);
	g_object_unref (task->priv->helper_repo_signature);

	G_OBJECT_CLASS (gpk_dbus_task_parent_class)->finalize (object);
}

/**
 * gpk_dbus_task_new:
 *
 * PkClient is a nice GObject wrapper for gnome-packagekit and makes installing software easy
 *
 * Return value: A new %GpkDbusTask instance
 **/
GpkDbusTask *
gpk_dbus_task_new (void)
{
	GpkDbusTask *task;
	task = g_object_new (GPK_TYPE_DBUS_TASK, NULL);
	return GPK_DBUS_TASK (task);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
gpk_dbus_task_test (gpointer data)
{
	EggTest *test = (EggTest *) data;
	GpkDbusTask *task;
	gchar *lang;
	gchar *language;
	gchar *package;
	gboolean ret;
#if 0
	const gchar *fonts[] = { ":lang=mn", NULL };
	GError *error;
#endif

	if (egg_test_start (test, "GpkChooser") == FALSE)
		return;

	/************************************************************/
	egg_test_title (test, "get GpkDbusTask object");
	task = gpk_dbus_task_new ();
	if (task != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "convert tag to lang");
	lang = gpk_dbus_task_font_tag_to_lang (":lang=mn");
	if (egg_strequal (lang, "mn"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "lang '%s'", lang);
	g_free (lang);

	/************************************************************/
	egg_test_title (test, "convert tag to language");
	language = gpk_dbus_task_font_tag_to_localised_name (task, ":lang=mn");
	if (egg_strequal (language, "Mongolian"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "language '%s'", language);
	g_free (language);

	/************************************************************/
	egg_test_title (test, "test trusted path");
	ret = gpk_dbus_task_path_is_trusted ("/usr/libexec/gst-install-plugins-helper");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to identify trusted");

	/************************************************************/
	egg_test_title (test, "test trusted path");
	ret = gpk_dbus_task_path_is_trusted ("/usr/bin/totem");
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "identify untrusted as trusted!");

	/************************************************************/
	egg_test_title (test, "get package for exec");
	package = gpk_dbus_task_get_package_for_exec (task, "/usr/bin/totem");
	if (egg_strequal (package, "totem"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "package '%s'", package);
	g_free (package);

	/************************************************************/
	egg_test_title (test, "set exec");
	ret = gpk_dbus_task_set_exec (task, "/usr/bin/totem");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set exec");

#if 0
	/************************************************************/
	egg_test_title (test, "install fonts (no UI)");
	error = NULL;
	gpk_dbus_task_set_interaction (task, GPK_CLIENT_INTERACT_NEVER);
	ret = gpk_dbus_task_install_fontconfig_resources (task, (gchar**)fonts, &error);
	if (ret)
		egg_test_success (test, NULL);
	else {
		/* success until we can do the server parts */
		egg_test_success (test, "failed to install font : %s", error->message);
		g_error_free (error);
	}

	/************************************************************/
	egg_test_title (test, "install fonts (if found)");
	error = NULL;
	gpk_dbus_task_set_interaction (task, pk_bitfield_from_enums (GPK_CLIENT_INTERACT_CONFIRM_SEARCH, GPK_CLIENT_INTERACT_FINISHED, -1));
	ret = gpk_dbus_task_install_fontconfig_resources (task, (gchar**)fonts, &error);
	if (ret)
		egg_test_success (test, NULL);
	else {
		/* success until we can do the server parts */
		egg_test_success (test, "failed to install font : %s", error->message);
		g_error_free (error);
	}

	/************************************************************/
	egg_test_title (test, "install fonts (always)");
	error = NULL;
	gpk_dbus_task_set_interaction (task, GPK_CLIENT_INTERACT_ALWAYS);
	ret = gpk_dbus_task_install_fontconfig_resources (task, (gchar**)fonts, &error);
	if (ret)
		egg_test_success (test, NULL);
	else {
		/* success until we can do the server parts */
		egg_test_success (test, "failed to install font : %s", error->message);
		g_error_free (error);
	}
#endif

	egg_test_end (test);
}
#endif

