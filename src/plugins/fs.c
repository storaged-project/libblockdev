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

#define _GNU_SOURCE
#include <unistd.h>

#include <blockdev/utils.h>
#include <fcntl.h>
#include <string.h>
#include <blkid.h>
#include <ctype.h>
#include <parted/parted.h>
#include <part_err.h>
#include <libmount/libmount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include "fs.h"
#include "check_deps.h"

#define EXT2 "ext2"
#define EXT3 "ext3"
#define EXT4 "ext4"

#define MOUNT_ERR_BUF_SIZE 1024

/**
 * SECTION: fs
 * @short_description: plugin for operations with file systems
 * @title: FS
 * @include: fs.h
 *
 * A plugin for operations with file systems
 */

typedef enum {
    BD_FS_RESIZE,
    BD_FS_REPAIR,
    BD_FS_CHECK,
    BD_FS_LABEL
} BDFsOpType;

/**
 * BDFSInfo:
 * @type: filesystem identifier, must be present
 * @check_util: required utility for consistency checking, "" if not needed and NULL for no support
 * @repair_util: required utility for repair, "" if not needed and NULL for no support
 * @resize_util: required utility for resize, "" if not needed and NULL for no support
 * @resize_mode: resize availability flags, 0 if no support
 */
typedef struct BDFSInfo
{
    const gchar *type;
    const gchar *check_util;
    const gchar *repair_util;
    const gchar *resize_util;
    BDFsResizeFlags resize_mode;
    const gchar *label_util;
} BDFSInfo;

const BDFSInfo fs_info[] = {
    {"xfs", "xfs_db", "xfs_repair", "xfs_growfs", BD_FS_ONLINE_GROW | BD_FS_OFFLINE_GROW, "xfs_admin"},
    {"ext2", "e2fsck", "e2fsck", "resize2fs", BD_FS_ONLINE_GROW | BD_FS_OFFLINE_GROW | BD_FS_OFFLINE_SHRINK, "tune2fs"},
    {"ext3", "e2fsck", "e2fsck", "resize2fs", BD_FS_ONLINE_GROW | BD_FS_OFFLINE_GROW | BD_FS_OFFLINE_SHRINK, "tune2fs"},
    {"ext4", "e2fsck", "e2fsck", "resize2fs", BD_FS_ONLINE_GROW | BD_FS_OFFLINE_GROW | BD_FS_OFFLINE_SHRINK, "tune2fs"},
    {"vfat", "fsck.vfat", "fsck.vfat", "", BD_FS_OFFLINE_GROW | BD_FS_OFFLINE_SHRINK, "fatlabel"},
    {"ntfs", "ntfsfix", "ntfsfix", "ntfsresize", BD_FS_OFFLINE_GROW | BD_FS_OFFLINE_SHRINK, "ntfslabel"},
    {NULL, NULL, NULL, NULL, 0, NULL}
};

static const BDFSInfo *
get_fs_info (const gchar *type)
{
    g_return_val_if_fail (type != NULL, NULL);

    for (guint n = 0; fs_info[n].type != NULL; n++) {
        if (strcmp (fs_info[n].type, type) == 0)
            return &fs_info[n];
    }

    return NULL;
}

/**
 * bd_fs_error_quark: (skip)
 */
GQuark bd_fs_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-fs-error-quark");
}

/**
 * bd_fs_ext2_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSExt2Info* bd_fs_ext2_info_copy (BDFSExt2Info *data) {
    BDFSExt2Info *ret = g_new0 (BDFSExt2Info, 1);

    ret->label = g_strdup (data->label);
    ret->uuid = g_strdup (data->uuid);
    ret->state = g_strdup (data->state);
    ret->block_size = data->block_size;
    ret->block_count = data->block_count;
    ret->free_blocks = data->free_blocks;

    return ret;
}

/**
 * bd_fs_ext3_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSExt3Info* bd_fs_ext3_info_copy (BDFSExt3Info *data) {
    return (BDFSExt3Info*) bd_fs_ext2_info_copy (data);
}

/**
 * bd_fs_ext4_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSExt4Info* bd_fs_ext4_info_copy (BDFSExt4Info *data) {
    return (BDFSExt4Info*) bd_fs_ext2_info_copy (data);
}

/**
 * bd_fs_ext2_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_ext2_info_free (BDFSExt2Info *data) {
    g_free (data->label);
    g_free (data->uuid);
    g_free (data->state);
    g_free (data);
}

/**
 * bd_fs_ext3_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_ext3_info_free (BDFSExt3Info *data) {
    bd_fs_ext2_info_free ((BDFSExt2Info*) data);
}

/**
 * bd_fs_ext4_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_ext4_info_free (BDFSExt4Info *data) {
    bd_fs_ext2_info_free ((BDFSExt2Info*) data);
}

/**
 * bd_fs_xfs_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSXfsInfo* bd_fs_xfs_info_copy (BDFSXfsInfo *data) {
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
    g_free (data->label);
    g_free (data->uuid);
    g_free (data);
}

/**
 * bd_fs_vfat_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSVfatInfo* bd_fs_vfat_info_copy (BDFSVfatInfo *data) {
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
    g_free (data->label);
    g_free (data->uuid);
    g_free (data);
}

/**
 * bd_fs_ntfs_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSNtfsInfo* bd_fs_ntfs_info_copy (BDFSNtfsInfo *data) {
    BDFSNtfsInfo *ret = g_new0 (BDFSNtfsInfo, 1);

    ret->size = data->size;
    ret->free_space = data->free_space;

    return ret;
}

/**
 * bd_fs_ntfs_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_ntfs_info_free (BDFSNtfsInfo *data) {
    g_free (data);
}

typedef struct MountArgs {
    const gchar *mountpoint;
    const gchar *device;
    const gchar *fstype;
    const gchar *options;
    const gchar *spec;
    gboolean lazy;
    gboolean force;
} MountArgs;

typedef gboolean (*MountFunc) (MountArgs *args, GError **error);

static gboolean do_mount (MountArgs *args, GError **error);

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


/**
 * set_parted_error: (skip)
 *
 * Set error from the parted error stored in 'error_msg'. In case there is none,
 * the error is set up with an empty string. Otherwise it is set up with the
 * parted's error message and is a subject to later g_prefix_error() call.
 *
 * Returns: whether there was some message from parted or not
 */
static gboolean set_parted_error (GError **error, BDFsError type) {
    gchar *error_msg = NULL;
    error_msg = bd_get_error_msg ();
    if (error_msg) {
        g_set_error (error, BD_FS_ERROR, type,
                     " (%s)", error_msg);
        g_free (error_msg);
        error_msg = NULL;
        return TRUE;
    } else {
        g_set_error_literal (error, BD_FS_ERROR, type, "");
        return FALSE;
    }
}

static gint synced_close (gint fd) {
    gint ret = 0;
    ret = fsync (fd);
    if (close (fd) != 0)
        ret = 1;
    return ret;
}

#ifndef LIBMOUNT_NEW_ERR_API
static void parse_unmount_error_old (struct libmnt_context *cxt, int rc, const gchar *spec, GError **error) {
    int syscall_errno = 0;

    if (mnt_context_syscall_called (cxt)) {
        syscall_errno = mnt_context_get_syscall_errno (cxt);
        switch (syscall_errno) {
            case EBUSY:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Target busy.");
                break;
            case EINVAL:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Not a mount point.");
                break;
            case EPERM:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_AUTH,
                             "Operation not permitted.");
                break;
            default:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Unmount syscall failed: %d.", syscall_errno);
                break;
        }
    } else {
        if (rc == -EPERM) {
            if (mnt_context_tab_applied (cxt))
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_AUTH,
                             "Operation not permitted.");
            else
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Not mounted.");
        } else {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to unmount %s.", spec);
        }
    }
    return;
}
#else
static void parse_unmount_error_new (struct libmnt_context *cxt, int rc, const gchar *spec, GError **error) {
    int ret = 0;
    int syscall_errno = 0;
    char buf[MOUNT_ERR_BUF_SIZE] = {0};
    gboolean permission = FALSE;

    ret = mnt_context_get_excode (cxt, rc, buf, MOUNT_ERR_BUF_SIZE - 1);
    if (ret != 0) {
        /* check whether the call failed because of lack of permission */
        if (mnt_context_syscall_called (cxt)) {
            syscall_errno = mnt_context_get_syscall_errno (cxt);
            permission = syscall_errno == EPERM;
        } else
            permission = ret == MNT_EX_USAGE && mnt_context_tab_applied (cxt);

        if (permission)
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_AUTH,
                         "Operation not permitted.");
        else {
            if (*buf == '\0')
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Unknow error when unmounting %s", spec);
            else
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "%s", buf);
        }
    }
    return;
}
#endif

