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

#include <glib.h>
#include <glib/gstdio.h>
#include <blkid.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <fcntl.h>

#include <blockdev/utils.h>

#include "generic.h"
#include "mount.h"
#include "fs.h"
#include "common.h"
#include "ext.h"
#include "xfs.h"
#include "vfat.h"
#include "ntfs.h"
#include "f2fs.h"
#include "reiserfs.h"

typedef enum {
    BD_FS_MKFS,
    BD_FS_RESIZE,
    BD_FS_REPAIR,
    BD_FS_CHECK,
    BD_FS_LABEL,
    BD_FS_GET_SIZE,
    BD_FS_UUID,
    BD_FS_GET_FREE_SPACE,
} BDFsOpType;

/**
 * BDFSInfo:
 * @type: filesystem identifier, must be present
 * @check_util: required utility for consistency checking, "" if not needed and NULL for no support
 * @repair_util: required utility for repair, "" if not needed and NULL for no support
 * @resize_util: required utility for resize, "" if not needed and NULL for no support
 * @resize_mode: resize availability flags, 0 if no support
 * @label_util: required utility for labelling, "" if not needed and NULL for no support
 * @info_util: required utility for getting information about the filesystem, "" if not needed and NULL for no support
 * @uuid_util: required utility for setting UUID, "" if not needed and NULL for no support
 */
typedef struct BDFSInfo {
    const gchar *type;
    const gchar *mkfs_util;
    BDFSMkfsOptionsFlags mkfs_options;
    const gchar *check_util;
    const gchar *repair_util;
    const gchar *resize_util;
    BDFsResizeFlags resize_mode;
    const gchar *label_util;
    const gchar *info_util;
    const gchar *uuid_util;
} BDFSInfo;

