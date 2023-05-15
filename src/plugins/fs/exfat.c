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

#include "exfat.h"
#include "fs.h"
#include "common.h"

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MKEXFAT 0
#define DEPS_MKEXFAT_MASK (1 << DEPS_MKEXFAT)
#define DEPS_FSCKEXFAT 1
#define DEPS_FSCKEXFAT_MASK (1 << DEPS_FSCKEXFAT)
#define DEPS_TUNEEXFAT 2
#define DEPS_TUNEEXFAT_MASK (1 <<  DEPS_TUNEEXFAT)

#define DEPS_LAST 4

static const UtilDep deps[DEPS_LAST] = {
    {"mkfs.exfat", NULL, NULL, NULL},
    {"fsck.exfat", NULL, NULL, NULL},
    {"tune.exfat", NULL, NULL, NULL},
    {"tune.exfat", NULL, NULL, NULL},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    DEPS_MKEXFAT_MASK,      /* mkfs */
    0,                      /* wipe */
    DEPS_FSCKEXFAT_MASK,    /* check */
    DEPS_FSCKEXFAT_MASK,    /* repair */
    DEPS_TUNEEXFAT_MASK,    /* set-label */
    DEPS_TUNEEXFAT_MASK,    /* query */
    0,                      /* resize */
    DEPS_TUNEEXFAT_MASK,    /* set-uuid */
};

/* line prefixes in tune.exfat output for parsing */
#define BLOCK_SIZE_PREFIX "Block sector size : "
#define BLOCK_SIZE_PREFIX_LEN 20
#define SECTORS_PREFIX "Number of the sectors : "
#define SECTORS_PREFIX_LEN 24
#define CLUSTERS_PREFIX "Number of the clusters : "
#define CLUSTERS_PREFIX_LEN 25


#define UNUSED __attribute__((unused))

/**
 * bd_fs_exfat_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean __attribute__ ((visibility ("hidden")))
bd_fs_exfat_is_tech_avail (BDFSTech tech UNUSED, guint64 mode, GError **error) {
    guint32 required = 0;
    guint i = 0;

    if (mode & BD_FS_TECH_MODE_RESIZE) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_TECH_UNAVAIL,
                     "exFAT currently doesn't support resizing.");
        return FALSE;
    }

    for (i = 0; i <= BD_FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * bd_fs_exfat_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSExfatInfo* bd_fs_exfat_info_copy (BDFSExfatInfo *data) {
    if (data == NULL)
        return NULL;

    BDFSExfatInfo *ret = g_new0 (BDFSExfatInfo, 1);

    ret->label = g_strdup (data->label);
    ret->uuid = g_strdup (data->uuid);
    ret->sector_size = data->sector_size;
    ret->sector_count = data->sector_count;
    ret->cluster_count = data->cluster_count;

    return ret;
}

/**
 * bd_fs_exfat_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_exfat_info_free (BDFSExfatInfo *data) {
    if (data == NULL)
        return;

    g_free (data->label);
    g_free (data->uuid);
    g_free (data);
}

BDExtraArg __attribute__ ((visibility ("hidden")))
**bd_fs_exfat_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    GPtrArray *options_array = g_ptr_array_new ();
    const BDExtraArg **extra_p = NULL;

    if (options->label && g_strcmp0 (options->label, "") != 0)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-n", options->label));

    if (extra) {
        for (extra_p = extra; *extra_p; extra_p++)
            g_ptr_array_add (options_array, bd_extra_arg_copy ((BDExtraArg *) *extra_p));
    }

    g_ptr_array_add (options_array, NULL);

    return (BDExtraArg **) g_ptr_array_free (options_array, FALSE);
}

/**
 * bd_fs_exfat_mkfs:
 * @device: the device to create a new exfat fs on
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.exfat' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a new exfat fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_EXFAT-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_exfat_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[3] = {"mkfs.exfat", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKEXFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_exfat_check:
 * @device: the device containing the file system to check
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'fsck.exfat' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the exfat file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_EXFAT-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_exfat_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"fsck.exfat", "-n", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_FSCKEXFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    if (!ret && (status == 1)) {
        /* no error should be reported for exit code 1 */
        g_clear_error (error);
    }
    return ret;
}

/**
 * bd_fs_exfat_repair:
 * @device: the device containing the file system to repair
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'fsck.exfat' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the exfat file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_EXFAT-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_exfat_repair (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"fsck.exfat", "-y", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_FSCKEXFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    if (!ret && (status == 1)) {
        /* no error should be reported for exit code 1 */
        g_clear_error (error);
        ret = TRUE;
    }

    return ret;
}

