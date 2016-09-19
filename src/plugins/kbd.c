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
#include <glob.h>
#include <unistd.h>
#include <locale.h>
#include <utils.h>

#include "kbd.h"

#define SECTOR_SIZE 512

/**
 * SECTION: kbd
 * @short_description: plugin for operations with kernel block devices
 * @title: KernelBlockDevices
 * @include: kbd.h
 *
 * A plugin for operations with kernel block devices.
 */

static const gchar * const mode_str[BD_KBD_MODE_UNKNOWN+1] = {"writethrough", "writeback", "writearound", "none", "unknown"};

/* "C" locale to get the locale-agnostic error messages */
static locale_t c_locale = (locale_t) 0;

/**
 * init: (skip)
 */
gboolean init () {
    c_locale = newlocale (LC_ALL_MASK, "C", c_locale);
    return TRUE;
}

/**
 * bd_kbd_error_quark: (skip)
 */
GQuark bd_kbd_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-kbd-error-quark");
}

BDKBDZramStats* bd_kbd_zram_stats_copy (BDKBDZramStats *data) {
    BDKBDZramStats *new = g_new0 (BDKBDZramStats, 1);
    new->disksize = data->disksize;
    new->num_reads = data->num_reads;
    new->num_writes = data->num_writes;
    new->invalid_io = data->invalid_io;
    new->zero_pages = data->zero_pages;
    new->max_comp_streams = data->max_comp_streams;
    new->comp_algorithm = g_strdup (data->comp_algorithm);
    new->orig_data_size = data->orig_data_size;
    new->compr_data_size = data->compr_data_size;
    new->mem_used_total = data->mem_used_total;

    return new;
}

void bd_kbd_zram_stats_free (BDKBDZramStats *data) {
    g_free (data->comp_algorithm);
    g_free (data);
}

BDKBDBcacheStats* bd_kbd_bcache_stats_copy (BDKBDBcacheStats *data) {
    BDKBDBcacheStats *new = g_new0 (BDKBDBcacheStats, 1);

    new->state = g_strdup (data->state);
    new->block_size = data->block_size;
    new->cache_size = data->cache_size;
    new->cache_used = data->cache_used;
    new->hits = data->hits;
    new->misses = data->misses;
    new->bypass_hits = data->bypass_hits;
    new->bypass_misses = data->bypass_misses;

    return new;
}

void bd_kbd_bcache_stats_free (BDKBDBcacheStats *data) {
    g_free (data->state);
    g_free (data);
}

static gboolean have_kernel_module (const gchar *module_name, GError **error);

/**
 * check: (skip)
 */
gboolean check() {
    GError *error = NULL;
    gboolean ret = FALSE;

    ret = have_kernel_module ("zram", &error);
    if (!ret) {
        if (error) {
            g_warning("Cannot load the kbd plugin: %s" , error->message);
            g_clear_error (&error);
        } else
            g_warning("Cannot load the kbd plugin: the 'zram' kernel module is not available");
    }

    if (!ret)
        return FALSE;

    ret = bd_utils_check_util_version ("make-bcache", NULL, NULL, NULL, &error);
    if (!ret && error) {
        g_warning("Cannot load the kbd plugin: %s" , error->message);
        g_clear_error (&error);
    }

    return ret;
}

static gboolean have_kernel_module (const gchar *module_name, GError **error) {
    gint ret = 0;
    struct kmod_ctx *ctx = NULL;
    struct kmod_module *mod = NULL;
    gchar *null_config = NULL;
    const gchar *path = NULL;
    gboolean have_path = FALSE;

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
                     "Failed to get the module: %s", strerror_l (-ret, c_locale));
        kmod_unref (ctx);
        return FALSE;
    }

    path = kmod_module_get_path (mod);
    have_path = (path != NULL) && (g_strcmp0 (path, "") != 0);
    kmod_module_unref (mod);
    kmod_unref (ctx);

    return have_path;
}

static gboolean load_kernel_module (const gchar *module_name, const gchar *options, GError **error) {
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
                     "Failed to get the module: %s", strerror_l (-ret, c_locale));
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
                     module_name, options, strerror_l (-ret, c_locale));
        kmod_module_unref (mod);
        kmod_unref (ctx);
        return FALSE;
    }

    kmod_module_unref (mod);
    kmod_unref (ctx);
    return TRUE;
}

