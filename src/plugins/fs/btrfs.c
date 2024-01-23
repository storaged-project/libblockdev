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
#include <stdio.h>

#include "btrfs.h"
#include "fs.h"
#include "common.h"

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MKFSBTRFS 0
#define DEPS_MKFSBTRFS_MASK (1 << DEPS_MKFSBTRFS)
#define DEPS_BTRFSCK 1
#define DEPS_BTRFSCK_MASK (1 <<  DEPS_BTRFSCK)
#define DEPS_BTRFS 2
#define DEPS_BTRFS_MASK (1 <<  DEPS_BTRFS)
#define DEPS_BTRFSTUNE 3
#define DEPS_BTRFSTUNE_MASK (1 <<  DEPS_BTRFSTUNE)

#define DEPS_LAST 4

static const UtilDep deps[DEPS_LAST] = {
    {"mkfs.btrfs", NULL, NULL, NULL},
    {"btrfsck", NULL, NULL, NULL},
    {"btrfs", NULL, NULL, NULL},
    {"btrfstune", NULL, NULL, NULL},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    DEPS_MKFSBTRFS_MASK,    /* mkfs */
    0,                      /* wipe */
    DEPS_BTRFSCK_MASK,      /* check */
    DEPS_BTRFSCK_MASK,      /* repair */
    DEPS_BTRFS_MASK,        /* set-label */
    DEPS_BTRFS_MASK,        /* query */
    DEPS_BTRFS_MASK,        /* resize */
    DEPS_BTRFSTUNE_MASK,    /* set-uuid */
};


/**
 * bd_fs_btrfs_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
G_GNUC_INTERNAL gboolean
bd_fs_btrfs_is_tech_avail (BDFSTech tech G_GNUC_UNUSED, guint64 mode, GError **error) {
    guint32 required = 0;
    guint i = 0;

    for (i = 0; i <= BD_FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * bd_fs_btrfs_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSBtrfsInfo* bd_fs_btrfs_info_copy (BDFSBtrfsInfo *data) {
    if (data == NULL)
        return NULL;

    BDFSBtrfsInfo *ret = g_new0 (BDFSBtrfsInfo, 1);

    ret->label = g_strdup (data->label);
    ret->uuid = g_strdup (data->uuid);
    ret->size = data->size;
    ret->free_space = data->free_space;

    return ret;
}

/**
 * bd_fs_btrfs_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_btrfs_info_free (BDFSBtrfsInfo *data) {
    if (data == NULL)
        return;

    g_free (data->label);
    g_free (data->uuid);
    g_free (data);
}

G_GNUC_INTERNAL BDExtraArg **
bd_fs_btrfs_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    GPtrArray *options_array = g_ptr_array_new ();
    const BDExtraArg **extra_p = NULL;

    if (options->label && g_strcmp0 (options->label, "") != 0)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-L", options->label));

    if (options->uuid && g_strcmp0 (options->uuid, "") != 0)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-U", options->uuid));

    if (options->no_discard)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-K", ""));

    if (options->force)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-f", ""));

    if (extra) {
        for (extra_p = extra; *extra_p; extra_p++)
            g_ptr_array_add (options_array, bd_extra_arg_copy ((BDExtraArg *) *extra_p));
    }

    g_ptr_array_add (options_array, NULL);

    return (BDExtraArg **) g_ptr_array_free (options_array, FALSE);
}

/**
 * bd_fs_btrfs_mkfs:
 * @device: the device to create a new btrfs fs on
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a new btrfs fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_BTRFS-%BD_FS_TECH_MODE_MKFS
 *
 */
