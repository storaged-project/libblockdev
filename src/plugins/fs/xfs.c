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
#include <ctype.h>

#include "xfs.h"
#include "fs.h"
#include "common.h"

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MKFSXFS 0
#define DEPS_MKFSXFS_MASK (1 << DEPS_MKFSXFS)
#define DEPS_XFS_DB 1
#define DEPS_XFS_DB_MASK (1 << DEPS_XFS_DB)
#define DEPS_XFS_REPAIR 2
#define DEPS_XFS_REPAIR_MASK (1 << DEPS_XFS_REPAIR)
#define DEPS_XFS_ADMIN 3
#define DEPS_XFS_ADMIN_MASK (1 << DEPS_XFS_ADMIN)
#define DEPS_XFS_GROWFS 4
#define DEPS_XFS_GROWFS_MASK (1 << DEPS_XFS_GROWFS)

#define DEPS_LAST 5

static const UtilDep deps[DEPS_LAST] = {
    {"mkfs.xfs", NULL, NULL, NULL},
    {"xfs_db", NULL, NULL, NULL},
    {"xfs_repair", NULL, NULL, NULL},
    {"xfs_admin", NULL, NULL, NULL},
    {"xfs_growfs", NULL, NULL, NULL},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    DEPS_MKFSXFS_MASK,      /* mkfs */
    0,                      /* wipe */
    DEPS_XFS_DB_MASK,       /* check */
    DEPS_XFS_REPAIR_MASK,   /* repair */
    DEPS_XFS_ADMIN_MASK,    /* set-label */
    DEPS_XFS_ADMIN_MASK,    /* query */
    DEPS_XFS_GROWFS_MASK,   /* resize */
    DEPS_XFS_ADMIN_MASK     /* set-uuid */
};


#ifdef __clang__
#define ZERO_INIT {}
#else
#define ZERO_INIT {0}
#endif

/**
 * bd_fs_xfs_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
G_GNUC_INTERNAL gboolean
bd_fs_xfs_is_tech_avail (BDFSTech tech G_GNUC_UNUSED, guint64 mode, GError **error) {
    guint32 required = 0;
    guint i = 0;
    for (i = 0; i <= BD_FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * bd_fs_xfs_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSXfsInfo* bd_fs_xfs_info_copy (BDFSXfsInfo *data) {
    if (data == NULL)
        return NULL;

    BDFSXfsInfo *ret = g_new0 (BDFSXfsInfo, 1);

    ret->label = g_strdup (data->label);
    ret->uuid = g_strdup (data->uuid);
    ret->block_size = data->block_size;
    ret->block_count = data->block_count;

    return ret;
}

/**
 * bd_fs_xfs_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_xfs_info_free (BDFSXfsInfo *data) {
    if (data == NULL)
        return;

    g_free (data->label);
    g_free (data->uuid);
    g_free (data);
}

G_GNUC_INTERNAL BDExtraArg **
bd_fs_xfs_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    GPtrArray *options_array = g_ptr_array_new ();
    const BDExtraArg **extra_p = NULL;
    gchar *uuid_option = NULL;

    if (options->label && g_strcmp0 (options->label, "") != 0)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-L", options->label));

    if (options->uuid && g_strcmp0 (options->uuid, "") != 0) {
        uuid_option = g_strdup_printf ("uuid=%s", options->uuid);
        g_ptr_array_add (options_array, bd_extra_arg_new ("-m", uuid_option));
        g_free (uuid_option);
    }

    if (options->dry_run)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-N", ""));

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
 * bd_fs_xfs_mkfs:
 * @device: the device to create a new xfs fs on
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.xfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a new xfs fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_XFS-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_xfs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[3] = {"mkfs.xfs", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKFSXFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_xfs_check:
 * @device: the device containing the file system to check
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                               passed to the 'xfs_repair' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether an xfs file system on the @device is clean or not
 *
 * Note: If the file system is mounted RW, it will always be reported as not
 *       clean!
 *
 * Tech category: %BD_FS_TECH_XFS-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_xfs_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"xfs_repair", "-n", device, NULL};
    gboolean ret = FALSE;
    GError *l_error = NULL;

    if (!check_deps (&avail_deps, DEPS_XFS_REPAIR_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_error (args, extra, &l_error);
    if (!ret) {
        if (l_error && g_error_matches (l_error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED)) {
            /* non-zero exit status -> the fs is not clean, but not an error */
            /* TODO: should we check that the device exists and contains an XFS FS beforehand? */
            g_clear_error (&l_error);
        } else
            g_propagate_error (error, l_error);
    }
    return ret;
}

