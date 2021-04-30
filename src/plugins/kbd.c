/*
 * Copyright (C) 2015  Red Hat, Inc.
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

#include <libkmod.h>
#include <string.h>
#include <syslog.h>
#include <glob.h>
#include <unistd.h>
#include <locale.h>
#include <blockdev/utils.h>
#include <stdio.h>
#include <bs_size.h>

#include "kbd.h"
#include "check_deps.h"

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

static volatile guint avail_deps = 0;
static volatile guint avail_module_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MAKEBCACHE 0
#define DEPS_MAKEBCACHE_MASK (1 << DEPS_MAKEBCACHE)
#define DEPS_LAST 1

static const UtilDep deps[DEPS_LAST] = {
    {"make-bcache", NULL, NULL, NULL},
};

#define MODULE_DEPS_ZRAM 0
#define MODULE_DEPS_ZRAM_MASK (1 << MODULE_DEPS_ZRAM)
#define MODULE_DEPS_LAST 1

static const gchar *const module_deps[MODULE_DEPS_LAST] = { "zram" };


/**
 * bd_kbd_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_kbd_check_deps (void) {
    GError *error = NULL;
    gboolean ret = FALSE;
    guint i = 0;
    gboolean status = FALSE;

    ret = check_module_deps (&avail_module_deps, MODULE_DEPS_ZRAM_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, &error);
    if (!ret) {
        if (error) {
            g_warning("Cannot load the kbd plugin: %s" , error->message);
            g_clear_error (&error);
        } else
            g_warning("Cannot load the kbd plugin: the 'zram' kernel module is not available");
    }

    if (!ret)
        return FALSE;

    ret = TRUE;
#ifdef WITH_BD_BCACHE
    for (i=0; i < DEPS_LAST; i++) {
#else
    /* we need to disable the type-limits check because GCC is too clever and
       complains that 0 is never < 0, but DEPS_LAST may be something larger in
       the future */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
    /* skip checking for 'make-bcache' (MUST BE LAST IN THE LIST OF DEPS!) */
    /* coverity[unsigned_compare] */
    for (i=0; i < DEPS_LAST-1; i++) {
#pragma GCC diagnostic pop
#endif
        /* coverity[dead_error_begin] */
        status = bd_utils_check_util_version (deps[i].name, deps[i].version,
                                              deps[i].ver_arg, deps[i].ver_regexp, &error);
        if (!status)
            g_warning ("%s", error->message);
        else
            g_atomic_int_or (&avail_deps, 1 << i);
        g_clear_error (&error);
        ret = ret && status;
    }

    if (!ret)
        g_warning("Cannot load the kbd plugin");

    return ret;
}

/**
 * bd_kbd_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_kbd_init (void) {
    c_locale = newlocale (LC_ALL_MASK, "C", c_locale);
    return TRUE;
}

/**
 * bd_kbd_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_kbd_close (void) {
    freelocale (c_locale);
}

/**
 * bd_kbd_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDKBDTechMode) for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_kbd_is_tech_avail (BDKBDTech tech, guint64 mode, GError **error) {
    /* all combinations are supported by this implementation of the plugin, but
       bcache creation requires the 'make-bcache' utility */
    if (tech == BD_KBD_TECH_BCACHE && (mode & BD_KBD_TECH_MODE_CREATE))
        return check_deps (&avail_deps, DEPS_MAKEBCACHE_MASK, deps, DEPS_LAST, &deps_check_lock, error);
    else if (tech == BD_KBD_TECH_ZRAM)
        return check_module_deps (&avail_module_deps, MODULE_DEPS_ZRAM_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error);
    else
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
    if (data == NULL)
        return NULL;

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
    if (data == NULL)
        return;

    g_free (data->comp_algorithm);
    g_free (data);
}

