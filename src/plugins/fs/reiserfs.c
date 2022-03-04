/*
 * Copyright (C) 2020  Red Hat, Inc.
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

#include <blockdev/utils.h>
#include <check_deps.h>

#include "reiserfs.h"
#include "fs.h"
#include "common.h"

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MKREISERFS 0
#define DEPS_MKREISERFS_MASK (1 << DEPS_MKREISERFS)
#define DEPS_REISERFSCK 1
#define DEPS_REISERFSCK_MASK (1 << DEPS_REISERFSCK)
#define DEPS_REISERFSTUNE 2
#define DEPS_REISERFSTUNE_MASK (1 <<  DEPS_REISERFSTUNE)
#define DEPS_DEBUGREISERFS 3
#define DEPS_DEBUGREISERFS_MASK (1 << DEPS_DEBUGREISERFS)
#define DEPS_RESIZEREISERFS 4
#define DEPS_RESIZEREISERFS_MASK (1 << DEPS_RESIZEREISERFS)

#define DEPS_LAST 5

static const UtilDep deps[DEPS_LAST] = {
    {"mkreiserfs", NULL, NULL, NULL},
    {"reiserfsck", NULL, NULL, NULL},
    {"reiserfstune", NULL, NULL, NULL},
    {"debugreiserfs", NULL, NULL, NULL},
    {"resize_reiserfs", NULL, NULL, NULL},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    DEPS_MKREISERFS_MASK,       /* mkfs */
    0,                          /* wipe */
    DEPS_REISERFSCK_MASK,       /* check */
    DEPS_REISERFSCK_MASK,       /* repair */
    DEPS_REISERFSTUNE_MASK,     /* set-label */
    DEPS_DEBUGREISERFS_MASK,    /* query */
    DEPS_RESIZEREISERFS_MASK,   /* resize */
    DEPS_REISERFSTUNE_MASK,     /* set-uuid */
};

#define UNUSED __attribute__((unused))

#ifdef __clang__
#define ZERO_INIT {}
#else
#define ZERO_INIT {0}
#endif

/**
 * bd_fs_reiserfs_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean __attribute__ ((visibility ("hidden")))
bd_fs_reiserfs_is_tech_avail (BDFSTech tech UNUSED, guint64 mode, GError **error) {
    guint32 required = 0;
    guint i = 0;
    for (i = 0; i <= BD_FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * bd_fs_reiserfs_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSReiserFSInfo* bd_fs_reiserfs_info_copy (BDFSReiserFSInfo *data) {
    if (data == NULL)
        return NULL;

    BDFSReiserFSInfo *ret = g_new0 (BDFSReiserFSInfo, 1);

    ret->label = g_strdup (data->label);
    ret->uuid = g_strdup (data->uuid);
    ret->block_size = data->block_size;
    ret->block_count = data->block_count;
    ret->free_blocks = data->free_blocks;

    return ret;
}

/**
 * bd_fs_reiserfs_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_reiserfs_info_free (BDFSReiserFSInfo *data) {
    if (data == NULL)
        return;

    g_free (data->label);
    g_free (data->uuid);
    g_free (data);
}

BDExtraArg __attribute__ ((visibility ("hidden")))
**bd_fs_reiserfs_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    GPtrArray *options_array = g_ptr_array_new ();
    const BDExtraArg **extra_p = NULL;

    if (options->label)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-l", options->label));

    if (options->uuid)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-u", options->uuid));

    if (extra) {
        for (extra_p = extra; *extra_p; extra_p++)
            g_ptr_array_add (options_array, bd_extra_arg_copy ((BDExtraArg *) *extra_p));
    }

    g_ptr_array_add (options_array, NULL);

    return (BDExtraArg **) g_ptr_array_free (options_array, FALSE);
}

/**
 * bd_fs_reiserfs_mkfs:
 * @device: the device to create a new reiserfs fs on
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkreiserfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a new reiserfs fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_REISERFS-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_reiserfs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"mkreiserfs", "-ff", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKREISERFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_reiserfs_wipe:
 * @device: the device to wipe a reiserfs signature from
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the reiserfs signature was successfully wiped from the @device or
 *          not
 *
 * Tech category: %BD_FS_TECH_REISERFS-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_reiserfs_wipe (const gchar *device, GError **error) {
    return wipe_fs (device, "reiserfs", TRUE, error);
}

/**
 * bd_fs_reiserfs_check:
 * @device: the device containing the file system to check
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'reiserfsck' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the reiserfs file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_REISERFS-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_reiserfs_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"reiserfsck", "--check", "-y", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_REISERFSCK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    if (!ret && (status == 6)) {
        /* no error should be reported for exit code 6 -- File system fixable errors left uncorrected */
        g_clear_error (error);
    }
    return ret;
}

/**
 * bd_fs_reiserfs_repair:
 * @device: the device containing the file system to repair
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'reiserfsck' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the reiserfs file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_REISERFS-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_reiserfs_repair (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"reiserfsck", "--fix-fixable", "-y", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_REISERFSCK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    if (!ret && (status == 1)) {
        /* no error should be reported for exit code 1 -- File system errors corrected. */
        g_clear_error (error);
        ret = TRUE;
    }

    return ret;
}