static gboolean do_unmount (MountArgs *args, GError **error) {
    struct libmnt_context *cxt = NULL;
    int ret = 0;

    cxt = mnt_new_context ();

    if (mnt_context_set_target (cxt, args->spec) != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to set '%s' as target for umount", args->spec);
        mnt_free_context(cxt);
        return FALSE;
    }

    if (args->lazy) {
        if (mnt_context_enable_lazy (cxt, TRUE) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to set lazy unmount for '%s'", args->spec);
            mnt_free_context(cxt);
            return FALSE;
        }
    }

    if (args->force) {
        if (mnt_context_enable_force (cxt, TRUE) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to set force unmount for '%s'", args->spec);
            mnt_free_context(cxt);
            return FALSE;
        }
    }

    ret = mnt_context_umount (cxt);
    if (ret != 0) {
#ifdef LIBMOUNT_NEW_ERR_API
        parse_unmount_error_new (cxt, ret, args->spec, error);
#else
        parse_unmount_error_old (cxt, ret, args->spec, error);
#endif
        mnt_free_context(cxt);
        return FALSE;
    }

    mnt_free_context(cxt);
    return TRUE;
}

#ifndef LIBMOUNT_NEW_ERR_API
static gboolean parse_mount_error_old (struct libmnt_context *cxt, int rc, MountArgs *args, GError **error) {
    int syscall_errno = 0;
    unsigned long mflags = 0;

    if (mnt_context_get_mflags (cxt, &mflags) != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to get options from string '%s'.", args->options);
        return FALSE;
    }

    if (mnt_context_syscall_called (cxt) == 1) {
        syscall_errno = mnt_context_get_syscall_errno (cxt);
        switch (syscall_errno) {
            case EBUSY:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Source is already mounted or target is busy.");
                break;
            case EINVAL:
                if (mflags & MS_REMOUNT)
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Remount attempted, but %s is not mounted at %s.", args->device, args->mountpoint);
                else if (mflags & MS_MOVE)
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Move attempted, but %s is not a mount point.", args->device);
                else
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Wrong fs type, %s has an invalid superblock or missing helper program.", args->device);
                break;
            case EPERM:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_AUTH,
                             "Operation not permitted.");
                break;
            case ENOTBLK:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "%s is not a block device.", args->device);
                break;
            case ENOTDIR:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "%s is not a directory.", args->mountpoint);
                break;
            case ENODEV:
                if (strlen (args->fstype) == 0)
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Filesystem type not specified");
                else
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Filesystem type %s not configured in kernel.", args->fstype);
                break;
            case EROFS:
            case EACCES:
                  if (mflags & MS_RDONLY) {
                      g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                   "Cannot mount %s read-only.", args->device);
                      break;
                  } else if (args->options && (mnt_optstr_get_option (args->options, "rw", NULL, NULL) == 0)) {
                      g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                   "%s is write-protected but `rw' option given.", args->device);
                      break;
                  } else if (mflags & MS_BIND) {
                      g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                   "Mount %s on %s failed.", args->device, args->mountpoint);
                      break;
                  }
                  /* new versions of libmount do this automatically */
                  else {
                      MountArgs ro_args;
                      gboolean success = FALSE;

                      ro_args.device = args->device;
                      ro_args.mountpoint = args->mountpoint;
                      ro_args.fstype = args->fstype;
                      if (!args->options)
                          ro_args.options = g_strdup ("ro");
                      else
                          ro_args.options = g_strdup_printf ("%s,ro", args->options);

                      success = do_mount (&ro_args, error);

                      g_free ((gchar*) ro_args.options);

                      return success;
                  }
            default:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Mount syscall failed: %d.", syscall_errno);
                break;
        }
    } else {
        switch (rc) {
            case -EPERM:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_AUTH,
                             "Only root can mount %s.", args->device);
                break;
            case -EBUSY:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "%s is already mounted.", args->device);
                break;
            /* source or target explicitly defined and not found in fstab */
            case -MNT_ERR_NOFSTAB:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Can't find %s in %s.", args->device ? args->device : args->mountpoint, mnt_get_fstab_path ());
                break;
            case -MNT_ERR_MOUNTOPT:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to parse mount options");
                break;
            case -MNT_ERR_NOSOURCE:
                if (args->device)
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Can't find %s.", args->device);
                else
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Mount source not defined.");
                break;
            case -MNT_ERR_LOOPDEV:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to setup loop device");
                break;
            case -MNT_ERR_NOFSTYPE:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Filesystem type not specified");
                break;
            default:
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to mount %s.", args->device ? args->device : args->mountpoint);
                break;
        }
    }

    return FALSE;
}

#else
static void parse_mount_error_new (struct libmnt_context *cxt, int rc, MountArgs *args, GError **error) {
    int ret = 0;
    int syscall_errno = 0;
    char buf[MOUNT_ERR_BUF_SIZE] = {0};
    gboolean permission = FALSE;

    ret = mnt_context_get_excode (cxt, rc, buf, MOUNT_ERR_BUF_SIZE - 1);
    if (ret != 0) {
        /* check whether the call failed because of lack of permission */
        if (mnt_context_syscall_called (cxt)) {
            syscall_errno = mnt_context_get_syscall_errno (cxt);
            permission = syscall_errno == EPERM;
        } else
            permission = ret == MNT_EX_USAGE && mnt_context_tab_applied (cxt);

        if (permission)
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_AUTH,
                         "Operation not permitted.");
        else {
            if (*buf == '\0')
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Unknow error when mounting %s", args->device ? args->device : args->mountpoint);
            else
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "%s", buf);
        }
    }

    return;
}
#endif

static gboolean do_mount (MountArgs *args, GError **error) {
    struct libmnt_context *cxt = NULL;
    int ret = 0;
    gboolean success = FALSE;

    cxt = mnt_new_context ();

    if (!args->mountpoint && !args->device) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "You must specify at least one of: mount point, device.");
        mnt_free_context(cxt);
        return FALSE;
    }

    if (args->mountpoint) {
        if (mnt_context_set_target (cxt, args->mountpoint) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to set '%s' as target for mount", args->mountpoint);
            mnt_free_context(cxt);
            return FALSE;
        }
    }

    if (args->device) {
        if (mnt_context_set_source (cxt, args->device) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to set '%s' as source for mount", args->device);
            mnt_free_context(cxt);
            return FALSE;
        }
    }

    if (args->fstype) {
        if (mnt_context_set_fstype (cxt, args->fstype) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to set '%s' as fstype for mount", args->fstype);
            mnt_free_context(cxt);
            return FALSE;
        }
    }

    if (args->options) {
        if (mnt_context_set_options (cxt, args->options) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to set '%s' as options for mount", args->options);
            mnt_free_context(cxt);
            return FALSE;
        }
    }

#ifdef LIBMOUNT_NEW_ERR_API
    /* we don't want libmount to try RDONLY mounts if we were explicitly given the "rw" option */
    if (args->options && (mnt_optstr_get_option (args->options, "rw", NULL, NULL) == 0))
        mnt_context_enable_rwonly_mount (cxt, TRUE);
#endif

    ret = mnt_context_mount (cxt);

    if (ret != 0) {
#ifdef LIBMOUNT_NEW_ERR_API
      parse_mount_error_new (cxt, ret, args, error);
      success = FALSE;
#else
      success = parse_mount_error_old (cxt, ret, args, error);
#endif
    } else
        success = TRUE;

    mnt_free_context(cxt);
    return success;
}

static gboolean set_uid (uid_t uid, GError **error) {
    if (setresuid (uid, -1, -1) != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                    "Error setting uid: %m");
        return FALSE;
    }

    return TRUE;
}

static gboolean set_gid (gid_t gid, GError **error) {
    if (setresgid (gid, -1, -1) != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                    "Error setting gid: %m");
        return FALSE;
    }

    return TRUE;
}

static gboolean run_as_user (MountFunc func, MountArgs *args, uid_t run_as_uid, gid_t run_as_gid, GError ** error) {
    uid_t current_uid = -1;
    gid_t current_gid = -1;
    pid_t pid = -1;
    pid_t wpid = -1;
    int pipefd[2];
    int status = 0;
    GIOChannel *channel = NULL;
    GError *local_error = NULL;
    gchar *error_msg = NULL;
    gsize msglen = 0;

    current_uid = getuid ();
    current_gid = getgid ();

    if (pipe(pipefd) == -1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Error creating pipe.");
        return FALSE;
    }

    pid = fork ();

    if (pid == -1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Error forking.");
        return FALSE;
    } else if (pid == 0) {
        close (pipefd[0]);

        if (run_as_gid != current_gid) {
            if (!set_gid (run_as_gid, error)) {
                if (write(pipefd[1], (*error)->message, strlen((*error)->message)) < 0)
                    _exit (BD_FS_ERROR_PIPE);
                else
                    _exit ((*error)->code);
            }
        }

        if (run_as_uid != current_uid) {
            if (!set_uid (run_as_uid, error)) {
                if (write(pipefd[1], (*error)->message, strlen((*error)->message)) < 0)
                    _exit (BD_FS_ERROR_PIPE);
                else
                    _exit ((*error)->code);
            }
        }

        if (!func (args, error)) {
            if (write(pipefd[1], (*error)->message, strlen((*error)->message)) < 0)
                _exit (BD_FS_ERROR_PIPE);
            else
                _exit ((*error)->code);
        }

        _exit (EXIT_SUCCESS);

    } else {
        close (pipefd[1]);

        do {
            wpid = waitpid (pid, &status, WUNTRACED | WCONTINUED);
            if (wpid == -1) {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Error while waiting for process.");
                return FALSE;
            }

            if (WIFEXITED (status)) {
              if (WEXITSTATUS (status) != EXIT_SUCCESS) {
                  if (WEXITSTATUS (status) == BD_FS_ERROR_PIPE) {
                      g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                   "Error while reading error.");
                      return FALSE;
                  }

                  channel = g_io_channel_unix_new (pipefd[0]);
                  if (g_io_channel_read_to_end (channel, &error_msg, &msglen, &local_error) != G_IO_STATUS_NORMAL) {
                      g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                   "Error while reading error: %s (%d)",
                                   local_error->message, local_error->code);
                      g_clear_error (&local_error);
                      g_io_channel_unref (channel);
                      return FALSE;
                  }

                  if (g_io_channel_shutdown (channel, TRUE, &local_error) == G_IO_STATUS_ERROR) {
                      g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                   "Error shutting down GIO channel: %s (%d)",
                                   local_error->message, local_error->code);
                      g_clear_error (&local_error);
                      g_io_channel_unref (channel);
                      return FALSE;
                  }

                  if (WEXITSTATUS (status) > BD_FS_ERROR_AUTH)
                      g_set_error_literal (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                           error_msg);
                  else
                      g_set_error_literal (error, BD_FS_ERROR, WEXITSTATUS (status),
                                           error_msg);

                  g_io_channel_unref (channel);
                  return FALSE;
              } else
                  return TRUE;
            } else if (WIFSIGNALED (status)) {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Killed by signal %d.", WTERMSIG(status));
                return FALSE;
            }

        } while (!WIFEXITED (status) && !WIFSIGNALED (status));
    }

    return FALSE;
}

