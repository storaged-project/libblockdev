/*
 * Copyright (C) 2014-2025  Red Hat, Inc.
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
 */

#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <libdevmapper.h>

#include "lvm.h"
#include "lvm-private.h"
#include "check_deps.h"
#include "dm_logging.h"
#include "vdo_stats.h"


#define INT_FLOAT_EPS 1e-5

#define MIN_PE_SIZE (1 KiB)
#define MAX_PE_SIZE (16 GiB)

#define VDO_POOL_SUFFIX "vpool"

#define THPOOL_MD_FACTOR_NEW (0.2)
#define THPOOL_MD_FACTOR_EXISTS (1 / 6.0)

#define MIN_THPOOL_MD_SIZE (4 MiB)
/* DM_THIN_MAX_METADATA_SIZE is in 512 sectors */
#define MAX_THPOOL_MD_SIZE (DM_THIN_MAX_METADATA_SIZE * 512)

#define MIN_THPOOL_CHUNK_SIZE (64 KiB)
#define MAX_THPOOL_CHUNK_SIZE (1 GiB)
#define DEFAULT_CHUNK_SIZE (64 KiB)

/* according to lvmcache (7) */
#define MIN_CACHE_MD_SIZE (8 MiB)

#ifdef __LP64__
/* 64bit system */
#define MAX_LV_SIZE (8 EiB)
#else
/* 32bit system */
#define MAX_LV_SIZE (16 TiB)
#endif

GMutex global_config_lock;
gchar *global_config_str = NULL;
gchar *global_devices_str = NULL;

/**
 * bd_lvm_is_supported_pe_size:
 * @size: size (in bytes) to test
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given size is supported physical extent size or not
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
gboolean bd_lvm_is_supported_pe_size (guint64 size, GError **error G_GNUC_UNUSED) {
    return (((size % 2) == 0) && (size >= (MIN_PE_SIZE)) && (size <= (MAX_PE_SIZE)));
}

/**
 * bd_lvm_get_supported_pe_sizes:
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full) (array fixed-size=25): list of supported PE sizes
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
guint64 *bd_lvm_get_supported_pe_sizes (GError **error G_GNUC_UNUSED) {
    guint8 i;
    guint64 val = MIN_PE_SIZE;
    guint8 num_items = ((guint8) round (log2 ((double) MAX_PE_SIZE))) - ((guint8) round (log2 ((double) MIN_PE_SIZE))) + 2;
    guint64 *ret = g_new0 (guint64, num_items);

    for (i=0; (val <= MAX_PE_SIZE); i++, val = val * 2)
        ret[i] = val;

    ret[num_items-1] = 0;

    return ret;
}

/**
 * bd_lvm_get_max_lv_size:
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: maximum LV size in bytes
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_get_max_lv_size (GError **error G_GNUC_UNUSED) {
    return MAX_LV_SIZE;
}

/**
 * bd_lvm_round_size_to_pe:
 * @size: size to be rounded
 * @pe_size: physical extent (PE) size or 0 to use the default
 * @roundup: whether to round up or down (ceil or floor)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: @size rounded to @pe_size according to the @roundup
 *
 * Rounds given @size up/down to a multiple of @pe_size according to the value
 * of the @roundup parameter. If the rounded value is too big to fit in the
 * return type, the result is rounded down (floored) regardless of the @roundup
 * parameter.
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_round_size_to_pe (guint64 size, guint64 pe_size, gboolean roundup, GError **error G_GNUC_UNUSED) {
    pe_size = RESOLVE_PE_SIZE (pe_size);
    guint64 delta = size % pe_size;
    if (delta == 0)
        return size;

    if (roundup && (((G_MAXUINT64 - (pe_size - delta)) >= size)))
        return size + (pe_size - delta);
    else
        return size - delta;
}

/**
 * bd_lvm_get_lv_physical_size:
 * @lv_size: LV size
 * @pe_size: PE size
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: space taken on disk(s) by the LV with given @size
 *
 * Gives number of bytes needed for an LV with the size @lv_size on an LVM stack
 * using given @pe_size.
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_get_lv_physical_size (guint64 lv_size, guint64 pe_size, GError **error) {
    pe_size = RESOLVE_PE_SIZE (pe_size);

    /* the LV just takes space rounded up to the multiple of extent size */
    return bd_lvm_round_size_to_pe (lv_size, pe_size, TRUE, error);
}