static gboolean unload_kernel_module (const gchar *module_name, GError **error) {
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
                     "Failed to get the module: %s", strerror_l (-ret, c_locale));
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
                     module_name, strerror_l (-ret, c_locale));
        kmod_module_unref (mod);
        kmod_unref (ctx);
        return FALSE;
    }

    kmod_module_unref (mod);
    kmod_unref (ctx);
    return TRUE;
}

static gboolean echo_str_to_file (const gchar *str, const gchar *file_path, GError **error) {
    GIOChannel *out_file = NULL;
    gsize bytes_written = 0;

    out_file = g_io_channel_new_file (file_path, "w", error);
    if (!out_file || g_io_channel_write_chars (out_file, str, -1, &bytes_written, error) != G_IO_STATUS_NORMAL) {
        g_prefix_error (error, "Failed to write '%s' to file '%s': ", str, file_path);
        return FALSE;
    }
    if (g_io_channel_shutdown (out_file, TRUE, error) != G_IO_STATUS_NORMAL) {
        g_prefix_error (error, "Failed to flush and close the file '%s': ", file_path);
        g_io_channel_unref (out_file);
        return FALSE;
    }
    g_io_channel_unref (out_file);
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
gboolean bd_kbd_zram_create_devices (guint64 num_devices, const guint64 *sizes, const guint64 *nstreams, GError **error) {
    gchar *opts = NULL;
    gboolean success = FALSE;
    guint64 i = 0;
    gchar *num_str = NULL;
    gchar *file_name = NULL;
    guint64 progress_id = 0;

    progress_id = bd_utils_report_started ("Started creating zram devices");

    opts = g_strdup_printf ("num_devices=%"G_GUINT64_FORMAT, num_devices);
    success = load_kernel_module ("zram", opts, error);

    /* maybe it's loaded? Try to unload it first */
    if (!success && g_error_matches (*error, BD_KBD_ERROR, BD_KBD_ERROR_MODULE_FAIL)) {
        g_clear_error (error);
        success = unload_kernel_module ("zram", error);
        if (!success) {
            g_prefix_error (error, "zram module already loaded: ");
            g_free (opts);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
        success = load_kernel_module ("zram", opts, error);
        if (!success) {
            g_free (opts);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }
    g_free (opts);

    if (!success) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* compression streams have to be specified before the device is activated
       by setting its size */
    if (nstreams)
        for (i=0; i < num_devices; i++) {
            file_name = g_strdup_printf ("/sys/block/zram%"G_GUINT64_FORMAT"/max_comp_streams", i);
            num_str = g_strdup_printf ("%"G_GUINT64_FORMAT, nstreams[i]);
            success = echo_str_to_file (num_str, file_name, error);
            g_free (file_name);
            g_free (num_str);
            if (!success) {
                g_prefix_error (error, "Failed to set number of compression streams for '/dev/zram%"G_GUINT64_FORMAT"': ",
                                i);
                bd_utils_report_finished (progress_id, (*error)->message);
                return FALSE;
            }
        }

    /* now activate the devices by setting their sizes */
    for (i=0; i < num_devices; i++) {
        file_name = g_strdup_printf ("/sys/block/zram%"G_GUINT64_FORMAT"/disksize", i);
        num_str = g_strdup_printf ("%"G_GUINT64_FORMAT, sizes[i]);
        success = echo_str_to_file (num_str, file_name, error);
        g_free (file_name);
        g_free (num_str);
        if (!success) {
            g_prefix_error (error, "Failed to set size for '/dev/zram%"G_GUINT64_FORMAT"': ", i);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }

    bd_utils_report_finished (progress_id, "Completed");
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
    gboolean ret = FALSE;
    guint64 progress_id = 0;

    progress_id = bd_utils_report_started ("Started destroying zram devices");
    ret = unload_kernel_module ("zram", error);
    if (!ret && (*error))
        bd_utils_report_finished (progress_id, (*error)->message);
    else
        bd_utils_report_finished (progress_id, "Completed");
    return ret;
}

static guint64 get_number_from_file (const gchar *path, GError **error) {
    gchar *content = NULL;
    gboolean success = FALSE;
    guint64 ret = 0;

    success = g_file_get_contents (path, &content, NULL, error);
    if (!success) {
        /* error is already populated */
        return 0;
    }

    ret = g_ascii_strtoull (content, NULL, 0);
    g_free (content);

    return ret;
}

/**
 * bd_kbd_zram_add_device:
 * @size: size of the zRAM device to add
 * @nstreams: number of streams to use for the new device (or 0 to use the defaults)
 * @device: (allow-none) (out): place to store the name of the newly added device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a new zRAM device was added or not
 */
gboolean bd_kbd_zram_add_device (guint64 size, guint64 nstreams, gchar **device, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    guint64 dev_num = 0;
    gchar *num_str = NULL;
    guint64 progress_id = 0;

    progress_id = bd_utils_report_started ("Started adding new zram device");

    if (access ("/sys/class/zram-control/hot_add", R_OK) != 0) {
        success = load_kernel_module ("zram", NULL, error);
        if (!success) {
            g_prefix_error (error, "Failed to load the zram kernel module: ");
            return FALSE;
        }
    }

    dev_num = get_number_from_file ("/sys/class/zram-control/hot_add", error);
    if (*error) {
        g_prefix_error (error, "Failed to add new zRAM device: ");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (nstreams > 0) {
        path = g_strdup_printf ("/sys/block/zram%"G_GUINT64_FORMAT"/max_comp_streams", dev_num);
        num_str = g_strdup_printf ("%"G_GUINT64_FORMAT, nstreams);
        success = echo_str_to_file (num_str, path, error);
        g_free (path);
        g_free (num_str);
        if (!success) {
            g_prefix_error (error, "Failed to set number of compression streams: ");
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }

    path = g_strdup_printf ("/sys/block/zram%"G_GUINT64_FORMAT"/disksize", dev_num);
    num_str = g_strdup_printf ("%"G_GUINT64_FORMAT, size);
    success = echo_str_to_file (num_str, path, error);
    g_free (path);
    g_free (num_str);
    if (!success) {
        g_prefix_error (error, "Failed to set device size: ");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (device)
        *device = g_strdup_printf ("/dev/zram%"G_GUINT64_FORMAT, dev_num);

    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_kbd_zram_remove_device:
 * @device: zRAM device to remove
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully removed or not
 */
gboolean bd_kbd_zram_remove_device (const gchar *device, GError **error) {
    gchar *dev_num_str = NULL;
    gboolean success = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started removing zram device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (g_str_has_prefix (device, "/dev/zram"))
        dev_num_str = (gchar *) device + 9;
    else if (g_str_has_prefix (device, "zram"))
        dev_num_str = (gchar *) device + 4;
    else {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Invalid zRAM device given: '%s'", device);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    success = echo_str_to_file (dev_num_str, "/sys/class/zram-control/hot_remove", error);
    if (!success) {
        g_prefix_error (error, "Failed to remove device '%s': ", device);
        bd_utils_report_finished (progress_id, (*error)->message);
    }

    bd_utils_report_finished (progress_id, "Completed");
    return success;
}


/**
 * bd_kbd_zram_get_stats:
 * @device: zRAM device to get stats for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): statistics for the zRAM device
 */
BDKBDZramStats* bd_kbd_zram_get_stats (const gchar *device, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    BDKBDZramStats *ret = g_new0 (BDKBDZramStats, 1);

    if (g_str_has_prefix (device, "/dev/"))
        device += 5;

    path = g_strdup_printf ("/sys/block/%s", device);
    if (access (path, R_OK) != 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_NOEXIST,
                     "Device '%s' doesn't seem to exist", device);
        g_free (path);
        return NULL;
    }
    g_free (path);

    path = g_strdup_printf ("/sys/block/%s/disksize", device);
    ret->disksize = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'disksize' for '%s' zRAM device", device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/num_reads", device);
    ret->num_reads = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'num_reads' for '%s' zRAM device", device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/num_writes", device);
    ret->num_writes = get_number_from_file (path, error);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'num_writes' for '%s' zRAM device", device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/invalid_io", device);
    ret->invalid_io = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'invalid_io' for '%s' zRAM device", device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/zero_pages", device);
    ret->zero_pages = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'zero_pages' for '%s' zRAM device", device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/max_comp_streams", device);
    ret->max_comp_streams = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'max_comp_streams' for '%s' zRAM device", device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/orig_data_size", device);
    ret->orig_data_size = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'orig_data_size' for '%s' zRAM device", device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/compr_data_size", device);
    ret->compr_data_size = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'compr_data_size' for '%s' zRAM device", device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/mem_used_total", device);
    ret->mem_used_total = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'mem_used_total' for '%s' zRAM device", device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/comp_algorithm", device);
    success = g_file_get_contents (path, &(ret->comp_algorithm), NULL, error);
    g_free (path);
    if (!success) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'comp_algorithm' for '%s' zRAM device", device);
        g_free (path);
        g_free (ret);
        return NULL;
    }
    /* remove the trailing space and newline */
    g_strstrip (ret->comp_algorithm);

    return ret;
}


