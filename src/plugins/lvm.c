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
#include <sizes.h>
#include <math.h>
#include "lvm.h"

#define INT_FLOAT_EPS 1e-5

/**
 * SECTION: lvm
 * @short_description: libblockdev plugin for operations with LVM
 * @title: LVM
 * @include: lvm.h
 *
 * A libblockdev plugin for operations with LVM. All sizes passed in/out to/from
 * the functions are in bytes.
 */

static const gchar* supported_functions[] = {
    "bd_lvm_is_supported_pe_size",
    "bd_lvm_get_max_lv_size",
    "bd_lvm_round_size_to_pe",
    "bd_lvm_get_lv_physical_size",
    "bd_lvm_get_thpool_padding",
    NULL};

const gchar** get_supported_functions () {
    return supported_functions;
}

static gboolean call_lvm (gchar **args, gchar **stdout_data, gchar **stderr_data,
                          gint *status, gchar **error_message)
{
    guint i = 0;
    gboolean success;
    GError *error = NULL;

    guint args_length = g_strv_length (args);

    /* allocate enough space for the args plus "lvm" and NULL */
    gchar **argv = g_new (gchar*, args_length + 2);

    /* construct argv from args with "lvm" prepended */
    argv[0] = "lvm";
    for (i=0; i < args_length; i++)
        argv[i+1] = args[i];
    argv[args_length + 1] = NULL;

    success = g_spawn_sync (NULL, argv, NULL, G_SPAWN_DEFAULT|G_SPAWN_SEARCH_PATH,
                            NULL, NULL, stdout_data, stderr_data, status, &error);

    g_free (argv);

    if (!success) {
        *error_message = g_strdup (error->message);
        g_error_free (error);
        return FALSE;
    }

    return TRUE;
}

static gboolean call_lvm_and_report_error (gchar **argv, gchar **error_message) {
    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;
    gint status = 0;
    gboolean success = FALSE;

    success = call_lvm (argv, &stdout_data, &stderr_data, &status, error_message);
    if (!success)
        /* running lvm failed, the error message already is in the error_message
           variable so just return */
        return FALSE;

    if (status != 0) {
        /* lvm was run, but some error happened, the interesting information is
           either in the stdout or in the stderr */
        if (stderr_data && (g_strcmp0 ("", stderr_data) != 0)) {
            *error_message = stderr_data;
            g_free (stdout_data);
        } else {
            *error_message = stdout_data;
            g_free (stderr_data);
        }

        return FALSE;
    }

    /* gotting here means everything was okay */
    return TRUE;
}

static gboolean call_lvm_and_capture_output (gchar **argv, gchar **output, gchar **error_message) {
    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;
    gint status = 0;
    gboolean success = FALSE;

    success = call_lvm (argv, &stdout_data, &stderr_data, &status, error_message);
    if (!success)
        /* running lvm failed, the error message already is in the error_message
           variable so just return */
        return FALSE;

    if ((status != 0) || (g_strcmp0 ("", stdout_data) == 0)) {
        /* lvm was run, but some error happened or there is no output data which
           is an error because by calling this function the caller asked for the
           output */
        if (stderr_data && (g_strcmp0 ("", stderr_data) != 0)) {
            *error_message = stderr_data;
            g_free (stdout_data);
        }

        return FALSE;
    } else {
        *output = stdout_data;
        g_free (stderr_data);
        return TRUE;
    }
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

    return table;
}