/**
 * bd_fs_unmount:
 * @spec: mount point or device to unmount
 * @lazy: enable/disable lazy unmount
 * @force: enable/disable force unmount
 * @extra: (allow-none) (array zero-terminated=1): extra options for the unmount
 *                                                 currently only 'run_as_uid'
 *                                                 and 'run_as_gid' are supported
 *                                                 value must be a valid non zero
 *                                                 uid (gid)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether @spec was successfully unmounted or not
 *
 * Tech category: %BD_FS_TECH_GENERIC (no mode, ignored)
 */
gboolean bd_fs_unmount (const gchar *spec, gboolean lazy, gboolean force, const BDExtraArg **extra, GError **error) {
    uid_t run_as_uid = -1;
    gid_t run_as_gid = -1;
    uid_t current_uid = -1;
    gid_t current_gid = -1;
    const BDExtraArg **extra_p = NULL;
    MountArgs args;

    args.spec = spec;
    args.lazy = lazy;
    args.force = force;

    current_uid = getuid ();
    run_as_uid = current_uid;

    current_gid = getgid ();
    run_as_gid = current_gid;

    if (extra) {
        for (extra_p=extra; *extra_p; extra_p++) {
            if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "run_as_uid") == 0)) {
                run_as_uid = g_ascii_strtoull ((*extra_p)->val, NULL, 0);

                /* g_ascii_strtoull returns 0 in case of error */
                if (run_as_uid == 0 && (g_strcmp0 ((*extra_p)->opt, "0") != 0)) {
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Invalid specification of UID: '%s'", (*extra_p)->val);
                    return FALSE;
                }
            } else if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "run_as_gid") == 0)) {
                run_as_gid = g_ascii_strtoull ((*extra_p)->val, NULL, 0);

                /* g_ascii_strtoull returns 0 in case of error */
                if (run_as_gid == 0 && (g_strcmp0 ((*extra_p)->opt, "0") != 0)) {
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Invalid specification of GID: '%s'", (*extra_p)->val);
                    return FALSE;
                }
            } else {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Unsupported argument for unmount: '%s'", (*extra_p)->opt);
                return FALSE;
            }
        }
    }

    if (run_as_uid != current_uid || run_as_gid != current_gid) {
        return run_as_user ((MountFunc) do_unmount, &args, run_as_uid, run_as_gid, error);
    } else
        return do_unmount (&args, error);

    return TRUE;
}

/**
 * bd_fs_mount:
 * @device: (allow-none): device to mount, if not specified @mountpoint entry
 *                        from fstab will be used
 * @mountpoint: (allow-none): mountpoint for @device, if not specified @device
 *                            entry from fstab will be used
 * @fstype: (allow-none): filesystem type
 * @options: (allow-none): comma delimited options for mount
 * @extra: (allow-none) (array zero-terminated=1): extra options for the mount
 *                                                 currently only 'run_as_uid'
 *                                                 and 'run_as_gid' are supported
 *                                                 value must be a valid non zero
 *                                                 uid (gid)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether @device (or @mountpoint) was successfully mounted or not
 *
 * Tech category: %BD_FS_TECH_MOUNT (no mode, ignored)
 */
gboolean bd_fs_mount (const gchar *device, const gchar *mountpoint, const gchar *fstype, const gchar *options, const BDExtraArg **extra, GError **error) {
    uid_t run_as_uid = -1;
    gid_t run_as_gid = -1;
    uid_t current_uid = -1;
    gid_t current_gid = -1;
    const BDExtraArg **extra_p = NULL;
    MountArgs args;

    args.device = device;
    args.mountpoint = mountpoint;
    args.fstype = fstype;
    args.options = options;

    current_uid = getuid ();
    run_as_uid = current_uid;

    current_gid = getgid ();
    run_as_gid = current_gid;

    if (extra) {
        for (extra_p=extra; *extra_p; extra_p++) {
            if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "run_as_uid") == 0)) {
                run_as_uid = g_ascii_strtoull ((*extra_p)->val, NULL, 0);

                /* g_ascii_strtoull returns 0 in case of error */
                if (run_as_uid == 0 && (g_strcmp0 ((*extra_p)->opt, "0") != 0)) {
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Invalid specification of UID: '%s'", (*extra_p)->val);
                    return FALSE;
                }
            } else if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "run_as_gid") == 0)) {
                run_as_gid = g_ascii_strtoull ((*extra_p)->val, NULL, 0);

                /* g_ascii_strtoull returns 0 in case of error */
                if (run_as_gid == 0 && (g_strcmp0 ((*extra_p)->opt, "0") != 0)) {
                    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                                 "Invalid specification of GID: '%s'", (*extra_p)->val);
                    return FALSE;
                }
            } else {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Unsupported argument for unmount: '%s'", (*extra_p)->opt);
                return FALSE;
            }
        }
    }

    if (run_as_uid != current_uid || run_as_gid != current_gid) {
        return run_as_user ((MountFunc) do_mount, &args, run_as_uid, run_as_gid, error);
    } else
       return do_mount (&args, error);

    return TRUE;
}

/**
 * bd_fs_get_mountpoint:
 * @device: device to find mountpoint for
 * @error: (out): place to store error (if any)
 *
 * Get mountpoint for @device. If @device is mounted multiple times only
 * one mountpoint will be returned.
 *
 * Returns: (transfer full): mountpoint for @device, %NULL in case device is
 *                           not mounted or in case of an error (@error is set
 *                           in this case)
 *
 * Tech category: %BD_FS_TECH_MOUNT (no mode, ignored)
 */
gchar* bd_fs_get_mountpoint (const gchar *device, GError **error) {
    struct libmnt_table *table = NULL;
    struct libmnt_fs *fs = NULL;
    struct libmnt_cache *cache = NULL;
    gint ret = 0;
    gchar *mountpoint = NULL;
    const gchar *target = NULL;

    table = mnt_new_table ();
    cache = mnt_new_cache ();

    ret = mnt_table_set_cache (table, cache);
    if (ret != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to set cache for mount info table.");
        mnt_free_table (table);
        return NULL;
    }

    ret = mnt_table_parse_mtab (table, NULL);
    if (ret != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to parse mount info.");
        mnt_free_table (table);
        mnt_free_cache (cache);
        return NULL;
    }

    fs = mnt_table_find_source (table, device, MNT_ITER_FORWARD);
    if (!fs) {
        mnt_free_table (table);
        mnt_free_cache (cache);
        return NULL;
    }

    target = mnt_fs_get_target (fs);
    if (!target) {
        mnt_free_fs (fs);
        mnt_free_table (table);
        mnt_free_cache (cache);
        return NULL;
    }

    mountpoint = g_strdup (target);
    mnt_free_fs (fs);
    mnt_free_table (table);
    mnt_free_cache (cache);
    return mountpoint;
}