/**
 * bd_kbd_bcache_create:
 * @backing_device: backing (slow) device of the cache
 * @cache_device: cache (fast) device of the cache
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'make-bcache' utility)
 * @bcache_device: (out) (allow-none) (transfer full): place to store the name of the new bcache device (if any)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the bcache device was successfully created or not
 */
gboolean bd_kbd_bcache_create (const gchar *backing_device, const gchar *cache_device, const BDExtraArg **extra, const gchar **bcache_device, GError **error) {
    const gchar *argv[6] = {"make-bcache", "-B", backing_device, "-C", cache_device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    gchar *set_uuid = NULL;
    guint i = 0;
    gboolean found = FALSE;
    glob_t globbuf;
    gchar *pattern = NULL;
    gchar *path = NULL;
    gchar *dev_name = NULL;
    gchar *dev_name_end = NULL;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started creation of bcache on '%s' and '%s'", backing_device, cache_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    /* create cache device metadata and try to get Set UUID (needed later) */
    success = bd_utils_exec_and_capture_output (argv, extra, &output, error);
    if (!success) {
        /* error is already populated */
        g_free (output);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    bd_utils_report_progress (progress_id, 50, "Metadata written");

    lines = g_strsplit (output, "\n", 0);

    regex = g_regex_new ("Set UUID:\\s+([-a-z0-9]+)", 0, 0, error);
    if (!regex) {
        /* error is already populated */
        g_free (output);
        g_strfreev (lines);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    for (i=0; !found && lines[i]; i++) {
        success = g_regex_match (regex, lines[i], 0, &match_info);
        if (success) {
            found = TRUE;
            set_uuid = g_match_info_fetch (match_info, 1);
        }
        g_match_info_free (match_info);
    }
    g_regex_unref (regex);
    g_strfreev (lines);

    if (!found) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_PARSE,
                     "Failed to determine Set UUID from: %s", output);
        g_free (output);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    g_free (output);


    /* attach the cache device to the backing device */
    /* get the name of the bcache device based on the @backing_device being its slave */
    dev_name = strrchr (backing_device, '/');
    if (!dev_name) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_PARSE,
                     "Failed to get name of the backing device from '%s'", backing_device);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    /* move right after the last '/' (that's where the device name starts) */
    dev_name++;

    /* make sure the bcache device is registered */
    success = echo_str_to_file (backing_device, "/sys/fs/bcache/register", error);
    if (!success) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    pattern = g_strdup_printf ("/sys/block/*/slaves/%s", dev_name);
    if (glob (pattern, GLOB_NOSORT, NULL, &globbuf) != 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_SETUP_FAIL,
                     "Failed to determine bcache device name for '%s'", dev_name);
        g_free (pattern);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    g_free (pattern);

    /* get the first and only match */
    path = (*globbuf.gl_pathv);

    /* move three '/'s forward */
    dev_name = path + 1;
    for (i=0; i < 2 && dev_name; i++) {
        dev_name = strchr (dev_name, '/');
        dev_name = dev_name ? dev_name + 1: dev_name;
    }
    if (!dev_name) {
        globfree (&globbuf);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    /* get everything till the next '/' */
    dev_name_end = strchr (dev_name, '/');
    dev_name = g_strndup (dev_name, (dev_name_end - dev_name));

    globfree (&globbuf);

    success = bd_kbd_bcache_attach (set_uuid, dev_name, error);
    if (!success) {
        g_prefix_error (error, "Failed to attach the cache to the backing device: ");
        g_free (dev_name);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (bcache_device)
        *bcache_device = dev_name;
    else
        g_free (dev_name);

    bd_utils_report_finished (progress_id, "Completed");

    return TRUE;
}

/**
 * bd_kbd_bcache_attach:
 * @c_set_uuid: cache set UUID of the cache to attach
 * @bcache_device: bcache device to attach @c_set_uuid cache to
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @c_set_uuid cache was successfully attached to @bcache_device or not
 */
gboolean bd_kbd_bcache_attach (const gchar *c_set_uuid, const gchar *bcache_device, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started attaching '%s' cache to bcache device '%s'", c_set_uuid, bcache_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (g_str_has_prefix (bcache_device, "/dev/"))
        bcache_device += 5;

    path = g_strdup_printf ("/sys/block/%s/bcache/attach", bcache_device);
    success = echo_str_to_file (c_set_uuid, path, error);
    g_free (path);

    /* error is already populated (if any) */
    if (!success)
        bd_utils_report_finished (progress_id, (*error)->message);
    else
        bd_utils_report_finished (progress_id, "Completed");

    return success;
}

/**
 * bd_kbd_bcache_detach:
 * @bcache_device: bcache device to detach the cache from
 * @c_set_uuid: (out) (allow-none) (transfer full): cache set UUID of the detached cache
 * @error: (out): place to store error (if any)
 * Returns: whether the bcache device @bcache_device was successfully destroyed or not
 *
 * Note: Flushes the cache first.
 */
gboolean bd_kbd_bcache_detach (const gchar *bcache_device, gchar **c_set_uuid, GError **error) {
    gchar *path = NULL;
    gchar *link = NULL;
    gchar *uuid = NULL;
    gboolean success = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started detaching cache from the bcache device '%s'", bcache_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (g_str_has_prefix (bcache_device, "/dev/"))
        bcache_device += 5;

    path = g_strdup_printf ("/sys/block/%s/bcache/cache", bcache_device);
    if (access (path, R_OK) != 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_NOT_ATTACHED,
                     "No cache attached to '%s' or '%s' not set up", bcache_device, bcache_device);
        g_free (path);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* if existing, /sys/block/SOME_BCACHE/bcache/cache is a symlink to /sys/fs/bcache/C_SET_UUID */
    link = g_file_read_link (path, error);
    g_free (path);
    if (!link) {
        g_prefix_error (error, "Failed to determine cache set UUID for '%s'", bcache_device);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* find the last '/' */
    uuid = strrchr (link, '/');
    if (!uuid) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_UUID,
                     "Failed to determine cache set UUID for '%s'", bcache_device);
        g_free (link);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    /* move right after the '/' */
    uuid++;

    path = g_strdup_printf ("/sys/block/%s/bcache/detach", bcache_device);
    success = echo_str_to_file (uuid, path, error);
    if (!success) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_DETACH_FAIL,
                     "Failed to detach '%s' from '%s'", uuid, bcache_device);
        g_free (link);
        g_free (path);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (c_set_uuid)
        *c_set_uuid = g_strdup (uuid);

    g_free (link);
    g_free (path);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_kbd_bcache_destroy:
 * @bcache_device: bcache device to destroy
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the bcache device @bcache_device was successfully destroyed or not
 */
gboolean bd_kbd_bcache_destroy (const gchar *bcache_device, GError **error) {
    gchar *path = NULL;
    gchar *c_set_uuid = NULL;
    gboolean success = FALSE;
    BDKBDBcacheStats *status = NULL;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started destroying bcache device '%s'", bcache_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (g_str_has_prefix (bcache_device, "/dev/"))
        bcache_device += 5;

    status = bd_kbd_bcache_status (bcache_device, error);
    if (!status) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (g_strcmp0 (status->state, "no cache") != 0) {
        success = bd_kbd_bcache_detach (bcache_device, &c_set_uuid, error);
        if (!success) {
            /* error is already populated */
            bd_kbd_bcache_stats_free (status);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }
    bd_kbd_bcache_stats_free (status);

    if (c_set_uuid) {
        path = g_strdup_printf ("/sys/fs/bcache/%s/stop", c_set_uuid);
        success = echo_str_to_file ("1", path, error);
        g_free (path);
        if (!success) {
            g_prefix_error (error, "Failed to stop the cache set: ");
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }

    path = g_strdup_printf ("/sys/block/%s/bcache/stop", bcache_device);
    success = echo_str_to_file ("1", path, error);
    g_free (path);
    if (!success) {
        g_prefix_error (error, "Failed to stop the bcache: ");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_kbd_bcache_get_mode:
 * @bcache_device: device to get mode of
 * @error: (out): place to store error (if any)
 *
 * Returns: current mode of the @bcache_device
 */
BDKBDBcacheMode bd_kbd_bcache_get_mode (const gchar *bcache_device, GError **error) {
    gchar *path = NULL;
    gchar *content = NULL;
    gboolean success = FALSE;
    gchar *selected = NULL;
    BDKBDBcacheMode ret = BD_KBD_MODE_UNKNOWN;

    if (g_str_has_prefix (bcache_device, "/dev/"))
        bcache_device += 5;

    path = g_strdup_printf ("/sys/block/%s/bcache/cache_mode", bcache_device);
    success = g_file_get_contents (path, &content, NULL, error);
    if (!success) {
        g_prefix_error (error, "Failed to get cache modes for '%s'", bcache_device);
        g_free (path);
        return BD_KBD_MODE_UNKNOWN;
    }
    g_free (path);

    /* there are all cache modes in the file with the currently selected one
       having square brackets around it */
    selected = strchr (content, '[');
    if (!selected) {
        g_prefix_error (error, "Failed to determine cache mode for '%s'", bcache_device);
        g_free (content);
        return BD_KBD_MODE_UNKNOWN;
    }
    /* move right after the square bracket */
    selected++;

    if (g_str_has_prefix (selected, "writethrough"))
        ret = BD_KBD_MODE_WRITETHROUGH;
    else if (g_str_has_prefix (selected, "writeback"))
        ret = BD_KBD_MODE_WRITEBACK;
    else if (g_str_has_prefix (selected, "writearound"))
        ret = BD_KBD_MODE_WRITEAROUND;
    else if (g_str_has_prefix (selected, "none"))
        ret = BD_KBD_MODE_NONE;

    g_free (content);
    if (ret == BD_KBD_MODE_UNKNOWN)
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_MODE_FAIL,
                     "Failed to determine mode for '%s'", bcache_device);

    return ret;
}

/**
 * bd_kbd_bcache_get_mode_str:
 * @mode: mode to get string representation of
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer none): string representation of @mode or %NULL in case of error
 */
const gchar* bd_kbd_bcache_get_mode_str (BDKBDBcacheMode mode, GError **error) {
    if (mode <= BD_KBD_MODE_UNKNOWN)
        return mode_str[mode];
    else {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_MODE_INVAL,
                     "Invalid mode given: %d", mode);
        return NULL;
    }
}

/**
 * bd_kbd_bcache_get_mode_from_str:
 * @mode_str: string representation of mode
 * @error: (out): place to store error (if any)
 *
 * Returns: mode matching the @mode_str given or %BD_KBD_MODE_UNKNOWN in case of no match
 */
BDKBDBcacheMode bd_kbd_bcache_get_mode_from_str (const gchar *mode_str, GError **error) {
    if (g_strcmp0 (mode_str, "writethrough") == 0)
        return BD_KBD_MODE_WRITETHROUGH;
    else if (g_strcmp0 (mode_str, "writeback") == 0)
        return BD_KBD_MODE_WRITEBACK;
    else if (g_strcmp0 (mode_str, "writearound") == 0)
        return BD_KBD_MODE_WRITEAROUND;
    else if (g_strcmp0 (mode_str, "none") == 0)
        return BD_KBD_MODE_NONE;
    else if (g_strcmp0 (mode_str, "unknown") == 0)
        return BD_KBD_MODE_UNKNOWN;
    else {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_MODE_INVAL,
                     "Invalid mode given: '%s'", mode_str);
        return BD_KBD_MODE_UNKNOWN;
    }
}


/**
 * bd_kbd_bcache_set_mode:
 * @bcache_device: bcache device to set mode of
 * @mode: mode to set
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the mode was successfully set or not
 */
gboolean bd_kbd_bcache_set_mode (const gchar *bcache_device, BDKBDBcacheMode mode, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    const gchar *mode_str = NULL;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started setting mode of bcache device '%s'", bcache_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (g_str_has_prefix (bcache_device, "/dev/"))
        bcache_device += 5;

    path = g_strdup_printf ("/sys/block/%s/bcache/cache_mode", bcache_device);
    mode_str = bd_kbd_bcache_get_mode_str (mode, error);
    if (!mode_str) {
        /* error is already populated */
        g_free (path);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    } else if (g_strcmp0 (mode_str, "unknown") == 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_MODE_INVAL,
                     "Cannot set mode of '%s' to '%s'", bcache_device, mode_str);
        g_free (path);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    success = echo_str_to_file ((gchar*) mode_str, path, error);
    if (!success) {
        g_prefix_error (error, "Failed to set mode '%s' to '%s'", mode_str, bcache_device);
        g_free (path);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    g_free (path);

    bd_utils_report_finished (progress_id, "Completed");

    return TRUE;
}

static gboolean get_cache_size_used (const gchar *cache_dev_sys, guint64 *size, guint64 *used, GError **error) {
    gchar *path = NULL;
    GIOChannel *file = NULL;
    gchar *line = NULL;
    gboolean found = FALSE;
    GIOStatus status = G_IO_STATUS_NORMAL;
    guint64 percent_unused = 0;

    path = g_strdup_printf ("%s/../size", cache_dev_sys);
    *size = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_prefix_error (error, "Failed to get cache device size: ");
        return FALSE;
    }

    path = g_strdup_printf ("%s/priority_stats", cache_dev_sys);
    file = g_io_channel_new_file (path, "r", error);
    g_free (path);
    if (!file) {
        g_prefix_error (error, "Failed to get cache usage data: ");
        return FALSE;
    }

    while (!found && (status == G_IO_STATUS_NORMAL)) {
        status = g_io_channel_read_line (file, &line, NULL, NULL, error);
        if (status == G_IO_STATUS_NORMAL) {
            if (g_str_has_prefix (line, "Unused:"))
                found = TRUE;
            else
                g_free (line);
        }
    }
    g_io_channel_shutdown (file, FALSE, error);
    if (*error)
        /* we just read the file, no big deal if it for some really weird reason
           failed to get closed */
        g_clear_error (error);
    g_io_channel_unref (file);

    if (!found) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                     "Failed to get cache usage data");
        return FALSE;
    }

    /* try to read the number after "Unused:" */
    percent_unused = g_ascii_strtoull (line + 8, NULL, 0);
    if (percent_unused == 0) {
        g_free (line);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                     "Failed to get cache usage data");
        return FALSE;
    }
    g_free (line);

    *used = (guint64) ((100ULL - percent_unused) * 0.01 * (*size));

    return TRUE;
}

/**
 * bd_kbd_bcache_status:
 * @bcache_device: bcache device to get status for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): status of the @bcache_device or %NULL in case of
 *                           error (@error is set)
 */
BDKBDBcacheStats* bd_kbd_bcache_status (const gchar *bcache_device, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    BDKBDBcacheStats *ret = g_new0 (BDKBDBcacheStats, 1);
    glob_t globbuf;
    gchar **path_list;
    guint64 size = 0;
    guint64 used = 0;

    if (g_str_has_prefix (bcache_device, "/dev/"))
        bcache_device += 5;

    path = g_strdup_printf ("/sys/block/%s/bcache", bcache_device);
    if (access (path, R_OK) != 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_NOEXIST,
                     "Bcache device '%s' doesn't seem to exist", bcache_device);
        g_free (path);
        return NULL;
    }
    g_free (path);

    path = g_strdup_printf ("/sys/block/%s/bcache/state", bcache_device);
    success = g_file_get_contents (path, &(ret->state), NULL, error);
    g_free (path);
    if (!success) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                     "Failed to get 'state' for '%s' Bcache device", bcache_device);
        g_free (ret);
        return NULL;
    }
    /* remove the trailing space and newline */
    g_strstrip (ret->state);

    if (g_strcmp0 (ret->state, "no cache") == 0)
        /* no cache, nothing more to get */
        return ret;

    path = g_strdup_printf ("/sys/block/%s/bcache/cache/block_size", bcache_device);
    ret->block_size = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                     "Failed to get 'block_size' for '%s' Bcache device", bcache_device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/bcache/cache/cache*/", bcache_device);
    if (glob (path, GLOB_NOSORT, NULL, &globbuf) != 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                     "Failed to get 'cache_size' for '%s' Bcache device", bcache_device);
        g_free (path);
        g_free (ret);
        return NULL;
    }
    g_free (path);

    /* sum up sizes of all (potential) cache devices */
    for (path_list=globbuf.gl_pathv; *path_list; path_list++) {
        success = get_cache_size_used (*path_list, &size, &used, error);
        if (!success) {
            g_clear_error (error);
            g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                         "Failed to get 'cache_size' for '%s' Bcache device", bcache_device);
            globfree (&globbuf);
            g_free (ret);
            return NULL;
        } else {
            // the /sys/*/size values are multiples of sector size
            ret->cache_size += (SECTOR_SIZE * size);
            ret->cache_used += (SECTOR_SIZE * used);
        }
    }
    globfree (&globbuf);

    path = g_strdup_printf ("/sys/block/%s/bcache/stats_total/cache_hits", bcache_device);
    ret->hits = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                     "Failed to get 'hits' for '%s' Bcache device", bcache_device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/bcache/stats_total/cache_misses", bcache_device);
    ret->misses = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                     "Failed to get 'misses' for '%s' Bcache device", bcache_device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/bcache/stats_total/cache_bypass_hits", bcache_device);
    ret->bypass_hits = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                     "Failed to get 'bypass_hits' for '%s' Bcache device", bcache_device);
        g_free (ret);
        return NULL;
    }

    path = g_strdup_printf ("/sys/block/%s/bcache/stats_total/cache_bypass_misses", bcache_device);
    ret->bypass_misses = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                     "Failed to get 'bypass_misses' for '%s' Bcache device", bcache_device);
        g_free (ret);
        return NULL;
    }

    return ret;
}

