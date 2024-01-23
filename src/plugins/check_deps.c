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
#include <gio/gio.h>
#include <blockdev/utils.h>

#include "check_deps.h"

#define DBUS_PROPS_IFACE "org.freedesktop.DBus.Properties"


G_GNUC_INTERNAL gboolean
check_deps (volatile guint *avail_deps, guint req_deps, const UtilDep *deps_specs, guint l_deps, GMutex *deps_check_lock, GError **error) {
    guint i = 0;
    gboolean ret = FALSE;
    GError *l_error = NULL;
    guint val = 0;

    val = (guint) g_atomic_int_get (avail_deps);
    if ((val & req_deps) == req_deps)
        /* we have everything we need */
        return TRUE;

    /* else */
    /* grab a lock to prevent multiple checks from running in parallel */
    g_mutex_lock (deps_check_lock);

    /* maybe the other thread found out we have all we needed? */
    val = (guint) g_atomic_int_get (avail_deps);
    if ((val & req_deps) == req_deps) {
        g_mutex_unlock (deps_check_lock);
        return TRUE;
    }

    for (i=0; i < l_deps; i++) {
        if (((1 << i) & req_deps) && !((1 << i) & val)) {
            ret = bd_utils_check_util_version (deps_specs[i].name, deps_specs[i].version,
                                               deps_specs[i].ver_arg, deps_specs[i].ver_regexp, &l_error);
            /* if not ret and l_error -> set/prepend error */
            if (!ret) {
                if (error) {
                    if (*error)
                        g_prefix_error (error, "%s\n", l_error->message);
                    else
                        g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_CHECK_ERROR,
                                     "%s", l_error->message);
                }
                g_clear_error (&l_error);
            } else
                g_atomic_int_or (avail_deps, 1 << i);
        }
    }

    g_mutex_unlock (deps_check_lock);
    val = (guint) g_atomic_int_get (avail_deps);
    return (val & req_deps) == req_deps;
}

G_GNUC_INTERNAL gboolean
check_module_deps (volatile guint *avail_deps, guint req_deps, const gchar *const*modules, guint l_modules, GMutex *deps_check_lock, GError **error) {
    guint i = 0;
    gboolean ret = FALSE;
    GError *l_error = NULL;
    guint val = 0;

    val = (guint) g_atomic_int_get (avail_deps);
    if ((val & req_deps) == req_deps)
        /* we have everything we need */
        return TRUE;

    /* else */
    /* grab a lock to prevent multiple checks from running in parallel */
    g_mutex_lock (deps_check_lock);

    /* maybe the other thread found out we have all we needed? */
    val = (guint) g_atomic_int_get (avail_deps);
    if ((val & req_deps) == req_deps) {
        g_mutex_unlock (deps_check_lock);
        return TRUE;
    }

    for (i=0; i < l_modules; i++) {
        if (((1 << i) & req_deps) && !((1 << i) & val)) {
            ret = bd_utils_have_kernel_module (modules[i], &l_error);
            /* if not ret and l_error -> set/prepend error */
            if (!ret) {
                if (l_error) {
                    if (error) {
                        if (*error)
                            g_prefix_error (error, "%s\n", l_error->message);
                        else
                            g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_MODULE_CHECK_ERROR,
                                         "%s", l_error->message);
                    }
                    g_clear_error (&l_error);
                } else {
                    /* no error from have_kernel_module means we don't have it */
                    if (error) {
                        if (*error)
                            g_prefix_error (error, "Kernel module '%s' not available\n", modules[i]);
                        else
                            g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_MODULE_CHECK_ERROR,
                                         "Kernel module '%s' not available", modules[i]);
                    }
                }

            } else
                g_atomic_int_or (avail_deps, 1 << i);
        }
    }

    g_mutex_unlock (deps_check_lock);
    val = (guint) g_atomic_int_get (avail_deps);
    return (val & req_deps) == req_deps;
}

