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
#include <libmount/libmount.h>

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
extern gboolean bd_fs_f2fs_is_tech_avail (BDFSTech tech, guint64 mode, GError **error);
extern gboolean bd_fs_nilfs2_is_tech_avail (BDFSTech tech, guint64 mode, GError **error);
extern gboolean bd_fs_exfat_is_tech_avail (BDFSTech tech, guint64 mode, GError **error);
extern gboolean bd_fs_btrfs_is_tech_avail (BDFSTech tech, guint64 mode, GError **error);
extern gboolean bd_fs_udf_is_tech_avail (BDFSTech tech, guint64 mode, GError **error);

/**
 * bd_fs_error_quark: (skip)
 */
GQuark bd_fs_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-fs-error-quark");
}

/**
 * bd_fs_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_fs_init (void) {
    /* tell libmount to honour the LIBMOUNT_DEBUG env var */
    mnt_init_debug (0);

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
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
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
        case BD_FS_TECH_F2FS:
            return bd_fs_f2fs_is_tech_avail (tech, mode, error);
        case BD_FS_TECH_NILFS2:
            return bd_fs_nilfs2_is_tech_avail (tech, mode, error);
        case BD_FS_TECH_EXFAT:
            return bd_fs_exfat_is_tech_avail (tech, mode, error);
        case BD_FS_TECH_BTRFS:
            return bd_fs_btrfs_is_tech_avail (tech, mode, error);
        case BD_FS_TECH_UDF:
            return bd_fs_udf_is_tech_avail (tech, mode, error);
        /* coverity[dead_error_begin] */
        default:
            /* this should never be reached (see the comparison with LAST_FS
               above), but better safe than sorry */
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_TECH_UNAVAIL, "Unknown technology");
            return FALSE;
    }
}
