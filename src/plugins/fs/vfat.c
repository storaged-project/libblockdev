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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include "vfat.h"
#include "fs.h"
#include "common.h"

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MKFSVFAT 0
#define DEPS_MKFSVFAT_MASK (1 << DEPS_MKFSVFAT)
#define DEPS_FATLABEL 1
#define DEPS_FATLABEL_MASK (1 << DEPS_FATLABEL)
#define DEPS_FSCKVFAT 2
#define DEPS_FSCKVFAT_MASK (1 << DEPS_FSCKVFAT)
#define DEPS_RESIZEVFAT 3
#define DEPS_RESIZEVFAT_MASK (1 << DEPS_RESIZEVFAT)
#define DEPS_FATLABELUUID 4
#define DEPS_FATLABELUUID_MASK (1 << DEPS_FATLABELUUID)

#define DEPS_LAST 5

static const UtilDep deps[DEPS_LAST] = {
    {"mkfs.vfat", NULL, NULL, NULL},
    {"fatlabel", NULL, NULL, NULL},
    {"fsck.vfat", NULL, NULL, NULL},
    {"vfat-resize", NULL, NULL, NULL},
    {"fatlabel", "4.2", "--version", "fatlabel\\s+([\\d\\.]+).+"},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    DEPS_MKFSVFAT_MASK,     /* mkfs */
    0,                      /* wipe */
    DEPS_FSCKVFAT_MASK,     /* check */
    DEPS_FSCKVFAT_MASK,     /* repair */
    DEPS_FATLABEL_MASK,     /* set-label */
    DEPS_FSCKVFAT_MASK,     /* query */
    DEPS_RESIZEVFAT_MASK,   /* resize */
    DEPS_FATLABELUUID_MASK, /* set-uuid */
};


#ifdef __clang__
#define ZERO_INIT {}
#else
#define ZERO_INIT {0}
#endif

/**
 * bd_fs_vfat_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
G_GNUC_INTERNAL gboolean
bd_fs_vfat_is_tech_avail (BDFSTech tech G_GNUC_UNUSED, guint64 mode, GError **error) {
    guint32 required = 0;
    guint i = 0;

    for (i = 0; i <= BD_FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * bd_fs_vfat_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSVfatInfo* bd_fs_vfat_info_copy (BDFSVfatInfo *data) {
    if (data == NULL)
        return NULL;

    BDFSVfatInfo *ret = g_new0 (BDFSVfatInfo, 1);

    ret->label = g_strdup (data->label);
    ret->uuid = g_strdup (data->uuid);
    ret->cluster_size = data->cluster_size;
    ret->cluster_count = data->cluster_count;
    ret->free_cluster_count = data->free_cluster_count;

    return ret;
}

/**
 * bd_fs_vfat_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_vfat_info_free (BDFSVfatInfo *data) {
    if (data == NULL)
        return;

    g_free (data->label);
    g_free (data->uuid);
    g_free (data);
}

/* we want to support vol ID in the "udev format", e.g. "2E24-EC82" */
static gchar *_fix_uuid (const gchar *uuid) {
    gchar *new_uuid = NULL;
    size_t len = 0;

    len = strlen (uuid);
    if (len == 9 && uuid[4] == '-') {
        new_uuid = g_new0 (gchar, 9);
        memcpy (new_uuid, uuid, 4);
        memcpy (new_uuid + 4, uuid + 5, 4);
    } else
        new_uuid = g_strdup (uuid);

    return new_uuid;
}

