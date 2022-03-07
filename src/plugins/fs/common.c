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
#include <blkid.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <uuid.h>

#include <blockdev/utils.h>

#include "fs.h"
#include "common.h"

gint __attribute__ ((visibility ("hidden")))
synced_close (gint fd) {
    gint ret = 0;
    ret = fsync (fd);
    if (close (fd) != 0)
        ret = 1;
    return ret;
}

gboolean __attribute__ ((visibility ("hidden")))
has_fs (blkid_probe probe, const gchar *device, const gchar *fs_type, GError **error) {
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

/**
 * wipe_fs: (skip)
 * @device: the device to wipe signature(s) from
 * @fs_type: (nullable): filesystem type to wipe from the device or %NULL for any
 * @wipe_all: whether to wipe all signatures from the device or not: some filesystems might have
 *            multiple nested signatures, this will make sure to wipe all of them including
 *            those that are different from @fs_type -- e.g. for NTFS we will wipe both the
 *            NTFS signature and the nested DOS partition table created by mkfs.ntfs
 * @error: (out): place to store error
 *
 * Returns: whether the wipe was successful or not
 */
gboolean __attribute__ ((visibility ("hidden")))
wipe_fs (const gchar *device, const gchar *fs_type, gboolean wipe_all, GError **error) {
    blkid_probe probe = NULL;
    gint fd = 0;
    gint status = 0;
    const gchar *value = NULL;
    size_t len = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    guint n_try = 0;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started wiping '%s' signatures from the device '%s'", fs_type, device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    probe = blkid_new_probe ();
    if (!probe) {
        g_set_error (&l_error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", device);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    fd = open (device, O_RDWR|O_CLOEXEC);
    if (fd == -1) {
        g_set_error (&l_error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", device);
        blkid_free_probe (probe);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    for (n_try=5, status=-1; (status != 0) && (n_try > 0); n_try--) {
        status = blkid_probe_set_device (probe, fd, 0, 0);
        if (status != 0)
            g_usleep (100 * 1000); /* microseconds */
    }
    status = blkid_probe_set_device (probe, fd, 0, 0);
    if (status != 0) {
        g_set_error (&l_error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
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
        g_set_error (&l_error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to probe the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    status = blkid_probe_lookup_value (probe, "USAGE", &value, NULL);
    if (status != 0) {
        g_set_error (&l_error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to get signature type for the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (strncmp (value, "filesystem", 10) != 0) {
        g_set_error (&l_error, BD_FS_ERROR, BD_FS_ERROR_INVAL,
                     "The signature on the device '%s' is of type '%s', not 'filesystem'", device, value);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (fs_type) {
        status = blkid_probe_lookup_value (probe, "TYPE", &value, &len);
        if (status != 0) {
            g_set_error (&l_error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to get filesystem type for the device '%s'", device);
            blkid_free_probe (probe);
            synced_close (fd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }

        if (strncmp (value, fs_type, len-1) != 0) {
            g_set_error (&l_error, BD_FS_ERROR, BD_FS_ERROR_INVAL,
                         "The file system type on the device '%s' is '%s', not '%s'", device, value, fs_type);
            blkid_free_probe (probe);
            synced_close (fd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    }

    status = blkid_do_wipe (probe, FALSE);
    if (status != 0) {
        g_set_error (&l_error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to wipe the filesystem signature on the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    blkid_reset_probe (probe);

    if (wipe_all) {
        while (blkid_do_probe (probe) == 0) {
            status = blkid_do_wipe (probe, FALSE);
            if (status != 0) {
                g_set_error (&l_error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to wipe the filesystem signature on the device '%s'", device);
                blkid_free_probe (probe);
                synced_close (fd);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return FALSE;
            }

            blkid_reset_probe (probe);
        }
    }

    blkid_free_probe (probe);
    synced_close (fd);

    bd_utils_report_finished (progress_id, "Completed");

    return TRUE;
}


gboolean __attribute__ ((visibility ("hidden")))
get_uuid_label (const gchar *device, gchar **uuid, gchar **label, GError **error) {
    blkid_probe probe = NULL;
    gint fd = 0;
    gint status = 0;
    const gchar *value = NULL;

    probe = blkid_new_probe ();
    if (!probe) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", device);
        return FALSE;
    }

    fd = open (device, O_RDONLY|O_CLOEXEC);
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

    blkid_probe_enable_partitions (probe, 1);

    status = blkid_do_probe (probe);
    if (status != 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to probe the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        return FALSE;
    }

    status = blkid_probe_has_value (probe, "LABEL");

    if (status == 0)
        *label = g_strdup ("");
    else {
        status = blkid_probe_lookup_value (probe, "LABEL", &value, NULL);
        if (status != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                         "Failed to get label for the device '%s'", device);
            blkid_free_probe (probe);
            synced_close (fd);
            return FALSE;
        }

        if (value)
            *label = g_strdup (value);
        else
            *label = g_strdup ("");
    }

    status = blkid_probe_has_value (probe, "UUID");
    if (status == 0)
        *uuid = g_strdup ("");
    else {
        status = blkid_probe_lookup_value (probe, "UUID", &value, NULL);
        if (status != 0) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                        "Failed to get UUID for the device '%s'", device);
            blkid_free_probe (probe);
            synced_close (fd);
            g_free (label);
            return FALSE;
        }

        if (value)
            *uuid = g_strdup (value);
        else
            *uuid = g_strdup ("");
    }

    blkid_free_probe (probe);
    synced_close (fd);

    return TRUE;
}

gboolean __attribute__ ((visibility ("hidden")))
check_uuid (const gchar *uuid, GError **error) {
    g_autofree gchar *lowercase = NULL;
    gint ret = 0;
    uuid_t uu;

    if (!g_str_is_ascii (uuid)) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UUID_INVALID,
                     "Provided UUID is not a valid RFC-4122 UUID.");
        return FALSE;
    }

    lowercase = g_ascii_strdown (uuid, -1);
    ret = uuid_parse (lowercase, uu);
    if (ret < 0){
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UUID_INVALID,
                     "Provided UUID is not a valid RFC-4122 UUID.");
        return FALSE;
    }

    return TRUE;
}