static const BDFSInfo fs_info[] = {
    {"xfs", "mkfs.xfs", BD_FS_MKFS_LABEL | BD_FS_MKFS_UUID | BD_FS_MKFS_DRY_RUN | BD_FS_MKFS_NODISCARD, "xfs_db", "xfs_repair", "xfs_growfs", BD_FS_ONLINE_GROW | BD_FS_OFFLINE_GROW, "xfs_admin", "xfs_admin", "xfs_admin"},
    {"ext2", "mkfs.ext2", BD_FS_MKFS_LABEL | BD_FS_MKFS_UUID | BD_FS_MKFS_DRY_RUN | BD_FS_MKFS_NODISCARD, "e2fsck", "e2fsck", "resize2fs", BD_FS_ONLINE_GROW | BD_FS_OFFLINE_GROW | BD_FS_OFFLINE_SHRINK, "tune2fs", "dumpe2fs", "tune2fs"},
    {"ext3", "mkfs.ext3", BD_FS_MKFS_LABEL | BD_FS_MKFS_UUID | BD_FS_MKFS_DRY_RUN | BD_FS_MKFS_NODISCARD, "e2fsck", "e2fsck", "resize2fs", BD_FS_ONLINE_GROW | BD_FS_OFFLINE_GROW | BD_FS_OFFLINE_SHRINK, "tune2fs", "dumpe2fs", "tune2fs"},
    {"ext4", "mkfs.ext4", BD_FS_MKFS_LABEL | BD_FS_MKFS_UUID | BD_FS_MKFS_DRY_RUN | BD_FS_MKFS_NODISCARD, "e2fsck", "e2fsck", "resize2fs", BD_FS_ONLINE_GROW | BD_FS_OFFLINE_GROW | BD_FS_OFFLINE_SHRINK, "tune2fs", "dumpe2fs", "tune2fs"},
    {"vfat", "mkfs.vfat", BD_FS_MKFS_LABEL | BD_FS_MKFS_UUID, "fsck.vfat", "fsck.vfat", "vfat-resize", BD_FS_OFFLINE_GROW | BD_FS_OFFLINE_SHRINK, "fatlabel", "fsck.vfat", NULL},
    {"ntfs", "mkfs.ntfs", BD_FS_MKFS_LABEL | BD_FS_MKFS_DRY_RUN, "ntfsfix", "ntfsfix", "ntfsresize", BD_FS_OFFLINE_GROW | BD_FS_OFFLINE_SHRINK, "ntfslabel", "ntfscluster", "ntfslabel"},
    {"f2fs", "mkfs.f2fs", BD_FS_MKFS_LABEL | BD_FS_MKFS_NODISCARD, "fsck.f2fs", "fsck.f2fs", "resize.f2fs", BD_FS_OFFLINE_GROW | BD_FS_OFFLINE_SHRINK, NULL, "dump.f2fs", NULL},
    {"reiserfs", "mkfs.reiserfs", BD_FS_MKFS_LABEL | BD_FS_MKFS_UUID, "reiserfsck", "reiserfsck", "resize_reiserfs", BD_FS_ONLINE_GROW | BD_FS_OFFLINE_GROW | BD_FS_OFFLINE_SHRINK, "reiserfstune", "debugreiserfs", "reiserfstune"},
    {"nilfs2", "mkfs.nilfs", BD_FS_MKFS_LABEL | BD_FS_MKFS_DRY_RUN | BD_FS_MKFS_NODISCARD, NULL, NULL, "nilfs-resize", BD_FS_ONLINE_GROW | BD_FS_ONLINE_SHRINK, "tune-nilfs", "tune-nilfs", "tune-nilfs"},
    {"exfat", "mkfs.exfat", BD_FS_MKFS_LABEL, "fsck.exfat", "fsck.exfat", NULL, 0, "tune.exfat", "tune.exfat", NULL},
    {"btrfs", "mkfs.btrfs", BD_FS_MKFS_LABEL | BD_FS_MKFS_UUID | BD_FS_MKFS_NODISCARD, "btrfsck", "btrfsck", "btrfs", BD_FS_ONLINE_GROW | BD_FS_ONLINE_SHRINK, "btrfs", "btrfs", "btrfstune"},
    {"udf", "mkudffs", BD_FS_MKFS_LABEL | BD_FS_MKFS_UUID, NULL, NULL, NULL, 0, "udflabel", "udfinfo", "udflabel"},
    {NULL, NULL, 0, NULL, NULL, NULL, 0, NULL, NULL, NULL}
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
 * bd_fs_wipe:
 * @device: the device to wipe signatures from
 * @all: whether to wipe all (%TRUE) signatures or just the first (%FALSE) one
 * @force: whether to wipe signatures on a mounted @device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether signatures were successfully wiped on @device or not
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_WIPE
 */
gboolean bd_fs_wipe (const gchar *device, gboolean all, gboolean force, GError **error) {
    blkid_probe probe = NULL;
    gint fd = 0;
    gint status = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    guint n_try = 0;
    gint mode = 0;
    gboolean reprobed = FALSE;

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

    mode = O_RDWR | O_CLOEXEC;
    if (!force)
        mode |= O_EXCL;

    fd = open (device, mode);
    if (fd == -1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to open the device '%s'", device);
        blkid_free_probe (probe);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* we may need to try multiple times with some delays in case the device is
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

    /* we may need to try multiple times with some delays in case the device is
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
    while ((status = blkid_do_probe (probe)) >= 0) {
        if (status == 0 && blkid_do_wipe (probe, FALSE) != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to wipe signatures on the device '%s'", device);
            blkid_free_probe (probe);
            synced_close (fd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
        if (!all)
            break;
        if (status == 1) {
            if (!reprobed) {
                blkid_reset_probe (probe);
                reprobed = TRUE;
            } else
                break;
        }
    }
    if (status < 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to probe the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    blkid_free_probe (probe);
    synced_close (fd);

    bd_utils_report_finished (progress_id, "Completed");

    return TRUE;
}

/**
 * bd_fs_clean:
 * @device: the device to clean
 * @force: whether to wipe signatures on a mounted @device
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
gboolean bd_fs_clean (const gchar *device, gboolean force, GError **error) {
  gboolean ret = FALSE;

  ret = bd_fs_wipe (device, TRUE, force, error);

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

    fd = open (device, O_RDONLY|O_CLOEXEC);
    if (fd == -1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to open the device '%s'", device);
        blkid_free_probe (probe);
        return FALSE;
    }

    /* we may need to try multiple times with some delays in case the device is
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

    /* we may need to try multiple times with some delays in case the device is
       busy at the very moment */
    for (n_try=5, status=-1; !(status == 0 || status == 1) && (n_try > 0); n_try--) {
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
        blkid_free_probe (probe);
        synced_close (fd);
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
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    }

    fstype = g_strdup (value);
    blkid_free_probe (probe);
    synced_close (fd);

    return fstype;
}

/**
 * fs_mount:
 * @device: the device to mount for an FS operation
 * @fstype: (allow-none): filesystem type on @device
 * @unmount: (out): whether caller should unmount the device (was mounted by us) or
 *                  not (was already mounted before)
 * @error: (out): place to store error (if any)
 *
 * This is just a helper function for FS operations that need @device to be mounted.
 * If the device is already mounted, this will just return the existing mountpoint.
 * If the device is not mounted, we will mount it to a temporary directory and set
 * @unmount to %TRUE.
 *
 * Returns: (transfer full): mountpoint @device is mounted at (or %NULL in case of error)
 */
static gchar* fs_mount (const gchar *device, gchar *fstype, gboolean *unmount, GError **error) {
    gchar *mountpoint = NULL;
    gboolean ret = FALSE;

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
                return NULL;
            }
            ret = bd_fs_mount (device, mountpoint, fstype, NULL, NULL, error);
            if (!ret) {
                g_prefix_error (error, "Failed to mount '%s' before resizing it: ", device);
                return NULL;
            } else
                *unmount = TRUE;
        } else {
            g_prefix_error (error, "Error when trying to get mountpoint for '%s': ", device);
            return NULL;
        }
    } else
        *unmount = FALSE;

    return mountpoint;
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

    mountpoint = fs_mount (device, "xfs", &unmount, error);
    if (!mountpoint)
        return FALSE;

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