static BDLVMPVdata* get_pv_data_from_table (GHashTable *table, gboolean free_table) {
    BDLVMPVdata *data = g_new (BDLVMPVdata, 1);
    gchar *value = NULL;

    data->pv_name = g_strdup ((gchar*) g_hash_table_lookup (table, "LVM2_PV_NAME"));
    data->pv_uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "LVM2_PV_UUID"));

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PE_START");
    if (value)
        data->pe_start = g_ascii_strtoull (value, NULL, 0);

    data->vg_name = g_strdup ((gchar*) g_hash_table_lookup (table, "LVM2_VG_NAME"));
    data->vg_uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "LVM2_VG_UUID"));

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_SIZE");
    if (value)
        data->vg_size = g_ascii_strtoull (value, NULL, 0);

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_FREE");
    if (value)
        data->vg_free = g_ascii_strtoull (value, NULL, 0);

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_EXTENT_SIZE");
    if (value)
        data->vg_extent_size = g_ascii_strtoull (value, NULL, 0);

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_EXTENT_COUNT");
    if (value)
        data->vg_extent_count = g_ascii_strtoull (value, NULL, 0);

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_FREE_COUNT");
    if (value)
        data->vg_free_count = g_ascii_strtoull (value, NULL, 0);

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_PV_COUNT");
    if (value)
        data->vg_pv_count = g_ascii_strtoull (value, NULL, 0);

    if (free_table)
        g_hash_table_destroy (table);

    return data;
}

/**
 * bd_lvm_is_supported_pe_size:
 * @size: size (in bytes) to test
 *
 * Returns: whether the given size is supported physical extent size or not
 */
gboolean bd_lvm_is_supported_pe_size (guint64 size) {
    return (((size % 2) == 0) && (size >= (1 KiB)) && (size <= (16 GiB)));
}

/**
 * bd_lvm_get_supported_pe_sizes:
 *
 * Returns: (transfer full) (array zero-terminated=1): list of supported PE sizes
 */
guint64 *bd_lvm_get_supported_pe_sizes () {
    guint8 i;
    guint64 val = MIN_PE_SIZE;
    guint8 num_items = ((guint8) round (log2 ((double) MAX_PE_SIZE))) - ((guint8) round (log2 ((double) MIN_PE_SIZE))) + 2;
    guint64 *ret = g_new (guint64, num_items);

    for (i=0; (val <= MAX_PE_SIZE); i++, val = val * 2)
        ret[i] = val;

    ret[num_items-1] = 0;

    return ret;
}

/**
 * bd_lvm_get_max_lv_size:
 * Returns: maximum LV size in bytes
 */
guint64 bd_lvm_get_max_lv_size () {
    return MAX_LV_SIZE;
}

/**
 * bd_lvm_round_size_to_pe:
 * @size: size to be rounded
 * @pe_size: physical extent (PE) size or 0 to use the default
 * @roundup: whether to round up or down (ceil or floor)
 *
 * Returns: @size rounded to @pe_size according to the @roundup
 *
 * Rounds given @size up/down to a multiple of @pe_size according to the value of
 * the @roundup parameter.
 */
guint64 bd_lvm_round_size_to_pe (guint64 size, guint64 pe_size, gboolean roundup) {
    pe_size = RESOLVE_PE_SIZE(pe_size);
    guint64 delta = size % pe_size;
    if (delta == 0)
        return size;

    if (roundup)
        return size + (pe_size - delta);
    else
        return size - delta;
}

/**
 * bd_lvm_get_lv_physical_size:
 * @lv_size: LV size
 * @pe_size: PE size
 *
 * Returns: space taken on disk(s) by the LV with given @size
 *
 * Gives number of bytes needed for an LV with the size @lv_size on an LVM stack
 * using given @pe_size.
 */
guint64 bd_lvm_get_lv_physical_size (guint64 lv_size, guint64 pe_size) {
    /* TODO: should take into account mirroring and RAID in general? */

    /* add one PE for metadata */
    pe_size = RESOLVE_PE_SIZE(pe_size);

    return bd_lvm_round_size_to_pe (lv_size, pe_size, TRUE) + pe_size;
}

/**
 * bd_lvm_get_thpool_padding:
 * @size: size of the thin pool
 * @pe_size: PE size or 0 if the default value should be used
 * @included: if padding is already included in the size
 *
 * Returns: size of the padding needed for a thin pool with the given @size
 *         according to the @pe_size and @included
 */
