/*
 * Copyright (C) 2016  Red Hat, Inc.
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
#include <blockdev/part_err.h>

#include <parted/parted.h>

#include <check_deps.h>
#include "fs.h"

/**
 * SECTION: fs
 * @short_description: plugin for operations with file systems
 * @title: FS
 * @include: fs.h
 *
 * A plugin for operations with file systems
 */

extern gboolean bd_fs_ext_is_tech_avail (BDFSTech tech, guint64 mode, GError **error);
extern gboolean bd_fs_xfs_is_tech_avail (BDFSTech tech, guint64 mode, GError **error);
extern gboolean bd_fs_vfat_is_tech_avail (BDFSTech tech, guint64 mode, GError **error);
extern gboolean bd_fs_ntfs_is_tech_avail (BDFSTech tech, guint64 mode, GError **error);

/**
 * bd_fs_error_quark: (skip)
 */
GQuark bd_fs_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-fs-error-quark");
}

/**
 * bd_fs_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_fs_check_deps (void) {
    gboolean ret = TRUE;
    guint i = 0;
    GError *error = NULL;

    for (i = BD_FS_TECH_EXT2; i <= BD_FS_TECH_EXT4; i++) {
        ret = ret && bd_fs_ext_is_tech_avail (i, 0xffffffff, &error);
        if (!ret && error) {
            g_warning ("%s", error->message);
            g_clear_error (&error);
        }
    }
    ret = ret && bd_fs_xfs_is_tech_avail (BD_FS_TECH_XFS, 0xffffffff, &error);
    if (!ret && error) {
        g_warning ("%s", error->message);
        g_clear_error (&error);
    }
    ret = ret && bd_fs_vfat_is_tech_avail (BD_FS_TECH_VFAT, 0xffffffff, &error);
    if (!ret && error) {
        g_warning ("%s", error->message);
        g_clear_error (&error);
    }
    ret = ret && bd_fs_ntfs_is_tech_avail (BD_FS_TECH_NTFS, 0xffffffff, &error);
    if (!ret && error) {
        g_warning ("%s", error->message);
        g_clear_error (&error);
    }
    return ret;
}

/**
 * bd_fs_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_fs_init (void) {
    ped_exception_set_handler ((PedExceptionHandler*) bd_exc_handler);
    return TRUE;
}

/**
 * bd_fs_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_fs_close (void) {
    /* nothing to do here */
}

/**
 * bd_fs_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_fs_is_tech_avail (BDFSTech tech, guint64 mode, GError **error) {
    if (tech == BD_FS_TECH_GENERIC || tech == BD_FS_TECH_MOUNT)
        /* @mode is ignored, there are no special modes for GENERIC and MOUNT technologies */
        /* generic features and mounting are supported by this plugin without any dependencies */
        return TRUE;

    if (tech > BD_FS_LAST_FS) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_TECH_UNAVAIL, "Unknown technology");
        return FALSE;
    }

    switch (tech) {
        case BD_FS_TECH_EXT2:
        case BD_FS_TECH_EXT3:
        case BD_FS_TECH_EXT4:
            return bd_fs_ext_is_tech_avail (tech, mode, error);
        case BD_FS_TECH_XFS:
            return bd_fs_xfs_is_tech_avail (tech, mode, error);
        case BD_FS_TECH_VFAT:
            return bd_fs_vfat_is_tech_avail (tech, mode, error);
        case BD_FS_TECH_NTFS:
            return bd_fs_ntfs_is_tech_avail (tech, mode, error);
        /* coverity[dead_error_begin] */
        default:
            /* this should never be reached (see the comparison with LAST_FS
               above), but better safe than sorry */
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_TECH_UNAVAIL, "Unknown technology");
            return FALSE;
    }
}
