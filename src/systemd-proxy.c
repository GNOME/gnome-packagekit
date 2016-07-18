/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Matthias Clasen
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

#include <config.h>

#include <gio/gio.h>
#include <polkit/polkit.h>

#include "systemd-proxy.h"

#define SYSTEMD_DBUS_NAME      "org.freedesktop.login1"
#define SYSTEMD_DBUS_PATH      "/org/freedesktop/login1"
#define SYSTEMD_DBUS_INTERFACE "org.freedesktop.login1.Manager"
#define SYSTEMD_REBOOT_ACTION  "org.freedesktop.login1.reboot"

struct _SystemdProxy {
        PolkitAuthority *authority;
        PolkitSubject *subject;
};

SystemdProxy *
systemd_proxy_new (void)
{
        SystemdProxy *proxy;

        proxy = g_new0 (SystemdProxy, 1);

        proxy->authority = polkit_authority_get_sync (NULL, NULL);
        proxy->subject = polkit_unix_process_new_for_owner(getpid(), 0, -1);

        return proxy;
}

void
systemd_proxy_free (SystemdProxy *proxy)
{
        g_object_unref (proxy->authority);
        g_object_unref (proxy->subject);

        g_free (proxy);
}

gboolean
systemd_proxy_can_restart (SystemdProxy  *proxy,
                           gboolean      *can_restart,
                           GError       **error)
{
        PolkitAuthorizationResult *res;
        GError *local_error = NULL;

        *can_restart = FALSE;
        res = polkit_authority_check_authorization_sync (proxy->authority,
                                                         proxy->subject,
                                                         SYSTEMD_REBOOT_ACTION,
                                                         NULL,
                                                         POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                                         NULL,
                                                         &local_error);
        if (res == NULL) {
                g_propagate_error (error, local_error);
                return FALSE;
        }

        *can_restart = polkit_authorization_result_get_is_authorized (res) ||
                       polkit_authorization_result_get_is_challenge (res);

        g_object_unref (res);

        return TRUE;
}

gboolean
systemd_proxy_restart (SystemdProxy  *proxy,
                       GError       **error)
{
        g_autoptr(GDBusConnection) bus = NULL;

        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
        g_dbus_connection_call_sync (bus,
                                    SYSTEMD_DBUS_NAME,
                                    SYSTEMD_DBUS_PATH,
                                    SYSTEMD_DBUS_INTERFACE,
                                    "Reboot",
                                    g_variant_new ("(b)", TRUE),
                                    NULL, 0, G_MAXINT, NULL, NULL);
        return TRUE;
}