/**
 * bd_lvm_get_thpool_padding:
 * @size: size of the thin pool
 * @pe_size: PE size or 0 if the default value should be used
 * @included: if padding is already included in the size
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: size of the padding needed for a thin pool with the given @size
 *         according to the @pe_size and @included
 *
 * Tech category: %BD_LVM_TECH_THIN_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_get_thpool_padding (guint64 size, guint64 pe_size, gboolean included, GError **error) {
    guint64 raw_md_size;
    pe_size = RESOLVE_PE_SIZE (pe_size);

    if (included)
        raw_md_size = (guint64) ceil (size * THPOOL_MD_FACTOR_EXISTS);
    else
        raw_md_size = (guint64) ceil (size * THPOOL_MD_FACTOR_NEW);

    return MIN (bd_lvm_round_size_to_pe (raw_md_size, pe_size, TRUE, error),
                bd_lvm_round_size_to_pe (MAX_THPOOL_MD_SIZE, pe_size, TRUE, error));
}

/**
 * bd_lvm_get_thpool_meta_size:
 * @size: size of the thin pool
 * @chunk_size: chunk size of the thin pool or 0 to use the default
 * @n_snapshots: ignored
 * @error: (out) (optional): place to store error (if any)
 *
 * Note: This function will be changed in 3.0: the @n_snapshots parameter
 *       is currently not used and will be removed.
 *
 * Returns: recommended size of the metadata space for the specified pool
 *
 * Tech category: %BD_LVM_TECH_THIN_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_get_thpool_meta_size (guint64 size, guint64 chunk_size, guint64 n_snapshots G_GNUC_UNUSED, GError **error G_GNUC_UNUSED) {
    guint64 md_size = 0;

    /* based on lvcreate metadata size calculation */
    md_size = UINT64_C (64) * size / (chunk_size ? chunk_size : DEFAULT_CHUNK_SIZE);

    if (md_size > MAX_THPOOL_MD_SIZE)
        md_size = MAX_THPOOL_MD_SIZE;
    else if (md_size < MIN_THPOOL_MD_SIZE)
        md_size = MIN_THPOOL_MD_SIZE;

    return md_size;
}

/**
 * bd_lvm_is_valid_thpool_md_size:
 * @size: the size to be tested
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given size is a valid thin pool metadata size or not
 *
 * Tech category: %BD_LVM_TECH_THIN_CALCS no mode (it is ignored)
 */
gboolean bd_lvm_is_valid_thpool_md_size (guint64 size, GError **error G_GNUC_UNUSED) {
    return ((MIN_THPOOL_MD_SIZE <= size) && (size <= MAX_THPOOL_MD_SIZE));
}

/**
 * bd_lvm_is_valid_thpool_chunk_size:
 * @size: the size to be tested
 * @discard: whether discard/TRIM is required to be supported or not
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given size is a valid thin pool chunk size or not
 *
 * Tech category: %BD_LVM_TECH_THIN_CALCS no mode (it is ignored)
 */
gboolean bd_lvm_is_valid_thpool_chunk_size (guint64 size, gboolean discard, GError **error G_GNUC_UNUSED) {
    gdouble size_log2 = 0.0;

    if ((size < MIN_THPOOL_CHUNK_SIZE) || (size > MAX_THPOOL_CHUNK_SIZE))
        return FALSE;

    /* To support discard, chunk size must be a power of two. Otherwise it must be a
       multiple of 64 KiB. */
    if (discard) {
        size_log2 = log2 ((double) size);
        return ABS (((int) round (size_log2)) - size_log2) <= INT_FLOAT_EPS;
    } else
        return (size % (64 KiB)) == 0;
}

