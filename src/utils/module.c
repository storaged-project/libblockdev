/*
 * Copyright (C) 2017  Red Hat, Inc.
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
#include <glib/gprintf.h>
#include <libkmod.h>
#include <string.h>
#include <syslog.h>
#include <locale.h>
#include <sys/utsname.h>

#include "module.h"
#include "exec.h"
#include "logging.h"

#define UNUSED __attribute__((unused))

/**
 * bd_utils_module_error_quark: (skip)
 */
GQuark bd_utils_module_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-utils-module-error-quark");
}

static void utils_kmod_log_redirect (void *log_data UNUSED, int priority,
                                     const char *file UNUSED, int line UNUSED,
                                     const char *fn UNUSED, const char *format,
                                     va_list args) {
    gchar *kmod_msg = NULL;
    gchar *message = NULL;
    gint ret = 0;

    ret = g_vasprintf (&kmod_msg, format, args);
    if (ret < 0) {
        g_free (kmod_msg);
        return;
    }

#ifdef DEBUG
    message = g_strdup_printf ("[libmkod] %s:%d %s() %s", file, line, fn, kmod_msg);
#else
    message = g_strdup_printf ("[libmkod] %s", kmod_msg);
#endif
    bd_utils_log (priority, message);

    g_free (kmod_msg);
    g_free (message);

}

static void set_kmod_logging (struct kmod_ctx *ctx) {
#ifdef DEBUG
    kmod_set_log_priority (ctx, LOG_DEBUG);
#else
    kmod_set_log_priority (ctx, LOG_INFO);
#endif
    kmod_set_log_fn (ctx, utils_kmod_log_redirect, NULL);
}

/**
 * bd_utils_have_kernel_module:
 * @module_name: name of the kernel module to check
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @module_name was found in the system, either as a module
 * or built-in in the kernel
 */
gboolean bd_utils_have_kernel_module (const gchar *module_name, GError **error) {
    gint ret = 0;
    struct kmod_ctx *ctx = NULL;
    struct kmod_module *mod = NULL;
    gchar *null_config = NULL;
    const gchar *path = NULL;
    gboolean have_path = FALSE;
    gboolean builtin = FALSE;
    locale_t c_locale = newlocale (LC_ALL_MASK, "C", (locale_t) 0);

    ctx = kmod_new (NULL, (const gchar * const*) &null_config);
    if (!ctx) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_KMOD_INIT_FAIL,
                     "Failed to initialize kmod context");
        freelocale (c_locale);
        return FALSE;
    }
    set_kmod_logging (ctx);

    ret = kmod_module_new_from_name (ctx, module_name, &mod);
    if (ret < 0) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_FAIL,
                     "Failed to get the module: %s", strerror_l (-ret, c_locale));
        kmod_unref (ctx);
        freelocale (c_locale);
        return FALSE;
    }

    path = kmod_module_get_path (mod);
    have_path = (path != NULL) && (g_strcmp0 (path, "") != 0);
    if (!have_path) {
      builtin = kmod_module_get_initstate (mod) == KMOD_MODULE_BUILTIN;
    }
    kmod_module_unref (mod);
    kmod_unref (ctx);
    freelocale (c_locale);

    return have_path || builtin;
}

/**
 * bd_utils_load_kernel_module:
 * @module_name: name of the kernel module to load
 * @options: (nullable): module options
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @module_name was successfully loaded or not
 */
gboolean bd_utils_load_kernel_module (const gchar *module_name, const gchar *options, GError **error) {
    gint ret = 0;
    struct kmod_ctx *ctx = NULL;
    struct kmod_module *mod = NULL;
    gchar *null_config = NULL;
    locale_t c_locale = newlocale (LC_ALL_MASK, "C", (locale_t) 0);

    ctx = kmod_new (NULL, (const gchar * const*) &null_config);
    if (!ctx) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_KMOD_INIT_FAIL,
                     "Failed to initialize kmod context");
        freelocale (c_locale);
        return FALSE;
    }
    set_kmod_logging (ctx);

    ret = kmod_module_new_from_name (ctx, module_name, &mod);
    if (ret < 0) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_FAIL,
                     "Failed to get the module: %s", strerror_l (-ret, c_locale));
        kmod_unref (ctx);
        freelocale (c_locale);
        return FALSE;
    }

    if (!kmod_module_get_path (mod)) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_NOEXIST,
                     "Module '%s' doesn't exist", module_name);
        kmod_module_unref (mod);
        kmod_unref (ctx);
        freelocale (c_locale);
        return FALSE;
    }

    /* module, flags, options, run_install, data, print_action
       flag KMOD_PROBE_FAIL_ON_LOADED is used for backwards compatibility */
    ret = kmod_module_probe_insert_module (mod, KMOD_PROBE_FAIL_ON_LOADED,
                                           options, NULL, NULL, NULL);
    if (ret < 0) {
        if (options)
            g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_FAIL,
                         "Failed to load the module '%s' with options '%s': %s",
                         module_name, options, strerror_l (-ret, c_locale));
        else
            g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_FAIL,
                         "Failed to load the module '%s': %s",
                         module_name, strerror_l (-ret, c_locale));
        kmod_module_unref (mod);
        kmod_unref (ctx);
        freelocale (c_locale);
        return FALSE;
    }

    kmod_module_unref (mod);
    kmod_unref (ctx);
    freelocale (c_locale);
    return TRUE;
}