static gboolean f2fs_resize_device (const gchar *device, guint64 new_size, GError **error) {
    BDFSF2FSInfo *info = NULL;
    gboolean safe = FALSE;

    info = bd_fs_f2fs_get_info (device, error);
    if (!info) {
        /* error is already populated */
        return FALSE;
    }

    /* round to nearest sector_size multiple */
    new_size = (new_size + info->sector_size - 1) / info->sector_size;

    /* safe must be specified for shrining */
    safe = new_size < info->sector_count && new_size != 0;
    bd_fs_f2fs_info_free (info);

    return bd_fs_f2fs_resize (device, new_size, safe, NULL, error);
}

static gboolean nilfs2_resize_device (const gchar *device, guint64 new_size, GError **error) {
    g_autofree gchar* mountpoint = NULL;
    gboolean ret = FALSE;
    gboolean success = FALSE;
    gboolean unmount = FALSE;
    GError *local_error = NULL;

    mountpoint = fs_mount (device, "nilfs2", &unmount, error);
    if (!mountpoint)
        return FALSE;

    success = bd_fs_nilfs2_resize (device, new_size, error);

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

static BDFSBtrfsInfo* btrfs_get_info (const gchar *device, GError **error) {
    g_autofree gchar* mountpoint = NULL;
    gboolean unmount = FALSE;
    gboolean ret = FALSE;
    GError *local_error = NULL;
    BDFSBtrfsInfo* btrfs_info = NULL;

    mountpoint = fs_mount (device, "btrfs", &unmount, error);
    if (!mountpoint)
        return NULL;

    btrfs_info = bd_fs_btrfs_get_info (mountpoint, error);

    if (unmount) {
        ret = bd_fs_unmount (mountpoint, FALSE, FALSE, NULL, &local_error);
        if (!ret) {
            if (btrfs_info) {
                /* info was successful but unmount failed */
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UNMOUNT_FAIL,
                             "Failed to unmount '%s' after getting info: %s",
                             device, local_error->message);
                g_clear_error (&local_error);
                bd_fs_btrfs_info_free (btrfs_info);
                return NULL;
            } else
                /* both info and unmount were unsuccessful but the error
                   from the info is more important so just ignore the
                   unmount error */
                g_clear_error (&local_error);
        }
    }

    return btrfs_info;
}

static gboolean btrfs_resize_device (const gchar *device, guint64 new_size, GError **error) {
    g_autofree gchar* mountpoint = NULL;
    gboolean ret = FALSE;
    gboolean success = FALSE;
    gboolean unmount = FALSE;
    GError *local_error = NULL;

    mountpoint = fs_mount (device, "btrfs", &unmount, error);
    if (!mountpoint)
        return FALSE;

    success = bd_fs_btrfs_resize (mountpoint, new_size, NULL, error);

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

static gboolean btrfs_set_label (const gchar *device, const gchar *label, GError **error) {
    g_autofree gchar* mountpoint = NULL;
    gboolean ret = FALSE;
    gboolean success = FALSE;
    gboolean unmount = FALSE;
    GError *local_error = NULL;

    mountpoint = fs_mount (device, "btrfs", &unmount, error);
    if (!mountpoint)
        return FALSE;

    success = bd_fs_btrfs_set_label (mountpoint, label, error);

    if (unmount) {
        ret = bd_fs_unmount (mountpoint, FALSE, FALSE, NULL, &local_error);
        if (!ret) {
            if (success) {
                /* resize was successful but unmount failed */
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UNMOUNT_FAIL,
                             "Failed to unmount '%s' after setting label: %s",
                             device, local_error->message);
                g_clear_error (&local_error);
                return FALSE;
            } else
                /* both set label and unmount were unsuccessful but the error
                   from the set label is more important so just ignore the
                   unmount error */
                g_clear_error (&local_error);
        }
    }

    return success;
}