/**
 * bd_lvm_cache_get_default_md_size:
 * @cache_size: size of the cache to determine MD size for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: recommended default size of the cache metadata LV or 0 in case of error
 *
 * Tech category: %BD_LVM_TECH_CACHE_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_cache_get_default_md_size (guint64 cache_size, GError **error G_GNUC_UNUSED) {
    return MAX ((guint64) cache_size / 1000, MIN_CACHE_MD_SIZE);
}

/**
 * bd_lvm_set_global_config:
 * @new_config: (nullable): string representation of the new global libblockdev LVM
 *                          configuration to set or %NULL to reset to default
 * @error: (out) (optional): place to store error (if any)
 *
 *
 * Note: This function sets configuration options for LVM calls internally
 *       in libblockdev, it doesn't change the global lvm.conf config file.
 *       Calling this function with `backup {backup=0 archive=0}` for example
 *       means `--config=backup {backup=0 archive=0}"` will be added to all
 *       calls libblockdev makes.
 *
 * Returns: whether the new requested global config @new_config was successfully
 *          set or not
 *
 * Tech category: %BD_LVM_TECH_GLOB_CONF no mode (it is ignored)
 */
gboolean bd_lvm_set_global_config (const gchar *new_config, GError **error G_GNUC_UNUSED) {
    /* XXX: the error attribute will likely be used in the future when
       some validation comes into the game */

    g_mutex_lock (&global_config_lock);

    /* first free the old value */
    g_free (global_config_str);

    /* now store the new one */
    if (!new_config || g_strcmp0 (new_config, "") == 0)
         global_config_str = NULL;
    else
        global_config_str = g_strdup (new_config);

    g_mutex_unlock (&global_config_lock);
    return TRUE;
}

/**
 * bd_lvm_get_global_config:
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): a copy of a string representation of the currently
 *                           set libblockdev LVM global configuration
 *
 * Note: This function does not change the global `lvm.conf` config
 *       file, see %bd_lvm_set_global_config for details.
 *
 * Tech category: %BD_LVM_TECH_GLOB_CONF no mode (it is ignored)
 */
gchar* bd_lvm_get_global_config (GError **error G_GNUC_UNUSED) {
    gchar *ret = NULL;

    g_mutex_lock (&global_config_lock);
    ret = g_strdup (global_config_str ? global_config_str : "");
    g_mutex_unlock (&global_config_lock);

    return ret;
}

/**
 * bd_lvm_set_devices_filter:
 * @devices: (nullable) (array zero-terminated=1): list of devices for lvm commands to work on
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the devices filter was successfully set or not
 *
 * Tech category: %BD_LVM_TECH_DEVICES no mode (it is ignored)
 */
gboolean bd_lvm_set_devices_filter (const gchar **devices, GError **error) {
    if (!bd_lvm_is_tech_avail (BD_LVM_TECH_DEVICES, 0, error))
        return FALSE;

    g_mutex_lock (&global_config_lock);

    /* first free the old value */
    g_free (global_devices_str);

    /* now store the new one */
    if (!devices || !(*devices))
        global_devices_str = NULL;
    else
        global_devices_str = g_strjoinv (",", (gchar **) devices);

    g_mutex_unlock (&global_config_lock);
    return TRUE;
}

/**
 * bd_lvm_get_devices_filter:
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full) (array zero-terminated=1): a copy of a string representation of
 *                                                     the currently set LVM devices filter
 *
 * Tech category: %BD_LVM_TECH_DEVICES no mode (it is ignored)
 */
gchar** bd_lvm_get_devices_filter (GError **error G_GNUC_UNUSED) {
    gchar **ret = NULL;

    g_mutex_lock (&global_config_lock);

    if (global_devices_str)
        ret = g_strsplit (global_devices_str, ",", -1);
    else
        ret = NULL;

    g_mutex_unlock (&global_config_lock);

    return ret;
}

/**
 * bd_lvm_cache_get_mode_str:
 * @mode: mode to get the string representation for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: string representation of @mode or %NULL in case of error
 *
 * Tech category: always provided/supported
 */
const gchar* bd_lvm_cache_get_mode_str (BDLVMCacheMode mode, GError **error) {
    if (mode == BD_LVM_CACHE_MODE_WRITETHROUGH)
        return "writethrough";
    else if (mode == BD_LVM_CACHE_MODE_WRITEBACK)
        return "writeback";
    else if (mode == BD_LVM_CACHE_MODE_UNKNOWN)
        return "unknown";
    else {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                     "Invalid mode given: %d", mode);
        return NULL;
    }
}