static gchar* get_device_name (const gchar *major_minor, GError **error) {
    gchar *path = NULL;
    gchar *link = NULL;
    gchar *ret = NULL;

    path = g_strdup_printf ("/dev/block/%s", major_minor);
    link = g_file_read_link (path, error);
    g_free (path);
    if (!link) {
        g_prefix_error (error, "Failed to determine device name for '%s'",
                        major_minor);
        return NULL;
    }

    /* 'link' should be something like "../sda" */
    /* get the last '/' */
    ret = strrchr (link, '/');
    if (!ret) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_INVAL,
                     "Failed to determine device name for '%s'",
                     major_minor);
        g_free (link);
        return NULL;
    }
    /* move right after the last '/' */
    ret++;

    /* create a new copy and free the whole link path */
    ret = g_strdup (ret);
    g_free (link);

    return ret;
}

/**
 * bd_kbd_bcache_get_backing_device:
 * @bcache_device: Bcache device to get the backing device for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): name of the backing device of the @bcache_device
 *                           or %NULL if failed to determine (@error is populated)
 *
 * Note: returns the name of the first backing device of @bcache_device (in case
 *       there are more)
 */
gchar* bd_kbd_bcache_get_backing_device (const gchar *bcache_device, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    gchar *major_minor = NULL;
    gchar *ret = NULL;

    if (g_str_has_prefix (bcache_device, "/dev/"))
        bcache_device += 5;

    path = g_strdup_printf ("/sys/block/%s/bcache/cache/bdev0/../dev", bcache_device);
    if (access (path, R_OK) != 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_NOEXIST,
                     "Failed to get backing device for %s: there seems to be none",
                     bcache_device);
        g_free (path);
        return NULL;
    }

    success = g_file_get_contents (path, &major_minor, NULL, error);
    g_free (path);
    if (!success) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                     "Failed to get major:minor for '%s' Bcache device's backing device",
                     bcache_device);
        return NULL;
    }
    g_strstrip (major_minor);

    ret = get_device_name (major_minor, error);
    if (!ret) {
        g_prefix_error (error, "Failed to determine backing device's name for '%s': ",
                        bcache_device);
        g_free (major_minor);
        return NULL;
    }
    g_free (major_minor);

    return ret;
}