BDKBDBcacheStats* bd_kbd_bcache_stats_copy (BDKBDBcacheStats *data) {
    if (data == NULL)
        return NULL;

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
    if (data == NULL)
        return;

    g_free (data->state);
    g_free (data);
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
 *
 * Tech category: %BD_KBD_TECH_ZRAM-%BD_KBD_TECH_MODE_CREATE
 */
gboolean bd_kbd_zram_create_devices (guint64 num_devices, const guint64 *sizes, const guint64 *nstreams, GError **error) {
    gchar *opts = NULL;
    gboolean success = FALSE;
    guint64 i = 0;
    gchar *num_str = NULL;
    gchar *file_name = NULL;
    guint64 progress_id = 0;

    if (!check_module_deps (&avail_module_deps, MODULE_DEPS_ZRAM_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    progress_id = bd_utils_report_started ("Started creating zram devices");

    opts = g_strdup_printf ("num_devices=%"G_GUINT64_FORMAT, num_devices);
    success = bd_utils_load_kernel_module ("zram", opts, error);

    /* maybe it's loaded? Try to unload it first */
    if (!success && g_error_matches (*error, BD_UTILS_MODULE_ERROR, BD_UTILS_MODULE_ERROR_FAIL)) {
        g_clear_error (error);
        success = bd_utils_unload_kernel_module ("zram", error);
        if (!success) {
            g_prefix_error (error, "zram module already loaded: ");
            g_free (opts);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
        success = bd_utils_load_kernel_module ("zram", opts, error);
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
            success = bd_utils_echo_str_to_file (num_str, file_name, error);
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
        success = bd_utils_echo_str_to_file (num_str, file_name, error);
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
 *
 * Tech category: %BD_KBD_TECH_ZRAM-%BD_KBD_TECH_MODE_DESTROY
 */
gboolean bd_kbd_zram_destroy_devices (GError **error) {
    gboolean ret = FALSE;
    guint64 progress_id = 0;

    if (!check_module_deps (&avail_module_deps, MODULE_DEPS_ZRAM_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    progress_id = bd_utils_report_started ("Started destroying zram devices");
    ret = bd_utils_unload_kernel_module ("zram", error);
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
 *
 * Tech category: %BD_KBD_TECH_ZRAM-%BD_KBD_TECH_MODE_MODIFY
 */
gboolean bd_kbd_zram_add_device (guint64 size, guint64 nstreams, gchar **device, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    guint64 dev_num = 0;
    gchar *num_str = NULL;
    guint64 progress_id = 0;

    if (!check_module_deps (&avail_module_deps, MODULE_DEPS_ZRAM_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    progress_id = bd_utils_report_started ("Started adding new zram device");

    if (access ("/sys/class/zram-control/hot_add", R_OK) != 0) {
        success = bd_utils_load_kernel_module ("zram", NULL, error);
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
        success = bd_utils_echo_str_to_file (num_str, path, error);
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
    success = bd_utils_echo_str_to_file (num_str, path, error);
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
 *
 * Tech category: %BD_KBD_TECH_ZRAM-%BD_KBD_TECH_MODE_MODIFY
 */
gboolean bd_kbd_zram_remove_device (const gchar *device, GError **error) {
    gchar *dev_num_str = NULL;
    gboolean success = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    if (!check_module_deps (&avail_module_deps, MODULE_DEPS_ZRAM_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

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

    success = bd_utils_echo_str_to_file (dev_num_str, "/sys/class/zram-control/hot_remove", error);
    if (!success) {
        g_prefix_error (error, "Failed to remove device '%s': ", device);
        bd_utils_report_finished (progress_id, (*error)->message);
    }

    bd_utils_report_finished (progress_id, "Completed");
    return success;
}

/* Get the zRAM stats using the "old" sysfs files --  /sys/block/zram<id>/num_reads,
   /sys/block/zram<id>/invalid_io etc. */
static gboolean get_zram_stats_old (const gchar *device, BDKBDZramStats* stats, GError **error) {
    gchar *path = NULL;

    path = g_strdup_printf ("/sys/block/%s/num_reads", device);
    stats->num_reads = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'num_reads' for '%s' zRAM device", device);
        return FALSE;
    }

    path = g_strdup_printf ("/sys/block/%s/num_writes", device);
    stats->num_writes = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'num_writes' for '%s' zRAM device", device);
        return FALSE;
    }

    path = g_strdup_printf ("/sys/block/%s/invalid_io", device);
    stats->invalid_io = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'invalid_io' for '%s' zRAM device", device);
        return FALSE;
    }

    path = g_strdup_printf ("/sys/block/%s/zero_pages", device);
    stats->zero_pages = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'zero_pages' for '%s' zRAM device", device);
        return FALSE;
    }

    path = g_strdup_printf ("/sys/block/%s/orig_data_size", device);
    stats->orig_data_size = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'orig_data_size' for '%s' zRAM device", device);
        return FALSE;
    }

    path = g_strdup_printf ("/sys/block/%s/compr_data_size", device);
    stats->compr_data_size = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'compr_data_size' for '%s' zRAM device", device);
        return FALSE;
    }

    path = g_strdup_printf ("/sys/block/%s/mem_used_total", device);
    stats->mem_used_total = get_number_from_file (path, error);
    g_free (path);
    if (*error) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'mem_used_total' for '%s' zRAM device", device);
        return FALSE;
    }

    return TRUE;
}

/* Get the zRAM stats using the "new" sysfs files -- /sys/block/zram<id>/stat,
  /sys/block/zram<id>/io_stat etc. */
static gboolean get_zram_stats_new (const gchar *device, BDKBDZramStats* stats, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    gint scanned = 0;
    gchar *content = NULL;

    path = g_strdup_printf ("/sys/block/%s/stat", device);
    success = g_file_get_contents (path, &content, NULL, error);
    g_free (path);
    if (!success) {
        /* error is already populated */
        return FALSE;
    }

    scanned = sscanf (content,
                      "%*[ \t]%" G_GUINT64_FORMAT "%*[ \t]%*[0-9]%*[ \t]%*[0-9]%*[ \t]%*[0-9]%" G_GUINT64_FORMAT "",
                      &stats->num_reads, &stats->num_writes);
    g_free (content);
    if (scanned != 2) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'stat' for '%s' zRAM device", device);
        return FALSE;
    }

    path = g_strdup_printf ("/sys/block/%s/io_stat", device);
    success = g_file_get_contents (path, &content, NULL, error);
    g_free (path);
    if (!success) {
        /* error is already populated */
        return FALSE;
    }

    scanned = sscanf (content,
                      "%*[ \t]%*[0-9]%*[ \t]%*[0-9]%*[ \t]%" G_GUINT64_FORMAT "",
                      &stats->invalid_io);
    g_free (content);
    if (scanned != 1) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'io_stat' for '%s' zRAM device", device);
        return FALSE;
    }

    path = g_strdup_printf ("/sys/block/%s/mm_stat", device);
    success = g_file_get_contents (path, &content, NULL, error);
    g_free (path);
    if (!success) {
        /* error is already populated */
        return FALSE;
    }

    scanned = sscanf (content,
                      "%*[ \t]%" G_GUINT64_FORMAT "%*[ \t]%" G_GUINT64_FORMAT "%*[ \t]%" G_GUINT64_FORMAT \
                      "%*[ \t]%*[0-9]%*[ \t]%" G_GUINT64_FORMAT "",
                      &stats->orig_data_size, &stats->compr_data_size, &stats->mem_used_total, &stats->zero_pages);
    g_free (content);
    if (scanned != 4) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'mm_stat' for '%s' zRAM device", device);
        return FALSE;
    }

    return TRUE;
}


/**
 * bd_kbd_zram_get_stats:
 * @device: zRAM device to get stats for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): statistics for the zRAM device
 *
 * Tech category: %BD_KBD_TECH_ZRAM-%BD_KBD_TECH_MODE_QUERY
 */
BDKBDZramStats* bd_kbd_zram_get_stats (const gchar *device, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    BDKBDZramStats *ret = NULL;

    if (!check_module_deps (&avail_module_deps, MODULE_DEPS_ZRAM_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = g_new0 (BDKBDZramStats, 1);

    if (g_str_has_prefix (device, "/dev/"))
        device += 5;

    path = g_strdup_printf ("/sys/block/%s", device);
    if (access (path, R_OK) != 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_NOEXIST,
                     "Device '%s' doesn't seem to exist", device);
        g_free (path);
        g_free (ret);
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

    path = g_strdup_printf ("/sys/block/%s/comp_algorithm", device);
    success = g_file_get_contents (path, &(ret->comp_algorithm), NULL, error);
    g_free (path);
    if (!success) {
        g_clear_error (error);
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_ZRAM_INVAL,
                     "Failed to get 'comp_algorithm' for '%s' zRAM device", device);
        g_free (ret);
        return NULL;
    }
    /* remove the trailing space and newline */
    g_strstrip (ret->comp_algorithm);

    /* We need to read stats from different files on new and old kernels.
       e.g. "num_reads" exits only on old kernels and "stat" (that replaces
       "num_reads/writes/etc.") exists only on newer kernels.
    */
    path = g_strdup_printf ("/sys/block/%s/num_reads", device);
    if (g_file_test (path, G_FILE_TEST_EXISTS))
      success = get_zram_stats_old (device, ret, error);
    else
      success = get_zram_stats_new (device, ret, error);
    g_free (path);

    if (!success) {
        /* error is already populated */
        g_free (ret);
        return NULL;
    }

    return ret;
}


static gboolean wait_for_file (const char *filename) {
    gint count = 500;
    while (count > 0) {
        g_usleep (100000); /* microseconds */
        if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
            return TRUE;
        }
        --count;
    }
    return FALSE;
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
 *
 * Tech category: %BD_KBD_TECH_BCACHE-%BD_KBD_TECH_MODE_CREATE
 */
gboolean bd_kbd_bcache_create (const gchar *backing_device, const gchar *cache_device, const BDExtraArg **extra, const gchar **bcache_device, GError **error) {
    const gchar *argv[6] = {"make-bcache", "-B", backing_device, "-C", cache_device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    guint i = 0;
    glob_t globbuf;
    gchar *pattern = NULL;
    gchar *path = NULL;
    gchar *dev_name = NULL;
    gchar *dev_name_end = NULL;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    guint n = 0;
    gchar device_uuid[2][64];

    if (!check_deps (&avail_deps, DEPS_MAKEBCACHE_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

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

    regex = g_regex_new ("^UUID:\\s+([-a-z0-9]+)", 0, 0, error);
    if (!regex) {
        /* error is already populated */
        g_free (output);
        g_strfreev (lines);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    for (i=0; lines[i] && n < 2; i++) {
        success = g_regex_match (regex, lines[i], 0, &match_info);
        if (success) {
            gchar *s;

            s = g_match_info_fetch (match_info, 1);
            strncpy (device_uuid[n], s, 63);
            g_free (s);
            device_uuid[n][63] = '\0';
            n++;
            g_match_info_free (match_info);
        }
    }
    g_regex_unref (regex);
    g_strfreev (lines);

    if (n != 2) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_PARSE,
                     "Failed to determine UUIDs from: %s", output);
        g_free (output);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    g_free (output);

    /* Wait for the symlinks to show up, would it be better to do a udev settle? */
    for (i=0; i < 2; i++) {
        const char *uuid_file = g_strdup_printf ("/dev/disk/by-uuid/%s", device_uuid[i]);
        gboolean present = wait_for_file (uuid_file);
        g_free ((gpointer)uuid_file);
        if (!present) {
            g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_NOEXIST,
                         "Failed to locate uuid symlink '%s'", device_uuid[i]);
            return FALSE;
        }
     }

    /* get the name of the bcache device based on the @backing_device being its slave */
    dev_name = strrchr (backing_device, '/');

    /* move right after the last '/' (that's where the device name starts) */
    dev_name++;

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
 *
 * Tech category: %BD_KBD_TECH_BCACHE-%BD_KBD_TECH_MODE_MODIFY
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
    success = bd_utils_echo_str_to_file (c_set_uuid, path, error);
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
 *
 * Tech category: %BD_KBD_TECH_BCACHE-%BD_KBD_TECH_MODE_MODIFY
 */
gboolean bd_kbd_bcache_detach (const gchar *bcache_device, gchar **c_set_uuid, GError **error) {
    gchar *path = NULL;
    gchar *link = NULL;
    gchar *uuid = NULL;
    gboolean success = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    BDKBDBcacheStats *status = NULL;
    gboolean done = FALSE;

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
    success = bd_utils_echo_str_to_file (uuid, path, error);
    if (!success) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_DETACH_FAIL,
                     "Failed to detach '%s' from '%s'", uuid, bcache_device);
        g_free (link);
        g_free (path);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* wait for the dirty blocks to be flushed and the cache actually detached */
    while (!done) {
        status = bd_kbd_bcache_status (bcache_device, error);
        if (!status) {
            /* error is already populated */
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
        done = strncmp (status->state, "no cache", 8) == 0;
        bd_kbd_bcache_stats_free (status);
        /* let's wait half a second before trying again */
        if (!done)
            g_usleep (500000);
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
 *
 * Tech category: %BD_KBD_TECH_BCACHE-%BD_KBD_TECH_MODE_DESTROY
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
        g_free (c_set_uuid);
        success = bd_utils_echo_str_to_file ("1", path, error);
        g_free (path);
        if (!success) {
            g_prefix_error (error, "Failed to stop the cache set: ");
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }

    path = g_strdup_printf ("/sys/block/%s/bcache/stop", bcache_device);
    success = bd_utils_echo_str_to_file ("1", path, error);
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
 *
 * Tech category: %BD_KBD_TECH_BCACHE-%BD_KBD_TECH_MODE_QUERY
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
 *
 * Tech category: always available
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
 *
 * Tech category: always available
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
 *
 * Tech category: %BD_KBD_TECH_BCACHE-%BD_KBD_TECH_MODE_MODIFY
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

    success = bd_utils_echo_str_to_file ((gchar*) mode_str, path, error);
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
        g_free (line);
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

static guint64 get_bcache_block_size (const gchar *bcache_device, GError **error) {
    gchar *content = NULL;
    gboolean success = FALSE;
    guint64 ret = 0;
    gchar *path = NULL;
    BSError *bs_error = NULL;
    BSSize size = NULL;

    path = g_strdup_printf ("/sys/block/%s/bcache/cache/block_size", bcache_device);
    success = g_file_get_contents (path, &content, NULL, error);
    if (!success) {
        /* error is already populated */
        g_free (path);
        return 0;
    }

    size = bs_size_new_from_str (content, &bs_error);
    if (size)
        ret = bs_size_get_bytes (size, NULL, &bs_error);

    if (bs_error) {
        g_set_error_literal (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_INVAL,
                             bs_error->msg);
        bs_clear_error (&bs_error);
    }

    if (size)
        bs_size_free (size);
    g_free (content);
    g_free (path);
    return ret;
}

/**
 * bd_kbd_bcache_status:
 * @bcache_device: bcache device to get status for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): status of the @bcache_device or %NULL in case of
 *                           error (@error is set)
 *
 * Tech category: %BD_KBD_TECH_BCACHE-%BD_KBD_TECH_MODE_QUERY
 */
BDKBDBcacheStats* bd_kbd_bcache_status (const gchar *bcache_device, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    BDKBDBcacheStats *ret = g_new0 (BDKBDBcacheStats, 1);
    glob_t globbuf;
    gchar **path_list;
    guint64 size = 0;
    guint64 used = 0;
    GError *loc_error = NULL;

    if (g_str_has_prefix (bcache_device, "/dev/"))
        bcache_device += 5;

    path = g_strdup_printf ("/sys/block/%s/bcache", bcache_device);
    if (access (path, R_OK) != 0) {
        g_set_error (error, BD_KBD_ERROR, BD_KBD_ERROR_BCACHE_NOEXIST,
                     "Bcache device '%s' doesn't seem to exist", bcache_device);
        g_free (path);
        g_free (ret);
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

    ret->block_size = get_bcache_block_size (bcache_device, &loc_error);
    if (loc_error) {
        g_propagate_prefixed_error (error, loc_error,
                                    "Failed to get 'block_size' for '%s' Bcache device: ",
                                    bcache_device);
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
 *
 * Tech category: %BD_KBD_TECH_BCACHE-%BD_KBD_TECH_MODE_QUERY
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
 *
 * Tech category: %BD_KBD_TECH_BCACHE-%BD_KBD_TECH_MODE_QUERY
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