/**
 * bd_lvm_cache_get_mode_from_str:
 * @mode_str: string representation of a cache mode
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: cache mode for the @mode_str or %BD_LVM_CACHE_MODE_UNKNOWN if
 *          failed to determine
 *
 * Tech category: always provided/supported
 */
BDLVMCacheMode bd_lvm_cache_get_mode_from_str (const gchar *mode_str, GError **error) {
    if (g_strcmp0 (mode_str, "writethrough") == 0)
        return BD_LVM_CACHE_MODE_WRITETHROUGH;
    else if (g_strcmp0 (mode_str, "writeback") == 0)
        return BD_LVM_CACHE_MODE_WRITEBACK;
    else if (g_strcmp0 (mode_str, "unknown") == 0)
        return BD_LVM_CACHE_MODE_UNKNOWN;
    else {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                     "Invalid mode given: %s", mode_str);
        return BD_LVM_CACHE_MODE_UNKNOWN;
    }
}

/**
 * bd_lvm_get_vdo_operating_mode_str:
 * @mode: mode to get the string representation for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: string representation of @mode or %NULL in case of error
 *
 * Tech category: always provided/supported
 */
const gchar* bd_lvm_get_vdo_operating_mode_str (BDLVMVDOOperatingMode mode, GError **error) {
    switch (mode) {
    case BD_LVM_VDO_MODE_RECOVERING:
        return "recovering";
    case BD_LVM_VDO_MODE_READ_ONLY:
        return "read-only";
    case BD_LVM_VDO_MODE_NORMAL:
        return "normal";
    case BD_LVM_VDO_MODE_UNKNOWN:
        return "unknown";
    default:
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_FAIL,
                     "Invalid LVM VDO operating mode.");
        return NULL;
    }
}

/**
 * bd_lvm_get_vdo_compression_state_str:
 * @state: state to get the string representation for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: string representation of @state or %NULL in case of error
 *
 * Tech category: always provided/supported
 */
const gchar* bd_lvm_get_vdo_compression_state_str (BDLVMVDOCompressionState state, GError **error) {
    switch (state) {
    case BD_LVM_VDO_COMPRESSION_ONLINE:
        return "online";
    case BD_LVM_VDO_COMPRESSION_OFFLINE:
        return "offline";
    case BD_LVM_VDO_COMPRESSION_UNKNOWN:
        return "unknown";
    default:
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_FAIL,
                     "Invalid LVM VDO compression state.");
        return NULL;
    }
}

/**
 * bd_lvm_get_vdo_index_state_str:
 * @state: state to get the string representation for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: string representation of @state or %NULL in case of error
 *
 * Tech category: always provided/supported
 */
const gchar* bd_lvm_get_vdo_index_state_str (BDLVMVDOIndexState state, GError **error) {
    switch (state) {
    case BD_LVM_VDO_INDEX_ERROR:
        return "error";
    case BD_LVM_VDO_INDEX_CLOSED:
        return "closed";
    case BD_LVM_VDO_INDEX_OPENING:
        return "opening";
    case BD_LVM_VDO_INDEX_CLOSING:
        return "closing";
    case BD_LVM_VDO_INDEX_OFFLINE:
        return "offline";
    case BD_LVM_VDO_INDEX_ONLINE:
        return "online";
    case BD_LVM_VDO_INDEX_UNKNOWN:
        return "unknown";
    default:
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_FAIL,
                     "Invalid LVM VDO index state.");
        return NULL;
    }
}

/**
 * bd_lvm_get_vdo_write_policy_str:
 * @policy: policy to get the string representation for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: string representation of @policy or %NULL in case of error
 *
 * Tech category: always provided/supported
 */
const gchar* bd_lvm_get_vdo_write_policy_str (BDLVMVDOWritePolicy policy, GError **error) {
    switch (policy) {
    case BD_LVM_VDO_WRITE_POLICY_AUTO:
        return "auto";
    case BD_LVM_VDO_WRITE_POLICY_SYNC:
        return "sync";
    case BD_LVM_VDO_WRITE_POLICY_ASYNC:
        return "async";
    case BD_LVM_VDO_WRITE_POLICY_UNKNOWN:
        return "unknown";
    default:
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_FAIL,
                     "Invalid LVM VDO write policy.");
        return NULL;
    }
}