/**
 * bd_kbd_bcache_get_cache_device:
 * @bcache_device: Bcache device to get the cache device for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): name of the cache device of the @bcache_device
 *                           or %NULL if failed to determine (@error is populated)
 *
 * Note: returns the name of the first cache device of @bcache_device (in case
 *       there are more)
 */
gchar* bd_kbd_bcache_get_cache_device (const gchar *bcache_device, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    gchar *major_minor = NULL;
    gchar *ret = NULL;

    if (g_str_has_prefix (bcache_device, "/dev/"))
        bcache_device += 5;

    path = g_strdup_printf ("/sys/block/%s/bcache/cache/cache0/../dev", bcache_device);
    if (access (path, R_OK) != 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_NOEXIST,
                     "Failed to get cache device for %s: there seems to be none",
                     bcache_device);
        g_free (path);
        return NULL;
    }

    success = g_file_get_contents (path, &major_minor, NULL, error);
    g_free (path);
    if (!success) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                     "Failed to get major:minor for '%s' Bcache device's cache device",
                     bcache_device);
        return NULL;
    }
    g_strstrip (major_minor);

    ret = get_device_name (major_minor, error);
    if (!ret) {
        g_prefix_error (error, "Failed to determine cache device's name for '%s': ",
                        bcache_device);
        g_free (major_minor);
        return NULL;
    }
    g_free (major_minor);

    return ret;
}