/**
 * bd_fs_wipe:
 * @device: the device to wipe signatures from
 * @all: whether to wipe all (%TRUE) signatures or just the first (%FALSE) one
 * @error: (out): place to store error (if any)
 *
 * Returns: whether signatures were successfully wiped on @device or not
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_wipe (const gchar *device, gboolean all, GError **error) {
    blkid_probe probe = NULL;
    gint fd = 0;
    gint status = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    guint n_try = 0;

    msg = g_strdup_printf ("Started wiping signatures from the device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    probe = blkid_new_probe ();
    if (!probe) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a new probe");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    fd = open (device, O_RDWR|O_CLOEXEC);
    if (fd == -1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to open the device '%s'", device);
        blkid_free_probe (probe);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* we may need to try mutliple times with some delays in case the device is
       busy at the very moment */
    for (n_try=5, status=-1; (status != 0) && (n_try > 0); n_try--) {
        status = blkid_probe_set_device (probe, fd, 0, 0);
        if (status != 0)
            g_usleep (100 * 1000); /* microseconds */
    }
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    blkid_probe_enable_partitions(probe, 1);
    blkid_probe_set_partitions_flags(probe, BLKID_PARTS_MAGIC);
    blkid_probe_enable_superblocks(probe, 1);
    blkid_probe_set_superblocks_flags(probe, BLKID_SUBLKS_MAGIC | BLKID_SUBLKS_BADCSUM);

    /* we may need to try mutliple times with some delays in case the device is
       busy at the very moment */
    for (n_try=5, status=-1; (status != 0) && (n_try > 0); n_try--) {
        status = blkid_do_safeprobe (probe);
        if (status == 1)
            break;
        if (status < 0)
            g_usleep (100 * 1000); /* microseconds */
    }
    if (status == 1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOFS,
                     "No signature detected on the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    blkid_reset_probe (probe);
    status = blkid_do_probe (probe);

    if (status < 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to probe the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    status = blkid_do_wipe (probe, FALSE);
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to wipe signatures on the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    while (all && (blkid_do_probe (probe) == 0)) {
        status = blkid_do_wipe (probe, FALSE);
        if (status != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to wipe signatures on the device '%s'", device);
            blkid_free_probe (probe);
            synced_close (fd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }

    blkid_free_probe (probe);
    synced_close (fd);

    bd_utils_report_finished (progress_id, "Completed");

    return TRUE;
}

/**
 * bd_fs_clean:
 * @device: the device to clean
 * @error: (out): place to store error (if any)
 *
 * Clean all signatures from @device.
 * Difference between this and bd_fs_wipe() is that this function doesn't
 * return error if @device is already empty. This will also always remove
 * all signatures from @device, not only the first one.
 *
 * Returns: whether @device was successfully cleaned or not
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_clean (const gchar *device, GError **error) {
  gboolean ret = FALSE;

  ret = bd_fs_wipe (device, TRUE, error);

  if (!ret) {
    if (g_error_matches (*error, BD_FS_ERROR, BD_FS_ERROR_NOFS)) {
        /* ignore 'empty device' error */
        g_clear_error (error);
        return TRUE;
    } else {
        g_prefix_error (error, "Failed to clean %s:", device);
        return FALSE;
    }
  } else
      return TRUE;
}

/**
 * bd_fs_get_fstype:
 * @device: the device to probe
 * @error: (out): place to store error (if any)
 *
 * Get first signature on @device as a string.
 *
 * Returns: (transfer full): type of filesystem found on @device, %NULL in case
 *                           no signature has been detected or in case of error
 *                           (@error is set in this case)
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_QUERY
 */
gchar* bd_fs_get_fstype (const gchar *device,  GError **error) {
    blkid_probe probe = NULL;
    gint fd = 0;
    gint status = 0;
    const gchar *value = NULL;
    gchar *fstype = NULL;
    size_t len = 0;
    guint n_try = 0;

    probe = blkid_new_probe ();
    if (!probe) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a new probe");
        return FALSE;
    }

    fd = open (device, O_RDWR|O_CLOEXEC);
    if (fd == -1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to open the device '%s'", device);
        blkid_free_probe (probe);
        return FALSE;
    }

    /* we may need to try mutliple times with some delays in case the device is
       busy at the very moment */
    for (n_try=5, status=-1; (status != 0) && (n_try > 0); n_try--) {
        status = blkid_probe_set_device (probe, fd, 0, 0);
        if (status != 0)
            g_usleep (100 * 1000); /* microseconds */
    }
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        return FALSE;
    }

    blkid_probe_enable_partitions (probe, 1);
    blkid_probe_set_partitions_flags (probe, BLKID_PARTS_MAGIC);
    blkid_probe_enable_superblocks (probe, 1);
    blkid_probe_set_superblocks_flags (probe, BLKID_SUBLKS_USAGE | BLKID_SUBLKS_TYPE |
                                              BLKID_SUBLKS_MAGIC | BLKID_SUBLKS_BADCSUM);

    /* we may need to try mutliple times with some delays in case the device is
       busy at the very moment */
    for (n_try=5, status=-1; (status != 0 || status != 1) && (n_try > 0); n_try--) {
        status = blkid_do_safeprobe (probe);
        if (status < 0)
            g_usleep (100 * 1000); /* microseconds */
    }
    if (status < 0) {
        /* -1 or -2 = error during probing*/
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to probe the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    } else if (status == 1) {
        /* 1 = nothing detected */
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    }

    status = blkid_probe_lookup_value (probe, "USAGE", &value, &len);
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to get usage for the device '%s'", device);
        return NULL;
    }

    if (strncmp (value, "filesystem", 10) != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_INVAL,
                     "The signature on the device '%s' is of type '%s', not 'filesystem'", device, value);
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    }

    status = blkid_probe_lookup_value (probe, "TYPE", &value, &len);
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to get filesystem type for the device '%s'", device);
        return NULL;
    }

    fstype = g_strdup (value);
    blkid_free_probe (probe);
    synced_close (fd);

    return fstype;
}

static gboolean has_fs (blkid_probe probe, const gchar *device, const gchar *fs_type, GError **error) {
    gint status = 0;
    const gchar *value = NULL;
    size_t len = 0;

    status = blkid_do_safeprobe (probe);
    if (status != 0) {
        if (status < 0)
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to probe the device '%s'", device);
        return FALSE;
    }

    if (fs_type) {
        status = blkid_probe_lookup_value (probe, "TYPE", &value, &len);
        if (status != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to get filesystem type for the device '%s'", device);
            return FALSE;
        }

        if (strncmp (value, fs_type, len-1) != 0) {
            return FALSE;
        }
    }

    blkid_reset_probe (probe);
    return TRUE;
}