/**
 * bd_lvm_get_vdo_write_policy_from_str:
 * @policy_str: string representation of a policy
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: write policy for the @policy_str or %BD_LVM_VDO_WRITE_POLICY_UNKNOWN if
 *          failed to determine
 *
 * Tech category: always provided/supported
 */
BDLVMVDOWritePolicy bd_lvm_get_vdo_write_policy_from_str (const gchar *policy_str, GError **error) {
    if (g_strcmp0 (policy_str, "auto") == 0)
        return BD_LVM_VDO_WRITE_POLICY_AUTO;
    else if (g_strcmp0 (policy_str, "sync") == 0)
        return BD_LVM_VDO_WRITE_POLICY_SYNC;
    else if (g_strcmp0 (policy_str, "async") == 0)
        return BD_LVM_VDO_WRITE_POLICY_ASYNC;
    else {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_VDO_POLICY_INVAL,
                     "Invalid policy given: %s", policy_str);
        return BD_LVM_VDO_WRITE_POLICY_UNKNOWN;
    }
}

/**
 * bd_lvm_vdo_get_stats_full:
 * @vg_name: name of the VG that contains @pool_name VDO pool
 * @pool_name: name of the VDO pool to get statistics for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full) (element-type utf8 utf8): hashtable of type string - string of available
 *                                                    statistics or %NULL in case of error
 *                                                    (@error gets populated in those cases)
 *
 * Statistics are collected from the values exposed by the kernel `dm-vdo` module.
 *
 * Some of the keys are computed to mimic the information produced by the vdo tools.
 * Please note the contents of the hashtable may vary depending on the actual dm-vdo module version.
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_QUERY
 */
GHashTable* bd_lvm_vdo_get_stats_full (const gchar *vg_name, const gchar *pool_name, GError **error) {
    g_autofree gchar *kvdo_name = g_strdup_printf ("%s-%s-%s", vg_name, pool_name, VDO_POOL_SUFFIX);
    return vdo_get_stats_full (kvdo_name, error);
}

/**
 * bd_lvm_vdo_get_stats:
 * @vg_name: name of the VG that contains @pool_name VDO pool
 * @pool_name: name of the VDO pool to get statistics for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): a structure containing selected statistics or %NULL in case of error
 *                           (@error gets populated in those cases)
 *
 * In contrast to @bd_lvm_vdo_get_stats_full this function will only return selected statistics
 * in a fixed structure. In case a value is not available, -1 would be returned.
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMVDOStats* bd_lvm_vdo_get_stats (const gchar *vg_name, const gchar *pool_name, GError **error) {
    GHashTable *full_stats = NULL;
    BDLVMVDOStats *stats = NULL;

    full_stats = bd_lvm_vdo_get_stats_full (vg_name, pool_name, error);
    if (!full_stats)
        return NULL;

    stats = g_new0 (BDLVMVDOStats, 1);
    get_stat_val64_default (full_stats, "blockSize", &stats->block_size, -1);
    get_stat_val64_default (full_stats, "logicalBlockSize", &stats->logical_block_size, -1);
    get_stat_val64_default (full_stats, "physicalBlocks", &stats->physical_blocks, -1);
    get_stat_val64_default (full_stats, "dataBlocksUsed", &stats->data_blocks_used, -1);
    get_stat_val64_default (full_stats, "overheadBlocksUsed", &stats->overhead_blocks_used, -1);
    get_stat_val64_default (full_stats, "logicalBlocksUsed", &stats->logical_blocks_used, -1);
    get_stat_val64_default (full_stats, "usedPercent", &stats->used_percent, -1);
    get_stat_val64_default (full_stats, "savingPercent", &stats->saving_percent, -1);
    if (!get_stat_val_double (full_stats, "writeAmplificationRatio", &stats->write_amplification_ratio))
        stats->write_amplification_ratio = -1;

    g_hash_table_destroy (full_stats);

    return stats;
}

/* check whether the LVM devices file is enabled by LVM
 * we use the existence of the "lvmdevices" command to check whether the feature is available
 * or not, but this can still be disabled either in LVM or in lvm.conf
 */
