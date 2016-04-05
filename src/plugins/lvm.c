/*
 * Copyright (C) 2014  Red Hat, Inc.
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

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libdevmapper.h>
#include <unistd.h>
#include <utils.h>

#include "lvm.h"

#define INT_FLOAT_EPS 1e-5
#define SECTOR_SIZE 512

GMutex global_config_lock;
static gchar *global_config_str = NULL;

/**
 * SECTION: lvm
 * @short_description: plugin for operations with LVM
 * @title: LVM
 * @include: lvm.h
 *
 * A plugin for operations with LVM. All sizes passed in/out to/from
 * the functions are in bytes.
 */

/**
 * bd_lvm_error_quark: (skip)
 */
GQuark bd_lvm_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-lvm-error-quark");
}

BDLVMPVdata* bd_lvm_pvdata_copy (BDLVMPVdata *data) {
    BDLVMPVdata *new_data = g_new0 (BDLVMPVdata, 1);

    new_data->pv_name = g_strdup (data->pv_name);
    new_data->pv_uuid = g_strdup (data->pv_uuid);
    new_data->pv_free = data->pv_free;
    new_data->pv_size = data->pv_size;
    new_data->pe_start = data->pe_start;
    new_data->vg_name = g_strdup (data->vg_name);
    new_data->vg_size = data->vg_size;
    new_data->vg_free = data->vg_free;
    new_data->vg_extent_size = data->vg_extent_size;
    new_data->vg_extent_count = data->vg_extent_count;
    new_data->vg_free_count = data->vg_free_count;
    new_data->vg_pv_count = data->vg_pv_count;

    return new_data;
}

void bd_lvm_pvdata_free (BDLVMPVdata *data) {
    g_free (data->pv_name);
    g_free (data->pv_uuid);
    g_free (data->vg_name);
    g_free (data);
}

BDLVMVGdata* bd_lvm_vgdata_copy (BDLVMVGdata *data) {
    BDLVMVGdata *new_data = g_new0 (BDLVMVGdata, 1);

    new_data->name = g_strdup (data->name);
    new_data->uuid = g_strdup (data->uuid);
    new_data->size = data->size;
    new_data->free = data->free;
    new_data->extent_size = data->extent_size;
    new_data->extent_count = data->extent_count;
    new_data->free_count = data->free_count;
    new_data->pv_count = data->pv_count;
    return new_data;
}

void bd_lvm_vgdata_free (BDLVMVGdata *data) {
    g_free (data->name);
    g_free (data->uuid);
    g_free (data);
}

BDLVMLVdata* bd_lvm_lvdata_copy (BDLVMLVdata *data) {
    BDLVMLVdata *new_data = g_new0 (BDLVMLVdata, 1);

    new_data->lv_name = g_strdup (data->lv_name);
    new_data->vg_name = g_strdup (data->vg_name);
    new_data->uuid = g_strdup (data->uuid);
    new_data->size = data->size;
    new_data->attr = g_strdup (data->attr);
    new_data->segtype = g_strdup (data->segtype);
    return new_data;
}

void bd_lvm_lvdata_free (BDLVMLVdata *data) {
    g_free (data->lv_name);
    g_free (data->vg_name);
    g_free (data->uuid);
    g_free (data->attr);
    g_free (data->segtype);
    g_free (data);
}

BDLVMCacheStats* bd_lvm_cache_stats_copy (BDLVMCacheStats *data) {
    BDLVMCacheStats *new = g_new0 (BDLVMCacheStats, 1);

    new->block_size = data->block_size;
    new->cache_size = data->cache_size;
    new->cache_used = data->cache_used;
    new->md_block_size = data->md_block_size;
    new->md_size = data->md_size;
    new->md_used = data->md_used;
    new->read_hits = data->read_hits;
    new->read_misses = data->read_misses;
    new->write_hits = data->write_hits;
    new->write_misses = data->write_misses;
    new->mode = data->mode;

    return new;
}

void bd_lvm_cache_stats_free (BDLVMCacheStats *data) {
    g_free (data);
}

static gchar const * const supported_functions[] = {
    "bd_lvm_is_supported_pe_size",
    "bd_lvm_get_max_lv_size",
    "bd_lvm_round_size_to_pe",
    "bd_lvm_get_lv_physical_size",
    "bd_lvm_get_thpool_padding",
    NULL};

gchar const * const * get_supported_functions () {
    return supported_functions;
}

/**
 * check: (skip)
 */
gboolean check() {
    GError *error = NULL;
    gboolean ret = bd_utils_check_util_version ("lvm", LVM_MIN_VERSION, "version", "LVM version:\\s+([\\d\\.]+)", &error);

    if (!ret && error) {
        g_warning("Cannot load the LVM plugin: %s" , error->message);
        g_clear_error (&error);
    }
    return ret;
}

static gboolean call_lvm_and_report_error (gchar **args, GError **error) {
    gboolean success = FALSE;
    guint i = 0;
    guint args_length = g_strv_length (args);

    /* don't allow global config string changes during the run */
    g_mutex_lock (&global_config_lock);

    /* allocate enough space for the args plus "lvm", "--config" and NULL */
    gchar **argv = g_new0 (gchar*, args_length + 3);

    /* construct argv from args with "lvm" prepended */
    argv[0] = "lvm";
    for (i=0; i < args_length; i++)
        argv[i+1] = args[i];
    argv[args_length + 1] = global_config_str ? g_strdup_printf("--config=%s", global_config_str) : NULL;
    argv[args_length + 2] = NULL;

    success = bd_utils_exec_and_report_error (argv, error);
    g_mutex_unlock (&global_config_lock);
    g_free (argv[args_length + 1]);
    g_free (argv);

    return success;
}

static gboolean call_lvm_and_capture_output (gchar **args, gchar **output, GError **error) {
    gboolean success = FALSE;
    guint i = 0;
    guint args_length = g_strv_length (args);

    /* don't allow global config string changes during the run */
    g_mutex_lock (&global_config_lock);

    /* allocate enough space for the args plus "lvm", "--config" and NULL */
    gchar **argv = g_new0 (gchar*, args_length + 3);

    /* construct argv from args with "lvm" prepended */
    argv[0] = "lvm";
    for (i=0; i < args_length; i++)
        argv[i+1] = args[i];
    argv[args_length + 1] = global_config_str ? g_strdup_printf("--config=%s", global_config_str) : NULL;
    argv[args_length + 2] = NULL;

    success = bd_utils_exec_and_capture_output (argv, output, error);
    g_mutex_unlock (&global_config_lock);
    g_free (argv[args_length + 1]);
    g_free (argv);

    return success;
}

/**
 * parse_lvm_vars:
 * @str: string to parse
 * @num_items: (out): number of parsed items
 *
 * Returns: (transfer full): a GHashTable containing key-value items parsed from the @string
 */
static GHashTable* parse_lvm_vars (gchar *str, guint *num_items) {
    GHashTable *table = NULL;
    gchar **items = NULL;
    gchar **item_p = NULL;
    gchar **key_val = NULL;

    table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    *num_items = 0;

    items = g_strsplit_set (str, " \t\n", 0);
    for (item_p=items; *item_p; item_p++) {
        key_val = g_strsplit (*item_p, "=", 2);
        if (g_strv_length (key_val) == 2) {
            /* we only want to process valid lines (with the '=' character) */
            g_hash_table_insert (table, key_val[0], key_val[1]);
            (*num_items)++;
        } else
            /* invalid line, just free key_val */
            g_strfreev (key_val);
    }

    g_strfreev (items);
    return table;
}