/**
 * bd_utils_unload_kernel_module:
 * @module_name: name of the kernel module to unload
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @module_name was successfully unloaded or not
 */
gboolean bd_utils_unload_kernel_module (const gchar *module_name, GError **error) {
    gint ret = 0;
    struct kmod_ctx *ctx = NULL;
    struct kmod_module *mod = NULL;
    struct kmod_list *list = NULL;
    struct kmod_list *cur = NULL;
    gchar *null_config = NULL;
    gboolean found = FALSE;
    locale_t c_locale = newlocale (LC_ALL_MASK, "C", (locale_t) 0);

    ctx = kmod_new (NULL, (const gchar * const*) &null_config);
    if (!ctx) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_KMOD_INIT_FAIL,
                     "Failed to initialize kmod context");
        freelocale (c_locale);
        return FALSE;
    }
    set_kmod_logging (ctx);

    ret = kmod_module_new_from_loaded (ctx, &list);
    if (ret < 0) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_FAIL,
                     "Failed to get the module: %s", strerror_l (-ret, c_locale));
        kmod_unref (ctx);
        freelocale (c_locale);
        return FALSE;
    }

    for (cur=list; !found && cur != NULL; cur = kmod_list_next(list, cur)) {
        mod = kmod_module_get_module (cur);
        if (g_strcmp0 (kmod_module_get_name (mod), module_name) == 0)
            found = TRUE;
        else
            kmod_module_unref (mod);
    }
    kmod_module_unref_list (list);

    if (!found) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_NOEXIST,
                     "Module '%s' is not loaded", module_name);
        kmod_unref (ctx);
        freelocale (c_locale);
        return FALSE;
    }

    /* module, flags */
    ret = kmod_module_remove_module (mod, 0);
    if (ret < 0) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_FAIL,
                     "Failed to unload the module '%s': %s",
                     module_name, strerror_l (-ret, c_locale));
        kmod_module_unref (mod);
        kmod_unref (ctx);
        freelocale (c_locale);
        return FALSE;
    }

    kmod_module_unref (mod);
    kmod_unref (ctx);
    freelocale (c_locale);
    return TRUE;
}


static BDUtilsLinuxVersion detected_linux_ver;
static gboolean have_linux_ver = FALSE;

G_LOCK_DEFINE_STATIC (detected_linux_ver);

/**
 * bd_utils_get_linux_version:
 * @error: (out) (optional): place to store error (if any)
 *
 * Retrieves version of currently running Linux kernel. Acts also as an initializer for statically cached data.
 *
 * Returns: (transfer none): Detected Linux kernel version or %NULL in case of an error. The returned value belongs to the library, do not free.
 */
BDUtilsLinuxVersion * bd_utils_get_linux_version (GError **error) {
    struct utsname buf;

    /* return cached value if available */
    if (have_linux_ver)
        return &detected_linux_ver;

    G_LOCK (detected_linux_ver);

    memset (&detected_linux_ver, 0, sizeof (BDUtilsLinuxVersion));

    if (uname (&buf)) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_FAIL,
                     "Failed to get linux kernel version: %m");
        G_UNLOCK (detected_linux_ver);
        return NULL;
      }

    if (g_ascii_strncasecmp (buf.sysname, "Linux", sizeof buf.sysname) != 0) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_INVALID_PLATFORM,
                     "Failed to get kernel version: spurious sysname '%s' detected", buf.sysname);
        G_UNLOCK (detected_linux_ver);
        return NULL;
      }

    if (sscanf (buf.release, "%d.%d.%d",
                &detected_linux_ver.major,
                &detected_linux_ver.minor,
                &detected_linux_ver.micro) < 1) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_FAIL,
                     "Failed to parse kernel version: malformed release string '%s'", buf.release);
        G_UNLOCK (detected_linux_ver);
        return NULL;
    }

    have_linux_ver = TRUE;
    G_UNLOCK (detected_linux_ver);

    return &detected_linux_ver;
}

/**
 * bd_utils_check_linux_version:
 * @major: Minimal major linux kernel version.
 * @minor: Minimal minor linux kernel version.
 * @micro: Minimal micro linux kernel version.
 *
 * Checks whether the currently running linux kernel version is equal or higher
 * than the specified required @major.@minor.@micro version.
 *
 * Returns: an integer less than, equal to, or greater than zero, if detected version is <, == or > than the specified @major.@minor.@micro version.
 */
gint bd_utils_check_linux_version (guint major, guint minor, guint micro) {
    gint ret;

    if (!have_linux_ver)
        bd_utils_get_linux_version (NULL);

    G_LOCK (detected_linux_ver);
    ret = detected_linux_ver.major - major;
    if (ret == 0)
        ret = detected_linux_ver.minor - minor;
    if (ret == 0)
        ret = detected_linux_ver.micro - micro;

    G_UNLOCK (detected_linux_ver);

    return ret;
}