static gboolean wipe_fs (const gchar *device, const gchar *fs_type, gboolean wipe_all, GError **error) {
    blkid_probe probe = NULL;
    gint fd = 0;
    gint status = 0;
    const gchar *value = NULL;
    size_t len = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    gboolean has_fs_type = TRUE;
    guint n_try = 0;

    msg = g_strdup_printf ("Started wiping '%s' signatures from the device '%s'", fs_type, device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    probe = blkid_new_probe ();
    if (!probe) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", device);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    fd = open (device, O_RDWR|O_CLOEXEC);
    if (fd == -1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", device);
        blkid_free_probe (probe);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    for (n_try=5, status=-1; (status != 0) && (n_try > 0); n_try--) {
        status = blkid_probe_set_device (probe, fd, 0, 0);
        if (status != 0)
            g_usleep (100 * 1000); /* microseconds */
    }
    status = blkid_probe_set_device (probe, fd, 0, 0);
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    blkid_probe_enable_partitions(probe, 1);
    blkid_probe_set_partitions_flags(probe, BLKID_PARTS_MAGIC);
    blkid_probe_enable_superblocks(probe, 1);
    blkid_probe_set_superblocks_flags(probe, BLKID_SUBLKS_USAGE | BLKID_SUBLKS_TYPE |
                                             BLKID_SUBLKS_MAGIC | BLKID_SUBLKS_BADCSUM);

    for (n_try=5, status=-1; (status != 0) && (n_try > 0); n_try--) {
        status = blkid_do_probe (probe);
        if (status != 0)
            g_usleep (100 * 1000); /* microseconds */
    }
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to probe the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    status = blkid_probe_lookup_value (probe, "USAGE", &value, NULL);
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to get signature type for the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (strncmp (value, "filesystem", 10) != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_INVAL,
                     "The signature on the device '%s' is of type '%s', not 'filesystem'", device, value);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (fs_type) {
        status = blkid_probe_lookup_value (probe, "TYPE", &value, &len);
        if (status != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to get filesystem type for the device '%s'", device);
            blkid_free_probe (probe);
            synced_close (fd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }

        if (strncmp (value, fs_type, len-1) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_INVAL,
                         "The file system type on the device '%s' is '%s', not '%s'", device, value, fs_type);
            blkid_free_probe (probe);
            synced_close (fd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }

    status = blkid_do_wipe (probe, FALSE);
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to wipe the filesystem signature on the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    blkid_reset_probe (probe);

    if (wipe_all) {
        has_fs_type = has_fs (probe, device, fs_type, error);

        while (has_fs_type) {
            status = blkid_do_probe (probe);
            if (status != 0) {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to probe the device '%s'", device);
                blkid_free_probe (probe);
                synced_close (fd);
                bd_utils_report_finished (progress_id, (*error)->message);
                return FALSE;
            }

            status = blkid_do_wipe (probe, FALSE);
            if (status != 0) {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to wipe the filesystem signature on the device '%s'", device);
                blkid_free_probe (probe);
                synced_close (fd);
                bd_utils_report_finished (progress_id, (*error)->message);
                return FALSE;
            }

            blkid_reset_probe (probe);
            has_fs_type = has_fs (probe, device, fs_type, error);

        }
    }

    blkid_free_probe (probe);
    synced_close (fd);

    bd_utils_report_finished (progress_id, "Completed");

    return TRUE;
}

static gboolean ext_mkfs (const gchar *device, const BDExtraArg **extra, const gchar *ext_version, GError **error) {
    const gchar *args[6] = {"mke2fs", "-t", ext_version, "-F", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKE2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * xfs_resize_device:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system *in bytes*
 *            (if 0, the file system is adapted to the underlying block device)
 * @extra: (allow-none) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'xfs_growfs' utility)
 * @error: (out): place to store error (if any)
 *
 * This is just a helper function for bd_fs_resize.
 *
 * Returns: whether the file system on @device was successfully resized or not
 */

static gboolean xfs_resize_device (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error) {
    g_autofree gchar* mountpoint = NULL;
    gboolean ret = FALSE;
    gboolean success = FALSE;
    gboolean unmount = FALSE;
    GError *local_error = NULL;
    BDFSXfsInfo* xfs_info = NULL;

    mountpoint = bd_fs_get_mountpoint (device, error);
    if (!mountpoint) {
        if (*error == NULL) {
            /* device is not mounted -- we need to mount it */
            mountpoint = g_build_path (G_DIR_SEPARATOR_S, g_get_tmp_dir (), "blockdev.XXXXXX", NULL);
            mountpoint = g_mkdtemp (mountpoint);
            if (!mountpoint) {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to create temporary directory for mounting '%s' "
                             "before resizing it.", device);
                return FALSE;
            }
            ret = bd_fs_mount (device, mountpoint, "xfs", NULL, NULL, error);
            if (!ret) {
                g_prefix_error (error, "Failed to mount '%s' before resizing it: ", device);
                return FALSE;
            } else
                unmount = TRUE;
        } else {
            g_prefix_error (error, "Error when trying to get mountpoint for '%s': ", device);
            return FALSE;
        }
    }

    xfs_info = bd_fs_xfs_get_info (device, error);
    if (!xfs_info) {
        return FALSE;
    }

    new_size = (new_size + xfs_info->block_size - 1) / xfs_info->block_size;
    bd_fs_xfs_info_free (xfs_info);

    success = bd_fs_xfs_resize (mountpoint, new_size, extra, error);

    if (unmount) {
        ret = bd_fs_unmount (mountpoint, FALSE, FALSE, NULL, &local_error);
        if (!ret) {
            if (success) {
                /* resize was successful but unmount failed */
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UNMOUNT_FAIL,
                             "Failed to unmount '%s' after resizing it: %s",
                             device, local_error->message);
                g_clear_error (&local_error);
                return FALSE;
            } else
                /* both resize and unmount were unsuccessful but the error
                   from the resize is more important so just ignore the
                   unmount error */
                g_clear_error (&local_error);
        }
    }

    return success;
}

static gboolean device_operation (const gchar *device, BDFsOpType op, guint64 new_size, const gchar *label, GError **error) {
    const gchar* op_name = NULL;
    g_autofree gchar* fstype = NULL;

    fstype = bd_fs_get_fstype (device, error);
    if (!fstype) {
        if (*error == NULL) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOFS,
                         "No filesystem detected on the device '%s'", device);
            return FALSE;
        } else {
            g_prefix_error (error, "Error when trying to detect filesystem on '%s': ", device);
            return FALSE;
        }
    }

    if (g_strcmp0 (fstype, "ext2") == 0 || g_strcmp0 (fstype, "ext3") == 0
                                        || g_strcmp0 (fstype, "ext4") == 0) {
        switch (op) {
            case BD_FS_RESIZE:
                return bd_fs_ext4_resize (device, new_size, NULL, error);
            case BD_FS_REPAIR:
                return bd_fs_ext4_repair (device, TRUE, NULL, error);
            case BD_FS_CHECK:
                return bd_fs_ext4_check (device, NULL, error);
            case BD_FS_LABEL:
                return bd_fs_ext4_set_label (device, label, error);
        }
    } else if (g_strcmp0 (fstype, "xfs") == 0) {
        switch (op) {
            case BD_FS_RESIZE:
                return xfs_resize_device (device, new_size, NULL, error);
            case BD_FS_REPAIR:
                return bd_fs_xfs_repair (device, NULL, error);
            case BD_FS_CHECK:
                return bd_fs_xfs_check (device, error);
            case BD_FS_LABEL:
                return bd_fs_xfs_set_label (device, label, error);
        }
    } else if (g_strcmp0 (fstype, "vfat") == 0) {
        switch (op) {
            case BD_FS_RESIZE:
                return bd_fs_vfat_resize (device, new_size, error);
            case BD_FS_REPAIR:
                return bd_fs_vfat_repair (device, NULL, error);
            case BD_FS_CHECK:
                return bd_fs_vfat_check (device, NULL, error);
            case BD_FS_LABEL:
                return bd_fs_vfat_set_label (device, label, error);
        }
    } else if (g_strcmp0 (fstype, "ntfs") == 0) {
        switch (op) {
            case BD_FS_RESIZE:
                return bd_fs_ntfs_resize (device, new_size, error);
            case BD_FS_REPAIR:
                return bd_fs_ntfs_repair (device, error);
            case BD_FS_CHECK:
                return bd_fs_ntfs_check (device, error);
            case BD_FS_LABEL:
                return bd_fs_ntfs_set_label (device, label, error);
        }
    }
    switch (op) {
        case BD_FS_RESIZE:
            op_name = "Resizing";
            break;
        case BD_FS_REPAIR:
            op_name = "Repairing";
            break;
        case BD_FS_CHECK:
            op_name = "Checking";
            break;
        case BD_FS_LABEL:
            op_name = "Setting the label of";
            break;
    }
    g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOT_SUPPORTED,
                 "%s filesystem '%s' is not supported.", op_name, fstype);
    return FALSE;
}

/**
 * bd_fs_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system (if 0, the file system is
 *            adapted to the underlying block device)
 * @error: (out): place to store error (if any)
 *
 * Resize filesystem on @device. This calls other fs resize functions from this
 * plugin based on detected filesystem (e.g. bd_fs_xfs_resize for XFS). This
 * function will return an error for unknown/unsupported filesystems.
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_resize (const gchar *device, guint64 new_size, GError **error) {
    return device_operation (device, BD_FS_RESIZE, new_size, NULL, error);
}

/**
 * bd_fs_repair:
 * @device: the device the file system of which to repair
 * @error: (out): place to store error (if any)
 *
 * Repair filesystem on @device. This calls other fs repair functions from this
 * plugin based on detected filesystem (e.g. bd_fs_xfs_repair for XFS). This
 * function will return an error for unknown/unsupported filesystems.
 *
 * Returns: whether the file system on @device was successfully repaired or not
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_repair (const gchar *device, GError **error) {
    return device_operation (device, BD_FS_REPAIR, 0, NULL, error);
 }

/**
 * bd_fs_check:
 * @device: the device the file system of which to check
 * @error: (out): place to store error (if any)
 *
 * Check filesystem on @device. This calls other fs check functions from this
 * plugin based on detected filesystem (e.g. bd_fs_xfs_check for XFS). This
 * function will return an error for unknown/unsupported filesystems.
 *
 * Returns: whether the file system on @device passed the consistency check or not
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_check (const gchar *device, GError **error) {
    return device_operation (device, BD_FS_CHECK, 0, NULL, error);
}

/**
 * bd_fs_set_label:
 * @device: the device with file system to set the label for
 * @error: (out): place to store error (if any)
 *
 * Set label for filesystem on @device. This calls other fs label functions from this
 * plugin based on detected filesystem (e.g. bd_fs_xfs_set_label for XFS). This
 * function will return an error for unknown/unsupported filesystems.
 *
 * Returns: whether the file system on @device was successfully relabled or not
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_set_label (const gchar *device, const gchar *label, GError **error) {
    return device_operation (device, BD_FS_LABEL, 0, label, error);
 }

static gboolean query_fs_operation (const gchar *fs_type, BDFsOpType op, gchar **required_utility, BDFsResizeFlags *mode, GError **error) {
    gboolean ret;
    const BDFSInfo *fsinfo = NULL;
    const gchar* op_name = NULL;
    const gchar* exec_util = NULL;

    if (required_utility != NULL)
        *required_utility = NULL;

    if (mode != NULL)
        *mode = 0;

    fsinfo = get_fs_info (fs_type);
    if (fsinfo != NULL) {
        switch (op) {
            case BD_FS_RESIZE:
                op_name = "Resizing";
                exec_util = fsinfo->resize_util;
                break;
            case BD_FS_REPAIR:
                op_name = "Repairing";
                exec_util = fsinfo->repair_util;
                break;
            case BD_FS_CHECK:
                op_name = "Checking";
                exec_util = fsinfo->check_util;
                break;
            case BD_FS_LABEL:
                op_name = "Setting the label of";
                exec_util = fsinfo->label_util;
                break;
        }
    }

    if (fsinfo == NULL || exec_util == NULL) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOT_SUPPORTED,
                     "%s filesystem '%s' is not supported.", op_name, fs_type);
        return FALSE;
    }

    if (mode != NULL)
        *mode = fsinfo->resize_mode;

    if (strlen(exec_util) == 0) { /* empty string if no util needed */
        return TRUE;
    }

    ret = bd_utils_check_util_version (exec_util, NULL, "", NULL, NULL);
    if (!ret && required_utility != NULL)
        *required_utility = g_strdup (exec_util);

    return ret;
}

