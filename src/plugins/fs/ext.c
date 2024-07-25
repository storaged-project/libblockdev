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

#include <ext2fs.h>
#include <e2p.h>

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
#define DEPS_RESIZE2FS 3
#define DEPS_RESIZE2FS_MASK (1 << DEPS_RESIZE2FS)

#define DEPS_LAST 4

static const UtilDep deps[DEPS_LAST] = {
    {"mke2fs", NULL, NULL, NULL},
    {"e2fsck", NULL, NULL, NULL},
    {"tune2fs", NULL, NULL, NULL},
    {"resize2fs", NULL, NULL, NULL},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    DEPS_MKE2FS_MASK,       /* mkfs */
    0,                      /* wipe */
    DEPS_E2FSCK_MASK,       /* check */
    DEPS_E2FSCK_MASK,       /* repair */
    DEPS_TUNE2FS_MASK,      /* set-label */
    0,                      /* query */
    DEPS_RESIZE2FS_MASK,    /* resize */
    DEPS_TUNE2FS_MASK       /* set-uuid */
};


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
static gint8 filter_line_fsck (const gchar * line, guint8 total_stages) {
    static GRegex *output_regex = NULL;
    GMatchInfo *match_info;
    gint8 perc = -1;
    GError *l_error = NULL;

    if (output_regex == NULL) {
        /* Compile regular expression that matches to e2fsck progress output */
        output_regex = g_regex_new ("^([0-9][0-9]*) ([0-9][0-9]*) ([0-9][0-9]*) (/.*)", 0, 0, &l_error);
        if (output_regex == NULL) {
            bd_utils_log_format (BD_UTILS_LOG_ERR,
                                 "Failed to create regex for parsing progress: %s", l_error->message);
            g_clear_error (&l_error);
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
        bd_utils_log_format (BD_UTILS_LOG_DEBUG,
                             "Failed to parse progress from: %s", line);
        return -1;
    }
    g_match_info_free (match_info);
    return perc;
}

static gboolean extract_e2fsck_progress (const gchar *line, guint8 *completion) {
    /* A magic number 5, e2fsck has 5 stages, but this can't be read from the output in advance. */
    gint8 perc;

    perc = filter_line_fsck (line, 5);
    if (perc < 0)
        return FALSE;

    *completion = perc;
    return TRUE;
}


/**
 * bd_fs_ext_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
G_GNUC_INTERNAL gboolean
bd_fs_ext_is_tech_avail (BDFSTech tech G_GNUC_UNUSED, guint64 mode, GError **error) {
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

static BDExtraArg **ext_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    GPtrArray *options_array = g_ptr_array_new ();
    const BDExtraArg **extra_p = NULL;

    if (options->label && g_strcmp0 (options->label, "") != 0)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-L", options->label));

    if (options->uuid && g_strcmp0 (options->uuid, "") != 0)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-U", options->uuid));

    if (options->dry_run)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-n", ""));

    if (options->no_discard)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-E", "nodiscard"));

    if (options->force)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-F", ""));

    if (extra) {
        for (extra_p = extra; *extra_p; extra_p++)
            g_ptr_array_add (options_array, bd_extra_arg_copy ((BDExtraArg *) *extra_p));
    }

    g_ptr_array_add (options_array, NULL);

    return (BDExtraArg **) g_ptr_array_free (options_array, FALSE);
}

G_GNUC_INTERNAL BDExtraArg **
bd_fs_ext2_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    return ext_mkfs_options (options, extra);
}

G_GNUC_INTERNAL BDExtraArg **
bd_fs_ext3_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    return ext_mkfs_options (options, extra);
}

G_GNUC_INTERNAL BDExtraArg **
bd_fs_ext4_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    return ext_mkfs_options (options, extra);
}

static gboolean ext_mkfs (const gchar *device, const BDExtraArg **extra, const gchar *ext_version, GError **error) {
    const gchar *args[5] = {"mke2fs", "-t", ext_version, device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKE2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_ext2_mkfs:
 * @device: the device to create a new ext2 fs on
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mke2fs' utility)
 * @error: (out) (optional): place to store error (if any)
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mke2fs' utility)
 * @error: (out) (optional): place to store error (if any)
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.ext4' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a new ext4 fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_ext4_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_mkfs (device, extra, EXT4, error);
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the check (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out) (optional): place to store error (if any)
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the check (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out) (optional): place to store error (if any)
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the check (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out) (optional): place to store error (if any)
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
            bd_utils_log_format (BD_UTILS_LOG_WARNING,
                                 "File system errors on %s were successfully corrected, but system reboot is advised.",
                                 device);
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out) (optional): place to store error (if any)
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out) (optional): place to store error (if any)
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out) (optional): place to store error (if any)
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
 * @error: (out) (optional): place to store error (if any)
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
 * @error: (out) (optional): place to store error (if any)
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
 * @error: (out) (optional): place to store error (if any)
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
 * bd_fs_ext2_check_label:
 * @label: label to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @label is a valid label for the ext2 file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_ext2_check_label (const gchar *label, GError **error) {
    if (strlen (label) > 16) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                     "Label for ext filesystem must be at most 16 characters long.");
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_fs_ext3_check_label:
 * @label: label to check
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether @label is a valid label for the ext3 file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_ext3_check_label (const gchar *label, GError **error) {
    return bd_fs_ext2_check_label (label, error);
}

/**
 * bd_fs_ext4_check_label:
 * @label: label to check
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether @label is a valid label for the ext4 file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_ext4_check_label (const gchar *label, GError **error) {
    return bd_fs_ext2_check_label (label, error);
}

static gboolean ext_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    const gchar *args[5] = {"tune2fs", "-U", NULL, device, NULL};

    if (!check_deps (&avail_deps, DEPS_TUNE2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (!uuid)
        args[2] = "random";
    else
        args[2] = uuid;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_ext2_set_uuid:
 * @device: the device the file system on which to set UUID for
 * @uuid: (nullable): UUID to set %NULL to generate a new one
 *                      UUID can also be one of "clear", "random" and "time" to clear,
 *                      generate a new random/time-based UUID
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the UUID of ext2 file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_ext2_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    return ext_set_uuid (device, uuid, error);
}

/**
 * bd_fs_ext3_set_uuid:
 * @device: the device the file system on which to set UUID for
 * @uuid: (nullable): UUID to set %NULL to generate a new one
 *                      UUID can also be one of "clear", "random" and "time" to clear,
 *                      generate a new random/time-based UUID
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the UUID of ext3 file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_ext3_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    return ext_set_uuid (device, uuid, error);
}

/**
 * bd_fs_ext4_set_uuid:
 * @device: the device the file system on which to set UUID for
 * @uuid: (nullable): UUID to set %NULL to generate a new one
 *                      UUID can also be one of "clear", "random" and "time" to clear,
 *                      generate a new random/time-based UUID
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the UUID of ext4 file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_ext4_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    return ext_set_uuid (device, uuid, error);
}

/**
 * bd_fs_ext2_check_uuid:
 * @uuid: UUID to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @uuid is a valid UUID for the ext2 file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_ext2_check_uuid (const gchar *uuid, GError **error) {
    return check_uuid (uuid, error);
}

/**
 * bd_fs_ext3_check_uuid:
 * @uuid: UUID to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @uuid is a valid UUID for the ext3 file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_ext3_check_uuid (const gchar *uuid, GError **error) {
    return check_uuid (uuid, error);
}

/**
 * bd_fs_ext4_check_uuid:
 * @uuid: UUID to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @uuid is a valid UUID for the ext4 file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_ext4_check_uuid (const gchar *uuid, GError **error) {
    return check_uuid (uuid, error);
}

static gchar *decode_fs_state (unsigned short state) {
    return g_strdup_printf ("%s%s",
                            (state & EXT2_VALID_FS) ? "clean" : "not clean",
                            (state & EXT2_ERROR_FS) ? " with errors" : "");
}

static gchar *decode_uuid (void *uuid) {
    const char *str = e2p_uuid2str (uuid);
    if (g_strcmp0 (str, "<none>") == 0)
        str = "";
    return g_strdup (str);
}

static BDFSExtInfo* ext_get_info (const gchar *device, GError **error) {
    errcode_t retval;
    ext2_filsys fs;
    struct ext2_super_block *sb;
    BDFSExtInfo *ret = NULL;

    int flags = (EXT2_FLAG_JOURNAL_DEV_OK | EXT2_FLAG_SOFTSUPP_FEATURES |
                 EXT2_FLAG_64BITS | EXT2_FLAG_SUPER_ONLY |
                 EXT2_FLAG_IGNORE_CSUM_ERRORS);

#ifdef EXT2_FLAG_THREADS
    flags |= EXT2_FLAG_THREADS;
#endif

    retval = ext2fs_open (device,
                          flags,
                          0, /* use_superblock */
                          0, /* use_blocksize */
                          unix_io_manager,
                          &fs);
    if (retval) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL, "Failed to open ext4 file system");
        return NULL;
    }

    sb = fs->super;
    ret = g_new0 (BDFSExtInfo, 1);

    ret->label = g_strndup ((gchar *)sb->s_volume_name, sizeof (sb->s_volume_name));
    ret->uuid = decode_uuid (sb->s_uuid);
    ret->state = decode_fs_state (sb->s_state);
    ret->block_size = EXT2_BLOCK_SIZE (sb);
    ret->block_count = ext2fs_blocks_count (sb);
    ret->free_blocks = ext2fs_free_blocks_count (sb);

    ext2fs_close_free (&fs);
    return ret;
}

