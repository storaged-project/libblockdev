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