static BDLVMPVdata* get_pv_data_from_table (GHashTable *table, gboolean free_table) {
    BDLVMPVdata *data = g_new0 (BDLVMPVdata, 1);
    gchar *value = NULL;

    data->pv_name = g_strdup ((gchar*) g_hash_table_lookup (table, "LVM2_PV_NAME"));
    data->pv_uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "LVM2_PV_UUID"));

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PV_FREE");
    if (value)
        data->pv_free = g_ascii_strtoull (value, NULL, 0);
    else
        data->pv_free = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PV_SIZE");
    if (value)
        data->pv_size = g_ascii_strtoull (value, NULL, 0);
    else
        data->pv_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PE_START");
    if (value)
        data->pe_start = g_ascii_strtoull (value, NULL, 0);
    else
        data->pe_start = 0;

    data->vg_name = g_strdup ((gchar*) g_hash_table_lookup (table, "LVM2_VG_NAME"));
    data->vg_uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "LVM2_VG_UUID"));

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_SIZE");
    if (value)
        data->vg_size = g_ascii_strtoull (value, NULL, 0);
    else
        data->vg_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_FREE");
    if (value)
        data->vg_free = g_ascii_strtoull (value, NULL, 0);
    else
        data->vg_free = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_EXTENT_SIZE");
    if (value)
        data->vg_extent_size = g_ascii_strtoull (value, NULL, 0);
    else
        data->vg_extent_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_EXTENT_COUNT");
    if (value)
        data->vg_extent_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->vg_extent_count = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_FREE_COUNT");
    if (value)
        data->vg_free_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->vg_free_count = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PV_COUNT");
    if (value)
        data->vg_pv_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->vg_pv_count = 0;

    if (free_table)
        g_hash_table_destroy (table);

    return data;
}

static BDLVMVGdata* get_vg_data_from_table (GHashTable *table, gboolean free_table) {
    BDLVMVGdata *data = g_new0 (BDLVMVGdata, 1);
    gchar *value = NULL;

    data->name = g_strdup (g_hash_table_lookup (table, "LVM2_VG_NAME"));
    data->uuid = g_strdup (g_hash_table_lookup (table, "LVM2_VG_UUID"));

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_SIZE");
    if (value)
        data->size = g_ascii_strtoull (value, NULL, 0);
    else
        data->size = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_FREE");
    if (value)
        data->free = g_ascii_strtoull (value, NULL, 0);
    else
        data->free= 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_EXTENT_SIZE");
    if (value)
        data->extent_size = g_ascii_strtoull (value, NULL, 0);
    else
        data->extent_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_EXTENT_COUNT");
    if (value)
        data->extent_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->extent_count = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_FREE_COUNT");
    if (value)
        data->free_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->free_count = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PV_COUNT");
    if (value)
        data->pv_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->pv_count = 0;

    if (free_table)
        g_hash_table_destroy (table);

    return data;
}

static BDLVMLVdata* get_lv_data_from_table (GHashTable *table, gboolean free_table) {
    BDLVMLVdata *data = g_new0 (BDLVMLVdata, 1);
    gchar *value = NULL;

    data->lv_name = g_strdup (g_hash_table_lookup (table, "LVM2_LV_NAME"));
    data->vg_name = g_strdup (g_hash_table_lookup (table, "LVM2_VG_NAME"));
    data->uuid = g_strdup (g_hash_table_lookup (table, "LVM2_LV_UUID"));

    value = (gchar*) g_hash_table_lookup (table, "LVM2_LV_SIZE");
    if (value)
        data->size = g_ascii_strtoull (value, NULL, 0);
    else
        data->size = 0;

    data->attr = g_strdup (g_hash_table_lookup (table, "LVM2_LV_ATTR"));
    data->segtype = g_strdup (g_hash_table_lookup (table, "LVM2_SEGTYPE"));

    if (free_table)
        g_hash_table_destroy (table);

    return data;
}

/**
 * bd_lvm_is_supported_pe_size:
 * @size: size (in bytes) to test
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given size is supported physical extent size or not
 */
gboolean bd_lvm_is_supported_pe_size (guint64 size, GError **error __attribute__((unused))) {
    return (((size % 2) == 0) && (size >= (BD_LVM_MIN_PE_SIZE)) && (size <= (BD_LVM_MAX_PE_SIZE)));
}

/**
 * bd_lvm_get_supported_pe_sizes:
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full) (array zero-terminated=1): list of supported PE sizes
 */
guint64 *bd_lvm_get_supported_pe_sizes (GError **error __attribute__((unused))) {
    guint8 i;
    guint64 val = BD_LVM_MIN_PE_SIZE;
    guint8 num_items = ((guint8) round (log2 ((double) BD_LVM_MAX_PE_SIZE))) - ((guint8) round (log2 ((double) BD_LVM_MIN_PE_SIZE))) + 2;
    guint64 *ret = g_new0 (guint64, num_items);

    for (i=0; (val <= BD_LVM_MAX_PE_SIZE); i++, val = val * 2)
        ret[i] = val;

    ret[num_items-1] = 0;

    return ret;
}

/**
 * bd_lvm_get_max_lv_size:
 * @error: (out): place to store error (if any)
 *
 * Returns: maximum LV size in bytes
 */
guint64 bd_lvm_get_max_lv_size (GError **error __attribute__((unused))) {
    return BD_LVM_MAX_LV_SIZE;
}

/**
 * bd_lvm_round_size_to_pe:
 * @size: size to be rounded
 * @pe_size: physical extent (PE) size or 0 to use the default
 * @roundup: whether to round up or down (ceil or floor)
 * @error: (out): place to store error (if any)
 *
 * Returns: @size rounded to @pe_size according to the @roundup
 *
 * Rounds given @size up/down to a multiple of @pe_size according to the value
 * of the @roundup parameter. If the rounded value is too big to fit in the
 * return type, the result is rounded down (floored) regardless of the @roundup
 * parameter.
 */
