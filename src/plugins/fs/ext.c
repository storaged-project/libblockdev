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
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <blockdev/utils.h>
#include <check_deps.h>

#include "common.h"
#include "fs.h"
#include "ext.h"

#define EXT2 "ext2"
#define EXT3 "ext3"
#define EXT4 "ext4"

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MKE2FS 0
#define DEPS_MKE2FS_MASK (1 << DEPS_MKE2FS)
#define DEPS_E2FSCK 1
#define DEPS_E2FSCK_MASK (1 << DEPS_E2FSCK)
#define DEPS_TUNE2FS 2
#define DEPS_TUNE2FS_MASK (1 << DEPS_TUNE2FS)
#define DEPS_DUMPE2FS 3
#define DEPS_DUMPE2FS_MASK (1 << DEPS_DUMPE2FS)
#define DEPS_RESIZE2FS 4
#define DEPS_RESIZE2FS_MASK (1 << DEPS_RESIZE2FS)

#define DEPS_LAST 5

static const UtilDep deps[DEPS_LAST] = {
    {"mke2fs", NULL, NULL, NULL},
    {"e2fsck", NULL, NULL, NULL},
    {"tune2fs", NULL, NULL, NULL},
    {"dumpe2fs", NULL, NULL, NULL},
    {"resize2fs", NULL, NULL, NULL},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    /*   mkfs          wipe     check               repair                set-label            query                resize */
    DEPS_MKE2FS_MASK,   0, DEPS_E2FSCK_MASK,   DEPS_E2FSCK_MASK,     DEPS_TUNE2FS_MASK,   DEPS_DUMPE2FS_MASK,  DEPS_RESIZE2FS_MASK
};

#define UNUSED __attribute__((unused))

static gint8 compute_percents (guint8 pass_cur, guint8 pass_total, gint val_cur, gint val_total) {
    gint perc;
    gint one_pass;
    /* first get a percentage in the current pass/stage */
    perc = (val_cur * 100) / val_total;

    /* now map it to the total progress, splitting the stages equally */
    one_pass = 100 / pass_total;
    perc = ((pass_cur - 1) * one_pass) + (perc / pass_total);

    return perc;
}

/**
 * filter_line_fsck: (skip)
 * Filter one line - decide what to do with it.
 *
 * Returns: Zero or positive number as a percentage, -1 if not a percentage, -2 on an error
 */
static gint8 filter_line_fsck (const gchar * line, guint8 total_stages, GError **error) {
    static GRegex *output_regex = NULL;
    GMatchInfo *match_info;
    gint8 perc = -1;

    if (output_regex == NULL) {
        /* Compile regular expression that matches to e2fsck progress output */
        output_regex = g_regex_new ("^([0-9][0-9]*) ([0-9][0-9]*) ([0-9][0-9]*) (/.*)", 0, 0, error);
        if (output_regex == NULL) {
            return -2;
        }
    }

    /* Execute regular expression */
    if (g_regex_match (output_regex, line, 0, &match_info)) {
        guint8 stage;
        gint64 val_cur;
        gint64 val_total;
        gchar *s;

        /* The output_regex ensures we have a number in these matches, so we can skip
         * tests for conversion errors.
         */
        s = g_match_info_fetch (match_info, 1);
        stage = (guint8) g_ascii_strtoull (s, (char **)NULL, 10);
        g_free (s);

        s = g_match_info_fetch (match_info, 2);
        val_cur = g_ascii_strtoll (s, (char **)NULL, 10);
        g_free (s);

        s = g_match_info_fetch (match_info, 3);
        val_total = g_ascii_strtoll (s, (char **)NULL, 10);
        g_free (s);

        perc = compute_percents (stage, total_stages, val_cur, val_total);
    } else {
        g_match_info_free (match_info);
        return -1;
    }
    g_match_info_free (match_info);
    return perc;
}

static gboolean extract_e2fsck_progress (const gchar *line, guint8 *completion) {
    /* A magic number 5, e2fsck has 5 stages, but this can't be read from the output in advance. */
    gint8 perc;
    GError **error = NULL;

    perc = filter_line_fsck (line, 5, error);
    if (perc < 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "An error occured when trying to parse a line with progress");
        return FALSE;
    }

    *completion = perc;
    return TRUE;
}


