/*
 * Copyright (C) 2014  Red Hat, Inc.
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

#include <glib.h>
#include <unistd.h>
#include <exec.h>
#include <sizes.h>

#include "mdraid.h"

/**
 * SECTION: mdraid
 * @short_description: libblockdev plugin for basic operations with MD RAID
 * @title: MD RAID
 * @include: mdraid.h
 *
 * A libblockdev plugin for basic operations with MD RAID. Also sizes are in
 * bytes unless specified otherwise.
 */

GQuark bd_md_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-md-error-quark");
}

/**
 * bd_md_get_superblock_size:
 * @size: size of the array
 * @version: (allow-none): metadata version or %NULL to use the current default version
 *
 * Returns: Calculated superblock size for given array @size and metadata @version
 * or default if unsupported @version is used.
 */
guint64 bd_md_get_superblock_size (guint64 size, gchar *version) {
    guint64 headroom = MD_SUPERBLOCK_SIZE;
    guint64 min_headroom = (1 MiB);

    /* mdadm 3.2.4 made a major change in the amount of space used for 1.1 and
     * 1.2 in order to reserve space for reshaping. See commit 508a7f16 in the
     * upstream mdadm repository. */
    if (!version || (g_strcmp0 (version, "1.1") == 0) ||
        (g_strcmp0 (version, "1.2") == 0) || (g_strcmp0 (version, "default") == 0)) {
        /* MDADM: We try to leave 0.1% at the start for reshape
         * MDADM: operations, but limit this to 128Meg (0.1% of 10Gig)
         * MDADM: which is plenty for efficient reshapes
         * NOTE: In the mdadm code this is in 512b sectors. Converted to use MiB */
        headroom = (128 MiB);
        while (((headroom << 10) > size) && (headroom > min_headroom))
            headroom >>= 1;
    }

    return headroom;
}

/**
 * bd_md_create:
 * @device_name: name of the device to create
 * @level: RAID level (as understood by mdadm, see mdadm(8))
 * @disks: (array zero-terminated=1): disks to use for the new RAID (including spares)
 * @spares: number of spare devices
 * @version: (allow-none): metadata version
 * @bitmap: whether to create an internal bitmap on the device or not
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the new MD RAID device @device_name was successfully created or not
 */
gboolean bd_md_create (gchar *device_name, gchar *level, gchar **disks, guint64 spares, gchar *version, gboolean bitmap, GError **error) {
    gchar **argv = NULL;
    /* ["mdadm", "create", device, "--run", "level", "raid-devices",...] */
    guint argv_len = 6;
    guint argv_top = 0;
    guint i = 0;
    guint num_disks = 0;
    gchar *level_str = NULL;
    gchar *rdevices_str = NULL;
    gchar *spares_str = NULL;
    gchar *version_str = NULL;
    gboolean ret = FALSE;

    if (spares != 0)
        argv_len++;
    if (version)
        argv_len++;
    if (bitmap)
        argv_len++;
    num_disks = g_strv_length (disks);
    argv_len += num_disks;

    argv = g_new (gchar*, argv_len + 1);

    level_str = g_strdup_printf ("--level=%s", level);
    rdevices_str = g_strdup_printf ("--raid-devices=%"G_GUINT64_FORMAT, (num_disks - spares));

    argv[argv_top++] = "mdadm";
    argv[argv_top++] = "--create";
    argv[argv_top++] = device_name;
    argv[argv_top++] = "--run";
    argv[argv_top++] = level_str;
    argv[argv_top++] = rdevices_str;

    if (spares != 0) {
        spares_str = g_strdup_printf ("--spare-devices=%"G_GUINT64_FORMAT, spares);
        argv[argv_top++] = spares_str;
    }
    if (version) {
        version_str = g_strdup_printf ("--metadata=%s", version);
        argv[argv_top++] = version_str;
    }
    if (bitmap)
        argv[argv_top++] = "--bitmap=internal";

    for (i=0; i < num_disks; i++)
        argv[argv_top++] = disks[i];
    argv[argv_top] = NULL;

    ret = bd_utils_exec_and_report_error (argv, error);

    g_free (level_str);
    g_free (rdevices_str);
    g_free (spares_str);
    g_free (version_str);
    g_free (argv);

    return ret;
}

