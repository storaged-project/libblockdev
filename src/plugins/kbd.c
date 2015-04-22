/*
 * Copyright (C) 2015  Red Hat, Inc.
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
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <libkmod.h>
#include <string.h>
#include <syslog.h>

#include "kbd.h"

/**
 * SECTION: kbd
 * @short_description: plugin for operations with kernel block devices
 * @title: KernelBlockDevices
 * @include: kbd.h
 *
 * A plugin for operations with kernel block devices.
 */

/**
 * bd_kbd_error_quark: (skip)
 */
GQuark bd_kbd_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-kbd-error-quark");
}

static gboolean load_kernel_module (gchar *module_name, gchar *options, GError **error) {
    gint ret = 0;
    struct kmod_ctx *ctx = NULL;
    struct kmod_module *mod = NULL;
    gchar *null_config = NULL;

    ctx = kmod_new (NULL, (const gchar * const*) &null_config);
    if (!ctx) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_KMOD_INIT_FAIL,
                     "Failed to initialize kmod context");
        return FALSE;
    }
    /* prevent libkmod from spamming our STDERR */
    kmod_set_log_priority(ctx, LOG_CRIT);

    ret = kmod_module_new_from_name (ctx, module_name, &mod);
    if (ret < 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_MODULE_FAIL,
                     "Failed to get the module: %s", strerror (-ret));
        kmod_unref (ctx);
        return FALSE;
    }

    if (!kmod_module_get_path (mod)) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_MODULE_NOEXIST,
                     "Module '%s' doesn't exist", module_name);
        kmod_module_unref (mod);
        kmod_unref (ctx);
        return FALSE;
    }

    /* module, flags, options */
    ret = kmod_module_insert_module (mod, 0, options);
    if (ret < 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_MODULE_FAIL,
                     "Failed to load the module '%s' with options '%s': %s",
                     module_name, options, strerror (-ret));
        kmod_module_unref (mod);
        kmod_unref (ctx);
        return FALSE;
    }

    kmod_module_unref (mod);
    kmod_unref (ctx);
    return TRUE;
}

static gboolean unload_kernel_module (gchar *module_name, GError **error) {
    gint ret = 0;
    struct kmod_ctx *ctx = NULL;
    struct kmod_module *mod = NULL;
    struct kmod_list *list = NULL;
    struct kmod_list *cur = NULL;
    gchar *null_config = NULL;
    gboolean found = FALSE;

    ctx = kmod_new (NULL, (const gchar * const*) &null_config);
    if (!ctx) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_KMOD_INIT_FAIL,
                     "Failed to initialize kmod context");
        return FALSE;
    }
    /* prevent libkmod from spamming our STDERR */
    kmod_set_log_priority(ctx, LOG_CRIT);

    ret = kmod_module_new_from_loaded (ctx, &list);
    if (ret < 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_MODULE_FAIL,
                     "Failed to get the module: %s", strerror (-ret));
        kmod_unref (ctx);
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
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_MODULE_NOEXIST,
                     "Module '%s' is not loaded", module_name);
        kmod_unref (ctx);
        return FALSE;
    }

    /* module, flags */
    ret = kmod_module_remove_module (mod, 0);
    if (ret < 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_MODULE_FAIL,
                     "Failed to unload the module '%s': %s",
                     module_name, strerror (-ret));
        kmod_module_unref (mod);
        kmod_unref (ctx);
        return FALSE;
    }

    kmod_module_unref (mod);
    kmod_unref (ctx);
    return TRUE;
}

/**
 * bd_kbd_zram_create_devices:
 * @num_devices: number of devices to create
 * @sizes: (array zero-terminated=1): requested sizes (in bytes) for created zRAM
 *                                    devices
 * @nstreams: (allow-none) (array zero-terminated=1): numbers of streams for created
 *                                                    zRAM devices
 * @error: (out): place to store error (if any)
 *
 * Returns: whether @num_devices zRAM devices were successfully created or not
 *
 * **Lengths of @size and @nstreams (if given) have to be >= @num_devices!**
 */
gboolean bd_kbd_zram_create_devices (guint64 num_devices, guint64 *sizes, guint64 *nstreams, GError **error) {
    gchar *opts = NULL;
    gboolean success = FALSE;
    guint64 i = 0;
    gchar *num_str = NULL;
    gchar *file_name = NULL;
    GIOChannel *out_file = NULL;
    gsize bytes_written = 0;

    opts = g_strdup_printf ("num_devices=%"G_GUINT64_FORMAT, num_devices);
    success = load_kernel_module ("zram", opts, error);

    /* maybe it's loaded? Try to unload it first */
    if (!success && g_error_matches (*error, BD_KBD_ERROR, BD_KBD_ERROR_MODULE_FAIL)) {
        g_clear_error (error);
        success = unload_kernel_module ("zram", error);
        if (!success) {
            g_prefix_error (error, "zram module already loaded: ");
            g_free (opts);
            return FALSE;
        }
        success = load_kernel_module ("zram", opts, error);
        if (!success) {
            g_free (opts);
            return FALSE;
        }
    }
    g_free (opts);

    if (!success)
        /* error is already populated */
        return FALSE;

    /* compression streams have to be specified before the device is activated
       by setting its size */
    if (nstreams)
        for (i=0; i < num_devices; i++) {
            file_name = g_strdup_printf ("/sys/block/zram%"G_GUINT64_FORMAT"/max_comp_streams", i);
            num_str = g_strdup_printf ("%"G_GUINT64_FORMAT, nstreams[i]);
            out_file = g_io_channel_new_file (file_name, "w", error);
            g_free (file_name);
            if (!out_file || g_io_channel_write_chars (out_file, num_str, -1, &bytes_written, error) != G_IO_STATUS_NORMAL) {
                g_prefix_error (error, "Failed to set the number of compression streams: ");
                g_free (num_str);
                return FALSE;
            }
            g_free (num_str);
            if (g_io_channel_shutdown (out_file, TRUE, error) != G_IO_STATUS_NORMAL) {
                g_prefix_error (error, "Failed to set the number of compression streams: ");
                g_io_channel_unref (out_file);
                return FALSE;
            }
            g_io_channel_unref (out_file);
        }

    /* now activate the devices by setting their sizes */
    for (i=0; i < num_devices; i++) {
        file_name = g_strdup_printf ("/sys/block/zram%"G_GUINT64_FORMAT"/disksize", i);
        num_str = g_strdup_printf ("%"G_GUINT64_FORMAT, sizes[i]);
        out_file = g_io_channel_new_file (file_name, "w", error);
        g_free (file_name);
        if (!out_file || g_io_channel_write_chars (out_file, num_str, -1, &bytes_written, error) != G_IO_STATUS_NORMAL) {
            g_prefix_error (error, "Failed to set the size for the device: ");
            g_free (num_str);
            return FALSE;
        }
        g_free (num_str);
        if (g_io_channel_shutdown (out_file, TRUE, error) != G_IO_STATUS_NORMAL) {
            g_prefix_error (error, "Failed to set the size for the device: ");
            g_io_channel_unref (out_file);
            return FALSE;
        }
        g_io_channel_unref (out_file);
    }

    return TRUE;
}

/**
 * bd_kbd_zram_destroy_devices:
 * @error: (out): place to store error (if any)
 *
 * Returns: whether zRAM devices were successfully destroyed or not
 *
 * The only way how to destroy zRAM device right now is to unload the 'zram'
 * module and thus destroy all of them. That's why this function doesn't allow
 * specification of which devices should be destroyed.
 */
gboolean bd_kbd_zram_destroy_devices (GError **error) {
    return unload_kernel_module ("zram", error);
}