/**
 * bd_fs_can_resize:
 * @type: the filesystem type to be tested for installed resize support
 * @mode: (out): flags for allowed resizing (i.e. growing/shrinking support for online/offline)
 * @required_utility: (out) (transfer full): the utility binary which is required for resizing (if missing i.e. returns FALSE but no error)
 * @error: (out): place to store error (if any)
 *
 * Searches for the required utility to resize the given filesystem and returns whether
 * it is installed. The mode flags indicate if growing and/or shrinking resize is available if
 * mounted/unmounted.
 * Unknown filesystems or filesystems which do not support resizing result in errors.
 *
 * Returns: whether filesystem resize is available
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_QUERY
 */
gboolean bd_fs_can_resize (const gchar *type, BDFsResizeFlags *mode, gchar **required_utility, GError **error) {
    return query_fs_operation (type, BD_FS_RESIZE, required_utility, mode, error);
}

/**
 * bd_fs_can_check:
 * @type: the filesystem type to be tested for installed consistency check support
 * @required_utility: (out) (transfer full): the utility binary which is required for checking (if missing i.e. returns FALSE but no error)
 * @error: (out): place to store error (if any)
 *
 * Searches for the required utility to check the given filesystem and returns whether
 * it is installed.
 * Unknown filesystems or filesystems which do not support checking result in errors.
 *
 * Returns: whether filesystem check is available
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_QUERY
 */
gboolean bd_fs_can_check (const gchar *type, gchar **required_utility, GError **error) {
    return query_fs_operation (type, BD_FS_CHECK, required_utility, NULL, error);
}

/**
 * bd_fs_can_repair:
 * @type: the filesystem type to be tested for installed repair support
 * @required_utility: (out) (transfer full): the utility binary which is required for repairing (if missing i.e. return FALSE but no error)
 * @error: (out): place to store error (if any)
 *
 * Searches for the required utility to repair the given filesystem and returns whether
 * it is installed.
 * Unknown filesystems or filesystems which do not support reparing result in errors.
 *
 * Returns: whether filesystem repair is available
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_QUERY
 */
gboolean bd_fs_can_repair (const gchar *type, gchar **required_utility, GError **error) {
    return query_fs_operation (type, BD_FS_REPAIR, required_utility, NULL, error);
}

/**
 * bd_fs_can_set_label:
 * @type: the filesystem type to be tested for installed label support
 * @required_utility: (out) (transfer full): the utility binary which is required for relabeling (if missing i.e. return FALSE but no error)
 * @error: (out): place to store error (if any)
 *
 * Searches for the required utility to set the label of the given filesystem and returns whether
 * it is installed.
 * Unknown filesystems or filesystems which do not support setting the label result in errors.
 *
 * Returns: whether setting filesystem label is available
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_QUERY
 */
gboolean bd_fs_can_set_label (const gchar *type, gchar **required_utility, GError **error) {
    return query_fs_operation (type, BD_FS_LABEL, required_utility, NULL, error);
}

/**
 * bd_fs_ext2_mkfs:
 * @device: the device to create a new ext2 fs on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mke2fs' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a new ext2 fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_ext2_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_mkfs (device, extra, EXT2, error);
}

/**
 * bd_fs_ext3_mkfs:
 * @device: the device to create a new ext3 fs on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mke2fs' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a new ext3 fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_ext3_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_mkfs (device, extra, EXT3, error);
}

/**
 * bd_fs_ext4_mkfs:
 * @device: the device to create a new ext4 fs on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.ext4' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a new ext4 fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_ext4_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_mkfs (device, extra, EXT4, error);
}

/**
 * bd_fs_ext2_wipe:
 * @device: the device to wipe an ext2 signature from
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext2 signature was successfully wiped from the @device or
 *          not
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_ext2_wipe (const gchar *device, GError **error) {
    return wipe_fs (device, EXT2, FALSE, error);
}

/**
 * bd_fs_ext3_wipe:
 * @device: the device to wipe an ext3 signature from
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext3 signature was successfully wiped from the @device or
 *          not
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_ext3_wipe (const gchar *device, GError **error) {
    return wipe_fs (device, EXT3, FALSE, error);
}

/**
 * bd_fs_ext4_wipe:
 * @device: the device to wipe an ext4 signature from
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext4 signature was successfully wiped from the @device or
 *          not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_ext4_wipe (const gchar *device, GError **error) {
    return wipe_fs (device, EXT4, FALSE, error);
}

static gboolean ext_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    /* Force checking even if the file system seems clean. AND
     * Open the filesystem read-only, and assume an answer of no to all
     * questions. */
    const gchar *args[5] = {"e2fsck", "-f", "-n", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_E2FSCK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    if (!ret && (status == 4)) {
        /* no error should be reported for exit code 4 - File system errors left uncorrected */
        g_clear_error (error);
    }
    return ret;
}

/**
 * bd_fs_ext2_check:
 * @device: the device the file system on which to check
 * @extra: (allow-none) (array zero-terminated=1): extra options for the check (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext2 file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_ext2_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_check (device, extra, error);
}

/**
 * bd_fs_ext3_check:
 * @device: the device the file system on which to check
 * @extra: (allow-none) (array zero-terminated=1): extra options for the check (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext3 file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_ext3_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_check (device, extra, error);
}

/**
 * bd_fs_ext4_check:
 * @device: the device the file system on which to check
 * @extra: (allow-none) (array zero-terminated=1): extra options for the check (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext4 file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_ext4_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    return ext_check (device, extra, error);
}

static gboolean ext_repair (const gchar *device, gboolean unsafe, const BDExtraArg **extra, GError **error) {
    /* Force checking even if the file system seems clean. AND
     *     Automatically repair what can be safely repaired. OR
     *     Assume an answer of `yes' to all questions. */
    const gchar *args[5] = {"e2fsck", "-f", unsafe ? "-y" : "-p", device, NULL};

    if (!check_deps (&avail_deps, DEPS_E2FSCK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_ext2_repair:
 * @device: the device the file system on which to repair
 * @unsafe: whether to do unsafe operations too
 * @extra: (allow-none) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext2 file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_ext2_repair (const gchar *device, gboolean unsafe, const BDExtraArg **extra, GError **error) {
    return ext_repair (device, unsafe, extra, error);
}

/**
 * bd_fs_ext3_repair:
 * @device: the device the file system on which to repair
 * @unsafe: whether to do unsafe operations too
 * @extra: (allow-none) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext3 file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_ext3_repair (const gchar *device, gboolean unsafe, const BDExtraArg **extra, GError **error) {
    return ext_repair (device, unsafe, extra, error);
}

/**
 * bd_fs_ext4_repair:
 * @device: the device the file system on which to repair
 * @unsafe: whether to do unsafe operations too
 * @extra: (allow-none) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'e2fsck' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ext4 file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_ext4_repair (const gchar *device, gboolean unsafe, const BDExtraArg **extra, GError **error) {
    return ext_repair (device, unsafe, extra, error);
}

static gboolean ext_set_label (const gchar *device, const gchar *label, GError **error) {
    const gchar *args[5] = {"tune2fs", "-L", label, device, NULL};

    if (!check_deps (&avail_deps, DEPS_TUNE2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_ext2_set_label:
 * @device: the device the file system on which to set label for
 * @label: label to set
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the label of ext2 file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_ext2_set_label (const gchar *device, const gchar *label, GError **error) {
    return ext_set_label (device, label, error);
}

/**
 * bd_fs_ext3_set_label:
 * @device: the device the file system on which to set label for
 * @label: label to set
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the label of ext3 file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_ext3_set_label (const gchar *device, const gchar *label, GError **error) {
    return ext_set_label (device, label, error);
}

/**
 * bd_fs_ext4_set_label:
 * @device: the device the file system on which to set label for
 * @label: label to set
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the label of ext4 file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_ext4_set_label (const gchar *device, const gchar *label, GError **error) {
    return ext_set_label (device, label, error);
}

/**
 * parse_output_vars: (skip)
 * @str: string to parse
 * @item_sep: item separator(s) (key-value pairs separator)
 * @key_val_sep: key-value separator(s) (typically ":" or "=")
 * @num_items: (out): number of parsed items (key-value pairs)
 *
 * Returns: (transfer full): GHashTable containing the key-value pairs parsed
 * from the @str.
 */
static GHashTable* parse_output_vars (const gchar *str, const gchar *item_sep, const gchar *key_val_sep, guint *num_items) {
    GHashTable *table = NULL;
    gchar **items = NULL;
    gchar **item_p = NULL;
    gchar **key_val = NULL;

    table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    *num_items = 0;

    items = g_strsplit_set (str, item_sep, 0);
    for (item_p=items; *item_p; item_p++) {
        key_val = g_strsplit (*item_p, key_val_sep, 2);
        if (g_strv_length (key_val) == 2) {
            /* we only want to process valid lines (with the separator) */
            g_hash_table_insert (table, g_strstrip (key_val[0]), g_strstrip (key_val[1]));
            (*num_items)++;
        } else
            /* invalid line, just free key_val */
            g_strfreev (key_val);
    }

    g_strfreev (items);
    return table;
}

static BDFSExtInfo* get_ext_info_from_table (GHashTable *table, gboolean free_table) {
    BDFSExtInfo *ret = g_new0 (BDFSExtInfo, 1);
    gchar *value = NULL;

    ret->label = g_strdup ((gchar*) g_hash_table_lookup (table, "Filesystem volume name"));
    if ((!ret->label) || (g_strcmp0 (ret->label, "<none>") == 0))
        ret->label = g_strdup ("");
    ret->uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "Filesystem UUID"));
    ret->state = g_strdup ((gchar*) g_hash_table_lookup (table, "Filesystem state"));

    value = (gchar*) g_hash_table_lookup (table, "Block size");
    if (value)
        ret->block_size = g_ascii_strtoull (value, NULL, 0);
    else
        ret->block_size = 0;
    value = (gchar*) g_hash_table_lookup (table, "Block count");
    if (value)
        ret->block_count = g_ascii_strtoull (value, NULL, 0);
    else
        ret->block_count = 0;
    value = (gchar*) g_hash_table_lookup (table, "Free blocks");
    if (value)
        ret->free_blocks = g_ascii_strtoull (value, NULL, 0);
    else
        ret->free_blocks = 0;

    if (free_table)
        g_hash_table_destroy (table);

    return ret;
}