static gboolean _lvm_devices_enabled () {
    const gchar *args[6] = {"lvmconfig", "--typeconfig", NULL, "devices/use_devicesfile", NULL, NULL};
    gboolean ret = FALSE;
    GError *loc_error = NULL;
    gchar *output = NULL;
    gboolean enabled = FALSE;
    gint scanned = 0;
    g_autofree gchar *config_arg = NULL;

    /* try full config first -- if we get something from this it means the feature is
       explicitly enabled or disabled by system lvm.conf or using the --config option */
    args[2] = "full";

    /* make sure to include the global config from us when getting the current config value */
    g_mutex_lock (&global_config_lock);
    if (global_config_str) {
        config_arg = g_strdup_printf ("--config=%s", global_config_str);
        args[4] = config_arg;
    }

    ret = bd_utils_exec_and_capture_output (args, NULL, &output, &loc_error);
    g_mutex_unlock (&global_config_lock);
    if (ret) {
        scanned = sscanf (output, "use_devicesfile=%u", &enabled);
        g_free (output);
        if (scanned != 1)
            return FALSE;

        return enabled;
    } else {
        g_clear_error (&loc_error);
        g_free (output);
    }

    output = NULL;

    /* now try default */
    args[2] = "default";
    ret = bd_utils_exec_and_capture_output (args, NULL, &output, &loc_error);
    if (ret) {
        scanned = sscanf (output, "# use_devicesfile=%u", &enabled);
        g_free (output);
        if (scanned != 1)
            return FALSE;

        return enabled;
    } else {
        g_clear_error (&loc_error);
        g_free (output);
    }

    return FALSE;
}

/**
 * bd_lvm_devices_add:
 * @device: device (PV) to add to the devices file
 * @devices_file: (nullable): LVM devices file or %NULL for default
 * @extra: (nullable) (array zero-terminated=1): extra options for the lvmdevices command
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @device was successfully added to @devices_file or not
 *
 * Tech category: %BD_LVM_TECH_DEVICES no mode (it is ignored)
 */
gboolean bd_lvm_devices_add (const gchar *device, const gchar *devices_file, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"lvmdevices", "--adddev", device, NULL, NULL};
    g_autofree gchar *devfile = NULL;

    if (!bd_lvm_is_tech_avail (BD_LVM_TECH_DEVICES, 0, error))
        return FALSE;

    if (!_lvm_devices_enabled ()) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DEVICES_DISABLED,
                     "LVM devices file not enabled.");
        return FALSE;
    }

    if (devices_file) {
        devfile = g_strdup_printf ("--devicesfile=%s", devices_file);
        args[3] = devfile;
    }

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_lvm_devices_delete:
 * @device: device (PV) to delete from the devices file
 * @devices_file: (nullable): LVM devices file or %NULL for default
 * @extra: (nullable) (array zero-terminated=1): extra options for the lvmdevices command
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @device was successfully removed from @devices_file or not
 *
 * Tech category: %BD_LVM_TECH_DEVICES no mode (it is ignored)
 */
gboolean bd_lvm_devices_delete (const gchar *device, const gchar *devices_file, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"lvmdevices", "--deldev", device, NULL, NULL};
    g_autofree gchar *devfile = NULL;

    if (!bd_lvm_is_tech_avail (BD_LVM_TECH_DEVICES, 0, error))
        return FALSE;

    if (!_lvm_devices_enabled ()) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DEVICES_DISABLED,
                     "LVM devices file not enabled.");
        return FALSE;
    }

    if (devices_file) {
        devfile = g_strdup_printf ("--devicesfile=%s", devices_file);
        args[3] = devfile;
    }

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_lvm_config_get:
 * @section: (nullable): LVM config section, e.g. 'global' or %NULL to print the entire config
 * @setting: (nullable): name of the specific setting, e.g. 'umask' or %NULL to print the entire @section
 * @type: type of the config, e.g. 'full' or 'current'
 * @values_only: whether to include only values without keys in the output
 * @global_config: whether to include our internal global config in the call or not
 * @extra: (nullable) (array zero-terminated=1): extra options for the lvmconfig command
 *                                               (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): Requested LVM config @section and @setting configuration or %NULL in case of error.
 *
 * Tech category: %BD_LVM_TECH_CONFIG no mode (it is ignored)
 */