guint64 bd_lvm_get_thpool_padding (guint64 size, guint64 pe_size, gboolean included) {
    guint64 raw_md_size;
    pe_size = RESOLVE_PE_SIZE(pe_size);

    if (included)
        raw_md_size = (guint64) ceil (size * THPOOL_MD_FACTOR_EXISTS);
    else
        raw_md_size = (guint64) ceil (size * THPOOL_MD_FACTOR_NEW);

    return MIN (bd_lvm_round_size_to_pe(raw_md_size, pe_size, TRUE),
                bd_lvm_round_size_to_pe(MAX_THPOOL_MD_SIZE, pe_size, TRUE));
}

/**
 * bd_lvm_is_valid_thpool_md_size:
 * @size: the size to be tested
 *
 * Returns: whether the given size is a valid thin pool metadata size or not
 */
gboolean bd_lvm_is_valid_thpool_md_size (guint64 size) {
    return ((MIN_THPOOL_MD_SIZE <= size) && (size <= MAX_THPOOL_MD_SIZE));
}

/**
 * bd_lvm_is_valid_thpool_chunk_size:
 * @size: the size to be tested
 * @discard: whether discard/TRIM is required to be supported or not
 *
 * Returns: whether the given size is a valid thin pool chunk size or not
 */
gboolean bd_lvm_is_valid_thpool_chunk_size (guint64 size, gboolean discard) {
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
 * bd_lvm_pvcreate:
 * @device: the device to make PV from
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the PV was successfully created or not
 */
gboolean bd_lvm_pvcreate (gchar *device, gchar **error_message) {
    gchar *args[3] = {"pvcreate", device, NULL};

    return call_lvm_and_report_error (args, error_message);
}

/**
 * bd_lvm_pvresize:
 * @device: the device to resize
 * @size: the new requested size of the PV or 0 if it should be adjusted to device's size
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the PV's size was successfully changed or not
 *
 * If given @size different from 0, sets the PV's size to the given value (see
 * pvresize(8)). If given @size 0, adjusts the PV's size to the underlaying
 * block device's size.
 */
gboolean bd_lvm_pvresize (gchar *device, guint64 size, gchar **error_message) {
    gchar *size_str = NULL;
    gchar *args[5] = {"pvresize", NULL, NULL, NULL, NULL};
    guint8 next_pos = 1;
    guint8 to_free_pos = 0;
    gboolean ret = FALSE;

    if (size != 0) {
        size_str = g_strdup_printf ("%"G_GUINT64_FORMAT"b", size);
        args[next_pos] = "--setphysicalvolumesize";
        next_pos++;
        args[next_pos] = size_str;
        to_free_pos = next_pos;
        next_pos++;
    }

    args[next_pos] = device;

    ret = call_lvm_and_report_error (args, error_message);
    if (to_free_pos > 0)
        g_free (args[to_free_pos]);

    return ret;
}

/**
 * bd_lvm_pvremove:
 * @device: the PV device to be removed/destroyed
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the PV was successfully removed/destroyed or not
 */
gboolean bd_lvm_pvremove (gchar *device, gchar **error_message) {
    /* one has to be really persuasive to remove a PV (the double --force is not
       bug, at least not in this code) */
    gchar *args[6] = {"pvremove", "--force", "--force", "--yes", device, NULL};

    return call_lvm_and_report_error (args, error_message);
}

/**
 * bd_lvm_pvmove:
 * @src: the PV device to move extents off of
 * @dest: (allow-none): the PV device to move extents onto or #NULL
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the extents from the @src PV where successfully moved or not
 *
 * If @dest is #NULL, VG allocation rules are used for the extents from the @src
 * PV (see pvmove(8)).
 */
gboolean bd_lvm_pvmove (gchar *src, gchar *dest, gchar **error_message) {
    gchar *args[4] = {"pvmove", src, NULL, NULL};
    if (dest)
        args[2] = dest;

    return call_lvm_and_report_error (args, error_message);
}

/**
 * bd_lvm_pvscan:
 * @device: the device to scan for PVs
 * @update_cache: whether to update the lvmetad cache or not
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @device was successfully scanned for PVs or not
 */
gboolean bd_lvm_pvscan (gchar *device, gboolean update_cache, gchar **error_message) {
    gchar *args[4] = {"pvscan", NULL, NULL, NULL};
    if (update_cache) {
        args[1] = "--cache";
        args[2] = device;
    } else
        args[1] = device;

    return call_lvm_and_report_error (args, error_message);
}

/**
 * bd_lvm_pvinfo:
 * @device: a PV to get information about or #NULL
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: (transfer full): information about the PV on the given @device or
 * #NULL in case of error (the @error_message gets populated in those cases)
 */
BDLVMPVdata* bd_lvm_pvinfo (gchar *device, gchar **error_message) {
    gchar *args[9] = {"pvs", "--unit=k", "--nosuffix", "--nameprefixes",
                      "--unquoted", "--noheadings",
                      "-opv_name,pv_uuid,pe_start,vg_name,vg_uuid,vg_size,vg_free," \
                      "vg_extent_size,vg_extent_count,vg_free_count,pv_count",
                      device, NULL};
    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;

    success = call_lvm_and_capture_output (args, &output, error_message);
    if (!success)
        /* the error_message is already populated from the call */
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 11)) {
            g_strfreev (lines);
            return get_pv_data_from_table (table, TRUE);
        } else
            if (table)
                g_hash_table_destroy (table);
    }

    /* getting here means no usable info was found */
    return NULL;
}