static gboolean device_operation (const gchar *device, BDFsOpType op, guint64 new_size, const gchar *label, const gchar *uuid, GError **error) {
    const gchar* op_name = NULL;
    g_autofree gchar* fstype = NULL;

    /* MKFS is covered as a special case, it's a bug to use this function for this case */
    g_assert_true (op != BD_FS_MKFS);

    /* GET_SIZE is covered as a special case, it's a bug to use this function for this case */
    g_assert_true (op != BD_FS_GET_SIZE);

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
            case BD_FS_UUID:
                return bd_fs_ext4_set_uuid (device, uuid, error);
            default:
                g_assert_not_reached ();
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
            case BD_FS_UUID:
                return bd_fs_xfs_set_uuid (device, uuid, error);
            default:
                g_assert_not_reached ();
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
            case BD_FS_UUID:
                break;
            default:
                g_assert_not_reached ();
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
            case BD_FS_UUID:
                return bd_fs_ntfs_set_uuid (device, uuid, error);
            default:
                g_assert_not_reached ();
        }
    } else if (g_strcmp0 (fstype, "f2fs") == 0) {
        switch (op) {
            case BD_FS_RESIZE:
                return f2fs_resize_device (device, new_size, error);
            case BD_FS_REPAIR:
                return bd_fs_f2fs_repair (device, NULL, error);
            case BD_FS_CHECK:
                return bd_fs_f2fs_check (device, NULL, error);
            case BD_FS_LABEL:
                break;
            case BD_FS_UUID:
                break;
            default:
                g_assert_not_reached ();
        }
    } else if (g_strcmp0 (fstype, "reiserfs") == 0) {
        switch (op) {
            case BD_FS_RESIZE:
                return bd_fs_reiserfs_resize (device, new_size, error);
            case BD_FS_REPAIR:
                return bd_fs_reiserfs_repair (device, NULL, error);
            case BD_FS_CHECK:
                return bd_fs_reiserfs_check (device, NULL, error);
            case BD_FS_LABEL:
                return bd_fs_reiserfs_set_label (device, label, error);
            case BD_FS_UUID:
                return bd_fs_reiserfs_set_uuid (device, uuid, error);
            default:
                g_assert_not_reached ();
        }
    } else if (g_strcmp0 (fstype, "nilfs2") == 0) {
        switch (op) {
            case BD_FS_RESIZE:
                return nilfs2_resize_device (device, new_size, error);
            case BD_FS_REPAIR:
                break;
            case BD_FS_CHECK:
                break;
            case BD_FS_LABEL:
                return bd_fs_nilfs2_set_label (device, label, error);
            case BD_FS_UUID:
                return bd_fs_nilfs2_set_uuid (device, uuid, error);
            default:
                g_assert_not_reached ();
        }
    } else if (g_strcmp0 (fstype, "exfat") == 0) {
        switch (op) {
            case BD_FS_RESIZE:
                break;
            case BD_FS_REPAIR:
                return bd_fs_exfat_repair (device, NULL, error);
            case BD_FS_CHECK:
                return bd_fs_exfat_check (device, NULL, error);
            case BD_FS_LABEL:
                return bd_fs_exfat_set_label (device, label, error);
            case BD_FS_UUID:
                break;
            default:
                g_assert_not_reached ();
        }
    } else if (g_strcmp0 (fstype, "btrfs") == 0) {
        switch (op) {
            case BD_FS_RESIZE:
                return btrfs_resize_device (device, new_size, error);
            case BD_FS_REPAIR:
                return bd_fs_btrfs_repair (device, NULL, error);
            case BD_FS_CHECK:
                return bd_fs_btrfs_check (device, NULL, error);
            case BD_FS_LABEL:
                return btrfs_set_label (device, label, error);
            case BD_FS_UUID:
                return bd_fs_btrfs_set_uuid (device, uuid, error);
            default:
                g_assert_not_reached ();
        }
    } else if (g_strcmp0 (fstype, "udf") == 0) {
        switch (op) {
            case BD_FS_RESIZE:
                break;
            case BD_FS_REPAIR:
                break;
            case BD_FS_CHECK:
                break;
            case BD_FS_LABEL:
                return bd_fs_udf_set_label (device, label, error);
            case BD_FS_UUID:
                return bd_fs_udf_set_uuid (device, uuid, error);
            default:
                g_assert_not_reached ();
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
        case BD_FS_UUID:
            op_name = "Setting UUID of";
            break;
        default:
            g_assert_not_reached ();
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
    return device_operation (device, BD_FS_RESIZE, new_size, NULL, NULL, error);
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
    return device_operation (device, BD_FS_REPAIR, 0, NULL, NULL, error);
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
    return device_operation (device, BD_FS_CHECK, 0, NULL, NULL, error);
}

/**
 * bd_fs_set_label:
 * @device: the device with file system to set the label for
 * @label: label to set
 * @error: (out): place to store error (if any)
 *
 * Set label for filesystem on @device. This calls other fs label functions from this
 * plugin based on detected filesystem (e.g. bd_fs_xfs_set_label for XFS). This
 * function will return an error for unknown/unsupported filesystems.
 *
 * Returns: whether the file system on @device was successfully relabeled or not
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_set_label (const gchar *device, const gchar *label, GError **error) {
    return device_operation (device, BD_FS_LABEL, 0, label, NULL, error);
}

/**
 * bd_fs_set_uuid:
 * @device: the device with file system to set the UUID for
 * @uuid: (allow-none): UUID to set or %NULL to generate a new one
 * @error: (out): place to store error (if any)
 *
 * Set UUID for filesystem on @device. This calls other fs UUID functions from this
 * plugin based on detected filesystem (e.g. bd_fs_xfs_set_uuid for XFS). This
 * function will return an error for unknown/unsupported filesystems.
 *
 * Returns: whether the UUID on the file system on @device was successfully changed or not
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    return device_operation (device, BD_FS_UUID, 0, NULL, uuid, error);
}

static BDFSXfsInfo* xfs_get_info (const gchar *device, GError **error) {
    g_autofree gchar* mountpoint = NULL;
    gboolean unmount = FALSE;
    gboolean ret = FALSE;
    GError *local_error = NULL;
    BDFSXfsInfo* xfs_info = NULL;

    mountpoint = fs_mount (device, "xfs", &unmount, error);
    if (!mountpoint)
        return NULL;

    xfs_info = bd_fs_xfs_get_info (device, error);

    if (unmount) {
        ret = bd_fs_unmount (mountpoint, FALSE, FALSE, NULL, &local_error);
        if (!ret) {
            bd_utils_log_format (BD_UTILS_LOG_INFO, "Failed to unmount %s after getting information about it: %s", device, local_error->message);
            g_clear_error (&local_error);
        } else
            g_rmdir (mountpoint);
    }

    return xfs_info;
}

/**
 * bd_fs_get_size:
 * @device: the device with file system to get size for
 * @error: (out): place to store error (if any)
 *
 * Get size for filesystem on @device. This calls other fs info functions from this
 * plugin based on detected filesystem (e.g. bd_fs_xfs_get_info for XFS). This
 * function will return an error for unknown/unsupported filesystems.
 *
 * Returns: size of filesystem on @device, 0 in case of error.
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_QUERY
 */
guint64 bd_fs_get_size (const gchar *device, GError **error) {
    g_autofree gchar* fstype = NULL;
    guint64 size = 0;

    fstype = bd_fs_get_fstype (device, error);
    if (!fstype) {
        if (*error == NULL) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOFS,
                         "No filesystem detected on the device '%s'", device);
            return 0;
        } else {
            g_prefix_error (error, "Error when trying to detect filesystem on '%s': ", device);
            return 0;
        }
    }

    if (g_strcmp0 (fstype, "ext2") == 0 || g_strcmp0 (fstype, "ext3") == 0
                                        || g_strcmp0 (fstype, "ext4") == 0) {
        BDFSExt4Info* info = bd_fs_ext4_get_info (device, error);
        if (info) {
            size = info->block_size * info->block_count;
            bd_fs_ext4_info_free (info);
        }
        return size;

    } else if (g_strcmp0 (fstype, "xfs") == 0) {
        BDFSXfsInfo *info = xfs_get_info (device, error);
        if (info) {
            size = info->block_size * info->block_count;
            bd_fs_xfs_info_free (info);
        }
        return size;
    } else if (g_strcmp0 (fstype, "vfat") == 0) {
        BDFSVfatInfo *info = bd_fs_vfat_get_info (device, error);
        if (info) {
            size = info->cluster_size * info->cluster_count;
            bd_fs_vfat_info_free (info);
        }
        return size;
    } else if (g_strcmp0 (fstype, "ntfs") == 0) {
        BDFSNtfsInfo *info = bd_fs_ntfs_get_info (device, error);
        if (info) {
            size = info->size;
            bd_fs_ntfs_info_free (info);
        }
        return size;
    } else if (g_strcmp0 (fstype, "f2fs") == 0) {
        BDFSF2FSInfo *info = bd_fs_f2fs_get_info (device, error);
        if (info) {
            size = info->sector_size * info->sector_count;
            bd_fs_f2fs_info_free (info);
        }
        return size;
    } else if (g_strcmp0 (fstype, "reiserfs") == 0) {
        BDFSReiserFSInfo *info = bd_fs_reiserfs_get_info (device, error);
        if (info) {
            size = info->block_size * info->block_count;
            bd_fs_reiserfs_info_free (info);
        }
        return size;
    } else if (g_strcmp0 (fstype, "nilfs2") == 0) {
        BDFSNILFS2Info *info = bd_fs_nilfs2_get_info (device, error);
        if (info) {
            size = info->size;
            bd_fs_nilfs2_info_free (info);
        }
        return size;
    } else if (g_strcmp0 (fstype, "exfat") == 0) {
        BDFSExfatInfo *info = bd_fs_exfat_get_info (device, error);
        if (info) {
            size = info->sector_size * info->sector_count;
            bd_fs_exfat_info_free (info);
        }
        return size;
    } else if (g_strcmp0 (fstype, "btrfs") == 0) {
        BDFSBtrfsInfo *info = btrfs_get_info (device, error);
        if (info) {
            size = info->size;
            bd_fs_btrfs_info_free (info);
        }
        return size;
    } else if (g_strcmp0 (fstype, "udf") == 0) {
        BDFSUdfInfo *info = bd_fs_udf_get_info (device, error);
        if (info) {
            size = info->block_size * info->block_count;
            bd_fs_udf_info_free (info);
        }
        return size;
    } else {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOT_SUPPORTED,
                    "Getting size of filesystem '%s' is not supported.", fstype);
        return 0;
    }
}

