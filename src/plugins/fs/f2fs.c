/*
 * Copyright (C) 2019  Red Hat, Inc.
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

#include "f2fs.h"
#include "fs.h"
#include "common.h"

static volatile guint avail_deps = 0;
static volatile guint avail_shrink_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MKFSF2FS 0
#define DEPS_MKFSF2FS_MASK (1 << DEPS_MKFSF2FS)
#define DEPS_CHECKF2FS 1
#define DEPS_CHECKF2FS_MASK (1 << DEPS_CHECKF2FS)
#define DEPS_FSCKF2FS 2
#define DEPS_FSCKF2FS_MASK (1 << DEPS_FSCKF2FS)
#define DEPS_DUMPF2FS 3
#define DEPS_DUMPF2FS_MASK (1 << DEPS_DUMPF2FS)
#define DEPS_RESIZEF2FS 4
#define DEPS_RESIZEF2FS_MASK (1 << DEPS_RESIZEF2FS)

#define DEPS_LAST 5

static const UtilDep deps[DEPS_LAST] = {
    {"mkfs.f2fs", NULL, NULL, NULL},
    {"fsck.f2fs", "1.11.0", "-V", "fsck.f2fs\\s+([\\d\\.]+).+"},
    {"fsck.f2fs", NULL, NULL, NULL},
    {"dump.f2fs", NULL, NULL, NULL},
    {"resize.f2fs", NULL, NULL, NULL}
};

/* shrinking needs newer version of f2fs */
#define SHRINK_DEPS_RESIZEF2FS 0
#define SHRINK_DEPS_RESIZEF2FS_MASK (1 << SHRINK_DEPS_RESIZEF2FS)

#define SHRINK_DEPS_LAST 1

static const UtilDep shrink_deps[SHRINK_DEPS_LAST] = {
    {"resize.f2fs", "1.12.0", "-V", "resize.f2fs\\s+([\\d\\.]+).+"}
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    DEPS_MKFSF2FS_MASK,     /* mkfs */
    0,                      /* wipe */
    DEPS_CHECKF2FS_MASK,    /* check */
    DEPS_FSCKF2FS_MASK,     /* repair */
    0,                      /* set-label */
    DEPS_DUMPF2FS_MASK,     /* query */
    DEPS_RESIZEF2FS_MASK,   /* resize */
    0                       /* set-uuid */
};

#define UNUSED __attribute__((unused))

/* option to get version was added in 1.11.0 so we need to cover situation
   where the version is too old to check the version */
static gboolean can_check_f2fs_version (UtilDep dep, GError **error) {
    gboolean available = FALSE;
    GError *loc_error = NULL;

    available = bd_utils_check_util_version (dep.name, dep.version,
                                             dep.ver_arg, dep.ver_regexp,
                                             &loc_error);
    if (!available) {
        if (g_error_matches (loc_error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_UNKNOWN_VER)) {
            /* assuming version of f2fs is too low to check version of f2fs */
            g_clear_error (&loc_error);
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_LOW_VER,
                         "Too low version of %s. At least %s required.",
                         dep.name, dep.version);
            return FALSE;
        }
    } else {
        /* just ignore other errors (e.g. version was detected but is still
           too low) -- check_deps call below will cover this and create a
           better error message for these cases */
        g_clear_error (&loc_error);
    }

    return TRUE;
}

/**
 * bd_fs_f2fs_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean __attribute__ ((visibility ("hidden")))
bd_fs_f2fs_is_tech_avail (BDFSTech tech UNUSED, guint64 mode, GError **error) {
    guint32 required = 0;
    guint i = 0;

    if (mode & BD_FS_TECH_MODE_SET_LABEL) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_TECH_UNAVAIL,
                     "F2FS doesn't support setting label for an existing device.");
        return FALSE;
    }

    if (mode & BD_FS_TECH_MODE_SET_UUID) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_TECH_UNAVAIL,
                     "F2FS doesn't support setting UUID for an existing device.");
        return FALSE;
    }

    if (mode & BD_FS_TECH_MODE_CHECK) {
        if (!can_check_f2fs_version (deps[DEPS_CHECKF2FS], error))
            return FALSE;
    }

    for (i = 0; i <= BD_FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * bd_fs_f2fs_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSF2FSInfo* bd_fs_f2fs_info_copy (BDFSF2FSInfo *data) {
    if (data == NULL)
        return NULL;

    BDFSF2FSInfo *ret = g_new0 (BDFSF2FSInfo, 1);

    ret->label = g_strdup (data->label);
    ret->uuid = g_strdup (data->uuid);
    ret->sector_size = data->sector_size;
    ret->sector_count = data->sector_count;
    ret->features = data->features;

    return ret;
}

/**
 * bd_fs_f2fs_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_f2fs_info_free (BDFSF2FSInfo *data) {
    if (data == NULL)
        return;

    g_free (data->label);
    g_free (data->uuid);
    g_free (data);
}

BDExtraArg __attribute__ ((visibility ("hidden")))
**bd_fs_f2fs_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    GPtrArray *options_array = g_ptr_array_new ();
    const BDExtraArg **extra_p = NULL;

    if (options->label && g_strcmp0 (options->label, "") != 0)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-l", options->label));

    if (options->no_discard)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-t", "nodiscard"));

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
 * bd_fs_f2fs_mkfs:
 * @device: the device to create a new f2fs fs on
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.f2fs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a new f2fs fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_F2FS-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_f2fs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[3] = {"mkfs.f2fs", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKFSF2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_f2fs_check:
 * @device: the device containing the file system to check
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'fsck.f2fs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether an f2fs file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_F2FS-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_f2fs_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"fsck.f2fs", "--dry-run", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!bd_fs_f2fs_is_tech_avail (BD_FS_TECH_F2FS, BD_FS_TECH_MODE_CHECK, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    if (!ret && (status == 255)) {
        /* no error should be reported for exit code 255 -- there are errors on the filesystem */
        g_clear_error (error);
    }
    return ret;
}

