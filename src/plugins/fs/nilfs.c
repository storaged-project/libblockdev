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
#include <uuid.h>

#include "nilfs.h"
#include "fs.h"
#include "common.h"

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MKFSNILFS2 0
#define DEPS_MKFSNILFS2_MASK (1 << DEPS_MKFSNILFS2)
#define DEPS_NILFSTUNE 1
#define DEPS_NILFSTUNE_MASK (1 <<  DEPS_NILFSTUNE)
#define DEPS_NILFSRESIZE 2
#define DEPS_NILFSRESIZE_MASK (1 << DEPS_NILFSRESIZE)

#define DEPS_LAST 3

static const UtilDep deps[DEPS_LAST] = {
    {"mkfs.nilfs2", NULL, NULL, NULL},
    {"nilfs-tune", NULL, NULL, NULL},
    {"nilfs-resize", NULL, NULL, NULL},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    DEPS_MKFSNILFS2_MASK,       /* mkfs */
    0,                          /* wipe */
    0,                          /* check */
    0,                          /* repair */
    DEPS_NILFSTUNE_MASK,        /* set-label */
    DEPS_NILFSTUNE_MASK,        /* query */
    DEPS_NILFSRESIZE_MASK,      /* resize */
    DEPS_NILFSTUNE_MASK,        /* set-uuid */
};

#define UNUSED __attribute__((unused))

#ifdef __clang__
#define ZERO_INIT {}
#else
#define ZERO_INIT {0}
#endif

/**
 * bd_fs_nilfs2_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean __attribute__ ((visibility ("hidden")))
bd_fs_nilfs2_is_tech_avail (BDFSTech tech UNUSED, guint64 mode, GError **error) {
    guint32 required = 0;
    guint i = 0;

    if (mode & BD_FS_TECH_MODE_CHECK) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_TECH_UNAVAIL,
                     "NILFS2 doesn't support filesystem check.");
        return FALSE;
    }

    if (mode & BD_FS_TECH_MODE_REPAIR) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_TECH_UNAVAIL,
                     "NILFS2 doesn't support filesystem repair.");
        return FALSE;
    }

    for (i = 0; i <= BD_FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * bd_fs_nilfs2_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSNILFS2Info* bd_fs_nilfs2_info_copy (BDFSNILFS2Info *data) {
    if (data == NULL)
        return NULL;

    BDFSNILFS2Info *ret = g_new0 (BDFSNILFS2Info, 1);

    ret->label = g_strdup (data->label);
    ret->uuid = g_strdup (data->uuid);
    ret->size = data->size;
    ret->block_size = data->block_size;
    ret->free_blocks = data->free_blocks;

    return ret;
}

/**
 * bd_fs_nilfs2_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_nilfs2_info_free (BDFSNILFS2Info *data) {
    if (data == NULL)
        return;

    g_free (data->label);
    g_free (data->uuid);
    g_free (data);
}

BDExtraArg __attribute__ ((visibility ("hidden")))
**bd_fs_nilfs2_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    GPtrArray *options_array = g_ptr_array_new ();
    const BDExtraArg **extra_p = NULL;

    if (options->label && g_strcmp0 (options->label, "") != 0)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-L", options->label));

    if (options->dry_run)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-n", ""));

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
 * bd_fs_nilfs2_mkfs:
 * @device: the device to create a new nilfs fs on
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.nilfs2' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a new nilfs fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_NILFS2-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_nilfs2_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"mkfs.nilfs2", "-q", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKFSNILFS2_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_nilfs2_set_label:
 * @device: the device containing the file system to set label for
 * @label: label to set
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the label of nilfs file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_NILFS2-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_nilfs2_set_label (const gchar *device, const gchar *label, GError **error) {
    const gchar *args[5] = {"nilfs-tune", "-L", label, device, NULL};

    if (!check_deps (&avail_deps, DEPS_NILFSTUNE_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_nilfs2_check_label:
 * @label: label to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @label is a valid label for the nilfs2 file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_nilfs2_check_label (const gchar *label, GError **error) {
    if (strlen (label) > 80) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                     "Label for nilfs2 filesystem must be at most 80 characters long.");
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_fs_nilfs2_set_uuid:
 * @device: the device containing the file system to set UUID for
 * @uuid: (nullable): UUID to set or %NULL to generate a new one
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the uuid of nilfs file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_NILFS2-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_nilfs2_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    const gchar *args[5] = {"nilfs-tune", "-U", uuid, device, NULL};
    uuid_t uu;
    gchar uuidbuf[37] = {0};

    if (!uuid) {
        uuid_generate (uu);
        uuid_unparse (uu, uuidbuf);
        args[2] = uuidbuf;
    }

    if (!check_deps (&avail_deps, DEPS_NILFSTUNE_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_nilfs2_check_uuid:
 * @uuid: UUID to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @uuid is a valid UUID for the nilfs file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_nilfs2_check_uuid (const gchar *uuid, GError **error) {
    return check_uuid (uuid, error);
}

/**
 * bd_fs_nilfs2_get_info:
 * @device: the device containing the file system to get info for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_NILFS2-%BD_FS_TECH_MODE_QUERY
 */