/**
 * bd_fs_ext_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean __attribute__ ((visibility ("hidden")))
bd_fs_ext_is_tech_avail (BDFSTech tech UNUSED, guint64 mode, GError **error) {
    guint32 required = 0;
    guint i = 0;
    for (i = 0; i <= BD_FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * bd_fs_ext2_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSExt2Info* bd_fs_ext2_info_copy (BDFSExt2Info *data) {
    if (data == NULL)
        return NULL;

    BDFSExt2Info *ret = g_new0 (BDFSExt2Info, 1);

    ret->label = g_strdup (data->label);
    ret->uuid = g_strdup (data->uuid);
    ret->state = g_strdup (data->state);
    ret->block_size = data->block_size;
    ret->block_count = data->block_count;
    ret->free_blocks = data->free_blocks;

    return ret;
}

/**
 * bd_fs_ext3_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSExt3Info* bd_fs_ext3_info_copy (BDFSExt3Info *data) {
    return (BDFSExt3Info*) bd_fs_ext2_info_copy (data);
}

/**
 * bd_fs_ext4_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSExt4Info* bd_fs_ext4_info_copy (BDFSExt4Info *data) {
    return (BDFSExt4Info*) bd_fs_ext2_info_copy (data);
}

/**
 * bd_fs_ext2_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_ext2_info_free (BDFSExt2Info *data) {
  if (data == NULL)
      return;

    g_free (data->label);
    g_free (data->uuid);
    g_free (data->state);
    g_free (data);
}

/**
 * bd_fs_ext3_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_ext3_info_free (BDFSExt3Info *data) {
    bd_fs_ext2_info_free ((BDFSExt2Info*) data);
}

/**
 * bd_fs_ext4_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_ext4_info_free (BDFSExt4Info *data) {
    bd_fs_ext2_info_free ((BDFSExt2Info*) data);
}

static gboolean ext_mkfs (const gchar *device, const BDExtraArg **extra, const gchar *ext_version, GError **error) {
    const gchar *args[6] = {"mke2fs", "-t", ext_version, "-F", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKE2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_ext2_mkfs:
 * @device: the device to create a new ext2 fs on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mke2fs' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a new ext2 fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_ext2_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_mkfs (device, extra, EXT2, error);
}

/**
 * bd_fs_ext3_mkfs:
 * @device: the device to create a new ext3 fs on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mke2fs' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a new ext3 fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_ext3_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_mkfs (device, extra, EXT3, error);
}

/**
 * bd_fs_ext4_mkfs:
 * @device: the device to create a new ext4 fs on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.ext4' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a new ext4 fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_ext4_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_mkfs (device, extra, EXT4, error);
}

/**
 * bd_fs_ext2_wipe:
 * @device: the device to wipe an ext2 signature from
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext2 signature was successfully wiped from the @device or
 *          not
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_ext2_wipe (const gchar *device, GError **error) {
    return wipe_fs (device, EXT2, FALSE, error);
}

/**
 * bd_fs_ext3_wipe:
 * @device: the device to wipe an ext3 signature from
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext3 signature was successfully wiped from the @device or
 *          not
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_ext3_wipe (const gchar *device, GError **error) {
    return wipe_fs (device, EXT3, FALSE, error);
}

/**
 * bd_fs_ext4_wipe:
 * @device: the device to wipe an ext4 signature from
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext4 signature was successfully wiped from the @device or
 *          not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_ext4_wipe (const gchar *device, GError **error) {
    return wipe_fs (device, EXT4, FALSE, error);
}

static gboolean ext_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    /* Force checking even if the file system seems clean. AND
     * Open the filesystem read-only, and assume an answer of no to all
     * questions. */
    const gchar *args_progress[7] = {"e2fsck", "-f", "-n", "-C", "1", device, NULL};
    const gchar *args[5] = {"e2fsck", "-f", "-n", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_E2FSCK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (bd_utils_prog_reporting_initialized ()) {
        ret = bd_utils_exec_and_report_progress (args_progress, extra, extract_e2fsck_progress, &status, error);
    } else {
        ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    }

    if (!ret && (status == 4)) {
        /* no error should be reported for exit code 4 - File system errors left uncorrected */
        g_clear_error (error);
    }
    return ret;
}