guint64 bd_lvm_round_size_to_pe (guint64 size, guint64 pe_size, gboolean roundup, GError **error __attribute__((unused))) {
    pe_size = RESOLVE_PE_SIZE(pe_size);
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
 * @error: (out): place to store error (if any)
 *
 * Returns: space taken on disk(s) by the LV with given @size
 *
 * Gives number of bytes needed for an LV with the size @lv_size on an LVM stack
 * using given @pe_size.
 */
guint64 bd_lvm_get_lv_physical_size (guint64 lv_size, guint64 pe_size, GError **error) {
    pe_size = RESOLVE_PE_SIZE(pe_size);

    /* the LV just takes space rounded up the the a multiple of extent size */
    return bd_lvm_round_size_to_pe (lv_size, pe_size, TRUE, error);
}

/**
 * bd_lvm_get_thpool_padding:
 * @size: size of the thin pool
 * @pe_size: PE size or 0 if the default value should be used
 * @included: if padding is already included in the size
 * @error: (out): place to store error (if any)
 *
 * Returns: size of the padding needed for a thin pool with the given @size
 *         according to the @pe_size and @included
 */
guint64 bd_lvm_get_thpool_padding (guint64 size, guint64 pe_size, gboolean included, GError **error) {
    guint64 raw_md_size;
    pe_size = RESOLVE_PE_SIZE(pe_size);

    if (included)
        raw_md_size = (guint64) ceil (size * THPOOL_MD_FACTOR_EXISTS);
    else
        raw_md_size = (guint64) ceil (size * THPOOL_MD_FACTOR_NEW);

    return MIN (bd_lvm_round_size_to_pe(raw_md_size, pe_size, TRUE, error),
                bd_lvm_round_size_to_pe(BD_LVM_MAX_THPOOL_MD_SIZE, pe_size, TRUE, error));
}

/**
 * bd_lvm_is_valid_thpool_md_size:
 * @size: the size to be tested
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given size is a valid thin pool metadata size or not
 */
gboolean bd_lvm_is_valid_thpool_md_size (guint64 size, GError **error __attribute__((unused))) {
    return ((BD_LVM_MIN_THPOOL_MD_SIZE <= size) && (size <= BD_LVM_MAX_THPOOL_MD_SIZE));
}

/**
 * bd_lvm_is_valid_thpool_chunk_size:
 * @size: the size to be tested
 * @discard: whether discard/TRIM is required to be supported or not
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given size is a valid thin pool chunk size or not
 */
gboolean bd_lvm_is_valid_thpool_chunk_size (guint64 size, gboolean discard, GError **error __attribute__((unused))) {
    gdouble size_log2 = 0.0;

    if ((size < BD_LVM_MIN_THPOOL_CHUNK_SIZE) || (size > BD_LVM_MAX_THPOOL_CHUNK_SIZE))
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
 * bd_lvm_pvcreate:
 * @device: the device to make PV from
 * @data_alignment: data (first PE) alignment or 0 to use the default
 * @metadata_size: size of the area reserved for metadata or 0 to use the default
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the PV was successfully created or not
 */
gboolean bd_lvm_pvcreate (gchar *device, guint64 data_alignment, guint64 metadata_size, GError **error) {
    gchar *args[5] = {"pvcreate", device, NULL, NULL, NULL};
    guint next_arg = 2;
    gchar *dataalign_str = NULL;
    gchar *metadata_str = NULL;
    gboolean ret = FALSE;

    if (data_alignment != 0) {
        dataalign_str = g_strdup_printf ("--dataalignment=%"G_GUINT64_FORMAT"K", data_alignment / 1024);
        args[next_arg++] = dataalign_str;
    }

    if (metadata_size != 0) {
        metadata_str = g_strdup_printf ("--metadatasize=%"G_GUINT64_FORMAT"K", metadata_size / 1024);
        args[next_arg++] = metadata_str;
    }

    ret = call_lvm_and_report_error (args, error);
    g_free (dataalign_str);
    g_free (metadata_str);

    return ret;
}

/**
 * bd_lvm_pvresize:
 * @device: the device to resize
 * @size: the new requested size of the PV or 0 if it should be adjusted to device's size
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the PV's size was successfully changed or not
 *
 * If given @size different from 0, sets the PV's size to the given value (see
 * pvresize(8)). If given @size 0, adjusts the PV's size to the underlaying
 * block device's size.
 */
gboolean bd_lvm_pvresize (gchar *device, guint64 size, GError **error) {
    gchar *size_str = NULL;
    gchar *args[5] = {"pvresize", NULL, NULL, NULL, NULL};
    guint8 next_pos = 1;
    guint8 to_free_pos = 0;
    gboolean ret = FALSE;

    if (size != 0) {
        size_str = g_strdup_printf ("%"G_GUINT64_FORMAT"K", size / 1024);
        args[next_pos] = "--setphysicalvolumesize";
        next_pos++;
        args[next_pos] = size_str;
        to_free_pos = next_pos;
        next_pos++;
    }

    args[next_pos] = device;

    ret = call_lvm_and_report_error (args, error);
    if (to_free_pos > 0)
        g_free (args[to_free_pos]);

    return ret;
}

/**
 * bd_lvm_pvremove:
 * @device: the PV device to be removed/destroyed
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the PV was successfully removed/destroyed or not
 */
gboolean bd_lvm_pvremove (gchar *device, GError **error) {
    /* one has to be really persuasive to remove a PV (the double --force is not
       bug, at least not in this code) */
    gchar *args[6] = {"pvremove", "--force", "--force", "--yes", device, NULL};

    return call_lvm_and_report_error (args, error);
}

/**
 * bd_lvm_pvmove:
 * @src: the PV device to move extents off of
 * @dest: (allow-none): the PV device to move extents onto or %NULL
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the extents from the @src PV where successfully moved or not
 *
 * If @dest is %NULL, VG allocation rules are used for the extents from the @src
 * PV (see pvmove(8)).
 */
gboolean bd_lvm_pvmove (gchar *src, gchar *dest, GError **error) {
    gchar *args[4] = {"pvmove", src, NULL, NULL};
    if (dest)
        args[2] = dest;

    return call_lvm_and_report_error (args, error);
}

/**
 * bd_lvm_pvscan:
 * @device: (allow-none): the device to scan for PVs or %NULL
 * @update_cache: whether to update the lvmetad cache or not
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the system or @device was successfully scanned for PVs or not
 *
 * The @device argument is used only if @update_cache is %TRUE. Otherwise the
 * whole system is scanned for PVs.
 */
gboolean bd_lvm_pvscan (gchar *device, gboolean update_cache, GError **error) {
    gchar *args[4] = {"pvscan", NULL, NULL, NULL};
    if (update_cache) {
        args[1] = "--cache";
        args[2] = device;
    }
    else
        if (device)
            g_warning ("Ignoring the device argument in pvscan (cache update not requested)");

    return call_lvm_and_report_error (args, error);
}

/**
 * bd_lvm_pvinfo:
 * @device: a PV to get information about or %NULL
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the PV on the given @device or
 * %NULL in case of error (the @error) gets populated in those cases)
 */
BDLVMPVdata* bd_lvm_pvinfo (gchar *device, GError **error) {
    gchar *args[10] = {"pvs", "--unit=b", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--noheadings",
                       "-o", "pv_name,pv_uuid,pv_free,pv_size,pe_start,vg_name,vg_uuid,vg_size," \
                       "vg_free,vg_extent_size,vg_extent_count,vg_free_count,pv_count",
                       device, NULL};
    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;

    success = call_lvm_and_capture_output (args, &output, error);
    if (!success)
        /* the error is already populated from the call */
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 13)) {
            g_clear_error (error);
            g_strfreev (lines);
            return get_pv_data_from_table (table, TRUE);
        } else
            if (table)
                g_hash_table_destroy (table);
    }

    /* getting here means no usable info was found */
    g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                 "Failed to parse information about the PV");
    return NULL;
}

/**
 * bd_lvm_pvs:
 * @error: (out): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about PVs found in the system
 */
BDLVMPVdata** bd_lvm_pvs (GError **error) {
    gchar *args[9] = {"pvs", "--unit=b", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--noheadings",
                       "-o", "pv_name,pv_uuid,pv_free,pv_size,pe_start,vg_name,vg_uuid,vg_size," \
                       "vg_free,vg_extent_size,vg_extent_count,vg_free_count,pv_count",
                       NULL};
    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;
    GPtrArray *pvs = g_ptr_array_new ();
    BDLVMPVdata *pvdata = NULL;
    BDLVMPVdata **ret = NULL;
    guint64 i = 0;

    success = call_lvm_and_capture_output (args, &output, error);

    if (!success) {
        if (g_error_matches (*error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_NOOUT)) {
            /* no output => no VGs, not an error */
            g_clear_error (error);
            ret = g_new0 (BDLVMPVdata*, 1);
            ret[0] = NULL;
            return ret;
        }
        else
            /* the error is already populated from the call */
            return NULL;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 13)) {
            /* valid line, try to parse and record it */
            pvdata = get_pv_data_from_table (table, TRUE);
            if (pvdata)
                g_ptr_array_add (pvs, pvdata);
        } else
            if (table)
                g_hash_table_destroy (table);
    }

    g_strfreev (lines);

    if (pvs->len == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                     "Failed to parse information about PVs");
        return NULL;
    }

    /* now create the return value -- NULL-terminated array of BDLVMPVdata */
    ret = g_new0 (BDLVMPVdata*, pvs->len + 1);
    for (i=0; i < pvs->len; i++)
        ret[i] = (BDLVMPVdata*) g_ptr_array_index (pvs, i);
    ret[i] = NULL;

    g_ptr_array_free (pvs, FALSE);

    return ret;
}