/**
 * bd_md_destroy:
 * @device: device to destroy MD RAID metadata on
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the MD RAID metadata was successfully destroyed on @device or not
 */
gboolean bd_md_destroy (gchar *device, GError **error) {
    gchar *argv[] = {"mdadm", "--zero-superblock", device, NULL};

    return bd_utils_exec_and_report_error (argv, error);
}

/**
 * bd_md_deactivate:
 * @device_name: name of the RAID device to deactivate
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the RAID device @device_name was successfully deactivated or not
 */
gboolean bd_md_deactivate (gchar *device_name, GError **error) {
    gchar *argv[] = {"mdadm", "--stop", device_name, NULL};
    gchar *dev_md_path = NULL;
    gboolean ret = FALSE;

    /* XXX: mdadm doesn't recognize the user-defined name without the '/dev/md/'
       prefix, but its own device (e.g. md121) is okay */
    dev_md_path = g_strdup_printf ("/dev/md/%s", device_name);
    if (access (dev_md_path, F_OK) == 0)
        argv[2] = dev_md_path;

    ret = bd_utils_exec_and_report_error (argv, error);
    g_free (dev_md_path);

    return ret;
}

/**
 * bd_md_activate:
 * @device_name: name of the RAID device to activate
 * @members: (allow-none) (array zero-terminated=1): member devices to be considered for @device activation
 * @uuid: (allow-none): UUID (in the MD RAID format!) of the MD RAID to activate
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the MD RAID @device was successfully activated or not
 *
 * Note: either @members or @uuid (or both) have to be specified.
 */
gboolean bd_md_activate (gchar *device_name, gchar **members, gchar *uuid, GError **error) {
    guint64 num_members = members ? g_strv_length (members) : 0;
    gchar **argv = NULL;
    gchar *uuid_str = NULL;
    guint argv_top = 0;
    guint i = 0;
    gboolean ret = FALSE;

    /* mdadm, --assemble, device_name, --run, --uuid=uuid, member1, member2,..., NULL*/
    if (uuid) {
        argv = g_new (gchar*, num_members + 6);
        uuid_str = g_strdup_printf ("--uuid=%s", uuid);
    }
    else
        argv = g_new (gchar*, num_members + 5);

    argv[argv_top++] = "mdadm";
    argv[argv_top++] = "--assemble";
    argv[argv_top++] = device_name;
    argv[argv_top++] = "--run";
    if (uuid)
        argv[argv_top++] = uuid_str;
    for (i=0; i < num_members; i++)
        argv[argv_top++] = members[i];
    argv[argv_top] = NULL;

    ret = bd_utils_exec_and_report_error (argv, error);

    g_free (uuid_str);
    g_free (argv);

    return ret;
}

/**
 * bd_md_nominate:
 * @device: device to nominate (add to its appropriate RAID) as a MD RAID device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully nominated (added to its
 * appropriate RAID) or not
 *
 * Note: may start the MD RAID if it becomes ready by adding @device.
 */
gboolean bd_md_nominate (gchar *device, GError **error) {
    gchar *argv[] = {"mdadm", "--incremental", "--quiet", "--run", device, NULL};

    return bd_utils_exec_and_report_error (argv, error);
}

/**
 * bd_md_denominate:
 * @device: device to denominate (remove from its appropriate RAID) as a MD RAID device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully denominated (added to its
 * appropriate RAID) or not
 *
 * Note: may start the MD RAID if it becomes ready by adding @device.
 */
gboolean bd_md_denominate (gchar *device, GError **error) {
    gchar *argv[] = {"mdadm", "--incremental", "--fail", device, NULL};

    /* XXX: stupid mdadm! --incremental --fail requires "sda1" instead of "/dev/sda1" */
    if (g_str_has_prefix (device, "/dev/"))
        argv[3] = (device + 5);

    return bd_utils_exec_and_report_error (argv, error);
}
