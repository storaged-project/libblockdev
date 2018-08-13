/*
 * Copyright (C) 2018  Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Vojtech Trefny <vtrefny@redhat.com>
 */

#include <glib.h>
#include <gio/gio.h>

#include "dbus.h"

#define DBUS_TOP_IFACE "org.freedesktop.DBus"
#define DBUS_TOP_OBJ "/org/freedesktop/DBus"
#define DBUS_PROPS_IFACE "org.freedesktop.DBus.Properties"
#define DBUS_INTRO_IFACE "org.freedesktop.DBus.Introspectable"


/**
 * bd_utils_dbus_error_quark: (skip)
 */
GQuark bd_utils_dbus_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-utils-dbus-error-quark");
}

/**
 * bd_utils_dbus_service_available:
 * @connection: (allow-none): existing GDBusConnection or %NULL
 * @bus_type: bus type (system or session), ignored if @connection is specified
 * @bus_name: name of the service to check (e.g. "com.redhat.lvmdbus1")
 * @obj_prefix: object path prefix for the service (e.g. "/com/redhat/lvmdbus1")
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the service was found in the system
 */
gboolean bd_utils_dbus_service_available (GDBusConnection *connection, GBusType bus_type, const gchar *bus_name, const gchar *obj_prefix, GError **error) {
    GVariant *ret = NULL;
    GVariant *real_ret = NULL;
    GVariantIter iter;
    GVariant *service = NULL;
    gboolean found = FALSE;
    GDBusConnection *bus = NULL;

    if (connection)
        bus = g_object_ref (connection);
    else {
        bus = g_bus_get_sync (bus_type, NULL, error);
        if (!bus) {
            g_critical ("Failed to get system bus: %s\n", (*error)->message);
            return FALSE;
        }

        ret = g_dbus_connection_call_sync (bus, DBUS_TOP_IFACE, DBUS_TOP_OBJ, DBUS_TOP_IFACE,
                                           "ListNames", NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
                                           -1, NULL, error);
        if (!ret) {
            g_object_unref (bus);
            return FALSE;
        }
    }

    real_ret = g_variant_get_child_value (ret, 0);
    g_variant_unref (ret);

    g_variant_iter_init (&iter, real_ret);
    while (!found && (service = g_variant_iter_next_value (&iter))) {
        found = (g_strcmp0 (g_variant_get_string (service, NULL), bus_name) == 0);
        g_variant_unref (service);
    }
    g_variant_unref (real_ret);

    ret = g_dbus_connection_call_sync (bus, DBUS_TOP_IFACE, DBUS_TOP_OBJ, DBUS_TOP_IFACE,
                                       "ListActivatableNames", NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
                                       -1, NULL, error);
    if (!ret) {
        g_object_unref (bus);
        return FALSE;
    }

    real_ret = g_variant_get_child_value (ret, 0);
    g_variant_unref (ret);

    g_variant_iter_init (&iter, real_ret);
    while (!found && (service = g_variant_iter_next_value (&iter))) {
        found = (g_strcmp0 (g_variant_get_string (service, NULL), bus_name) == 0);
        g_variant_unref (service);
    }
    g_variant_unref (real_ret);

    if (!found) {
        g_object_unref (bus);
        return FALSE;
    }

    /* try to introspect the root node - i.e. check we can access it and possibly
       autostart the service */
    ret = g_dbus_connection_call_sync (bus, bus_name, obj_prefix, DBUS_INTRO_IFACE,
                                       "Introspect", NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
                                       -1, NULL, error);
    if (!ret) {
        if (*error) {
            g_object_unref (bus);
            return FALSE;
        } else {
            g_object_unref (bus);
            return TRUE;
        }
    } else
        g_variant_unref (ret);

    g_object_unref (bus);
    return TRUE;
}