/**
 * bd_lvm_vgcreate:
 * @name: name of the newly created VG
 * @pv_list: (array zero-terminated=1): list of PVs the newly created VG should use
 * @pe_size: PE size or 0 if the default value should be used
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG @name was successfully created or not
 */
gboolean bd_lvm_vgcreate (gchar *name, gchar **pv_list, guint64 pe_size, GError **error) {
    guint8 i = 0;
    guint8 pv_list_len = pv_list ? g_strv_length (pv_list) : 0;
    gchar **argv = g_new0 (gchar*, pv_list_len + 5);
    pe_size = RESOLVE_PE_SIZE (pe_size);
    gboolean success = FALSE;

    argv[0] = "vgcreate";
    argv[1] = "-s";
    argv[2] = g_strdup_printf ("%"G_GUINT64_FORMAT"K", pe_size / 1024);
    argv[3] = name;
    for (i=4; i < (pv_list_len + 4); i++) {
        argv[i] = pv_list[i-4];
    }
    argv[i] = NULL;

    success = call_lvm_and_report_error (argv, error);
    g_free (argv[2]);
    g_free (argv);

    return success;
}

/**
 * bd_lvm_vgremove:
 * @vg_name: name of the to be removed VG
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG was successfully removed or not
 */
gboolean bd_lvm_vgremove (gchar *vg_name, GError **error) {
    gchar *args[4] = {"vgremove", "--force", vg_name, NULL};

    return call_lvm_and_report_error (args, error);
}

/**
 * bd_lvm_vgrename:
 * @old_vg_name: old name of the VG to rename
 * @new_vg_name: new name for the @old_vg_name VG
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG was successfully renamed or not
 */
gboolean bd_lvm_vgrename (gchar *old_vg_name, gchar *new_vg_name, GError **error) {
    gchar *args[4] = {"vgrename", old_vg_name, new_vg_name, NULL};

    return call_lvm_and_report_error (args, error);
}

/**
 * bd_lvm_vgactivate:
 * @vg_name: name of the to be activated VG
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG was successfully activated or not
 */
gboolean bd_lvm_vgactivate (gchar *vg_name, GError **error) {
    gchar *args[4] = {"vgchange", "-ay", vg_name, NULL};

    return call_lvm_and_report_error (args, error);
}

/**
 * bd_lvm_vgdeactivate:
 * @vg_name: name of the to be deactivated VG
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG was successfully deactivated or not
 */
gboolean bd_lvm_vgdeactivate (gchar *vg_name, GError **error) {
    gchar *args[4] = {"vgchange", "-an", vg_name, NULL};

    return call_lvm_and_report_error (args, error);
}

/**
 * bd_lvm_vgextend:
 * @vg_name: name of the to be extended VG
 * @device: PV device to extend the @vg_name VG with
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG @vg_name was successfully extended with the given @device or not.
 */
gboolean bd_lvm_vgextend (gchar *vg_name, gchar *device, GError **error) {
    gchar *args[4] = {"vgextend", vg_name, device, NULL};

    return call_lvm_and_report_error (args, error);
}

/**
 * bd_lvm_vgreduce:
 * @vg_name: name of the to be reduced VG
 * @device: (allow-none): PV device the @vg_name VG should be reduced of or %NULL
 *                        if the VG should be reduced of the missing PVs
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG @vg_name was successfully reduced of the given @device or not
 *
 * Note: This function does not move extents off of the PV before removing
 *       it from the VG. You must do that first by calling #bd_lvm_pvmove.
 */
gboolean bd_lvm_vgreduce (gchar *vg_name, gchar *device, GError **error) {
    gchar *args[5] = {"vgreduce", NULL, NULL, NULL, NULL};

    if (!device) {
        args[1] = "--removemissing";
        args[2] = "--force";
        args[3] = vg_name;
    } else {
        args[1] = vg_name;
        args[2] = device;
    }

    return call_lvm_and_report_error (args, error);
}

/**
 * bd_lvm_vginfo:
 * @vg_name: a VG to get information about
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the @vg_name VG or %NULL in case
 * of error (the @error) gets populated in those cases)
 */
BDLVMVGdata* bd_lvm_vginfo (gchar *vg_name, GError **error) {
    gchar *args[10] = {"vgs", "--noheadings", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--units=b",
                       "-o", "name,uuid,size,free,extent_size,extent_count,free_count,pv_count",
                       vg_name, NULL};

    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;

    success = call_lvm_and_capture_output (args, &output, error);
    if (!success)
        /* the error is already populated from the call */
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 8)) {
            g_strfreev (lines);
            return get_vg_data_from_table (table, TRUE);
        } else
            if (table)
                g_hash_table_destroy (table);
    }

    /* getting here means no usable info was found */
    g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                 "Failed to parse information about the VG");
    return NULL;
}

/**
 * bd_lvm_vgs:
 * @error: (out): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about VGs found in the system
 */
BDLVMVGdata** bd_lvm_vgs (GError **error) {
    gchar *args[9] = {"vgs", "--noheadings", "--nosuffix", "--nameprefixes",
                      "--unquoted", "--units=b",
                      "-o", "name,uuid,size,free,extent_size,extent_count,free_count,pv_count",
                      NULL};
    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;
    GPtrArray *vgs = g_ptr_array_new ();
    BDLVMVGdata *vgdata = NULL;
    BDLVMVGdata **ret = NULL;
    guint64 i = 0;

    success = call_lvm_and_capture_output (args, &output, error);
    if (!success) {
        if (g_error_matches (*error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_NOOUT)) {
            /* no output => no VGs, not an error */
            g_clear_error (error);
            ret = g_new0 (BDLVMVGdata*, 1);
            ret[0] = NULL;
            return ret;
        }
        else
            /* the error is already populated from the call */
            return NULL;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 8)) {
            /* valid line, try to parse and record it */
            vgdata = get_vg_data_from_table (table, TRUE);
            if (vgdata)
                g_ptr_array_add (vgs, vgdata);
        } else
            if (table)
                g_hash_table_destroy (table);
    }

    g_strfreev (lines);

    if (vgs->len == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                     "Failed to parse information about VGs");
        return NULL;
    }

    /* now create the return value -- NULL-terminated array of BDLVMVGdata */
    ret = g_new0 (BDLVMVGdata*, vgs->len + 1);
    for (i=0; i < vgs->len; i++)
        ret[i] = (BDLVMVGdata*) g_ptr_array_index (vgs, i);
    ret[i] = NULL;

    g_ptr_array_free (vgs, FALSE);

    return ret;
}

/**
 * bd_lvm_lvorigin:
 * @vg_name: name of the VG containing the queried LV
 * @lv_name: name of the queried LV
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): the origin volume for the @vg_name/@lv_name LV or
 * %NULL if failed to determine (@error) is set in those cases)
 */
gchar* bd_lvm_lvorigin (gchar *vg_name, gchar *lv_name, GError **error) {
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar *args[6] = {"lvs", "--noheadings", "-o", "origin", NULL, NULL};
    args[4] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_capture_output (args, &output, error);
    g_free (args[4]);

    if (!success)
        /* the error is already populated from the call */
        return NULL;

    return g_strstrip (output);
}