/**
 * bd_fs_get_free_space:
 * @device: the device with file system to get free space for
 * @error: (out): place to store error (if any)
 *
 * Get free space for filesystem on @device. This calls other fs info functions from this
 * plugin based on detected filesystem (e.g. bd_fs_ext4_get_info for ext4). This
 * function will return an error for unknown/unsupported filesystems.
 *
 * Returns: free space of filesystem on @device, 0 in case of error.
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_QUERY
 */
guint64 bd_fs_get_free_space (const gchar *device, GError **error) {
    g_autofree gchar* fstype = NULL;
    guint64 size = 0;

    fstype = bd_fs_get_fstype (device, error);
    if (!fstype) {
        if (*error == NULL) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOFS,
                         "No filesystem detected on the device '%s'", device);
            return 0;
        } else {
            g_prefix_error (error, "Error when trying to detect filesystem on '%s': ", device);
            return 0;
        }
    }

    if (g_strcmp0 (fstype, "ext2") == 0 || g_strcmp0 (fstype, "ext3") == 0
                                        || g_strcmp0 (fstype, "ext4") == 0) {
        BDFSExt4Info* info = bd_fs_ext4_get_info (device, error);
        if (info) {
            size = info->block_size * info->free_blocks;
            bd_fs_ext4_info_free (info);
        }
        return size;

    } else if (g_strcmp0 (fstype, "vfat") == 0) {
        BDFSVfatInfo *info = bd_fs_vfat_get_info (device, error);
        if (info) {
            size = info->cluster_size * info->free_cluster_count;
            bd_fs_vfat_info_free (info);
        }
        return size;
    } else if (g_strcmp0 (fstype, "ntfs") == 0) {
        BDFSNtfsInfo *info = bd_fs_ntfs_get_info (device, error);
        if (info) {
            size = info->free_space;
            bd_fs_ntfs_info_free (info);
        }
        return size;
    } else if (g_strcmp0 (fstype, "reiserfs") == 0) {
        BDFSReiserFSInfo *info = bd_fs_reiserfs_get_info (device, error);
        if (info) {
            size = info->block_size * info->free_blocks;
            bd_fs_reiserfs_info_free (info);
        }
        return size;
    } else if (g_strcmp0 (fstype, "nilfs2") == 0) {
        BDFSNILFS2Info *info = bd_fs_nilfs2_get_info (device, error);
        if (info) {
            size = info->block_size * info->free_blocks;
            bd_fs_nilfs2_info_free (info);
        }
        return size;
    } else if (g_strcmp0 (fstype, "btrfs") == 0) {
        BDFSBtrfsInfo *info = btrfs_get_info (device, error);
        if (info) {
            size = info->free_space;
            bd_fs_btrfs_info_free (info);
        }
        return size;
    } else {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOT_SUPPORTED,
                    "Getting free space on filesystem '%s' is not supported.", fstype);
        return 0;
    }
}