/**
 * bd_fs_exfat_set_label:
 * @device: the device containing the file system to set label for
 * @label: label to set
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the label of exfat file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_EXFAT-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_exfat_set_label (const gchar *device, const gchar *label, GError **error) {
    const gchar *args[5] = {"tune.exfat", "-L", label, device, NULL};

    if (!check_deps (&avail_deps, DEPS_TUNEEXFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_exfat_check_label:
 * @label: label to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @label is a valid label for the exfat file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_exfat_check_label (const gchar *label, GError **error) {
    gsize bytes_written;
    g_autofree gchar *str = NULL;

    if (g_utf8_validate (label, -1, NULL)) {
        str = g_convert (label, -1, "UTF-16LE", "UTF-8", NULL, &bytes_written, NULL);
    }

    if (!str) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                     "Label for exFAT filesystem must be a valid UTF-8 string.");
        return FALSE;
    }

    if (bytes_written > 22) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                     "Label for exFAT filesystem is too long.");
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_fs_exfat_set_uuid:
 * @device: the device containing the file system to set uuid for
 * @uuid: (nullable): volume ID to set or %NULL to generate a new one
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the volume ID of exFAT file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_EXFAT-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_exfat_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    const gchar *args[5] = {"tune.exfat", "-I", NULL, device, NULL};
    g_autofree gchar *new_uuid = NULL;
    size_t len = 0;
    guint32 rand = 0;

    if (!check_deps (&avail_deps, DEPS_TUNEEXFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (!uuid || g_strcmp0 (uuid, "") == 0) {
        rand = g_random_int ();
        new_uuid = g_strdup_printf ("0x%08x", rand);
        args[2] = new_uuid;
    } else {
        if (g_str_has_prefix (uuid, "0x")) {
            /* already format taken by tune.exfat: hexadecimal number with the 0x prefix */
            args[2] = uuid;
        } else {
            len = strlen (uuid);
            if (len == 9 && uuid[4] == '-') {
                /* we want to support vol ID in the "udev format", e.g. "2E24-EC82" */
                new_uuid = g_new0 (gchar, 11);
                memcpy (new_uuid, "0x", 2);
                memcpy (new_uuid + 2, uuid, 4);
                memcpy (new_uuid + 6, uuid + 5, 4);
                args[2] = new_uuid;
            } else {
                new_uuid = g_strdup_printf ("0x%s", uuid);
                args[2] = new_uuid;
            }
        }
    }

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_exfat_check_uuid:
 * @uuid: UUID to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @uuid is a valid UUID for the exFAT file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_exfat_check_uuid (const gchar *uuid, GError **error) {
    guint64 vol_id;
    gchar *new_uuid = NULL;
    gchar *endptr = NULL;
    size_t len = 0;

    if (!uuid)
        return TRUE;

    len = strlen (uuid);
    if (len == 9 && uuid[4] == '-') {
        new_uuid = g_new0 (gchar, 9);
        memcpy (new_uuid, uuid, 4);
        memcpy (new_uuid + 4, uuid + 5, 4);
    } else
        new_uuid = g_strdup (uuid);

    vol_id = g_ascii_strtoull (new_uuid, &endptr, 16);
    if ((vol_id == 0 && endptr == new_uuid) || (endptr && *endptr)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UUID_INVALID,
                     "UUID for exFAT filesystem must be a hexadecimal number.");
        g_free (new_uuid);
        return FALSE;
    }

    if (vol_id > G_MAXUINT32) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UUID_INVALID,
                     "UUID for exFAT filesystem must fit into 32 bits.");
        g_free (new_uuid);
        return FALSE;
    }

    g_free (new_uuid);
    return TRUE;
}

/**
 * bd_fs_exfat_get_info:
 * @device: the device containing the file system to get info for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_EXFAT-%BD_FS_TECH_MODE_QUERY
 */
BDFSExfatInfo* bd_fs_exfat_get_info (const gchar *device, GError **error) {
    const gchar *args[4] = {"tune.exfat", "-v", device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    BDFSExfatInfo *ret = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar *val_start = NULL;

    if (!check_deps (&avail_deps, DEPS_TUNEEXFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return NULL;

    ret = g_new0 (BDFSExfatInfo, 1);

    success = get_uuid_label (device, &(ret->uuid), &(ret->label), error);
    if (!success) {
        /* error is already populated */
        bd_fs_exfat_info_free (ret);
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        bd_fs_exfat_info_free (ret);
        return NULL;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (line_p=lines; *line_p; line_p++) {
        if (ret->sector_size == 0) {
            val_start = g_strrstr (*line_p, BLOCK_SIZE_PREFIX);
            if (val_start)
                ret->sector_size = g_ascii_strtoull (val_start + BLOCK_SIZE_PREFIX_LEN, NULL, 0);
        }

        if (ret->sector_count == 0) {
            val_start = g_strrstr (*line_p, SECTORS_PREFIX);
            if (val_start)
                ret->sector_count = g_ascii_strtoull (val_start + SECTORS_PREFIX_LEN, NULL, 0);
        }

        if (ret->cluster_count == 0) {
            val_start = g_strrstr (*line_p, CLUSTERS_PREFIX);
            if (val_start)
                ret->cluster_count = g_ascii_strtoull (val_start + CLUSTERS_PREFIX_LEN, NULL, 0);
        }

        if (ret->sector_size > 0 && ret->sector_count > 0 && ret->cluster_count > 0)
            break;
    }
    g_strfreev (lines);

    if (ret->sector_size == 0 || ret->sector_count == 0 || ret->cluster_count == 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to to parse exFAT info.");
        bd_fs_exfat_info_free (ret);
        return NULL;
    }

    return ret;
}
