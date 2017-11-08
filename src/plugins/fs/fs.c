/*
 * Copyright (C) 2016  Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <blockdev/utils.h>
#include <blockdev/part_err.h>
#include <string.h>
#include <ctype.h>

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

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MKE2FS 0
#define DEPS_MKE2FS_MASK (1 << DEPS_MKE2FS)
#define DEPS_E2FSCK 1
#define DEPS_E2FSCK_MASK (1 << DEPS_E2FSCK)
#define DEPS_TUNE2FS 2
#define DEPS_TUNE2FS_MASK (1 << DEPS_TUNE2FS)
#define DEPS_DUMPE2FS 3
#define DEPS_DUMPE2FS_MASK (1 << DEPS_DUMPE2FS)
#define DEPS_RESIZE2FS 4
#define DEPS_RESIZE2FS_MASK (1 << DEPS_RESIZE2FS)

#define DEPS_MKFSXFS 5
#define DEPS_MKFSXFS_MASK (1 << DEPS_MKFSXFS)
#define DEPS_XFS_DB 6
#define DEPS_XFS_DB_MASK (1 << DEPS_XFS_DB)
#define DEPS_XFS_REPAIR 7
#define DEPS_XFS_REPAIR_MASK (1 << DEPS_XFS_REPAIR)
#define DEPS_XFS_ADMIN 8
#define DEPS_XFS_ADMIN_MASK (1 << DEPS_XFS_ADMIN)
#define DEPS_XFS_GROWFS 9
#define DEPS_XFS_GROWFS_MASK (1 << DEPS_XFS_GROWFS)

#define DEPS_MKFSVFAT 10
#define DEPS_MKFSVFAT_MASK (1 << DEPS_MKFSVFAT)
#define DEPS_FATLABEL 11
#define DEPS_FATLABEL_MASK (1 << DEPS_FATLABEL)
#define DEPS_FSCKVFAT 12
#define DEPS_FSCKVFAT_MASK (1 << DEPS_FSCKVFAT)

#define DEPS_MKNTFS 13
#define DEPS_MKNTFS_MASK (1 << DEPS_MKNTFS)
#define DEPS_NTFSFIX 14
#define DEPS_NTFSFIX_MASK (1 << DEPS_NTFSFIX)
#define DEPS_NTFSRESIZE 15
#define DEPS_NTFSRESIZE_MASK (1 << DEPS_NTFSRESIZE)
#define DEPS_NTFSLABEL 16
#define DEPS_NTFSLABEL_MASK (1 << DEPS_NTFSLABEL)
#define DEPS_NTFSCLUSTER 17
#define DEPS_NTFSCLUSTER_MASK (1 << DEPS_NTFSCLUSTER)

#define DEPS_LAST 18

static UtilDep deps[DEPS_LAST] = {
    {"mke2fs", NULL, NULL, NULL},
    {"e2fsck", NULL, NULL, NULL},
    {"tune2fs", NULL, NULL, NULL},
    {"dumpe2fs", NULL, NULL, NULL},
    {"resize2fs", NULL, NULL, NULL},

    {"mkfs.xfs", NULL, NULL, NULL},
    {"xfs_db", NULL, NULL, NULL},
    {"xfs_repair", NULL, NULL, NULL},
    {"xfs_admin", NULL, NULL, NULL},
    {"xfs_growfs", NULL, NULL, NULL},

    {"mkfs.vfat", NULL, NULL, NULL},
    {"fatlabel", NULL, NULL, NULL},
    {"fsck.vfat", NULL, NULL, NULL},

    {"mkntfs", NULL, NULL, NULL},
    {"ntfsfix", NULL, NULL, NULL},
    {"ntfsresize", NULL, NULL, NULL},
    {"ntfslabel", NULL, NULL, NULL},
    {"ntfscluster", NULL, NULL, NULL},
};

static guint32 fs_mode_util[][FS_MODE_LAST+1] = {
    /*           mkfs          wipe     check               repair                set-label            query                resize */
/* ext2 */ {DEPS_MKE2FS_MASK,   0, DEPS_E2FSCK_MASK,   DEPS_E2FSCK_MASK,     DEPS_TUNE2FS_MASK,   DEPS_DUMPE2FS_MASK,  DEPS_RESIZE2FS_MASK},
/* ext3 */ {DEPS_MKE2FS_MASK,   0, DEPS_E2FSCK_MASK,   DEPS_E2FSCK_MASK,     DEPS_TUNE2FS_MASK,   DEPS_DUMPE2FS_MASK,  DEPS_RESIZE2FS_MASK},
/* ext4 */ {DEPS_MKE2FS_MASK,   0, DEPS_E2FSCK_MASK,   DEPS_E2FSCK_MASK,     DEPS_TUNE2FS_MASK,   DEPS_DUMPE2FS_MASK,  DEPS_RESIZE2FS_MASK},
/* xfs  */ {DEPS_MKFSXFS_MASK,  0, DEPS_XFS_DB_MASK,   DEPS_XFS_REPAIR_MASK, DEPS_XFS_ADMIN_MASK, DEPS_XFS_ADMIN_MASK, DEPS_XFS_GROWFS_MASK},
/* vfat */ {DEPS_MKFSVFAT_MASK, 0, DEPS_FSCKVFAT_MASK, DEPS_FSCKVFAT_MASK,   DEPS_FATLABEL_MASK,  DEPS_FSCKVFAT_MASK,  0},
/* ntfs */ {DEPS_MKNTFS_MASK,   0, DEPS_NTFSFIX_MASK,  DEPS_NTFSFIX_MASK,    DEPS_NTFSLABEL_MASK, DEPS_NTFSCLUSTER_MASK, DEPS_NTFSRESIZE_MASK}
};

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
gboolean bd_fs_check_deps () {
    return TRUE;
}

/**
 * bd_fs_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_fs_init () {
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
void bd_fs_close () {
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
    guint32 required = 0;
    gint fs = 0;
    guint i = 0;

    if (tech == BD_FS_TECH_GENERIC || tech == BD_FS_TECH_MOUNT)
        /* @mode is ignored, there are no special modes for GENERIC and MOUNT technologies */
        /* generic features and mounting are supported by this plugin without any dependencies */
        return TRUE;

    if (tech > LAST_FS) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_TECH_UNAVAIL, "Unknown technology");
        return FALSE;
    }

    fs = tech - FS_OFFSET;
    for (i = 0; i <= FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[fs][i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}