static gboolean _check_dbus_api_version (GBusType bus_type, const gchar *version, const gchar *version_iface, const gchar* version_prop, const gchar *version_bus, const gchar *version_path, GError **error) {
    GDBusConnection *bus = NULL;
    GVariant *args = NULL;
    GVariant *ret = NULL;
    GVariant *prop = NULL;
    const gchar *bus_version = NULL;
    int cmp = 0;

    bus = g_bus_get_sync (bus_type, NULL, error);
    if (!bus)
        return FALSE;

    args = g_variant_new ("(ss)", version_iface, version_prop);

    /* consumes (frees) the 'args' parameter */
    ret = g_dbus_connection_call_sync (bus, version_bus, version_path, DBUS_PROPS_IFACE,
                                       "Get", args, NULL, G_DBUS_CALL_FLAGS_NONE,
                                       -1, NULL, error);
    if (!ret) {
        g_prefix_error (error, "Failed to get %s property of the %s object: ", version_prop, version_path);
        return FALSE;
    }

    g_variant_get (ret, "(v)", &prop);
    g_variant_unref (ret);

    bus_version = g_variant_get_string (prop, NULL);

    cmp = bd_utils_version_cmp (bus_version, version, error);
    g_variant_unref (prop);

    return cmp >= 0;
}

G_GNUC_INTERNAL gboolean
check_dbus_deps (volatile guint *avail_deps, guint req_deps, const DBusDep *buses, guint l_buses, GMutex *deps_check_lock, GError **error) {
    guint i = 0;
    gboolean ret = FALSE;
    GError *l_error = NULL;
    guint val = 0;

    val = (guint) g_atomic_int_get (avail_deps);
    if ((val & req_deps) == req_deps)
        /* we have everything we need */
        return TRUE;

    /* else */
    /* grab a lock to prevent multiple checks from running in parallel */
    g_mutex_lock (deps_check_lock);

    /* maybe the other thread found out we have all we needed? */
    val = (guint) g_atomic_int_get (avail_deps);
    if ((val & req_deps) == req_deps) {
        g_mutex_unlock (deps_check_lock);
        return TRUE;
    }

    for (i=0; i < l_buses; i++) {
        if (((1 << i) & req_deps) && !((1 << i) & val)) {
            ret = bd_utils_dbus_service_available (NULL, buses[i].bus_type, buses[i].bus_name, buses[i].obj_prefix, &l_error);
            /* if not ret and l_error -> set/prepend error */
            if (!ret) {
                if (l_error) {
                    if (error) {
                        if (*error)
                            g_prefix_error (error, "%s\n", l_error->message);
                        else
                            g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_MODULE_CHECK_ERROR,
                                         "%s", l_error->message);
                    }
                    g_clear_error (&l_error);
                } else {
                    if (error) {
                        if (*error)
                            g_prefix_error (error, "DBus service '%s' not available\n", buses[i].bus_name);
                        else
                            g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_MODULE_CHECK_ERROR,
                                         "DBus service '%s' not available", buses[i].bus_name);
                    }
                }
            } else {
                /* check version of the DBus API if specified */
                if (buses[i].version) {
                    ret = _check_dbus_api_version (buses[i].bus_type, buses[i].version, buses[i].ver_intf, buses[i].ver_prop,
                                                   buses[i].bus_name, buses[i].ver_path, &l_error);
                    /* if not ret and l_error -> set/prepend error */
                    if (!ret) {
                        if (l_error) {
                            if (error) {
                                if (*error)
                                    g_prefix_error (error, "%s\n", l_error->message);
                                else
                                    g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_MODULE_CHECK_ERROR,
                                                 "%s", l_error->message);
                            }
                            g_clear_error (&l_error);
                        } else {
                            if (error) {
                                if (*error)
                                    g_prefix_error (error, "DBus service '%s' not available in version '%s'\n", buses[i].bus_name, buses[i].version);
                                else
                                    g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_MODULE_CHECK_ERROR,
                                                 "DBus service '%s' not available in version '%s'", buses[i].bus_name, buses[i].version);
                            }
                        }
                    } else
                       g_atomic_int_or (avail_deps, 1 << i);
                } else
                    g_atomic_int_or (avail_deps, 1 << i);
            }
        }
    }

    g_mutex_unlock (deps_check_lock);
    val = (guint) g_atomic_int_get (avail_deps);
    return (val & req_deps) == req_deps;
}