/**
 * bd_fs_f2fs_repair:
 * @device: the device containing the file system to repair
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'fsck.f2fs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether an f2fs file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_F2FS-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_f2fs_repair (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"fsck.f2fs", "-a", device, NULL};

    if (!check_deps (&avail_deps, DEPS_FSCKF2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_f2fs_get_info:
 * @device: the device containing the file system to get info for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_F2FS-%BD_FS_TECH_MODE_QUERY
 */
BDFSF2FSInfo* bd_fs_f2fs_get_info (const gchar *device, GError **error) {
    const gchar *argv[3] = {"dump.f2fs", device, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    BDFSF2FSInfo*ret = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar *val_start = NULL;

    if (!check_deps (&avail_deps, DEPS_DUMPF2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return NULL;

    success = bd_utils_exec_and_capture_output (argv, NULL, &output, error);
    if (!success) {
        /* error is already populated from the call above or just empty
           output */
        return NULL;
    }

    ret = g_new0 (BDFSF2FSInfo, 1);

    success = get_uuid_label (device, &(ret->uuid), &(ret->label), error);
    if (!success) {
        /* error is already populated */
        bd_fs_f2fs_info_free (ret);
        g_free (output);
        return NULL;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    line_p = lines;

    while (line_p && *line_p && !g_str_has_prefix (*line_p, "Info: sector size"))
        line_p++;
    if (!line_p || !(*line_p)) {
        /* Sector size is not printed with dump.f2fs 1.15 */
        ret->sector_size = 0;
    } else {
        /* extract data from something like this: "Info: sector size = 4096" */
        val_start = strchr (*line_p, '=');
        val_start++;
        ret->sector_size = g_ascii_strtoull (val_start, NULL, 0);
    }

    line_p = lines;
    while (line_p && *line_p && !g_str_has_prefix (*line_p, "Info: total FS sectors"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse F2FS file system information");
        g_strfreev (lines);
        bd_fs_f2fs_info_free (ret);
        return NULL;
    }

    /* extract data from something like this: "Info: total sectors = 3932160 (15360 MB)" */
    val_start = strchr (*line_p, '=');
    val_start++;
    ret->sector_count = g_ascii_strtoull (val_start, NULL, 0);

    line_p = lines;
    while (line_p && *line_p && !g_str_has_prefix (*line_p, "Info: superblock features"))
        line_p++;
    if (!line_p || !(*line_p)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse F2FS file system information");
        g_strfreev (lines);
        bd_fs_f2fs_info_free (ret);
        return NULL;
    }

    /* extract data from something like this: "Info: superblock features = 0" */
    val_start = strchr (*line_p, '=');
    val_start++;
    ret->features = g_ascii_strtoull (val_start, NULL, 16);

    g_strfreev (lines);
    return ret;
}

/**
 * bd_fs_f2fs_resize:
 * @device: the device containing the file system to resize
 * @new_size: new requested size for the file system *in file system sectors* (see bd_fs_f2fs_get_info())
 *            (if 0, the file system is adapted to the underlying block device)
 * @safe: whether to perform safe resize or not (does not resize metadata)
 * @extra: (nullable) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'resize.f2fs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_F2FS-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_f2fs_resize (const gchar *device, guint64 new_size, gboolean safe, const BDExtraArg **extra, GError **error) {
    const gchar *args[6] = {"resize.f2fs", NULL, NULL, NULL, NULL, NULL};
    gchar *size_str = NULL;
    gboolean ret = FALSE;
    guint next_arg = 1;
    BDFSF2FSInfo *info = NULL;

    if (!check_deps (&avail_deps, DEPS_RESIZEF2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (safe) {
        if (!can_check_f2fs_version (shrink_deps[SHRINK_DEPS_RESIZEF2FS], error) ||
            !check_deps (&avail_shrink_deps, SHRINK_DEPS_RESIZEF2FS_MASK, shrink_deps, SHRINK_DEPS_LAST, &deps_check_lock, error)) {
            /* f2fs version is too low to either even check its version or
               to perform safe resize (shrink) */
            g_prefix_error (error, "Can't perform safe resize: ");
            return FALSE;
        }
    }

    info = bd_fs_f2fs_get_info (device, error);
    if (!info) {
      /* error is already populated */
      return FALSE;
    }

    if (new_size != 0 && new_size < info->sector_count && !safe) {
        /* resize.f2fs prints error and returns 0 in this case */
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_INVAL,
                     "F2FS filesystem doesn't support shrinking without using the 'safe' option");
        bd_fs_f2fs_info_free (info);
        return FALSE;
    }
    bd_fs_f2fs_info_free (info);

    if (safe) {
        args[next_arg++] = "-s";
    }

    if (new_size != 0) {
        args[next_arg++] = "-t";
        size_str = g_strdup_printf ("%"G_GUINT64_FORMAT, new_size);
        args[next_arg++] = size_str;
        args[next_arg++] = device;
    } else
        args[next_arg++] = device;

    ret = bd_utils_exec_and_report_error (args, extra, error);

    g_free (size_str);
    return ret;
}
