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
#include <stdio.h>

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
#define DEPS_NTFSINFO 4
#define DEPS_NTFSINFO_MASK (1 << DEPS_NTFSINFO)

#define DEPS_LAST 5

static const UtilDep deps[DEPS_LAST] = {
    {"mkntfs", NULL, NULL, NULL},
    {"ntfsfix", NULL, NULL, NULL},
    {"ntfsresize", NULL, NULL, NULL},
    {"ntfslabel", NULL, NULL, NULL},
    {"ntfsinfo", NULL, NULL, NULL},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    DEPS_MKNTFS_MASK,       /* mkfs */
    0,                      /* wipe */
    DEPS_NTFSFIX_MASK,      /* check */
    DEPS_NTFSFIX_MASK,      /* repair */
    DEPS_NTFSLABEL_MASK,    /* set-label */
    DEPS_NTFSINFO_MASK,     /* query */
    DEPS_NTFSRESIZE_MASK,   /* resize */
    DEPS_NTFSLABEL_MASK     /* set-uuid */
};


/**
 * bd_fs_ntfs_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
G_GNUC_INTERNAL gboolean
bd_fs_ntfs_is_tech_avail (BDFSTech tech G_GNUC_UNUSED, guint64 mode, GError **error) {
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

    ret->label = g_strdup (data->label);
    ret->uuid = g_strdup (data->uuid);
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
    if (data == NULL)
        return;

    g_free (data->label);
    g_free (data->uuid);
    g_free (data);
}

G_GNUC_INTERNAL BDExtraArg **
bd_fs_ntfs_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    GPtrArray *options_array = g_ptr_array_new ();
    const BDExtraArg **extra_p = NULL;

    if (options->label && g_strcmp0 (options->label, "") != 0)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-L", options->label));

    if (options->dry_run)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-n", ""));

    if (extra) {
        for (extra_p = extra; *extra_p; extra_p++)
            g_ptr_array_add (options_array, bd_extra_arg_copy ((BDExtraArg *) *extra_p));
    }

    g_ptr_array_add (options_array, NULL);

    return (BDExtraArg **) g_ptr_array_free (options_array, FALSE);
}

/**
 * bd_fs_ntfs_mkfs:
 * @device: the device to create a new ntfs fs on
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkntfs' utility)
 * @error: (out) (optional): place to store error (if any)
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
 * bd_fs_ntfs_check:
 * @device: the device containing the file system to check
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                               passed to the 'ntfsfix' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether an ntfs file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_ntfs_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"ntfsfix", "-n", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_NTFSFIX_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    if (!ret && (status == 1)) {
        /* no error should be reported for exit code 1 -- Recoverable errors have been detected */
        g_clear_error (error);
    }
    return ret;
}

/**
 * bd_fs_ntfs_repair:
 * @device: the device containing the file system to repair
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                               passed to the 'ntfsfix' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether an NTFS file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_ntfs_repair (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"ntfsfix", "-d", device, NULL};

    if (!check_deps (&avail_deps, DEPS_NTFSFIX_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_ntfs_set_label:
 * @device: the device containing the file system to set the label for
 * @label: label to set
 * @error: (out) (optional): place to store error (if any)
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
 * bd_fs_ntfs_check_label:
 * @label: label to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @label is a valid label for the ntfs file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_ntfs_check_label (const gchar *label, GError **error) {
    if (strlen (label) > 128) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                     "Label for NTFS filesystem must be at most 128 characters long.");
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_fs_ntfs_set_uuid:
 * @device: the device containing the file system to set the UUID (serial number) for
 * @uuid: (nullable): UUID to set or %NULL to generate a new one
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the UUID of the NTFS file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_ntfs_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    gboolean ret = FALSE;
    const gchar *args[4] = {"ntfslabel", device, NULL, NULL};

    if (!check_deps (&avail_deps, DEPS_NTFSLABEL_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (!uuid)
        args[2] = g_strdup ("--new-serial");
    else if (strlen (uuid) == 16)
        args[2] = g_strdup_printf ("--new-serial=%s", uuid);
    else if (strlen (uuid) == 8)
        args[2] = g_strdup_printf ("--new-half-serial=%s", uuid);
    else {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Invalid format of UUID/serial number for NTFS filesystem.");
        return FALSE;
    }

    ret = bd_utils_exec_and_report_error (args, NULL, error);

    g_free ((gchar *) args[2]);
    return ret;
}

/**
 * bd_fs_ntfs_check_uuid:
 * @uuid: UUID to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @uuid is a valid UUID for the ntfs file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_ntfs_check_uuid (const gchar *uuid, GError **error) {
    size_t len = 0;

    len = strlen (uuid);
    if (len != 8 && len != 16) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UUID_INVALID,
                     "UUID for NTFS filesystem must be either 8 or 16 characters long.");
        return FALSE;
    }

    for (size_t i = 0; i < len; i++) {
        if (!g_ascii_isxdigit (uuid[i])) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UUID_INVALID,
                         "UUID for NTFS filesystem must be a hexadecimal number.");
            return FALSE;
        }
    }

    return TRUE;
}


/**
 * bd_fs_ntfs_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system in bytes (if 0, the file system
 *            is adapted to the underlying block device)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_ntfs_resize (const gchar *device, guint64 new_size, GError **error) {
    const gchar *args[6] = {"ntfsresize", "--no-progress-bar", NULL, NULL, NULL, NULL};
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_NTFSRESIZE_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (new_size != 0) {
        args[2] = "-s";
        args[3] = g_strdup_printf ("%"G_GUINT64_FORMAT, new_size);
        args[4] = device;
    } else {
        args[2] = device;
    }
    ret = bd_utils_exec_and_report_error (args, NULL, error);

    g_free ((gchar *) args[3]);
    return ret;
}

/**
 * bd_fs_ntfs_get_info:
 * @device: the device containing the file system to get info for (device must
            not be mounted, trying to get info for a mounted device will result
            in an error)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_QUERY
 */