gboolean bd_fs_btrfs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[3] = {"mkfs.btrfs", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKFSBTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_btrfs_check:
 * @device: the device containing the file system to check
 * @extra: (nullable) (array zero-terminated=1): extra options for the check (right now
 *                                                 passed to the 'btrfsck' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the filesystem was successfully checked or not
 *
 * Tech category: %BD_FS_TECH_BTRFS-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_btrfs_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *argv[4] = {"btrfsck", device, NULL};

    if (!check_deps (&avail_deps, DEPS_BTRFSCK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, extra, error);
}

/**
 * bd_fs_btrfs_repair:
 * @device: the device containing the file system to repair
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'btrfsck' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the filesystem was successfully checked and repaired or not
 *
 * Tech category: %BD_FS_TECH_BTRFS-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_btrfs_repair (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *argv[5] = {"btrfsck", "--repair", device, NULL};

    if (!check_deps (&avail_deps, DEPS_BTRFSCK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, extra, error);
}

/**
 * bd_fs_btrfs_set_label:
 * @mpoint: the mount point of the file system to set label for
 * @label: label to set
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the label of btrfs file system on the @mpoint was
 *          successfully set or not
 *
 * Note: This function is intended to be used for btrfs filesystem on a single device,
 *       for more complicated setups use the btrfs plugin instead.
 *
 * Tech category: %BD_FS_TECH_BTRFS-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_btrfs_set_label (const gchar *mpoint, const gchar *label, GError **error) {
    const gchar *argv[6] = {"btrfs", "filesystem", "label", mpoint, label, NULL};

    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, NULL, error);
}

/**
 * bd_fs_btrfs_check_label:
 * @label: label to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @label is a valid label for the btrfs file system or not
 *          (reason is provided in @error)
 *
 * Note: This function is intended to be used for btrfs filesystem on a single device,
 *       for more complicated setups use the btrfs plugin instead.
 *
 * Tech category: always available
 */
gboolean bd_fs_btrfs_check_label (const gchar *label, GError **error) {
    if (strlen (label) > 256) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                     "Label for btrfs filesystem must be at most 256 characters long.");
        return FALSE;
    }

    if (strchr (label, '\n') != NULL) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                     "Label for btrfs filesystem cannot contain new lines.");
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_fs_btrfs_set_uuid:
 * @device: the device containing the file system to set the UUID (serial number) for
 * @uuid: (nullable): UUID to set or %NULL to generate a new one
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the UUID of the btrfs file system on the @device was
 *          successfully set or not
 *
 * Note: This function is intended to be used for btrfs filesystem on a single device,
 *       for more complicated setups use the btrfs plugin instead.
 *
 * Tech category: %BD_FS_TECH_BTRFS-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_btrfs_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    const gchar *args[5] = {"btrfstune", NULL, NULL, NULL, NULL};

    if (!check_deps (&avail_deps, DEPS_BTRFSTUNE_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (!uuid) {
        args[1] = "-u";
        args[2] = device;
    } else {
        args[1] = "-U";
        args[2] = uuid;
        args[3] = device;
    }

    return bd_utils_exec_with_input (args, "y\n", NULL, error);
}

/**
 * bd_fs_btrfs_check_uuid:
 * @uuid: UUID to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @uuid is a valid UUID for the btrfs file system or not
 *          (reason is provided in @error)
 *
 * Note: This function is intended to be used for btrfs filesystem on a single device,
 *       for more complicated setups use the btrfs plugin instead.
 *
 * Tech category: always available
 */
gboolean bd_fs_btrfs_check_uuid (const gchar *uuid, GError **error) {
    return check_uuid (uuid, error);
}

/**
 * bd_fs_btrfs_get_info:
 * @mpoint: a mountpoint of the btrfs filesystem to get information about
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Note: This function WON'T WORK for multi device btrfs filesystems,
 *       for more complicated setups use the btrfs plugin instead.
 *
 * Tech category: %BD_FS_TECH_BTRFS-%BD_FS_TECH_MODE_QUERY
 */
BDFSBtrfsInfo* bd_fs_btrfs_get_info (const gchar *mpoint, GError **error) {
    const gchar *argv[6] = {"btrfs", "filesystem", "show", "--raw", mpoint, NULL};
    g_autofree gchar *output = NULL;
    gboolean success = FALSE;
    gchar const * const pattern = "Label:\\s+(none|'(?P<label>.+)')\\s+" \
                                  "uuid:\\s+(?P<uuid>\\S+)\\s+" \
                                  "Total\\sdevices\\s+(?P<num_devices>\\d+)\\s+" \
                                  "FS\\sbytes\\sused\\s+(?P<used>\\S+)\\s+" \
                                  "devid\\s+1\\s+size\\s+(?P<size>\\S+)\\s+\\S+";
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    BDFSBtrfsInfo *ret = NULL;
    g_autofree gchar *item = NULL;
    guint64 num_devices = 0;
    guint64 min_size = 0;
    gint scanned = 0;

    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return NULL;

    regex = g_regex_new (pattern, G_REGEX_EXTENDED, 0, error);
    if (!regex) {
        bd_utils_log_format (BD_UTILS_LOG_WARNING, "Failed to create new GRegex");
        /* error is already populated */
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (argv, NULL, &output, error);
    if (!success) {
        /* error is already populated from the call above or just empty
           output */
        g_regex_unref (regex);
        return NULL;
    }

    success = g_regex_match (regex, output, 0, &match_info);
    if (!success) {
        g_regex_unref (regex);
        g_match_info_free (match_info);
        return NULL;
    }

    ret = g_new (BDFSBtrfsInfo, 1);

    ret->label = g_match_info_fetch_named (match_info, "label");
    ret->uuid = g_match_info_fetch_named (match_info, "uuid");

    item = g_match_info_fetch_named (match_info, "num_devices");
    num_devices = g_ascii_strtoull (item, NULL, 0);
    if (num_devices != 1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Btrfs filesystem mounted on %s spans multiple devices (%"G_GUINT64_FORMAT")." \
                     "Filesystem plugin is not suitable for multidevice Btrfs volumes, please use " \
                     "Btrfs plugin instead.", mpoint, num_devices);
        g_match_info_free (match_info);
        g_regex_unref (regex);
        bd_fs_btrfs_info_free (ret);
        return NULL;
    }

    item = g_match_info_fetch_named (match_info, "size");
    ret->size = g_ascii_strtoull (item, NULL, 0);

    g_match_info_free (match_info);
    g_regex_unref (regex);

    argv[1] = "inspect-internal";
    argv[2] = "min-dev-size";
    argv[3] = mpoint;
    argv[4] = NULL;

    success = bd_utils_exec_and_capture_output (argv, NULL, &output, error);
    if (!success) {
        /* error is already populated from the call above or just empty
           output */
        bd_fs_btrfs_info_free (ret);
        return NULL;
    }

    /* 114032640 bytes (108.75MiB) */
    scanned = sscanf (output, " %" G_GUINT64_FORMAT " bytes", &min_size);
    if (scanned != 1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE,
                     "Failed to parse btrfs filesystem min size.");
        bd_fs_btrfs_info_free (ret);
        return NULL;
    }

    ret->free_space = ret->size - min_size;

    return ret;
}

/**
 * bd_fs_btrfs_resize:
 * @mpoint: a mountpoint of the to be resized btrfs filesystem
 * @new_size: requested new size
 * @extra: (nullable) (array zero-terminated=1): extra options for the volume resize (right now
 *                                                 passed to the 'btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @mpoint filesystem was successfully resized to @new_size
 * or not
 *
 * Note: This function WON'T WORK for multi device btrfs filesystems,
 *       for more complicated setups use the btrfs plugin instead.
 *
 * Tech category: %BD_BTRFS_TECH_FS-%BD_BTRFS_TECH_MODE_MODIFY
 */
gboolean bd_fs_btrfs_resize (const gchar *mpoint, guint64 new_size, const BDExtraArg **extra, GError **error) {
    const gchar *argv[6] = {"btrfs", "filesystem", "resize", NULL, mpoint, NULL};
    gboolean ret = FALSE;
    BDFSBtrfsInfo *info = NULL;

    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    /* we don't want to allow resizing multidevice btrfs volumes and get_info
       returns error for these so just try to get the info here */
    info = bd_fs_btrfs_get_info (mpoint, error);
    if (!info)
        return FALSE;
    bd_fs_btrfs_info_free (info);

    if (new_size == 0)
        argv[3] = g_strdup ("max");
    else
        argv[3] = g_strdup_printf ("%"G_GUINT64_FORMAT, new_size);

    ret = bd_utils_exec_and_report_error (argv, extra, error);
    g_free ((gchar *) argv[3]);

    return ret;
}