G_GNUC_INTERNAL BDExtraArg **
bd_fs_vfat_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    GPtrArray *options_array = g_ptr_array_new ();
    const BDExtraArg **extra_p = NULL;
    gchar *label;
    UtilDep dep = {"mkfs.vfat", "4.2", "--help", "mkfs.fat\\s+([\\d\\.]+).+"};
    gboolean new_vfat = FALSE;
    gchar *new_uuid = NULL;

    if (options->label && g_strcmp0 (options->label, "") != 0) {
        /* convert the label uppercase */
        label = g_ascii_strup (options->label, -1);
        g_ptr_array_add (options_array, bd_extra_arg_new ("-n", label));
        g_free (label);
    }

    if (options->uuid && g_strcmp0 (options->uuid, "") != 0) {
        new_uuid = _fix_uuid (options->uuid);
        g_ptr_array_add (options_array, bd_extra_arg_new ("-i", new_uuid));
        g_free (new_uuid);
    }

    if (options->force)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-I", ""));

    if (options->no_pt) {
        /* only mkfs.vfat >= 4.2 (sometimes) creates the partition table */
        new_vfat = bd_utils_check_util_version (dep.name, dep.version,
                                                dep.ver_arg, dep.ver_regexp,
                                                NULL);
        if (new_vfat)
            g_ptr_array_add (options_array, bd_extra_arg_new ("--mbr=no", ""));
    }

    if (extra) {
        for (extra_p = extra; *extra_p; extra_p++)
            g_ptr_array_add (options_array, bd_extra_arg_copy ((BDExtraArg *) *extra_p));
    }

    g_ptr_array_add (options_array, NULL);

    return (BDExtraArg **) g_ptr_array_free (options_array, FALSE);
}

/**
 * bd_fs_vfat_mkfs:
 * @device: the device to create a new vfat fs on
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.vfat' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Please remember that FAT labels should always be uppercase.
 *
 * Returns: whether a new vfat fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_vfat_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[3] = {"mkfs.vfat", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKFSVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_vfat_check:
 * @device: the device containing the file system to check
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'fsck.vfat' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether an vfat file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_vfat_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"fsck.vfat", "-n", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_FSCKVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    if (!ret && (status == 1)) {
        /* no error should be reported for exit code 1 -- Recoverable errors have been detected */
        g_clear_error (error);
    }
    return ret;
}

/**
 * bd_fs_vfat_repair:
 * @device: the device containing the file system to repair
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'fsck.vfat' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether an vfat file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_vfat_repair (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"fsck.vfat", "-a", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_FSCKVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    if (!ret) {
        if (status == 1) {
            /* exit code 1 can also mean "errors have been detected and corrected" so we need
               to run fsck again to make sure the filesystem is now clean */
            g_clear_error (error);
            ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
        } else
            /* FALSE and exit code other than 1 always means error  */
            ret = FALSE;
    }

    return ret;
}

/**
 * bd_fs_vfat_set_label:
 * @device: the device containing the file system to set label for
 * @label: label to set
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the label of vfat file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_vfat_set_label (const gchar *device, const gchar *label, GError **error) {
    const gchar *args[4] = {"fatlabel", device, NULL, NULL};
    UtilDep dep = {"fatlabel", "4.2", "--version", "fatlabel\\s+([\\d\\.]+).+"};
    gchar *label_up = NULL;
    gboolean new_vfat = FALSE;
    gboolean ret;

    if (!check_deps (&avail_deps, DEPS_FATLABEL_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (!label || g_strcmp0 (label, "") == 0) {
        /* fatlabel >= 4.2 refuses to set empty label */
        new_vfat = bd_utils_check_util_version (dep.name, dep.version,
                                                dep.ver_arg, dep.ver_regexp,
                                                NULL);
        if (new_vfat)
            args[2] = "--reset";
    }

    /* forcefully convert the label uppercase in case no reset was requested */
    if (label && args[2] == NULL) {
        label_up = g_ascii_strup (label, -1);
        args[2] = label_up;
    }
    ret = bd_utils_exec_and_report_error (args, NULL, error);
    g_free (label_up);

    return ret;
}

/**
 * bd_fs_vfat_check_label:
 * @label: label to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @label is a valid label for the vfat file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_vfat_check_label (const gchar *label, GError **error) {
    const gchar *forbidden = "\"*/:<>?\\|";
    guint n;

    if (strlen (label) > 11) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                     "Label for VFAT filesystem must be at most 11 characters long.");
        return FALSE;
    }

    /* VFAT does not allow some characters; as dosfslabel does not enforce this,
     * check in advance; also, VFAT only knows upper-case characters, dosfslabel
     * enforces this */
    for (n = 0; forbidden[n] != 0; n++)
        if (strchr (label, forbidden[n]) != NULL) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                         "Invalid label: character '%c' not supported in VFAT labels.",
                         forbidden[n]);
            return FALSE;
        }

    return TRUE;
}