static gboolean query_fs_operation (const gchar *fs_type, BDFsOpType op, gchar **required_utility, BDFsResizeFlags *mode, BDFSMkfsOptionsFlags *options, GError **error) {
    gboolean ret;
    const BDFSInfo *fsinfo = NULL;
    const gchar* op_name = NULL;
    const gchar* exec_util = NULL;

    if (required_utility != NULL)
        *required_utility = NULL;

    if (mode != NULL)
        *mode = 0;

    if (options != NULL)
        *options = 0;

    fsinfo = get_fs_info (fs_type);
    if (fsinfo != NULL) {
        switch (op) {
            case BD_FS_MKFS:
                op_name = "Creating";
                exec_util = fsinfo->mkfs_util;
                break;
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
            case BD_FS_UUID:
                op_name = "Setting UUID of";
                exec_util = fs_info->uuid_util;
                break;
            case BD_FS_GET_SIZE:
                op_name = "Getting size of";
                exec_util = fsinfo->info_util;
                break;
            case BD_FS_GET_FREE_SPACE:
                op_name = "Getting free space on";
                exec_util = fsinfo->info_util;
                break;
            default:
                g_assert_not_reached ();
        }
    }

    if (fsinfo == NULL || exec_util == NULL) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOT_SUPPORTED,
                     "%s filesystem '%s' is not supported.", op_name, fs_type);
        return FALSE;
    }

    if (mode != NULL)
        *mode = fsinfo->resize_mode;

    if (options != NULL)
        *options = fsinfo->mkfs_options;

    if (strlen(exec_util) == 0) { /* empty string if no util needed */
        return TRUE;
    }

    ret = bd_utils_check_util_version (exec_util, NULL, "", NULL, NULL);
    if (!ret && required_utility != NULL)
        *required_utility = g_strdup (exec_util);

    return ret;
}

/**
 * bd_fs_can_mkfs:
 * @type: the filesystem type to be tested for installed mkfs support
 * @options: (out): flags for allowed mkfs options (i.e. support for setting label or UUID when creating the filesystem)
 * @required_utility: (out) (transfer full): the utility binary which is required for creating (if missing returns %FALSE but no @error)
 * @error: (out): place to store error (if any)
 *
 * Searches for the required utility to create the given filesystem and returns whether
 * it is installed. The options flags indicate what additional options can be specified for @type.
 * Unknown filesystems result in errors.
 *
 * Returns: whether filesystem mkfs tool is available
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_QUERY
 */
gboolean bd_fs_can_mkfs (const gchar *type, BDFSMkfsOptionsFlags *options, gchar **required_utility, GError **error) {
    return query_fs_operation (type, BD_FS_MKFS, required_utility, NULL, options, error);
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
    return query_fs_operation (type, BD_FS_RESIZE, required_utility, mode, NULL, error);
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
    return query_fs_operation (type, BD_FS_CHECK, required_utility, NULL, NULL, error);
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
    return query_fs_operation (type, BD_FS_REPAIR, required_utility, NULL, NULL, error);
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
    return query_fs_operation (type, BD_FS_LABEL, required_utility, NULL, NULL, error);
}