static BDFSExtInfo* ext_get_info (const gchar *device, GError **error) {
    const gchar *args[4] = {"dumpe2fs", "-h", device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    GHashTable *table = NULL;
    guint num_items = 0;
    BDFSExtInfo *ret = NULL;

    if (!check_deps (&avail_deps, DEPS_DUMPE2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        return FALSE;
    }

    table = parse_output_vars (output, "\n", ":", &num_items);
    g_free (output);
    if (!table || (num_items == 0)) {
        /* something bad happened or some expected items were missing  */
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse ext4 file system information");
        if (table)
            g_hash_table_destroy (table);
        return NULL;
    }

    ret = get_ext_info_from_table (table, TRUE);
    if (!ret) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse ext4 file system information");
        return NULL;
    }

    return ret;
}

/**
 * bd_fs_ext2_get_info:
 * @device: the device the file system of which to get info for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_QUERY
 */
BDFSExt2Info* bd_fs_ext2_get_info (const gchar *device, GError **error) {
    return (BDFSExt2Info*) ext_get_info (device, error);
}

/**
 * bd_fs_ext3_get_info:
 * @device: the device the file system of which to get info for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_QUERY
 */
BDFSExt3Info* bd_fs_ext3_get_info (const gchar *device, GError **error) {
    return (BDFSExt3Info*) ext_get_info (device, error);
}

/**
 * bd_fs_ext4_get_info:
 * @device: the device the file system of which to get info for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_QUERY
 */
BDFSExt4Info* bd_fs_ext4_get_info (const gchar *device, GError **error) {
    return (BDFSExt4Info*) ext_get_info (device, error);
}

static gboolean ext_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"resize2fs", device, NULL, NULL};
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_RESIZE2FS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (new_size != 0)
        /* resize2fs doesn't understand bytes, just 512B sectors */
        args[2] = g_strdup_printf ("%"G_GUINT64_FORMAT"s", new_size / 512);
    ret = bd_utils_exec_and_report_error (args, extra, error);

    g_free ((gchar *) args[2]);
    return ret;
}

/**
 * bd_fs_ext2_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system (if 0, the file system is
 *            adapted to the underlying block device)
 * @extra: (allow-none) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'resize2fs' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_EXT2-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_ext2_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error) {
    return ext_resize (device, new_size, extra, error);
}

/**
 * bd_fs_ext3_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system (if 0, the file system is
 *            adapted to the underlying block device)
 * @extra: (allow-none) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'resize2fs' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_EXT3-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_ext3_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error) {
    return ext_resize (device, new_size, extra, error);
}

/**
 * bd_fs_ext4_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system (if 0, the file system is
 *            adapted to the underlying block device)
 * @extra: (allow-none) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'resize2fs' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_EXT4-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_ext4_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error) {
    return ext_resize (device, new_size, extra, error);
}

/**
 * bd_fs_xfs_mkfs:
 * @device: the device to create a new xfs fs on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.xfs' utility)
 * @error: (out): place to store error (if any)
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
 * bd_fs_xfs_wipe:
 * @device: the device to wipe an xfs signature from
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an xfs signature was successfully wiped from the @device or
 *          not
 *
 * Tech category: %BD_FS_TECH_XFS-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_xfs_wipe (const gchar *device, GError **error) {
    return wipe_fs (device, "xfs", FALSE, error);
}

/**
 * bd_fs_xfs_check:
 * @device: the device containing the file system to check
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an xfs file system on the @device is clean or not
 *
 * Note: if the file system is mounted it may be reported as unclean even if
 *       everything is okay and there are just some pending/in-progress writes
 *
 * Tech category: %BD_FS_TECH_XFS-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_xfs_check (const gchar *device, GError **error) {
    const gchar *args[6] = {"xfs_db", "-r", "-c", "check", device, NULL};
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_XFS_DB_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_error (args, NULL, error);
    if (!ret && *error &&  g_error_matches ((*error), BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED))
        /* non-zero exit status -> the fs is not clean, but not an error */
        /* TODO: should we check that the device exists and contains an XFS FS beforehand? */
        g_clear_error (error);
    return ret;
}

/**
 * bd_fs_xfs_repair:
 * @device: the device containing the file system to repair
 * @extra: (allow-none) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'xfs_repair' utility)
 * @error: (out): place to store error (if any)
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
 * @error: (out): place to store error (if any)
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
 * bd_fs_xfs_get_info:
 * @device: the device containing the file system to get info for (device must
            be mounted, trying to get info for an unmounted device will result
            in an error)
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_XFS-%BD_FS_TECH_MODE_QUERY
 */
BDFSXfsInfo* bd_fs_xfs_get_info (const gchar *device, GError **error) {
    const gchar *args[4] = {"xfs_admin", "-lu", device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    BDFSXfsInfo *ret = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gboolean have_label = FALSE;
    gboolean have_uuid = FALSE;
    gchar *val_start = NULL;
    gchar *val_end = NULL;
    g_autofree gchar* mountpoint = NULL;

    if (!check_deps (&avail_deps, DEPS_XFS_ADMIN_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    mountpoint = bd_fs_get_mountpoint (device, error);
    if (mountpoint == NULL) {
        if (*error == NULL) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOT_MOUNTED,
                         "Can't get xfs file system information for '%s': Device is not mounted.", device);
            return NULL;
        } else {
            g_prefix_error (error, "Error when trying to get mountpoint for '%s': ", device);
            return NULL;
        }
    }

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success)
        /* error is already populated */
        return FALSE;

    ret = g_new0 (BDFSXfsInfo, 1);
    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    for (line_p=lines; *line_p && (!have_label || !have_uuid); line_p++) {
        if (!have_label && g_str_has_prefix (*line_p, "label")) {
            /* extract label from something like this: label = "TEST_LABEL" */
            val_start = strchr (*line_p, '"');
            if (val_start)
                val_end = strchr(val_start + 1, '"');
            if (val_start && val_end) {
                ret->label = g_strndup (val_start + 1, val_end - val_start - 1);
                have_label = TRUE;
            }
        } else if (!have_uuid && g_str_has_prefix (*line_p, "UUID")) {
            /* get right after the "UUID = " prefix */
            val_start = *line_p + 7;
            ret->uuid = g_strdup (val_start);
            have_uuid = TRUE;
        }
    }
    g_strfreev (lines);

    args[0] = "xfs_info";
    args[1] = mountpoint;
    args[2] = NULL;
    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        bd_fs_xfs_info_free (ret);
        return FALSE;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    line_p = lines;
    /* find the beginning of the (data) section we are interested in */
    while (*line_p && !g_str_has_prefix (*line_p, "data"))
        line_p++;
    if (!line_p) {
        /* error is already populated */
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse xfs file system information");
        g_strfreev (lines);
        bd_fs_xfs_info_free (ret);
        return FALSE;
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
        /* error is already populated */
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse xfs file system information");
        g_strfreev (lines);
        bd_fs_xfs_info_free (ret);
        return FALSE;
    }
    while (isdigit (*val_start) || isspace(*val_start))
        val_start++;
    if (g_str_has_prefix (val_start, "blocks")) {
        val_start = strchr (val_start, '=');
        val_start++;
        ret->block_count = g_ascii_strtoull (val_start, NULL, 0);
    } else {
        /* error is already populated */
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse xfs file system information");
        g_strfreev (lines);
        bd_fs_xfs_info_free (ret);
        return FALSE;
    }
    g_strfreev (lines);

    return ret;
}

/**
 * bd_fs_xfs_resize:
 * @mpoint: the mount point of the file system to resize
 * @new_size: new requested size for the file system *in file system blocks* (see bd_fs_xfs_get_info())
 *            (if 0, the file system is adapted to the underlying block device)
 * @extra: (allow-none) (array zero-terminated=1): extra options for the resize (right now
 *                                                 passed to the 'xfs_growfs' utility)
 * @error: (out): place to store error (if any)
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

/**
 * bd_fs_vfat_mkfs:
 * @device: the device to create a new vfat fs on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.vfat' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a new vfat fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_vfat_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"mkfs.vfat", "-I", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKFSVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_vfat_wipe:
 * @device: the device to wipe an vfat signature from
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an vfat signature was successfully wiped from the @device or
 *          not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_vfat_wipe (const gchar *device, GError **error) {
    return wipe_fs (device, "vfat", TRUE, error);
}

/**
 * bd_fs_vfat_check:
 * @device: the device containing the file system to check
 * @extra: (allow-none) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'fsck.vfat' utility)
 * @error: (out): place to store error (if any)
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
 * @extra: (allow-none) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'fsck.vfat' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an vfat file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_vfat_repair (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"fsck.vfat", "-a", device, NULL};

    if (!check_deps (&avail_deps, DEPS_FSCKVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_vfat_set_label:
 * @device: the device containing the file system to set label for
 * @label: label to set
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the label of vfat file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_vfat_set_label (const gchar *device, const gchar *label, GError **error) {
    const gchar *args[4] = {"fatlabel", device, label, NULL};

    if (!check_deps (&avail_deps, DEPS_FATLABEL_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_vfat_get_info:
 * @device: the device containing the file system to get info for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_QUERY
 */
