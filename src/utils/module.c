/*
 * Copyright (C) 2017  Red Hat, Inc.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Vojtech Trefny <vtrefny@redhat.com>
 */

#include <glib.h>
#include <libkmod.h>
#include <string.h>
#include <syslog.h>
#include <locale.h>

#include "module.h"


/**
 * bd_utils_module_error_quark: (skip)
 */
GQuark bd_utils_module_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-utils-module-error-quark");
}

/**
 * bd_utils_have_kernel_module:
 * @module_name: name of the kernel module to check
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @module_name was found in the system
 */
gboolean bd_utils_have_kernel_module (const gchar *module_name, GError **error) {
    gint ret = 0;
    struct kmod_ctx *ctx = NULL;
    struct kmod_module *mod = NULL;
    gchar *null_config = NULL;
    const gchar *path = NULL;
    gboolean have_path = FALSE;
    locale_t c_locale = newlocale (LC_ALL_MASK, "C", (locale_t) 0);

    ctx = kmod_new (NULL, (const gchar * const*) &null_config);
    if (!ctx) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_KMOD_INIT_FAIL,
                     "Failed to initialize kmod context");
        freelocale (c_locale);
        return FALSE;
    }
    /* prevent libkmod from spamming our STDERR */
    kmod_set_log_priority(ctx, LOG_CRIT);

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
    kmod_module_unref (mod);
    kmod_unref (ctx);
    freelocale (c_locale);

    return have_path;
}

/**
 * bd_utils_load_kernel_module:
 * @module_name: name of the kernel module to load
 * @options: (allow-none): module options
 * @error: (out): place to store error (if any)
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
    /* prevent libkmod from spamming our STDERR */
    kmod_set_log_priority(ctx, LOG_CRIT);

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

    /* module, flags, options */
    ret = kmod_module_insert_module (mod, 0, options);
    if (ret < 0) {
        g_set_error (error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_FAIL,
                     "Failed to load the module '%s' with options '%s': %s",
                     module_name, options, strerror_l (-ret, c_locale));
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
 * @error: (out): place to store error (if any)
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
    /* prevent libkmod from spamming our STDERR */
    kmod_set_log_priority(ctx, LOG_CRIT);

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