/**
 * bd_fs_can_set_uuid:
 * @type: the filesystem type to be tested for installed UUID support
 * @required_utility: (out) (transfer full): the utility binary which is required for setting UUID (if missing i.e. return FALSE but no error)
 * @error: (out): place to store error (if any)
 *
 * Searches for the required utility to set the UUID of the given filesystem and returns whether
 * it is installed.
 * Unknown filesystems or filesystems which do not support setting the UUID result in errors.
 *
 * Returns: whether setting filesystem UUID is available
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_QUERY
 */
gboolean bd_fs_can_set_uuid (const gchar *type, gchar **required_utility, GError **error) {
    return query_fs_operation (type, BD_FS_UUID, required_utility, NULL, NULL, error);
}

/**
 * bd_fs_can_get_size:
 * @type: the filesystem type to be tested for installed size querying support
 * @required_utility: (out) (transfer full): the utility binary which is required
 *                                           for size querying (if missing i.e. return FALSE but no error)
 * @error: (out): place to store error (if any)
 *
 * Searches for the required utility to get size of the given filesystem and
 * returns whether it is installed.
 * Unknown filesystems or filesystems which do not support size querying result in errors.
 *
 * Returns: whether getting filesystem size is available
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_QUERY
 */
gboolean bd_fs_can_get_size (const gchar *type, gchar **required_utility, GError **error) {
    return query_fs_operation (type, BD_FS_GET_SIZE, required_utility, NULL, NULL, error);
}

/**
 * bd_fs_can_get_free_space:
 * @type: the filesystem type to be tested for installed free space querying support
 * @required_utility: (out) (transfer full): the utility binary which is required
 *                                           for free space querying (if missing i.e. return FALSE but no error)
 * @error: (out): place to store error (if any)
 *
 * Searches for the required utility to get free space of the given filesystem and
 * returns whether it is installed.
 * Unknown filesystems or filesystems which do not support free space querying result in errors.
 *
 * Returns: whether getting filesystem free space is available
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_QUERY
 */
gboolean bd_fs_can_get_free_space (const gchar *type, gchar **required_utility, GError **error) {
    /* some filesystems can't tell us free space even if we have the tools */
    if (g_strcmp0 (type, "xfs") == 0 || g_strcmp0 (type, "f2fs") == 0 || g_strcmp0 (type, "exfat") == 0 || g_strcmp0 (type, "udf") == 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOT_SUPPORTED,
                     "Getting free space on filesystem '%s' is not supported.", type);
        return FALSE;
    }

    return query_fs_operation (type, BD_FS_GET_FREE_SPACE, required_utility, NULL, NULL, error);
}

static gboolean fs_freeze (const char *mountpoint, gboolean freeze, GError **error) {
    gint fd = -1;
    gint status = 0;

    if (!bd_fs_is_mountpoint (mountpoint, error)) {
        if (*error != NULL) {
            g_prefix_error (error, "Failed to check mountpoint '%s': ", mountpoint);
            return FALSE;
        } else {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOT_MOUNTED,
                         "'%s' doesn't appear to be a mountpoint.", mountpoint);
            return FALSE;
        }
    }

    fd = open (mountpoint, O_RDONLY);
    if (fd == -1) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to open the mountpoint '%s'", mountpoint);
        return FALSE;
    }

    if (freeze)
        status = ioctl (fd, FIFREEZE, 0);
    else
        status = ioctl (fd, FITHAW, 0);

    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to %s '%s': %m.", freeze ? "freeze" : "unfreeze", mountpoint);
        close (fd);
        return FALSE;
    }

    close (fd);

    return TRUE;
}

/**
 * bd_fs_freeze:
 * @mountpoint: mountpoint of the device (filesystem) to freeze
 * @error: (out): place to store error (if any)
 *
 * Freezes filesystem mounted on @mountpoint. The filesystem must
 * support freezing.
 *
 * Returns: whether @mountpoint was successfully freezed or not
 *
 */
gboolean bd_fs_freeze (const gchar *mountpoint, GError **error) {
    return fs_freeze (mountpoint, TRUE, error);
}

/**
 * bd_fs_unfreeze:
 * @mountpoint: mountpoint of the device (filesystem) to un-freeze
 * @error: (out): place to store error (if any)
 *
 * Un-freezes filesystem mounted on @mountpoint. The filesystem must
 * support freezing.
 *
 * Returns: whether @mountpoint was successfully unfreezed or not
 *
 */
gboolean bd_fs_unfreeze (const gchar *mountpoint, GError **error) {
    return fs_freeze (mountpoint, FALSE, error);
}

extern BDExtraArg** bd_fs_exfat_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra);
extern BDExtraArg** bd_fs_ext2_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra);
extern BDExtraArg** bd_fs_ext3_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra);
extern BDExtraArg** bd_fs_ext4_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra);
extern BDExtraArg** bd_fs_f2fs_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra);
extern BDExtraArg** bd_fs_nilfs2_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra);
extern BDExtraArg** bd_fs_ntfs_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra);
extern BDExtraArg** bd_fs_reiserfs_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra);
extern BDExtraArg** bd_fs_vfat_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra);
extern BDExtraArg** bd_fs_xfs_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra);
extern BDExtraArg** bd_fs_btrfs_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra);
extern BDExtraArg** bd_fs_udf_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra);