/**
 * bd_fs_reiserfs_set_label:
 * @device: the device containing the file system to set label for
 * @label: label to set
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the label of reiserfs file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_REISERFS-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_reiserfs_set_label (const gchar *device, const gchar *label, GError **error) {
    const gchar *args[5] = {"reiserfstune", "-l", label, device, NULL};

    if (!check_deps (&avail_deps, DEPS_REISERFSTUNE_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (strlen (label) > 16)
        bd_utils_log (BD_UTILS_LOG_WARNING, "Given label is too long for ReiserFS and will be truncated." \
                                            "Labels on ReiserFS can be at most 16 characters long");

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_reiserfs_check_label:
 * @label: label to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @label is a valid label for the reiserfs file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_reiserfs_check_label (const gchar *label, GError **error) {
    if (strlen (label) > 16) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                     "Label for ReiserFS filesystem must be at most 16 characters long.");
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_fs_reiserfs_set_uuid:
 * @device: the device containing the file system to set UUID for
 * @uuid: (nullable): UUID to set or %NULL to generate a new one
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the uuid of reiserfs file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_REISERFS-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_reiserfs_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    const gchar *args[5] = {"reiserfstune", "-u", uuid, device, NULL};

    if (!check_deps (&avail_deps, DEPS_REISERFSTUNE_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (!uuid)
        args[2] = "random";

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_reiserfs_check_uuid:
 * @uuid: UUID to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @uuid is a valid UUID for the ReiserFS file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_reiserfs_check_uuid (const gchar *uuid, GError **error) {
    return check_uuid (uuid, error);
}

/**
 * bd_fs_reiserfs_get_info:
 * @device: the device containing the file system to get info for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_REISERFS-%BD_FS_TECH_MODE_QUERY
 */
BDFSReiserFSInfo* bd_fs_reiserfs_get_info (const gchar *device, GError **error) {
    const gchar *args[3] = {"debugreiserfs", device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    BDFSReiserFSInfo *ret = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar *val_start = NULL;

    if (!check_deps (&avail_deps, DEPS_DEBUGREISERFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return NULL;

    ret = g_new0 (BDFSReiserFSInfo, 1);

    success = get_uuid_label (device, &(ret->uuid), &(ret->label), error);
    if (!success) {
        /* error is already populated */
        bd_fs_reiserfs_info_free (ret);
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        bd_fs_reiserfs_info_free (ret);
        return NULL;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    line_p = lines;
    /* find the beginning of the (data) section we are interested in */
    while (line_p && *line_p && !g_str_has_prefix (*line_p, "Count of blocks on the device:"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse ReiserFS file system information");
        g_strfreev (lines);
        bd_fs_reiserfs_info_free (ret);
        return NULL;
    }

    /* extract data from something like this: "Count of blocks on the device: 127744" */
    val_start = strchr (*line_p, ':');
    val_start++;
    ret->block_count = g_ascii_strtoull (val_start, NULL, 0);

    while (line_p && *line_p && !g_str_has_prefix (*line_p, "Blocksize:"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse ReiserFS file system information");
        g_strfreev (lines);
        bd_fs_reiserfs_info_free (ret);
        return NULL;
    }

    /* extract data from something like this: "Blocksize: 4096" */
    val_start = strchr (*line_p, ':');
    val_start++;
    ret->block_size = g_ascii_strtoull (val_start, NULL, 0);

    while (line_p && *line_p && !g_str_has_prefix (*line_p, "Free blocks"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse ReiserFS file system information");
        g_strfreev (lines);
        bd_fs_reiserfs_info_free (ret);
        return NULL;
    }

    /* extract data from something like this: "Free blocks (count of blocks - used [journal, bitmaps, data, reserved] blocks): 119529" */
    val_start = strchr (*line_p, ':');
    val_start++;
    ret->free_blocks = g_ascii_strtoull (val_start, NULL, 0);

    g_strfreev (lines);

    return ret;
}

/**
 * bd_fs_reiserfs_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system (if 0, the file system is
 *            adapted to the underlying block device)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_REISERFS-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_reiserfs_resize (const gchar *device, guint64 new_size, GError **error) {
    const gchar *args[5] = {"resize_reiserfs", NULL, NULL, NULL, NULL};
    gboolean ret = FALSE;
    gchar *size_str = NULL;
    BDFSReiserFSInfo *info = NULL;

    if (!check_deps (&avail_deps, DEPS_RESIZEREISERFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    info = bd_fs_reiserfs_get_info (device, error);
    if (!info) {
        g_prefix_error (error, "Failed to get information about ReiserFS filesystem before resizing: ");
        return FALSE;
    }

    if (new_size == info->block_count * info->block_size) {
        bd_utils_log_format (BD_UTILS_LOG_INFO, "Device '%s' has already requested size %"G_GUINT64_FORMAT", not resizing",
                             device, new_size);
        return TRUE;
    }

    if (new_size != 0) {
        size_str = g_strdup_printf ("%"G_GUINT64_FORMAT, new_size);
        args[1] = "-s";
        args[2] = size_str;
        args[3] = device;
    } else
        args[1] = device;

    ret = bd_utils_exec_with_input (args, "y\n", NULL, error);

    g_free (size_str);
    return ret;
}