/**
 * bd_fs_ext2_get_info:
 * @device: the device the file system of which to get info for
 * @error: (out) (optional): place to store error (if any)
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
 * @error: (out) (optional): place to store error (if any)
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
 * @error: (out) (optional): place to store error (if any)
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'resize2fs' utility)
 * @error: (out) (optional): place to store error (if any)
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'resize2fs' utility)
 * @error: (out) (optional): place to store error (if any)
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'resize2fs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_ext4_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error) {
    return ext_resize (device, new_size, extra, error);
}

static guint64 ext_get_min_size (const gchar *device, GError **error) {
    const gchar *args[4] = {"resize2fs", "-P", device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    guint64 min_size = 0;
    gchar **key_val = NULL;
    BDFSExtInfo *info = NULL;

    if (!check_deps (&avail_deps, DEPS_RESIZE2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    info = ext_get_info (device, error);
    if (!info)
        return 0;

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        bd_fs_ext2_info_free (info);
        return 0;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (line_p=lines; *line_p; line_p++) {
        if (g_str_has_prefix (*line_p, "Estimated minimum size")) {
            key_val = g_strsplit (*line_p, ":", 2);
            if (g_strv_length (key_val) == 2) {
                min_size = g_ascii_strtoull (key_val[1], NULL, 0) * info->block_size;
                g_strfreev (lines);
                g_strfreev (key_val);
                bd_fs_ext2_info_free (info);
                return min_size;
            } else {
                g_strfreev (key_val);
                break;
            }
        }
    }

    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                 "Failed to get minimum size for '%s'", device);
    g_strfreev (lines);
    bd_fs_ext2_info_free (info);
    return 0;
}

/**
 * bd_fs_ext2_get_min_size:
 * @device: the device containing the file system to get min size for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: smallest shrunken filesystem size as reported by resize2fs
 *          in case of error 0 is returned and @error is set
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_RESIZE
 */
guint64 bd_fs_ext2_get_min_size (const gchar *device, GError **error) {
    return ext_get_min_size (device, error);
}

/**
 * bd_fs_ext3_get_min_size:
 * @device: the device containing the file system to get min size for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: smallest shrunken filesystem size as reported by resize2fs
 *          in case of error 0 is returned and @error is set
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_RESIZE
 */
guint64 bd_fs_ext3_get_min_size (const gchar *device, GError **error) {
    return ext_get_min_size (device, error);
}

/**
 * bd_fs_ext4_get_min_size:
 * @device: the device containing the file system to get min size for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: smallest shrunken filesystem size as reported by resize2fs
 *          in case of error 0 is returned and @error is set
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_RESIZE
 */
guint64 bd_fs_ext4_get_min_size (const gchar *device, GError **error) {
    return ext_get_min_size (device, error);
}