/**
 * bd_lvm_lvcreate:
 * @vg_name: name of the VG to create a new LV in
 * @lv_name: name of the to-be-created LV
 * @size: requested size of the new LV
 * @type: (allow-none): type of the new LV ("striped", "raid1",..., see lvcreate (8))
 * @pv_list: (allow-none) (array zero-terminated=1): list of PVs the newly created LV should use or %NULL
 * if not specified
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given @vg_name/@lv_name LV was successfully created or not
 */
gboolean bd_lvm_lvcreate (gchar *vg_name, gchar *lv_name, guint64 size, gchar *type, gchar **pv_list, GError **error) {
    guint8 pv_list_len = pv_list ? g_strv_length (pv_list) : 0;
    gchar **args = g_new0 (gchar*, pv_list_len + 10);
    gboolean success = FALSE;
    guint64 i = 0;
    guint64 j = 0;
    gchar *size_str = NULL;
    gchar *type_str = NULL;

    args[i++] = "lvcreate";
    args[i++] = "-n";
    args[i++] = lv_name;
    args[i++] = "-L";
    size_str = g_strdup_printf ("%"G_GUINT64_FORMAT"K", size/1024);
    args[i++] = size_str;
    args[i++] = "-y";
    if (type) {
        if (g_strcmp0 (type, "striped") == 0) {
            args[i++] = "--stripes";
            type_str = g_strdup_printf ("%d", pv_list_len);
            args[i++] = type_str;
        } else {
            args[i++] = "--type";
            args[i++] = type;
        }
    }
    args[i++] = vg_name;

    for (j=0; j < pv_list_len; j++)
        args[i++] = pv_list[j];

    args[i] = NULL;

    success = call_lvm_and_report_error (args, error);
    g_free (size_str);
    g_free (type_str);
    g_free (args);

    return success;
}

/**
 * bd_lvm_lvremove:
 * @vg_name: name of the VG containing the to-be-removed LV
 * @lv_name: name of the to-be-removed LV
 * @force: whether to force removal or not
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully removed or not
 */
gboolean bd_lvm_lvremove (gchar *vg_name, gchar *lv_name, gboolean force, GError **error) {
    gchar *args[5] = {"lvremove", NULL, NULL, NULL, NULL};
    guint8 next_arg = 1;
    gboolean success = FALSE;

    if (force) {
        args[next_arg] = "--force";
        next_arg++;
        args[next_arg] = "--yes";
        next_arg++;
    }
    args[next_arg] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_report_error (args, error);
    g_free (args[next_arg]);

    return success;
}

/**
 * bd_lvm_lvrename:
 * @vg_name: name of the VG containing the to-be-renamed LV
 * @lv_name: name of the to-be-renamed LV
 * @new_name: new name for the @vg_name/@lv_name LV
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully renamed to
 * @vg_name/@new_name or not
 */
gboolean bd_lvm_lvrename (gchar *vg_name, gchar *lv_name, gchar *new_name, GError **error) {
    gchar *args[5] = {"lvrename", vg_name, lv_name, new_name, NULL};
    return call_lvm_and_report_error (args, error);
}


/**
 * bd_lvm_lvresize:
 * @vg_name: name of the VG containing the to-be-resized LV
 * @lv_name: name of the to-be-resized LV
 * @size: the requested new size of the LV
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully resized or not
 */
gboolean bd_lvm_lvresize (gchar *vg_name, gchar *lv_name, guint64 size, GError **error) {
    gchar *args[6] = {"lvresize", "--force", "-L", NULL, NULL, NULL};
    gboolean success = FALSE;

    args[3] = g_strdup_printf ("%"G_GUINT64_FORMAT"K", size/1024);
    args[4] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_report_error (args, error);
    g_free (args[3]);
    g_free (args[4]);

    return success;
}

/**
 * bd_lvm_lvactivate:
 * @vg_name: name of the VG containing the to-be-activated LV
 * @lv_name: name of the to-be-activated LV
 * @ignore_skip: whether to ignore the skip flag or not
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully activated or not
 */
gboolean bd_lvm_lvactivate (gchar *vg_name, gchar *lv_name, gboolean ignore_skip, GError **error) {
    gchar *args[5] = {"lvchange", "-ay", NULL, NULL, NULL};
    guint8 next_arg = 2;
    gboolean success = FALSE;

    if (ignore_skip) {
        args[next_arg] = "-K";
        next_arg++;
    }
    args[next_arg] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_report_error (args, error);
    g_free (args[next_arg]);

    return success;
}

/**
 * bd_lvm_lvdeactivate:
 * @vg_name: name of the VG containing the to-be-deactivated LV
 * @lv_name: name of the to-be-deactivated LV
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully deactivated or not
 */
gboolean bd_lvm_lvdeactivate (gchar *vg_name, gchar *lv_name, GError **error) {
    gchar *args[4] = {"lvchange", "-an", NULL, NULL};
    gboolean success = FALSE;

    args[2] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_report_error (args, error);
    g_free (args[2]);

    return success;
}

/**
 * bd_lvm_lvsnapshotcreate:
 * @vg_name: name of the VG containing the LV a new snapshot should be created of
 * @origin_name: name of the LV a new snapshot should be created of
 * @snapshot_name: name fo the to-be-created snapshot
 * @size: requested size for the snapshot
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @snapshot_name snapshot of the @vg_name/@origin_name LV
 * was successfully created or not.
 */
gboolean bd_lvm_lvsnapshotcreate (gchar *vg_name, gchar *origin_name, gchar *snapshot_name, guint64 size, GError **error) {
    gchar *args[8] = {"lvcreate", "-s", "-L", NULL, "-n", snapshot_name, NULL, NULL};
    gboolean success = FALSE;

    args[3] = g_strdup_printf ("%"G_GUINT64_FORMAT"K", size / 1024);
    args[6] = g_strdup_printf ("%s/%s", vg_name, origin_name);

    success = call_lvm_and_report_error (args, error);
    g_free (args[3]);
    g_free (args[6]);

    return success;
}

/**
 * bd_lvm_lvsnapshotmerge:
 * @vg_name: name of the VG containing the to-be-merged LV snapshot
 * @snapshot_name: name of the to-be-merged LV snapshot
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@snapshot_name LV snapshot was successfully merged or not
 */
gboolean bd_lvm_lvsnapshotmerge (gchar *vg_name, gchar *snapshot_name, GError **error) {
    gchar *args[4] = {"lvconvert", "--merge", NULL, NULL};
    gboolean success = FALSE;

    args[2] = g_strdup_printf ("%s/%s", vg_name, snapshot_name);

    success = call_lvm_and_report_error (args, error);
    g_free (args[2]);

    return success;
}

/**
 * bd_lvm_lvinfo:
 * @vg_name: name of the VG that contains the LV to get information about
 * @lv_name: name of the LV to get information about
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the @vg_name/@lv_name LV or %NULL in case
 * of error (the @error) gets populated in those cases)
 */
BDLVMLVdata* bd_lvm_lvinfo (gchar *vg_name, gchar *lv_name, GError **error) {
    gchar *args[11] = {"lvs", "--noheadings", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--units=b", "-a",
                       "-o", "vg_name,lv_name,lv_uuid,lv_size,lv_attr,segtype",
                       NULL, NULL};

    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;

    args[9] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_capture_output (args, &output, error);
    g_free (args[9]);

    if (!success)
        /* the error is already populated from the call */
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 6)) {
            g_strfreev (lines);
            return get_lv_data_from_table (table, TRUE);
        } else
            if (table)
                g_hash_table_destroy (table);
    }

    /* getting here means no usable info was found */
    g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                 "Failed to parse information about the LV");
    return NULL;
}