/**
 * bd_fs_xfs_repair:
 * @device: the device containing the file system to repair
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'xfs_repair' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether an xfs file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_XFS-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_xfs_repair (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[3] = {"xfs_repair", device, NULL};

    if (!check_deps (&avail_deps, DEPS_XFS_REPAIR_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_xfs_set_label:
 * @device: the device containing the file system to set label for
 * @label: label to set
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the label of xfs file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_XFS-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_xfs_set_label (const gchar *device, const gchar *label, GError **error) {
    const gchar *args[5] = {"xfs_admin", "-L", label, device, NULL};
    if (!label || (strncmp (label, "", 1) == 0))
        args[2] = "--";

    if (!check_deps (&avail_deps, DEPS_XFS_ADMIN_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_xfs_check_label:
 * @label: label to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @label is a valid label for the xfs file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_xfs_check_label (const gchar *label, GError **error) {
    size_t len = 0;

    len = strlen (label);
    if (len > 12) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                     "Label for XFS filesystem must be at most 12 characters long.");
        return FALSE;
    }

    if (strchr (label, ' ') != NULL) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                     "Label for XFS filesystem cannot contain spaces.");
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_fs_xfs_set_uuid:
 * @device: the device containing the file system to set uuid for
 * @uuid: (nullable): UUID to set %NULL to generate a new one
 *                      UUID can also be one of "nil" and "generate" to clear or
 *                      generate a new UUID
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the UUID of xfs file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_XFS-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_xfs_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    const gchar *args[5] = {"xfs_admin", "-U", uuid, device, NULL};
    if (!uuid)
        args[2] = "generate";

    if (!check_deps (&avail_deps, DEPS_XFS_ADMIN_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_xfs_check_uuid:
 * @uuid: UUID to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @uuid is a valid UUID for the xfs file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_xfs_check_uuid (const gchar *uuid, GError **error) {
    return check_uuid (uuid, error);
}

/**
 * bd_fs_xfs_get_info:
 * @device: the device containing the file system to get info for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_XFS-%BD_FS_TECH_MODE_QUERY
 */
BDFSXfsInfo* bd_fs_xfs_get_info (const gchar *device, GError **error) {
    const gchar *args[6] = ZERO_INIT;
    gboolean success = FALSE;
    gchar *output = NULL;
    BDFSXfsInfo *ret = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar *val_start = NULL;
    g_autofree gchar* mountpoint = NULL;

    if (!check_deps (&avail_deps, DEPS_XFS_ADMIN_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return NULL;

    ret = g_new0 (BDFSXfsInfo, 1);

    success = get_uuid_label (device, &(ret->uuid), &(ret->label), error);
    if (!success) {
        /* error is already populated */
        bd_fs_xfs_info_free (ret);
        return NULL;
    }

    /* It is important to use xfs_spaceman for a mounted filesystem
       since xfs_db might return old information.  xfs_info would be
       able to do the job for us (running xfs_spaceman or xfs_db
       depending on whether the fs is mounted), but it doesn't pass
       "-r" to xfs_db, which is important to avoid spurious udev
       events just for reading information.
    */

    mountpoint = bd_fs_get_mountpoint (device, NULL);
    if (mountpoint) {
      args[0] = "xfs_spaceman";
      args[1] = "-c";
      args[2] = "info";
      args[3] = mountpoint;
      args[4] = NULL;
    } else {
      args[0] = "xfs_db";
      args[1] = "-r";
      args[2] = "-c";
      args[3] = "info";
      args[4] = device;
      args[5] = NULL;
    }

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        bd_fs_xfs_info_free (ret);
        return NULL;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    line_p = lines;
    /* find the beginning of the (data) section we are interested in */
    while (line_p && *line_p && !g_str_has_prefix (*line_p, "data"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse xfs file system information");
        g_strfreev (lines);
        bd_fs_xfs_info_free (ret);
        return NULL;
    }

    /* extract data from something like this: "data     =      bsize=4096   blocks=262400, imaxpct=25" */
    val_start = strchr (*line_p, '=');
    val_start++;
    while (isspace (*val_start))
        val_start++;
    if (g_str_has_prefix (val_start, "bsize")) {
        val_start = strchr (val_start, '=');
        val_start++;
        ret->block_size = g_ascii_strtoull (val_start, NULL, 0);
    } else {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse xfs file system information");
        g_strfreev (lines);
        bd_fs_xfs_info_free (ret);
        return NULL;
    }
    while (isdigit (*val_start) || isspace(*val_start))
        val_start++;
    if (g_str_has_prefix (val_start, "blocks")) {
        val_start = strchr (val_start, '=');
        val_start++;
        ret->block_count = g_ascii_strtoull (val_start, NULL, 0);
    } else {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse xfs file system information");
        g_strfreev (lines);
        bd_fs_xfs_info_free (ret);
        return NULL;
    }
    g_strfreev (lines);

    return ret;
}

/**
 * bd_fs_xfs_resize:
 * @mpoint: the mount point of the file system to resize
 * @new_size: new requested size for the file system *in file system blocks* (see bd_fs_xfs_get_info())
 *            (if 0, the file system is adapted to the underlying block device)
 * @extra: (nullable) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'xfs_growfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the file system mounted on @mpoint was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_XFS-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_xfs_resize (const gchar *mpoint, guint64 new_size, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"xfs_growfs", NULL, NULL, NULL, NULL};
    gchar *size_str = NULL;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_XFS_GROWFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (new_size != 0) {
        args[1] = "-D";
        /* xfs_growfs doesn't understand bytes, just a number of blocks */
        size_str = g_strdup_printf ("%"G_GUINT64_FORMAT, new_size);
        args[2] = size_str;
        args[3] = mpoint;
    } else
        args[1] = mpoint;

    ret = bd_utils_exec_and_report_error (args, extra, error);

    g_free (size_str);
    return ret;
}