/**
 * bd_fs_vfat_set_uuid:
 * @device: the device containing the file system to set uuid for
 * @uuid: (nullable): volume ID to set or %NULL to generate a new one
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the volume ID of vfat file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_vfat_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    const gchar *args[5] = {"fatlabel", "-i", device, NULL, NULL};
    g_autofree gchar *new_uuid = NULL;

    if (!check_deps (&avail_deps, DEPS_FATLABELUUID_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (!uuid || g_strcmp0 (uuid, "") == 0)
        args[3] = "--reset";
    else {
        new_uuid = _fix_uuid (uuid);
        args[3] = new_uuid;
    }

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_vfat_check_uuid:
 * @uuid: UUID to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @uuid is a valid UUID for the vfat file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_vfat_check_uuid (const gchar *uuid, GError **error) {
    guint64 vol_id;
    gchar *new_uuid = NULL;
    gchar *endptr = NULL;

    if (!uuid)
        return TRUE;

    new_uuid = _fix_uuid (uuid);

    vol_id = g_ascii_strtoull (new_uuid, &endptr, 16);
    if ((vol_id == 0 && endptr == new_uuid) || (endptr && *endptr)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UUID_INVALID,
                     "UUID for VFAT filesystem must be a hexadecimal number.");
        g_free (new_uuid);
        return FALSE;
    }

    if (vol_id > G_MAXUINT32) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UUID_INVALID,
                     "UUID for VFAT filesystem must fit into 32 bits.");
        g_free (new_uuid);
        return FALSE;
    }

    g_free (new_uuid);
    return TRUE;
}

/**
 * bd_fs_vfat_get_info:
 * @device: the device containing the file system to get info for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_QUERY
 */
BDFSVfatInfo* bd_fs_vfat_get_info (const gchar *device, GError **error) {
    const gchar *args[4] = {"fsck.vfat", "-nv", device, NULL};
    gboolean success = FALSE;
    BDFSVfatInfo *ret = NULL;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gboolean have_cluster_size = FALSE;
    gboolean have_cluster_count = FALSE;
    guint64 full_cluster_count = 0;
    guint64 cluster_count = 0;
    gchar **key_val = NULL;
    gint scanned = 0;

    if (!check_deps (&avail_deps, DEPS_FSCKVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return NULL;

    ret = g_new0 (BDFSVfatInfo, 1);

    success = get_uuid_label (device, &(ret->uuid), &(ret->label), error);
    if (!success) {
        /* error is already populated */
        bd_fs_vfat_info_free (ret);
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        bd_fs_vfat_info_free (ret);
        return NULL;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    for (line_p=lines; *line_p && (!have_cluster_size || !have_cluster_count); line_p++) {
        if (!have_cluster_size && g_str_has_suffix (*line_p, "bytes per cluster")) {
            ret->cluster_size = g_ascii_strtoull (*line_p, NULL, 0);
            have_cluster_size = TRUE;
        } else if (!have_cluster_count && g_str_has_prefix (*line_p, device)) {
            key_val = g_strsplit (*line_p, ",", 2);
            scanned = sscanf (key_val[1], " %" G_GUINT64_FORMAT "/" "%" G_GUINT64_FORMAT " clusters",
                              &full_cluster_count, &cluster_count);
            if (scanned != 2) {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to get number of FAT clusters for '%s'", device);
                bd_fs_vfat_info_free (ret);
                g_strfreev (key_val);
                g_strfreev (lines);
                return NULL;
            }
            ret->cluster_count = cluster_count;
            ret->free_cluster_count = cluster_count - full_cluster_count;
            have_cluster_count = TRUE;
            g_strfreev (key_val);
        }
    }
    g_strfreev (lines);

    return ret;
}

/**
 * bd_fs_vfat_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system (if 0, the file system is
 *            adapted to the underlying block device)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_vfat_resize (const gchar *device, guint64 new_size, GError **error) {
    g_autofree gchar *size_str = NULL;
    const gchar *args[4] = {"vfat-resize", device, NULL, NULL};

    if (!check_deps (&avail_deps, DEPS_RESIZEVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (new_size != 0) {
        size_str = g_strdup_printf ("%"G_GUINT64_FORMAT, new_size);
        args[2] = size_str;
    }

    return bd_utils_exec_and_report_error (args, NULL, error);
}