/**
 * bd_lvm_lvs:
 * @vg_name: (allow-none): name of the VG to get information about LVs from
 * @error: (out): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about LVs found in the given
 * @vg_name VG or in system if @vg_name is %NULL
 */
BDLVMLVdata** bd_lvm_lvs (gchar *vg_name, GError **error) {
    gchar *args[11] = {"lvs", "--noheadings", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--units=b", "-a",
                       "-o", "vg_name,lv_name,lv_uuid,lv_size,lv_attr,segtype",
                       NULL, NULL};

    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;
    GPtrArray *lvs = g_ptr_array_new ();
    BDLVMLVdata *lvdata = NULL;
    BDLVMLVdata **ret = NULL;
    guint64 i = 0;

    if (vg_name)
        args[9] = vg_name;

    success = call_lvm_and_capture_output (args, &output, error);

    if (!success) {
        if (g_error_matches (*error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_NOOUT)) {
            /* no output => no LVs, not an error */
            g_clear_error (error);
            ret = g_new0 (BDLVMLVdata*, 1);
            ret[0] = NULL;
            return ret;
        }
        else
            /* the error is already populated from the call */
            return NULL;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 6)) {
            /* valid line, try to parse and record it */
            lvdata = get_lv_data_from_table (table, TRUE);
            if (lvdata)
                g_ptr_array_add (lvs, lvdata);
        } else
            if (table)
                g_hash_table_destroy (table);
    }

    g_strfreev (lines);

    if (lvs->len == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                     "Failed to parse information about LVs");
        return NULL;
    }

    /* now create the return value -- NULL-terminated array of BDLVMLVdata */
    ret = g_new0 (BDLVMLVdata*, lvs->len + 1);
    for (i=0; i < lvs->len; i++)
        ret[i] = (BDLVMLVdata*) g_ptr_array_index (lvs, i);
    ret[i] = NULL;

    g_ptr_array_free (lvs, FALSE);

    return ret;
}

/**
 * bd_lvm_thpoolcreate:
 * @vg_name: name of the VG to create a thin pool in
 * @lv_name: name of the to-be-created pool LV
 * @size: requested size of the to-be-created pool
 * @md_size: requested metadata size or 0 to use the default
 * @chunk_size: requested chunk size or 0 to use the default
 * @profile: (allow-none): profile to use (see lvm(8) for more information) or %NULL to use
 *                         the default
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name thin pool was successfully created or not
 */
gboolean bd_lvm_thpoolcreate (gchar *vg_name, gchar *lv_name, guint64 size, guint64 md_size, guint64 chunk_size, gchar *profile, GError **error) {
    gchar *args[9] = {"lvcreate", "-T", "-L", NULL, NULL, NULL, NULL, NULL, NULL};
    guint8 next_arg = 4;
    gboolean success = FALSE;

    args[3] = g_strdup_printf ("%"G_GUINT64_FORMAT"K", size/1024);

    if (md_size != 0) {
        args[next_arg] = g_strdup_printf("--poolmetadatasize=%"G_GUINT64_FORMAT"K", md_size / 1024);
        next_arg++;
    }

    if (chunk_size != 0) {
        args[next_arg] = g_strdup_printf("--chunksize=%"G_GUINT64_FORMAT"K", chunk_size / 1024);
        next_arg++;
    }

    if (profile) {
        args[next_arg] = g_strdup_printf("--profile=%s", profile);
        next_arg++;
    }

    args[next_arg] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_report_error (args, error);
    g_free (args[3]);
    g_free (args[4]);
    g_free (args[5]);
    g_free (args[6]);
    g_free (args[7]);

    return success;
}

/**
 * bd_lvm_thlvcreate:
 * @vg_name: name of the VG containing the thin pool providing extents for the to-be-created thin LV
 * @pool_name: name of the pool LV providing extents for the to-be-created thin LV
 * @lv_name: name of the to-be-created thin LV
 * @size: requested virtual size of the to-be-created thin LV
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name thin LV was successfully created or not
 */
gboolean bd_lvm_thlvcreate (gchar *vg_name, gchar *pool_name, gchar *lv_name, guint64 size, GError **error) {
    gchar *args[8] = {"lvcreate", "-T", NULL, "-V", NULL, "-n", lv_name, NULL};
    gboolean success;

    args[2] = g_strdup_printf ("%s/%s", vg_name, pool_name);
    args[4] = g_strdup_printf ("%"G_GUINT64_FORMAT"K", size / 1024);

    success = call_lvm_and_report_error (args, error);
    g_free (args[2]);
    g_free (args[4]);

    return success;
}

/**
 * bd_lvm_thlvpoolname:
 * @vg_name: name of the VG containing the queried thin LV
 * @lv_name: name of the queried thin LV
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): the name of the pool volume for the @vg_name/@lv_name
 * thin LV or %NULL if failed to determine (@error) is set in those cases)
 */
gchar* bd_lvm_thlvpoolname (gchar *vg_name, gchar *lv_name, GError **error) {
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar *args[6] = {"lvs", "--noheadings", "-o", "pool_lv", NULL, NULL};
    args[4] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_capture_output (args, &output, error);
    g_free (args[4]);

    if (!success)
        /* the error is already populated from the call */
        return NULL;

    return g_strstrip (output);
}

/**
 * bd_lvm_thsnapshotcreate:
 * @vg_name: name of the VG containing the thin LV a new snapshot should be created of
 * @origin_name: name of the thin LV a new snapshot should be created of
 * @snapshot_name: name fo the to-be-created snapshot
 * @pool_name: (allow-none): name of the thin pool to create the snapshot in or %NULL if not specified
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @snapshot_name snapshot of the @vg_name/@origin_name
 * thin LV was successfully created or not.
 */
gboolean bd_lvm_thsnapshotcreate (gchar *vg_name, gchar *origin_name, gchar *snapshot_name, gchar *pool_name, GError **error) {
    gchar *args[8] = {"lvcreate", "-s", "-n", snapshot_name, NULL, NULL, NULL, NULL};
    guint next_arg = 4;
    gboolean success = FALSE;

    if (pool_name) {
        args[next_arg] = "--thinpool";
        next_arg++;
        args[next_arg] = pool_name;
        next_arg++;
    }

    args[next_arg] = g_strdup_printf ("%s/%s", vg_name, origin_name);

    success = call_lvm_and_report_error (args, error);
    g_free (args[next_arg]);

    return success;
}

/**
 * bd_lvm_set_global_config:
 * @new_config: (allow-none): string representation of the new global LVM
 *                            configuration to set or %NULL to reset to default
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the new requested global config @new_config was successfully
 *          set or not
 */
gboolean bd_lvm_set_global_config (gchar *new_config, GError **error __attribute__((unused))) {
    /* XXX: the error attribute will likely be used in the future when
       some validation comes into the game */

    g_mutex_lock (&global_config_lock);

    /* first free the old value */
    g_free (global_config_str);

    /* now store the new one */
    global_config_str = g_strdup (new_config);

    g_mutex_unlock (&global_config_lock);
    return TRUE;
}

/**
 * bd_lvm_get_global_config:
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): a copy of a string representation of the currently
 *                           set LVM global configuration
 */
gchar* bd_lvm_get_global_config (GError **error __attribute__((unused))) {
    gchar *ret = NULL;

    g_mutex_lock (&global_config_lock);
    ret = g_strdup (global_config_str ? global_config_str : "");
    g_mutex_unlock (&global_config_lock);

    return ret;
}

