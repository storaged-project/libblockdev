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
#include <blockdev/utils.h>

#include "check_deps.h"

gboolean __attribute__ ((visibility ("hidden")))
check_deps (volatile guint *avail_deps, guint req_deps, const UtilDep *deps_specs, guint l_deps, GMutex *deps_check_lock, GError **error) {
    guint i = 0;
    gboolean ret = FALSE;
    GError *l_error = NULL;
    guint val = 0;

    val = (guint) g_atomic_int_get (avail_deps);
    if (val & req_deps)
        /* we have everything we need */
        return TRUE;

    /* else */
    /* grab a lock to prevent multiple checks from running in parallel */
    g_mutex_lock (deps_check_lock);

    /* maybe the other thread found out we have all we needed? */
    val = (guint) g_atomic_int_get (avail_deps);
    if (val & req_deps) {
        g_mutex_unlock (deps_check_lock);
        return TRUE;
    }

    for (i=0; i < l_deps; i++) {
        if (((1 << i) & req_deps) && !((1 << i) & val)) {
            ret = bd_utils_check_util_version (deps_specs[i].name, deps_specs[i].version,
                                               deps_specs[i].ver_arg, deps_specs[i].ver_regexp, &l_error);
            /* if not ret and l_error -> set/prepend error */
            if (!ret) {
                if (*error)
                    g_prefix_error (error, "%s\n", l_error->message);
                else
                    g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_CHECK_ERROR,
                                 "%s", l_error->message);
                g_clear_error (&l_error);
            } else
                g_atomic_int_or (avail_deps, 1 << i);
        }
    }

    g_mutex_unlock (deps_check_lock);
    val = (guint) g_atomic_int_get (avail_deps);
    return (val & req_deps) != 0;
}

gboolean __attribute__ ((visibility ("hidden")))
check_module_deps (volatile guint *avail_deps, guint req_deps, const gchar *const*modules, guint l_modules, GMutex *deps_check_lock, GError **error) {
    guint i = 0;
    gboolean ret = FALSE;
    GError *l_error = NULL;
    guint val = 0;

    val = (guint) g_atomic_int_get (avail_deps);
    if (val & req_deps)
        /* we have everything we need */
        return TRUE;

    /* else */
    /* grab a lock to prevent multiple checks from running in parallel */
    g_mutex_lock (deps_check_lock);

    /* maybe the other thread found out we have all we needed? */
    val = (guint) g_atomic_int_get (avail_deps);
    if (val & req_deps) {
        g_mutex_unlock (deps_check_lock);
        return TRUE;
    }

    for (i=0; i < l_modules; i++) {
        if (((1 << i) & req_deps) && !((1 << i) & val)) {
            ret = bd_utils_have_kernel_module (modules[i], &l_error);
            /* if not ret and l_error -> set/prepend error */
            if (!ret) {
                if (l_error) {
                    if (*error)
                        g_prefix_error (error, "%s\n", l_error->message);
                    else
                        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_MODULE_CHECK_ERROR,
                                     "%s", l_error->message);
                    g_clear_error (&l_error);
                } else {
                    /* no error from have_kernel_module means we don't have it */
                    if (*error)
                        g_prefix_error (error, "Kernel module '%s' not available\n", modules[i]);
                    else
                        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_MODULE_CHECK_ERROR,
                                     "Kernel module '%s' not available", modules[i]);
                }

            } else
                g_atomic_int_or (avail_deps, 1 << i);
        }
    }

    g_mutex_unlock (deps_check_lock);
    val = (guint) g_atomic_int_get (avail_deps);
    return (val & req_deps) != 0;
}