/**
 * bd_fs_ext2_check:
 * @device: the device the file system on which to check
 * @extra: (allow-none) (array zero-terminated=1): extra options for the check (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext2 file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_ext2_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_check (device, extra, error);
}

/**
 * bd_fs_ext3_check:
 * @device: the device the file system on which to check
 * @extra: (allow-none) (array zero-terminated=1): extra options for the check (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext3 file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_ext3_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_check (device, extra, error);
}

/**
 * bd_fs_ext4_check:
 * @device: the device the file system on which to check
 * @extra: (allow-none) (array zero-terminated=1): extra options for the check (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext4 file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_ext4_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_check (device, extra, error);
}

static gboolean ext_repair (const gchar *device, gboolean unsafe, const BDExtraArg **extra, GError **error) {
    /* Force checking even if the file system seems clean. AND
     *     Automatically repair what can be safely repaired. OR
     *     Assume an answer of `yes' to all questions. */
    const gchar *args_progress[7] = {"e2fsck", "-f", unsafe ? "-y" : "-p", "-C", "1", device, NULL};
    const gchar *args[5] = {"e2fsck", "-f", unsafe ? "-y" : "-p", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_E2FSCK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (bd_utils_prog_reporting_initialized ()) {
        ret = bd_utils_exec_and_report_progress (args_progress, extra, extract_e2fsck_progress, &status, error);
    } else {
        ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    }

    if (!ret) {
        if (status == 1) {
            /* no error should be reported for exit code 1 - File system errors corrected */
            g_clear_error (error);
            ret = TRUE;
        } else if (status == 2) {
            /* no error should be reported for exit code 2 - File system errors corrected, system should be rebooted */
            g_warning ("File system errors on %s were successfully corrected, but system reboot is advised.", device);
            g_clear_error (error);
            ret = TRUE;
        }
    }
    return ret;
}

/**
 * bd_fs_ext2_repair:
 * @device: the device the file system on which to repair
 * @unsafe: whether to do unsafe operations too
 * @extra: (allow-none) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext2 file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_ext2_repair (const gchar *device, gboolean unsafe, const BDExtraArg **extra, GError **error) {
    return ext_repair (device, unsafe, extra, error);
}

/**
 * bd_fs_ext3_repair:
 * @device: the device the file system on which to repair
 * @unsafe: whether to do unsafe operations too
 * @extra: (allow-none) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext3 file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_ext3_repair (const gchar *device, gboolean unsafe, const BDExtraArg **extra, GError **error) {
    return ext_repair (device, unsafe, extra, error);
}

/**
 * bd_fs_ext4_repair:
 * @device: the device the file system on which to repair
 * @unsafe: whether to do unsafe operations too
 * @extra: (allow-none) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext4 file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_ext4_repair (const gchar *device, gboolean unsafe, const BDExtraArg **extra, GError **error) {
    return ext_repair (device, unsafe, extra, error);
}

static gboolean ext_set_label (const gchar *device, const gchar *label, GError **error) {
    const gchar *args[5] = {"tune2fs", "-L", label, device, NULL};

    if (!check_deps (&avail_deps, DEPS_TUNE2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_ext2_set_label:
 * @device: the device the file system on which to set label for
 * @label: label to set
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the label of ext2 file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_ext2_set_label (const gchar *device, const gchar *label, GError **error) {
    return ext_set_label (device, label, error);
}

/**
 * bd_fs_ext3_set_label:
 * @device: the device the file system on which to set label for
 * @label: label to set
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the label of ext3 file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_ext3_set_label (const gchar *device, const gchar *label, GError **error) {
    return ext_set_label (device, label, error);
}

/**
 * bd_fs_ext4_set_label:
 * @device: the device the file system on which to set label for
 * @label: label to set
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the label of ext4 file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_ext4_set_label (const gchar *device, const gchar *label, GError **error) {
    return ext_set_label (device, label, error);
}

/**
 * parse_output_vars: (skip)
 * @str: string to parse
 * @item_sep: item separator(s) (key-value pairs separator)
 * @key_val_sep: key-value separator(s) (typically ":" or "=")
 * @num_items: (out): number of parsed items (key-value pairs)
 *
 * Returns: (transfer full): GHashTable containing the key-value pairs parsed
 * from the @str.
 */
static GHashTable* parse_output_vars (const gchar *str, const gchar *item_sep, const gchar *key_val_sep, guint *num_items) {
    GHashTable *table = NULL;
    gchar **items = NULL;
    gchar **item_p = NULL;
    gchar **key_val = NULL;

    table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    *num_items = 0;

    items = g_strsplit_set (str, item_sep, 0);
    for (item_p=items; *item_p; item_p++) {
        key_val = g_strsplit (*item_p, key_val_sep, 2);
        if (g_strv_length (key_val) == 2) {
            /* we only want to process valid lines (with the separator) */
            g_hash_table_insert (table, g_strstrip (key_val[0]), g_strstrip (key_val[1]));
            g_free (key_val);
            (*num_items)++;
        } else
            /* invalid line, just free key_val */
            g_strfreev (key_val);
    }

    g_strfreev (items);
    return table;
}