/**
 * bd_fs_mkfs:
 * @device: the device to create the new filesystem on
 * @fstype: name of the filesystem to create (e.g. "ext4")
 * @options: additional options like label or UUID for the filesystem
 * @extra: (allow-none) (array zero-terminated=1): extra mkfs options not provided in @options
 * @error: (out): place to store error (if any)
 *
 * This is a helper function for creating filesystems with extra options.
 * This is the same as running a filesystem-specific function like %bd_fs_ext4_mkfs
 * and manually specifying the extra command line options. %BDFsMkfsOptions
 * removes the need to specify supported options for selected filesystems,
 * make sure to check whether @fstype supports these options (see %bd_fs_can_mkfs)
 * for details.
 *
 * When specifying additional mkfs options using @extra, it's caller's
 * responsibility to make sure these options do not conflict with options
 * specified using @options. Extra options are added after the @options and
 * there are no additional checks for duplicate and/or conflicting options.
 *
 * Returns: whether @fstype was successfully created on @device or not.
 *
 * Tech category: %BD_FS_TECH_GENERIC-%BD_FS_TECH_MODE_CREATE
 *
 */
gboolean bd_fs_mkfs (const gchar *device, const gchar *fstype, BDFSMkfsOptions *options, const BDExtraArg **extra, GError **error) {
    BDExtraArg **extra_args = NULL;
    gboolean ret = FALSE;

    if (g_strcmp0 (fstype, "exfat") == 0) {
        extra_args = bd_fs_exfat_mkfs_options (options, extra);
        ret = bd_fs_exfat_mkfs (device, (const BDExtraArg **) extra_args, error);
    } else if (g_strcmp0 (fstype, "ext2") == 0) {
        extra_args = bd_fs_ext2_mkfs_options (options, extra);
        ret = bd_fs_ext2_mkfs (device, (const BDExtraArg **) extra_args, error);
    } else if (g_strcmp0 (fstype, "ext3") == 0) {
        extra_args = bd_fs_ext3_mkfs_options (options, extra);
        ret = bd_fs_ext3_mkfs (device, (const BDExtraArg **) extra_args, error);
    } else if (g_strcmp0 (fstype, "ext4") == 0) {
        extra_args = bd_fs_ext4_mkfs_options (options, extra);
        ret = bd_fs_ext4_mkfs (device, (const BDExtraArg **) extra_args, error);
    } else if (g_strcmp0 (fstype, "f2fs") == 0) {
        extra_args = bd_fs_f2fs_mkfs_options (options, extra);
        ret = bd_fs_f2fs_mkfs (device, (const BDExtraArg **) extra_args, error);
    } else if (g_strcmp0 (fstype, "nilfs2") == 0) {
        extra_args = bd_fs_nilfs2_mkfs_options (options, extra);
        ret = bd_fs_nilfs2_mkfs (device, (const BDExtraArg **) extra_args, error);
    } else if (g_strcmp0 (fstype, "ntfs") == 0) {
        extra_args = bd_fs_ntfs_mkfs_options (options, extra);
        ret = bd_fs_ntfs_mkfs (device, (const BDExtraArg **) extra_args, error);
    } else if (g_strcmp0 (fstype, "reiserfs") == 0) {
        extra_args = bd_fs_reiserfs_mkfs_options (options, extra);
        ret = bd_fs_reiserfs_mkfs (device, (const BDExtraArg **) extra_args, error);
    } else if (g_strcmp0 (fstype, "vfat") == 0) {
        extra_args = bd_fs_vfat_mkfs_options (options, extra);
        ret = bd_fs_vfat_mkfs (device, (const BDExtraArg **) extra_args, error);
    } else if (g_strcmp0 (fstype, "xfs") == 0) {
        extra_args = bd_fs_xfs_mkfs_options (options, extra);
        ret = bd_fs_xfs_mkfs (device, (const BDExtraArg **) extra_args, error);
    } else if (g_strcmp0 (fstype, "btrfs") == 0) {
        extra_args = bd_fs_btrfs_mkfs_options (options, extra);
        ret = bd_fs_btrfs_mkfs (device, (const BDExtraArg **) extra_args, error);
    } else if (g_strcmp0 (fstype, "udf") == 0) {
        extra_args = bd_fs_udf_mkfs_options (options, extra);
        ret = bd_fs_udf_mkfs (device, NULL, NULL, 0, (const BDExtraArg **) extra_args, error);
    } else {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_NOT_SUPPORTED,
                     "Filesystem '%s' is not supported.", fstype);
        return FALSE;
    }

    for (BDExtraArg **arg_p = extra_args; *arg_p; arg_p++)
        bd_extra_arg_free (*arg_p);
    g_free (extra_args);

    return ret;
}