/**
 * bd_lvm_cache_get_default_md_size:
 * @cache_size: size of the cache to determine MD size for
 * @error: (out): place to store error (if any)
 *
 * Returns: recommended default size of the cache metadata LV or 0 in case of error
 */
guint64 bd_lvm_cache_get_default_md_size (guint64 cache_size, GError **error __attribute__((unused))) {
    return MAX ((guint64) cache_size / 1000, BD_LVM_MIN_CACHE_MD_SIZE);
}

/**
 * get_lv_type_from_flags: (skip)
 * @meta: getting type for a (future) metadata LV
 *
 * Get LV type string from flags.
 */
static gchar* get_lv_type_from_flags (BDLVMCachePoolFlags flags, gboolean meta, GError **error __attribute__((unused))) {
    if (!meta) {
        if (flags & BD_LVM_CACHE_POOL_STRIPED)
            return "striped";
        else if (flags & BD_LVM_CACHE_POOL_RAID1)
            return "raid1";
        else if (flags & BD_LVM_CACHE_POOL_RAID5)
            return "raid5";
        else if (flags & BD_LVM_CACHE_POOL_RAID6)
            return "raid6";
        else if (flags & BD_LVM_CACHE_POOL_RAID10)
            return "raid10";
        else
            return NULL;
    } else {
        if (flags & BD_LVM_CACHE_POOL_META_STRIPED)
            return "striped";
        else if (flags & BD_LVM_CACHE_POOL_META_RAID1)
            return "raid1";
        else if (flags & BD_LVM_CACHE_POOL_META_RAID5)
            return "raid5";
        else if (flags & BD_LVM_CACHE_POOL_META_RAID6)
            return "raid6";
        else if (flags & BD_LVM_CACHE_POOL_META_RAID10)
            return "raid10";
        else
            return NULL;
    }
}

/**
 * bd_lvm_cache_get_mode_str:
 * @mode: mode to get the string representation for
 * @error: (out): place to store error (if any)
 *
 * Returns: string representation of @mode or %NULL in case of error
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
 * @error: (out): place to store error (if any)
 *
 * Returns: cache mode for the @mode_str or %BD_LVM_CACHE_MODE_UNKNOWN if
 *          failed to determine
 */
BDLVMCacheMode bd_lvm_cache_get_mode_from_str (gchar *mode_str, GError **error) {
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
 * bd_lvm_cache_create_pool:
 * @vg_name: name of the VG to create @pool_name in
 * @pool_name: name of the cache pool LV to create
 * @pool_size: desired size of the cache pool @pool_name
 * @md_size: desired size of the @pool_name cache pool's metadata LV or 0 to
 *           use the default
 * @mode: cache mode of the @pool_name cache pool
 * @flags: a combination of (ORed) #BDLVMCachePoolFlags
 * @fast_pvs: (array zero-terminated=1): list of (fast) PVs to create the @pool_name
 *                                       cache pool (and the metadata LV)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the cache pool @vg_name/@pool_name was successfully created or not
 */
gboolean bd_lvm_cache_create_pool (gchar *vg_name, gchar *pool_name, guint64 pool_size, guint64 md_size, BDLVMCacheMode mode, BDLVMCachePoolFlags flags, gchar **fast_pvs, GError **error) {
    gboolean success = FALSE;
    gchar *type = NULL;
    gchar *name = NULL;
    gchar *args[10] = {"lvconvert", "-y", "--type", "cache-pool", "--poolmetadata", NULL, "--cachemode", NULL, NULL, NULL};

    /* create an LV for the pool */
    type = get_lv_type_from_flags (flags, FALSE, error);
    success = bd_lvm_lvcreate (vg_name, pool_name, pool_size, type, fast_pvs, error);
    if (!success) {
        g_prefix_error (error, "Failed to create the pool LV: ");
        return FALSE;
    }

    /* determine the size of the metadata LV */
    type = get_lv_type_from_flags (flags, TRUE, error);
    if (md_size == 0)
        md_size = bd_lvm_cache_get_default_md_size (pool_size, error);
    if (*error) {
        g_prefix_error (error, "Failed to determine size for the pool metadata LV: ");
        return FALSE;
    }
    name = g_strdup_printf ("%s_meta", pool_name);

    /* create the metadata LV */
    success = bd_lvm_lvcreate (vg_name, name, md_size, type, fast_pvs, error);
    if (!success) {
        g_free (name);
        g_prefix_error (error, "Failed to create the pool metadata LV: ");
        return FALSE;
    }

    /* create the cache pool from the two LVs */
    args[5] = name;
    args[7] = (gchar *) bd_lvm_cache_get_mode_str (mode, error);
    if (!args[7]) {
        g_free (args[5]);
        return FALSE;
    }
    name = g_strdup_printf ("%s/%s", vg_name, pool_name);
    args[8] = name;
    success = call_lvm_and_report_error (args, error);
    g_free (args[5]);
    g_free (args[8]);

    /* just return the result of the last step (it sets error on fail) */
    return success;
}

/**
 * bd_lvm_cache_attach:
 * @vg_name: name of the VG containing the @data_lv and the @cache_pool_lv LVs
 * @data_lv: data LV to attache the @cache_pool_lv to
 * @cache_pool_lv: cache pool LV to attach to the @data_lv
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @cache_pool_lv was successfully attached to the @data_lv or not
 */
gboolean bd_lvm_cache_attach (gchar *vg_name, gchar *data_lv, gchar *cache_pool_lv, GError **error) {
    gchar *args[7] = {"lvconvert", "--type", "cache", "--cachepool", NULL, NULL, NULL};
    gboolean success = FALSE;

    args[4] = g_strdup_printf ("%s/%s", vg_name, cache_pool_lv);
    args[5] = g_strdup_printf ("%s/%s", vg_name, data_lv);
    success = call_lvm_and_report_error (args, error);

    g_free (args[4]);
    g_free (args[5]);
    return success;
}

/**
 * bd_lvm_cache_detach:
 * @vg_name: name of the VG containing the @cached_lv
 * @cached_lv: name of the cached LV to detach its cache from
 * @destroy: whether to destroy the cache after detach or not
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the cache was successfully detached from the @cached_lv or not
 *
 * Note: synces the cache first
 */
gboolean bd_lvm_cache_detach (gchar *vg_name, gchar *cached_lv, gboolean destroy, GError **error) {
    /* need to both "assume yes" and "force" to get rid of the interactive
       questions in case of "--uncache" */
    gchar *args[6] = {"lvconvert", "-y", "-f", NULL, NULL, NULL};
    gboolean success = FALSE;

    args[3] = destroy ? "--uncache" : "--splitcache";
    args[4] = g_strdup_printf ("%s/%s", vg_name, cached_lv);
    success = call_lvm_and_report_error (args, error);

    g_free (args[4]);
    return success;
}

/**
 * bd_lvm_cache_create_cached_lv:
 * @vg_name: name of the VG to create a cached LV in
 * @lv_name: name of the cached LV to create
 * @data_size: size of the data LV
 * @cache_size: size of the cache (or cached LV more precisely)
 * @md_size: size of the cache metadata LV or 0 to use the default
 * @mode: cache mode for the cached LV
 * @flags: a combination of (ORed) #BDLVMCachePoolFlags
 * @slow_pvs: (array zero-terminated=1): list of slow PVs (used for the data LV)
 * @fast_pvs: (array zero-terminated=1): list of fast PVs (used for the cache LV)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the cached LV @lv_name was successfully created or not
 */
gboolean bd_lvm_cache_create_cached_lv (gchar *vg_name, gchar *lv_name, guint64 data_size, guint64 cache_size, guint64 md_size, BDLVMCacheMode mode, BDLVMCachePoolFlags flags,
                                        gchar **slow_pvs, gchar **fast_pvs, GError **error) {
    gboolean success = FALSE;
    gchar *name = NULL;

    name = g_strdup_printf ("%s_cache", lv_name);
    success = bd_lvm_cache_create_pool (vg_name, name, cache_size, md_size, mode, flags, fast_pvs, error);
    if (!success) {
        g_prefix_error (error, "Failed to create the cache pool '%s': ", name);
        g_free (name);
        return FALSE;
    }

    success = bd_lvm_lvcreate (vg_name, lv_name, data_size, NULL, slow_pvs, error);
    if (!success) {
        g_prefix_error (error, "Failed to create the data LV: ");
        return FALSE;
    }

    success = bd_lvm_cache_attach (vg_name, lv_name, name, error);
    if (!success) {
        g_prefix_error (error, "Failed to attach the cache pool '%s' to the data LV: ", name);
        g_free (name);
        return FALSE;
    }

    g_free (name);
    return TRUE;
}

/**
 * bd_lvm_cache_pool_name:
 * @vg_name: name of the VG containing the @cached_lv
 * @cached_lv: cached LV to get the name of the its pool LV for
 * @error: (out): place to store error (if any)
 *
 * Returns: name of the cache pool LV used by the @cached_lv or %NULL in case of error
 */
gchar* bd_lvm_cache_pool_name (gchar *vg_name, gchar *cached_lv, GError **error) {
    gchar *ret = NULL;
    gchar *name_start = NULL;
    gchar *name_end = NULL;
    gchar *pool_name = NULL;

    /* same as for a thin LV, but with square brackets */
    ret = bd_lvm_thlvpoolname (vg_name, cached_lv, error);
    if (!ret)
        return NULL;

    name_start = strchr (ret, '[');
    if (!name_start) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                     "Failed to determine cache pool name from: '%s'", ret);
        g_free (ret);
        return FALSE;
    }
    name_start++;

    name_end = strchr (ret, ']');
    if (!name_end) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                     "Failed to determine cache pool name from: '%s'", ret);
        g_free (ret);
        return FALSE;
    }

    pool_name = g_strndup (name_start, name_end - name_start);
    g_free (ret);

    return pool_name;
}