BDFSVfatInfo* bd_fs_vfat_get_info (const gchar *device, GError **error) {
    const gchar *args[4] = {"fsck.vfat", "-nv", device, NULL};
    blkid_probe probe = NULL;
    gint fd = 0;
    gint status = 0;
    gboolean success = FALSE;
    const gchar *value = NULL;
    BDFSVfatInfo *ret = NULL;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gboolean have_cluster_size = FALSE;
    gboolean have_cluster_count = FALSE;
    guint64 full_cluster_count = 0;
    guint64 cluster_count = 0;
    gchar **key_val = NULL;

    if (!check_deps (&avail_deps, DEPS_FSCKVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    probe = blkid_new_probe ();
    if (!probe) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", device);
        return FALSE;
    }

    fd = open (device, O_RDWR|O_CLOEXEC);
    if (fd == -1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", device);
        blkid_free_probe (probe);
        return FALSE;
    }

    status = blkid_probe_set_device (probe, fd, 0, 0);
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        return FALSE;
    }

    blkid_probe_enable_partitions(probe, 1);

    status = blkid_do_probe (probe);
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to probe the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        return FALSE;
    }

    ret = g_new0 (BDFSVfatInfo, 1);

    status = blkid_probe_has_value (probe, "LABEL");

    if (status == 0)
        ret->label = g_strdup ("");
    else {
        status = blkid_probe_lookup_value (probe, "LABEL", &value, NULL);
        if (status != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to get label for the device '%s'", device);
            blkid_free_probe (probe);
            synced_close (fd);
            return FALSE;
        }

        ret->label = g_strdup (value);
    }

    status = blkid_probe_lookup_value (probe, "UUID", &value, NULL);
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to get label for the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        return FALSE;
    }

    ret->uuid = g_strdup (value);

    blkid_free_probe (probe);
    synced_close (fd);

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success)
        /* error is already populated */
        return FALSE;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    for (line_p=lines; *line_p && (!have_cluster_size || !have_cluster_count); line_p++) {
        if (!have_cluster_size && g_str_has_suffix (*line_p, "bytes per cluster")) {
            ret->cluster_size = g_ascii_strtoull (*line_p, NULL, 0);
            have_cluster_size = TRUE;
        } else if (!have_cluster_count && g_str_has_prefix (*line_p, device)) {
            key_val = g_strsplit (*line_p, ",", 2);
            sscanf (key_val[1], " %" G_GUINT64_FORMAT "/" "%" G_GUINT64_FORMAT " clusters",
                    &full_cluster_count, &cluster_count);
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
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_vfat_resize (const gchar *device, guint64 new_size, GError **error) {
    PedDevice *ped_dev = NULL;
    PedGeometry geom = {0};
    PedGeometry new_geom = {0};
    PedFileSystem *fs = NULL;
    PedSector start = 0;
    PedSector length = 0;
    gint status = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started resizing vfat filesystem on the device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ped_dev = ped_device_get (device);
    if (!ped_dev) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to get ped device for the device '%s'", device);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    status = ped_device_open (ped_dev);
    if (status == 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to get open the device '%s'", device);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    status = ped_geometry_init (&geom, ped_dev, start, ped_dev->length);
    if (status == 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to initialize geometry for the device '%s'", device);
        ped_device_close (ped_dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    fs = ped_file_system_open(&geom);
    if (!fs) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to read the filesystem on the device '%s'", device);
        ped_device_close (ped_dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (new_size == 0)
        length = ped_dev->length;
    else
        length = (PedSector) ((PedSector) new_size / ped_dev->sector_size);

    status = ped_geometry_init(&new_geom, ped_dev, start, length);
    if (status == 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to initialize new geometry for the filesystem on '%s'", device);
        ped_file_system_close (fs);
        ped_device_close (ped_dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    status = ped_file_system_resize(fs, &new_geom, NULL);
    if (status == 0) {
        set_parted_error (error, BD_FS_ERROR_FAIL);
        g_prefix_error (error, "Failed to resize the filesystem on '%s'", device);
        ped_file_system_close (fs);
        ped_device_close (ped_dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ped_file_system_close (fs);
    ped_device_close (ped_dev);
    bd_utils_report_finished (progress_id, "Completed");

    return TRUE;

}

/**
 * bd_fs_ntfs_mkfs:
 * @device: the device to create a new ntfs fs on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkntfs' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether a new NTFS fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_ntfs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"mkntfs", "-f", "-F", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKNTFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_ntfs_wipe:
 * @device: the device to wipe an ntfs signature from
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ntfs signature was successfully wiped from the @device or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_ntfs_wipe (const gchar *device, GError **error) {
    return wipe_fs (device, "ntfs", TRUE, error);
}

/**
 * bd_fs_ntfs_check:
 * @device: the device containing the file system to check
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an ntfs file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_ntfs_check (const gchar *device, GError **error) {
    const gchar *args[4] = {"ntfsfix", "-n", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_NTFSFIX_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, NULL, &status, error);
    if (!ret && (status == 1)) {
        /* no error should be reported for exit code 1 -- Recoverable errors have been detected */
        g_clear_error (error);
    }
    return ret;
}

/**
 * bd_fs_ntfs_repair:
 * @device: the device containing the file system to repair
 * @error: (out): place to store error (if any)
 *
 * Returns: whether an NTFS file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_ntfs_repair (const gchar *device, GError **error) {
    const gchar *args[4] = {"ntfsfix", "-d", device, NULL};

    if (!check_deps (&avail_deps, DEPS_NTFSFIX_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_ntfs_set_label:
 * @device: the device containing the file system to set the label for
 * @label: label to set
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the label of the NTFS file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_ntfs_set_label (const gchar *device, const gchar *label, GError **error) {
    const gchar *args[4] = {"ntfslabel", device, label, NULL};

    if (!check_deps (&avail_deps, DEPS_NTFSLABEL_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_ntfs_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system in bytes (if 0, the file system
 *            is adapted to the underlying block device)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_ntfs_resize (const gchar *device, guint64 new_size, GError **error) {
    const gchar *args[5] = {"ntfsresize", NULL, NULL, NULL, NULL};
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_NTFSRESIZE_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (new_size != 0) {
        args[1] = "-s";
        args[2] = g_strdup_printf ("%"G_GUINT64_FORMAT, new_size);
        args[3] = device;
    } else {
        args[1] = device;
    }
    ret = bd_utils_exec_and_report_error (args, NULL, error);

    g_free ((gchar *) args[2]);
    return ret;
}

/**
 * bd_fs_ntfs_get_info:
 * @device: the device containing the file system to get info for (device must
            not be mounted, trying to get info for a mounted device will result
            in an error)
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_NTFS-%BD_FS_TECH_MODE_QUERY
 */
BDFSNtfsInfo* bd_fs_ntfs_get_info (const gchar *device, GError **error) {
    const gchar *args[3] = {"ntfscluster", device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    BDFSNtfsInfo *ret = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar *val_start = NULL;
    g_autofree gchar* mountpoint = NULL;

    if (!check_deps (&avail_deps, DEPS_NTFSCLUSTER_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    mountpoint = bd_fs_get_mountpoint (device, error);
    if (mountpoint != NULL) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOT_MOUNTED,
                     "Can't get NTFS file system information for '%s': Device is mounted.", device);
        return NULL;
    } else {
        if (*error != NULL) {
            g_prefix_error (error, "Error when trying to get mountpoint for '%s': ", device);
            return NULL;
        }
    }

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success)
        /* error is already populated */
        return FALSE;

    ret = g_new0 (BDFSNtfsInfo, 1);
    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    line_p = lines;
    /* find the beginning of the (data) section we are interested in */
    while (*line_p && !g_str_has_prefix (*line_p, "bytes per volume"))
        line_p++;
    if (!line_p) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse NTFS file system information");
        g_strfreev (lines);
        bd_fs_ntfs_info_free (ret);
        return FALSE;
    }

    /* extract data from something like this: "bytes per volume        : 998240256" */
    val_start = strchr (*line_p, ':');
    val_start++;
    ret->size = g_ascii_strtoull (val_start, NULL, 0);

    while (*line_p && !g_str_has_prefix (*line_p, "bytes of free space"))
        line_p++;
    if (!line_p) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse NTFS file system information");
        g_strfreev (lines);
        bd_fs_ntfs_info_free (ret);
        return FALSE;
    }

    /* extract data from something like this: "bytes of free space     : 992759808" */
    val_start = strchr (*line_p, ':');
    val_start++;
    ret->free_space = g_ascii_strtoull (val_start, NULL, 0);

    g_strfreev (lines);

    return ret;
}