BDFSNtfsInfo* bd_fs_ntfs_get_info (const gchar *device, GError **error) {
    const gchar *args[4] = {"ntfsinfo", "-m", device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    BDFSNtfsInfo *ret = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar *val_start = NULL;
    g_autofree gchar* mountpoint = NULL;
    GError *l_error = NULL;
    size_t cluster_size = 0;

    if (!check_deps (&avail_deps, DEPS_NTFSINFO_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return NULL;

    mountpoint = bd_fs_get_mountpoint (device, &l_error);
    if (mountpoint != NULL) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOT_MOUNTED,
                     "Can't get NTFS file system information for '%s': Device is mounted.", device);
        return NULL;
    } else {
        if (l_error != NULL) {
            g_propagate_prefixed_error (error, l_error, "Error when trying to get mountpoint for '%s': ", device);
            return NULL;
        }
    }

    ret = g_new0 (BDFSNtfsInfo, 1);

    success = get_uuid_label (device, &(ret->uuid), &(ret->label), error);
    if (!success) {
        /* error is already populated */
        bd_fs_ntfs_info_free (ret);
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        bd_fs_ntfs_info_free (ret);
        return NULL;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    line_p = lines;

    /* find the beginning of the (data) section we are interested in */
    while (line_p && *line_p && !strstr (*line_p, "Cluster Size"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse NTFS file system information");
        g_strfreev (lines);
        bd_fs_ntfs_info_free (ret);
        return NULL;
    }

    /* extract data from something like this: "Cluster Size: 4096" */
    val_start = strchr (*line_p, ':');
    val_start++;
    cluster_size = g_ascii_strtoull (val_start, NULL, 0);

    while (line_p && *line_p && !strstr (*line_p, "Volume Size in Clusters"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse NTFS file system information");
        g_strfreev (lines);
        bd_fs_ntfs_info_free (ret);
        return NULL;
    }

    /* extract data from something like this: "Volume Size in Clusters: 15314943" */
    val_start = strchr (*line_p, ':');
    val_start++;
    ret->size = g_ascii_strtoull (val_start, NULL, 0) * cluster_size;

    while (line_p && *line_p && !strstr (*line_p, "Free Clusters"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse NTFS file system information");
        g_strfreev (lines);
        bd_fs_ntfs_info_free (ret);
        return NULL;
    }

    /* extract data from something like this: "Free Clusters: 7812655 (51,0%)" */
    val_start = strchr (*line_p, ':');
    val_start++;
    ret->free_space = g_ascii_strtoull (val_start, NULL, 0) * cluster_size;

    g_strfreev (lines);

    return ret;
}

/**
 * bd_fs_ntfs_get_min_size:
 * @device: the device containing the file system to get min size for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: smallest shrunken filesystem size as reported by ntfsresize
 *          in case of error 0 is returned and @error is set
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_RESIZE
 */
guint64 bd_fs_ntfs_get_min_size (const gchar *device, GError **error) {
    const gchar *args[4] = {"ntfsresize", "--info", device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    guint64 min_size = 0;
    gint scanned = 0;

    if (!check_deps (&avail_deps, DEPS_NTFSRESIZE_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success)
        /* error is already populated */
        return 0;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (line_p=lines; *line_p; line_p++) {
        if (g_str_has_prefix (*line_p, "You might resize at")) {
            scanned = sscanf (*line_p, "You might resize at %" G_GUINT64_FORMAT " bytes %*s.",
                              &min_size);
            if (scanned != 1) {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to get minimum size for '%s'", device);
                g_strfreev (lines);
                return 0;
            } else {
                g_strfreev (lines);
                return min_size;
            }
        }
    }

    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                 "Failed to get minimum size for '%s'", device);
    g_strfreev (lines);
    return 0;
}