/**
 * bd_lvm_cache_stats:
 * @vg_name: name of the VG containing the @cached_lv
 * @cached_lv: cached LV to get stats for
 * @error: (out): place to store error (if any)
 *
 * Returns: stats for the @cached_lv or %NULL in case of error
 */
BDLVMCacheStats* bd_lvm_cache_stats (gchar *vg_name, gchar *cached_lv, GError **error) {
    struct dm_pool *pool = NULL;
    struct dm_task *task = NULL;
    struct dm_info info;
    struct dm_status_cache *status = NULL;
    gchar *map_name = NULL;
    guint64 start = 0;
    guint64 length = 0;
    gchar *type = NULL;
    gchar *params = NULL;
    BDLVMCacheStats *ret = NULL;

    if (geteuid () != 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_NOT_ROOT,
                     "Not running as root, cannot query DM maps");
        return NULL;
    }

    pool = dm_pool_create("bd-pool", 20);

    /* translate the VG+LV name into the DM map name */
    map_name = dm_build_dm_name (pool, vg_name, cached_lv, NULL);

    task = dm_task_create (DM_DEVICE_STATUS);
    if (!task) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to create DM task for the cache map '%s': ", map_name);
        dm_pool_destroy (pool);
        return NULL;
    }

    if (dm_task_set_name (task, map_name) == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to create DM task for the cache map '%s': ", map_name);
        dm_task_destroy (task);
        dm_pool_destroy (pool);
        return NULL;
    }

    if (dm_task_run(task) == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to run the DM task for the cache map '%s': ", map_name);
        dm_task_destroy (task);
        dm_pool_destroy (pool);
        return NULL;
    }

    if (dm_task_get_info (task, &info) == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to get task info for the cache map '%s': ", map_name);
        dm_task_destroy (task);
        dm_pool_destroy (pool);
        return NULL;
    }

    if (!info.exists) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_NOCACHE,
                     "The cache map '%s' doesn't exist: ", map_name);
        dm_task_destroy (task);
        dm_pool_destroy (pool);
        return NULL;
    }

    dm_get_next_target(task, NULL, &start, &length, &type, &params);

    if (dm_get_status_cache (pool, params, &status) == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                     "Failed to get status of the cache map '%s': ", map_name);
        dm_task_destroy (task);
        dm_pool_destroy (pool);
        return NULL;
    }

    ret = g_new0 (BDLVMCacheStats, 1);
    ret->block_size = status->block_size * SECTOR_SIZE;
    ret->cache_size = status->total_blocks * ret->block_size;
    ret->cache_used = status->used_blocks * ret->block_size;

    ret->md_block_size = status->metadata_block_size * SECTOR_SIZE;
    ret->md_size = status->metadata_total_blocks * ret->md_block_size;
    ret->md_used = status->metadata_used_blocks * ret->md_block_size;

    ret->read_hits = status->read_hits;
    ret->read_misses = status->read_misses;
    ret->write_hits = status->write_hits;
    ret->write_misses = status->write_misses;

    if (status->feature_flags & DM_CACHE_FEATURE_WRITETHROUGH)
        ret->mode = BD_LVM_CACHE_MODE_WRITETHROUGH;
    else if (status->feature_flags & DM_CACHE_FEATURE_WRITEBACK)
        ret->mode = BD_LVM_CACHE_MODE_WRITEBACK;
    else {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                      "Failed to determine status of the cache from '%"G_GUINT64_FORMAT"': ",
                      status->feature_flags);
        dm_task_destroy (task);
        dm_pool_destroy (pool);
        return NULL;
    }

    dm_task_destroy (task);
    dm_pool_destroy (pool);

    return ret;
}

/**
 * bd_lvm_data_lv_name:
 * @vg_name: name of the VG containing the queried LV
 * @lv_name: name of the queried LV
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): the name of the (internal) data LV of the
 * @vg_name/@lv_name LV
 */
gchar* bd_lvm_data_lv_name (gchar *vg_name, gchar *lv_name, GError **error) {
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar *args[6] = {"lvs", "--noheadings", "-o", "data_lv", NULL, NULL};

    args[4] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_capture_output (args, &output, error);
    g_free (args[4]);

    if (!success)
        /* the error is already populated from the call */
        return NULL;

    /* replace the '[' and ']' (marking the LV as internal) with spaces and then
       remove all the leading and trailing whitespace */
    return g_strstrip (g_strdelimit (output, "[]", ' '));
}

/**
 * bd_lvm_metadata_lv_name:
 * @vg_name: name of the VG containing the queried LV
 * @lv_name: name of the queried LV
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): the name of the (internal) metadata LV of the
 * @vg_name/@lv_name LV
 */
gchar* bd_lvm_metadata_lv_name (gchar *vg_name, gchar *lv_name, GError **error) {
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar *args[6] = {"lvs", "--noheadings", "-o", "metadata_lv", NULL, NULL};

    args[4] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_capture_output (args, &output, error);
    g_free (args[4]);

    if (!success)
        /* the error is already populated from the call */
        return NULL;

    /* replace the '[' and ']' (marking the LV as internal) with spaces and then
       remove all the leading and trailing whitespace */
    return g_strstrip (g_strdelimit (output, "[]", ' '));
}