gchar* bd_lvm_config_get (const gchar *section, const gchar *setting, const gchar *type, gboolean values_only, gboolean global_config, const BDExtraArg **extra, GError **error) {
    g_autofree gchar *conf_spec = NULL;
    g_autofree gchar *config_arg = NULL;
    const gchar *args[7] = {"lvmconfig", "--typeconfig", NULL, NULL, NULL, NULL, NULL};
    guint next_arg = 2;
    gchar *output = NULL;
    gboolean success = FALSE;

    if (!section && setting) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_FAIL,
                     "Specifying setting without section is not supported.");
        return NULL;
    }

    if (section)
        if (setting)
            conf_spec = g_strdup_printf ("%s/%s", section, setting);
        else
            conf_spec = g_strdup (section);
    else
        conf_spec = NULL;

    args[next_arg++] = type;
    args[next_arg++] = conf_spec;
    if (values_only)
        args[next_arg++] = "--valuesonly";

    g_mutex_lock (&global_config_lock);
    if (global_config && global_config_str) {
        config_arg = g_strdup_printf ("--config=%s", global_config_str);
        args[next_arg++] = config_arg;
    }
    g_mutex_unlock (&global_config_lock);

    success = bd_utils_exec_and_capture_output (args, extra, &output, error);
    if (!success)
        return NULL;
    return g_strchomp (output);
}

gboolean _vgcfgbackup_restore (const gchar *command, const gchar *vg_name, const gchar *file, const BDExtraArg **extra, GError **error) {
    const gchar *args[6] = {"lvm", NULL, NULL, NULL, NULL, NULL};
    guint next_arg = 1;
    gchar *output = NULL;
    g_autofree gchar *config_arg = NULL;

    args[next_arg++] = command;
    if (file) {
        args[next_arg++] = "-f";
        args[next_arg++] = file;
    }
    args[next_arg++] = vg_name;

    g_mutex_lock (&global_config_lock);
    if (global_config_str) {
        config_arg = g_strdup_printf ("--config=%s", global_config_str);
        args[next_arg++] = config_arg;
    }
    g_mutex_unlock (&global_config_lock);

    return bd_utils_exec_and_capture_output (args, extra, &output, error);
}

/**
 * bd_lvm_vgcfgbackup:
 * @vg_name: name of the VG to backup configuration
 * @backup_file: (nullable): file to save the backup to or %NULL for using the default backup file
 *                           in /etc/lvm/backup
 * @extra: (nullable) (array zero-terminated=1): extra options for the vgcfgbackup command
 *                                               (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Note: This function does not back up the data content of LVs. See `vgcfbackup(8)` man page
 *       for more information.
 *
 * Returns: Whether the backup was successfully created or not.
 *
 * Tech category: %BD_LVM_TECH_VG_CFG_BACKUP_RESTORE no mode (it is ignored)
 */
gboolean bd_lvm_vgcfgbackup (const gchar *vg_name, const gchar *backup_file, const BDExtraArg **extra, GError **error) {
    return _vgcfgbackup_restore ("vgcfgbackup", vg_name, backup_file, extra, error);
}

/**
 * bd_lvm_vgcfgrestore:
 * @vg_name: name of the VG to restore configuration
 * @backup_file: (nullable): file to restore VG configuration from to or %NULL for using the
 *                           latest backup in /etc/lvm/backup
 * @extra: (nullable) (array zero-terminated=1): extra options for the vgcfgrestore command
 *                                               (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Note: This function restores VG configuration created by %bd_lvm_vgcfgbackup from given
 *       @backup_file or from the latest backup in /etc/lvm/backup.
 *
 * Returns: Whether the configuration was successfully restored or not.
 *
 * Tech category: %BD_LVM_TECH_VG_CFG_BACKUP_RESTORE no mode (it is ignored)
 */
gboolean bd_lvm_vgcfgrestore (const gchar *vg_name, const gchar *backup_file, const BDExtraArg **extra, GError **error) {
    return _vgcfgbackup_restore ("vgcfgrestore", vg_name, backup_file, extra, error);
}
