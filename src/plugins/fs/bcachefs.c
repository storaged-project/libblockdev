/*
 * Copyright (C) 2023  Red Hat, Inc.
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
 * Author: Jelle van der Waa <jvanderwaa@redhat.com>
 */

#include <blockdev/utils.h>
#include <check_deps.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <stdio.h>

#include "bcachefs.h"
#include "fs.h"
#include "common.h"

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MKFSBCACHEFS 0
#define DEPS_MKFSBCACHEFS_MASK (1 << DEPS_MKFSBCACHEFS)
#define DEPS_BCACHEFSCK 1
#define DEPS_BCACHEFSCK_MASK (1 <<  DEPS_BCACHEFSCK)
#define DEPS_BCACHEFS 2
#define DEPS_BCACHEFS_MASK (1 <<  DEPS_BCACHEFS)

#define DEPS_LAST 3

static const UtilDep deps[DEPS_LAST] = {
    {"mkfs.bcachefs", NULL, NULL, NULL},
    {"fsck.bcachefs", NULL, NULL, NULL},
    {"bcachefs", NULL, NULL, NULL},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    DEPS_MKFSBCACHEFS_MASK,    /* mkfs */
    0,                         /* wipe */
    DEPS_BCACHEFSCK_MASK,      /* check */
    DEPS_BCACHEFSCK_MASK,      /* repair */
    DEPS_BCACHEFS_MASK,        /* set-label */
    DEPS_BCACHEFS_MASK,        /* query */
    DEPS_BCACHEFS_MASK,        /* resize */
};

#define UNUSED __attribute__((unused))

/**
 * bd_fs_bcachefs_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean __attribute__ ((visibility ("hidden")))
bd_fs_bcachefs_is_tech_avail (BDFSTech tech UNUSED, guint64 mode, GError **error) {
    guint32 required = 0;
    guint i = 0;

    for (i = 0; i <= BD_FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * bd_fs_bcachefs_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSBcachefsInfo* bd_fs_bcachefs_info_copy (BDFSBcachefsInfo *data) {
    if (data == NULL)
        return NULL;

    g_info("copying");
    BDFSBcachefsInfo *ret = g_new0 (BDFSBcachefsInfo, 1);

    ret->uuid = g_strdup (data->uuid);
    ret->size = data->size;
    ret->free_space = data->free_space;

    return ret;
}

/**
 * bd_fs_bcachefs_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_bcachefs_info_free (BDFSBcachefsInfo *data) {
    if (data == NULL)
        return;
    g_free (data->uuid);
    g_free (data);
}

BDExtraArg __attribute__ ((visibility ("hidden")))
**bd_fs_bcachefs_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    GPtrArray *options_array = g_ptr_array_new ();
    const BDExtraArg **extra_p = NULL;

    if (options->label && g_strcmp0 (options->label, "") != 0)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-L", options->label));

    if (options->uuid && g_strcmp0 (options->uuid, "") != 0)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-U", options->uuid));

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
 * bd_fs_bcachefs_mkfs:
 * @device: the device to create a new bcachefs fs on
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.bcachefs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a new bcachefs fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_BCACHEFS-%BD_FS_TECH_MODE_MKFS
 *
 */
gboolean bd_fs_bcachefs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[3] = {"mkfs.bcachefs", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKFSBCACHEFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_bcachefs_get_info:
 * @mpoint: a mountpoint of the bcachefs filesystem to get information about
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Note: This function WON'T WORK for multi device bcachefs filesystems,
 *       for more complicated setups use the btrfs plugin instead.
 *
 * Tech category: %BD_FS_TECH_BCACHEFS-%BD_FS_TECH_MODE_QUERY
 */
BDFSBcachefsInfo* bd_fs_bcachefs_get_info (const gchar *mpoint, GError **error) {
    const gchar *args[5] = {"bcachefs", "fs", "usage", mpoint, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar const * const pattern = "Filesystem:\\s+(?P<uuid>\\S+)\\s+" \
                                  "Size:\\s+(?P<size>\\d+)\\s+" \
                                  "Used:\\s+(?P<used>\\d+)\\s+\\S+" ;
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    BDFSBcachefsInfo *ret = NULL;
    gchar *item = NULL;
    guint64 used = 0;

    if (!check_deps (&avail_deps, DEPS_BCACHEFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return NULL;

    regex = g_regex_new (pattern, G_REGEX_EXTENDED, 0, error);
    if (!regex) {
        bd_utils_log_format (BD_UTILS_LOG_WARNING, "Failed to create new GRegex");
        /* error is already populated */
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        return NULL;
    }

    success = g_regex_match (regex, output, 0, &match_info);
    if (!success) {
        g_regex_unref (regex);
        g_match_info_free (match_info);
        g_free (output);
        return NULL;
    }

    ret = g_new (BDFSBcachefsInfo, 1);

    ret->uuid = g_match_info_fetch_named (match_info, "uuid");
    item = g_match_info_fetch_named (match_info, "size");
    ret->size = g_ascii_strtoull (item, NULL, 0);
    g_free (item);
    item = g_match_info_fetch_named (match_info, "used");
    used = g_ascii_strtoull (item, NULL, 0);
    g_free (item);

    g_match_info_free (match_info);
    g_regex_unref (regex);
    g_free (output);

    // TODO error out if there are more then 1 devices
    // TODO: detect label
    ret->free_space = ret->size - used;

    return ret;
}
