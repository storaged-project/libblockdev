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
#include <string.h>

#include "ntfs.h"
#include "fs.h"
#include "common.h"

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MKNTFS 0
#define DEPS_MKNTFS_MASK (1 << DEPS_MKNTFS)
#define DEPS_NTFSFIX 1
#define DEPS_NTFSFIX_MASK (1 << DEPS_NTFSFIX)
#define DEPS_NTFSRESIZE 2
#define DEPS_NTFSRESIZE_MASK (1 << DEPS_NTFSRESIZE)
#define DEPS_NTFSLABEL 3
#define DEPS_NTFSLABEL_MASK (1 << DEPS_NTFSLABEL)
#define DEPS_NTFSCLUSTER 4
#define DEPS_NTFSCLUSTER_MASK (1 << DEPS_NTFSCLUSTER)

#define DEPS_LAST 5

static const UtilDep deps[DEPS_LAST] = {
    {"mkntfs", NULL, NULL, NULL},
    {"ntfsfix", NULL, NULL, NULL},
    {"ntfsresize", NULL, NULL, NULL},
    {"ntfslabel", NULL, NULL, NULL},
    {"ntfscluster", NULL, NULL, NULL},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    /*   mkfs          wipe     check               repair                set-label            query                resize */
    DEPS_MKNTFS_MASK,   0, DEPS_NTFSFIX_MASK,  DEPS_NTFSFIX_MASK,    DEPS_NTFSLABEL_MASK, DEPS_NTFSCLUSTER_MASK, DEPS_NTFSRESIZE_MASK
};

#define UNUSED __attribute__((unused))

/**
 * bd_fs_ntfs_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean __attribute__ ((visibility ("hidden")))
bd_fs_ntfs_is_tech_avail (BDFSTech tech UNUSED, guint64 mode, GError **error) {
    guint32 required = 0;
    guint i = 0;
    for (i = 0; i <= BD_FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * bd_fs_ntfs_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSNtfsInfo* bd_fs_ntfs_info_copy (BDFSNtfsInfo *data) {
    if (data == NULL)
        return NULL;

    BDFSNtfsInfo *ret = g_new0 (BDFSNtfsInfo, 1);

    ret->size = data->size;
    ret->free_space = data->free_space;

    return ret;
}

/**
 * bd_fs_ntfs_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_ntfs_info_free (BDFSNtfsInfo *data) {
    g_free (data);
}

/**
 * bd_fs_ntfs_mkfs:
 * @device: the device to create a new ntfs fs on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkntfs' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a new NTFS fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_ntfs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"mkntfs", "-f", "-F", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKNTFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_ntfs_wipe:
 * @device: the device to wipe an ntfs signature from
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ntfs signature was successfully wiped from the @device or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_ntfs_wipe (const gchar *device, GError **error) {
    return wipe_fs (device, "ntfs", TRUE, error);
}

/**
 * bd_fs_ntfs_check:
 * @device: the device containing the file system to check
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ntfs file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_ntfs_check (const gchar *device, GError **error) {
    const gchar *args[4] = {"ntfsfix", "-n", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_NTFSFIX_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, NULL, &status, error);
    if (!ret && (status == 1)) {
        /* no error should be reported for exit code 1 -- Recoverable errors have been detected */
        g_clear_error (error);
    }
    return ret;
}

/**
 * bd_fs_ntfs_repair:
 * @device: the device containing the file system to repair
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an NTFS file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_ntfs_repair (const gchar *device, GError **error) {
    const gchar *args[4] = {"ntfsfix", "-d", device, NULL};

    if (!check_deps (&avail_deps, DEPS_NTFSFIX_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_ntfs_set_label:
 * @device: the device containing the file system to set the label for
 * @label: label to set
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the label of the NTFS file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_ntfs_set_label (const gchar *device, const gchar *label, GError **error) {
    const gchar *args[4] = {"ntfslabel", device, label, NULL};

    if (!check_deps (&avail_deps, DEPS_NTFSLABEL_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_ntfs_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system in bytes (if 0, the file system
 *            is adapted to the underlying block device)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_ntfs_resize (const gchar *device, guint64 new_size, GError **error) {
    const gchar *args[5] = {"ntfsresize", NULL, NULL, NULL, NULL};
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_NTFSRESIZE_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (new_size != 0) {
        args[1] = "-s";
        args[2] = g_strdup_printf ("%"G_GUINT64_FORMAT, new_size);
        args[3] = device;
    } else {
        args[1] = device;
    }
    ret = bd_utils_exec_and_report_error (args, NULL, error);

    g_free ((gchar *) args[2]);
    return ret;
}

/**
 * bd_fs_ntfs_get_info:
 * @device: the device containing the file system to get info for (device must
            not be mounted, trying to get info for a mounted device will result
            in an error)
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_QUERY
 */
BDFSNtfsInfo* bd_fs_ntfs_get_info (const gchar *device, GError **error) {
    const gchar *args[3] = {"ntfscluster", device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    BDFSNtfsInfo *ret = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar *val_start = NULL;
    g_autofree gchar* mountpoint = NULL;

    if (!check_deps (&avail_deps, DEPS_NTFSCLUSTER_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    mountpoint = bd_fs_get_mountpoint (device, error);
    if (mountpoint != NULL) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOT_MOUNTED,
                     "Can't get NTFS file system information for '%s': Device is mounted.", device);
        return NULL;
    } else {
        if (*error != NULL) {
            g_prefix_error (error, "Error when trying to get mountpoint for '%s': ", device);
            return NULL;
        }
    }

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success)
        /* error is already populated */
        return FALSE;

    ret = g_new0 (BDFSNtfsInfo, 1);
    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    line_p = lines;
    /* find the beginning of the (data) section we are interested in */
    while (line_p && *line_p && !g_str_has_prefix (*line_p, "bytes per volume"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse NTFS file system information");
        g_strfreev (lines);
        bd_fs_ntfs_info_free (ret);
        return FALSE;
    }

    /* extract data from something like this: "bytes per volume        : 998240256" */
    val_start = strchr (*line_p, ':');
    val_start++;
    ret->size = g_ascii_strtoull (val_start, NULL, 0);

    while (line_p && *line_p && !g_str_has_prefix (*line_p, "bytes of free space"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse NTFS file system information");
        g_strfreev (lines);
        bd_fs_ntfs_info_free (ret);
        return FALSE;
    }

    /* extract data from something like this: "bytes of free space     : 992759808" */
    val_start = strchr (*line_p, ':');
    val_start++;
    ret->free_space = g_ascii_strtoull (val_start, NULL, 0);

    g_strfreev (lines);

    return ret;
}