BDFSNILFS2Info* bd_fs_nilfs2_get_info (const gchar *device, GError **error) {
    const gchar *args[4] = {"nilfs-tune", "-l", device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    BDFSNILFS2Info *ret = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar *val_start = NULL;

    if (!check_deps (&avail_deps, DEPS_NILFSTUNE_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return NULL;

    ret = g_new0 (BDFSNILFS2Info, 1);

    success = get_uuid_label (device, &(ret->uuid), &(ret->label), error);
    if (!success) {
        /* error is already populated */
        bd_fs_nilfs2_info_free (ret);
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        bd_fs_nilfs2_info_free (ret);
        return NULL;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    line_p = lines;

    while (line_p && *line_p && !g_str_has_prefix (*line_p, "Block size:"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse NILFS2 file system information");
        g_strfreev (lines);
        bd_fs_nilfs2_info_free (ret);
        return NULL;
    }

    /* * extract data from something like this: "Block size: 4096" */
    val_start = strchr (*line_p, ':');
    val_start++;
    ret->block_size = g_ascii_strtoull (val_start, NULL, 0);

    line_p = lines;
    while (line_p && *line_p && !g_str_has_prefix (*line_p, "Device size"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse NILFS2 file system information");
        g_strfreev (lines);
        bd_fs_nilfs2_info_free (ret);
        return NULL;
    }

    /* extract data from something like this: "Device size: 167772160" */
    val_start = strchr (*line_p, ':');
    val_start++;
    ret->size = g_ascii_strtoull (val_start, NULL, 0);

    line_p = lines;
    while (line_p && *line_p && !g_str_has_prefix (*line_p, "Free blocks count"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse NILFS2 file system information");
        g_strfreev (lines);
        bd_fs_nilfs2_info_free (ret);
        return NULL;
    }

    /* extract data from something like this: "Free blocks count: 389120" */
    val_start = strchr (*line_p, ':');
    val_start++;
    ret->free_blocks = g_ascii_strtoull (val_start, NULL, 0);

    g_strfreev (lines);

    return ret;
}

/**
 * bd_fs_nilfs2_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system (if 0, the file system is
 *            adapted to the underlying block device)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Note: Filesystem must be mounted for the resize operation.
 *
 * Tech category: %BD_FS_TECH_NILFS2-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_nilfs2_resize (const gchar *device, guint64 new_size, GError **error) {
    const gchar *args[5] = {"nilfs-resize", "-y", device, NULL, NULL};
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_NILFSRESIZE_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (new_size != 0)
        args[3] = g_strdup_printf ("%"G_GUINT64_FORMAT, new_size);

    ret = bd_utils_exec_and_report_error (args, NULL, error);

    g_free ((gchar *) args[3]);
    return ret;
}