/**
 * bd_lvm_vgcreate:
 * @name: name of the newly created VG
 * @pv_list: (array zero-terminated=1): list of PVs the newly created VG should use
 * @pe_size: PE size or 0 if the default value should be used
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the VG @name was successfully created or not
 */
gboolean bd_lvm_vgcreate (gchar *name, gchar **pv_list, guint64 pe_size, gchar **error_message) {
    guint8 i = 0;
    guint8 pv_list_len = g_strv_length (pv_list);
    gchar **argv = g_new (gchar*, pv_list_len + 5);
    pe_size = RESOLVE_PE_SIZE (pe_size);
    gboolean success = FALSE;

    argv[0] = "vgcreate";
    argv[1] = "-s";
    argv[2] = g_strdup_printf ("%"G_GUINT64_FORMAT"b", pe_size);
    argv[3] = name;
    for (i=4; i < (pv_list_len + 4); i++) {
        argv[i] = pv_list[i-4];
    }
    argv[i] = NULL;

    success = call_lvm_and_report_error (argv, error_message);
    g_free (argv[2]);
    g_free (argv);

    return success;
}

/**
 * bd_lvm_vgremove:
 * @vg_name: name of the to be removed VG
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the VG was successfully removed or not
 *
 */
gboolean bd_lvm_vgremove (gchar *vg_name, gchar **error_message) {
    gchar *args[4] = {"vgremove", "--force", vg_name, NULL};

    return call_lvm_and_report_error (args, error_message);
}

/**
 * bd_lvm_vgactivate:
 * @vg_name: name of the to be activated VG
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the VG was successfully activated or not
 *
 */
gboolean bd_lvm_vgactivate (gchar *vg_name, gchar **error_message) {
    gchar *args[4] = {"vgchange", "-ay", vg_name, NULL};

    return call_lvm_and_report_error (args, error_message);
}

/**
 * bd_lvm_vgdeactivate:
 * @vg_name: name of the to be deactivated VG
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the VG was successfully deactivated or not
 *
 */
gboolean bd_lvm_vgdeactivate (gchar *vg_name, gchar **error_message) {
    gchar *args[4] = {"vgchange", "-an", vg_name, NULL};

    return call_lvm_and_report_error (args, error_message);
}

#ifdef TESTING_LVM
#include "test_lvm.c"
#endif