static gboolean _check_util_feature (const gchar *util, const gchar *feature, const gchar *feature_arg, const gchar *feature_regexp, GError **error) {
    g_autofree gchar *util_path = NULL;
    const gchar *argv[] = {util, feature_arg, NULL};
    g_autofree gchar *output = NULL;
    gboolean succ = FALSE;
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    g_autofree gchar *features_str = NULL;
    GError *l_error = NULL;

    util_path = g_find_program_in_path (util);
    if (!util_path) {
        g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_UNAVAILABLE,
                     "The '%s' utility is not available", util);
        return FALSE;
    }

    succ = bd_utils_exec_and_capture_output (argv, NULL, &output, &l_error);
    if (!succ) {
        /* if we got nothing on STDOUT, try using STDERR data from error message */
        if (g_error_matches (l_error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_NOOUT)) {
            output = g_strdup (l_error->message);
            g_clear_error (&l_error);
        } else if (g_error_matches (l_error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED)) {
            /* exit status != 0, try using the output anyway */
            output = g_strdup (l_error->message);
            g_clear_error (&l_error);
        } else
            return FALSE;
    }

    if (feature_regexp) {
        regex = g_regex_new (feature_regexp, 0, 0, error);
        if (!regex) {
            /* error is already populated */
            return FALSE;
        }

        succ = g_regex_match (regex, output, 0, &match_info);
        if (!succ) {
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_FEATURE_CHECK_ERROR,
                         "Failed to determine %s's features from: %s", util, output);
            g_regex_unref (regex);
            g_match_info_free (match_info);
            return FALSE;
        }
        g_regex_unref (regex);

        features_str = g_match_info_fetch (match_info, 1);
        g_match_info_free (match_info);
    }
    else
        features_str = g_strstrip (g_strdup (output));

    if (!features_str || (g_strcmp0 (features_str, "") == 0)) {
        g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_FEATURE_CHECK_ERROR,
                     "Failed to determine %s's features from: %s", util, output);
        return FALSE;
    }


    if (!g_strrstr (features_str, feature)) {
        g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_FEATURE_UNAVAILABLE,
                     "Required feature %s not supported by this version of %s",
                     feature, util);
        return FALSE;
    }

    return TRUE;
}

G_GNUC_INTERNAL gboolean
check_features (volatile guint *avail_deps, guint req_deps, const UtilFeatureDep *deps_specs, guint l_deps, GMutex *deps_check_lock, GError **error) {
    guint i = 0;
    gboolean ret = FALSE;
    GError *l_error = NULL;
    guint val = 0;

    val = (guint) g_atomic_int_get (avail_deps);
    if ((val & req_deps) == req_deps)
        /* we have everything we need */
        return TRUE;

    /* else */
    /* grab a lock to prevent multiple checks from running in parallel */
    g_mutex_lock (deps_check_lock);

    /* maybe the other thread found out we have all we needed? */
    val = (guint) g_atomic_int_get (avail_deps);
    if ((val & req_deps) == req_deps) {
        g_mutex_unlock (deps_check_lock);
        return TRUE;
    }

    for (i=0; i < l_deps; i++) {
        if (((1 << i) & req_deps) && !((1 << i) & val)) {
            ret = _check_util_feature (deps_specs[i].util_name, deps_specs[i].feature,
                                       deps_specs[i].feature_arg, deps_specs[i].feature_regexp, &l_error);
            /* if not ret and l_error -> set/prepend error */
            if (!ret) {
                if (l_error) {
                    if (error) {
                        if (*error)
                            g_prefix_error (error, "%s\n", l_error->message);
                        else
                            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_FEATURE_CHECK_ERROR,
                                        "%s", l_error->message);
                    }
                    g_clear_error (&l_error);
                }
            } else
                g_atomic_int_or (avail_deps, 1 << i);
        }
    }

    g_mutex_unlock (deps_check_lock);
    val = (guint) g_atomic_int_get (avail_deps);
    return (val & req_deps) == req_deps;
}