static BDFSExtInfo* get_ext_info_from_table (GHashTable *table, gboolean free_table) {
    BDFSExtInfo *ret = g_new0 (BDFSExtInfo, 1);
    gchar *value = NULL;

    ret->label = g_strdup ((gchar*) g_hash_table_lookup (table, "Filesystem volume name"));
    if (!ret->label || g_strcmp0 (ret->label, "<none>") == 0) {
        g_free (ret->label);
        ret->label = g_strdup ("");
    }

    ret->uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "Filesystem UUID"));
    if (!ret->uuid || g_strcmp0 (ret->uuid, "<none>") == 0) {
        g_free (ret->uuid);
        ret->uuid = g_strdup ("");
    }

    ret->state = g_strdup ((gchar*) g_hash_table_lookup (table, "Filesystem state"));

    value = (gchar*) g_hash_table_lookup (table, "Block size");
    if (value)
        ret->block_size = g_ascii_strtoull (value, NULL, 0);
    else
        ret->block_size = 0;
    value = (gchar*) g_hash_table_lookup (table, "Block count");
    if (value)
        ret->block_count = g_ascii_strtoull (value, NULL, 0);
    else
        ret->block_count = 0;
    value = (gchar*) g_hash_table_lookup (table, "Free blocks");
    if (value)
        ret->free_blocks = g_ascii_strtoull (value, NULL, 0);
    else
        ret->free_blocks = 0;

    if (free_table)
        g_hash_table_destroy (table);

    return ret;
}

static BDFSExtInfo* ext_get_info (const gchar *device, GError **error) {
    const gchar *args[4] = {"dumpe2fs", "-h", device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    GHashTable *table = NULL;
    guint num_items = 0;
    BDFSExtInfo *ret = NULL;

    if (!check_deps (&avail_deps, DEPS_DUMPE2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        return FALSE;
    }

    table = parse_output_vars (output, "\n", ":", &num_items);
    g_free (output);
    if (!table || (num_items == 0)) {
        /* something bad happened or some expected items were missing  */
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse ext4 file system information");
        if (table)
            g_hash_table_destroy (table);
        return NULL;
    }

    ret = get_ext_info_from_table (table, TRUE);
    if (!ret) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse ext4 file system information");
        return NULL;
    }

    return ret;
}

/**
 * bd_fs_ext2_get_info:
 * @device: the device the file system of which to get info for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_QUERY
 */
BDFSExt2Info* bd_fs_ext2_get_info (const gchar *device, GError **error) {
    return (BDFSExt2Info*) ext_get_info (device, error);
}

/**
 * bd_fs_ext3_get_info:
 * @device: the device the file system of which to get info for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_QUERY
 */
BDFSExt3Info* bd_fs_ext3_get_info (const gchar *device, GError **error) {
    return (BDFSExt3Info*) ext_get_info (device, error);
}

/**
 * bd_fs_ext4_get_info:
 * @device: the device the file system of which to get info for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_QUERY
 */
BDFSExt4Info* bd_fs_ext4_get_info (const gchar *device, GError **error) {
    return (BDFSExt4Info*) ext_get_info (device, error);
}

static gboolean ext_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"resize2fs", device, NULL, NULL};
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_RESIZE2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (new_size != 0)
        /* resize2fs doesn't understand bytes, just 512B sectors */
        args[2] = g_strdup_printf ("%"G_GUINT64_FORMAT"s", new_size / 512);
    ret = bd_utils_exec_and_report_error (args, extra, error);

    g_free ((gchar *) args[2]);
    return ret;
}

/**
 * bd_fs_ext2_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system (if 0, the file system is
 *            adapted to the underlying block device)
 * @extra: (allow-none) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'resize2fs' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_ext2_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error) {
    return ext_resize (device, new_size, extra, error);
}

/**
 * bd_fs_ext3_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system (if 0, the file system is
 *            adapted to the underlying block device)
 * @extra: (allow-none) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'resize2fs' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_ext3_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error) {
    return ext_resize (device, new_size, extra, error);
}

/**
 * bd_fs_ext4_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system (if 0, the file system is
 *            adapted to the underlying block device)
 * @extra: (allow-none) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'resize2fs' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_ext4_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error) {
    return ext_resize (device, new_size, extra, error);
}
