/*
 * Copyright (C) 2017 Red Hat, Inc.
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
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <glib.h>

typedef struct UtilDep {
    const gchar *name;
    const gchar *version;
    const gchar *ver_arg;
    const gchar *ver_regexp;
} UtilDep;

typedef struct DBusDep {
    const gchar *bus_name;
    const gchar *obj_prefix;
    GBusType bus_type;
} DBusDep;

typedef struct UtilFeatureDep {
    const gchar *util_name;
    const gchar *feature;
    const gchar *feature_arg;
    const gchar *feature_regexp;
} UtilFeatureDep;

gboolean check_deps (volatile guint *avail_deps, guint req_deps, const UtilDep *deps_specs, guint l_deps, GMutex *deps_check_lock, GError **error);
gboolean check_module_deps (volatile guint *avail_deps, guint req_deps, const gchar *const*modules, guint l_modules, GMutex *deps_check_lock, GError **error);
gboolean check_dbus_deps (volatile guint *avail_deps, guint req_deps, const DBusDep *buses, guint l_buses, GMutex *deps_check_lock, GError **error);
gboolean check_features (volatile guint *avail_deps, guint req_deps, const UtilFeatureDep *deps_specs, guint l_deps, GMutex *deps_check_lock, GError **error);
