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
#include <string.h>
#include <unistd.h>
#include <exec.h>

#include "btrfs.h"

/**
 * SECTION: btrfs
 * @short_description: libblockdev plugin for operations with btrfs devices
 * @title: Btrfs
 * @include: btrfs.h
 *
 * A libblockdev plugin for operations with swap space.
 */

static gboolean path_is_mountpoint (gchar *path) {
    GError *error = NULL;
    gchar *real_path = NULL;
    gchar *slash_p = NULL;
    gchar *file_contents = NULL;
    gchar *pattern = NULL;
    GRegex *regex;
    gboolean success = FALSE;
    gboolean ret = FALSE;

    /* try to resolve symlink and store the real path*/
    real_path = g_file_read_link (path, &error);
    if (!real_path) {
        real_path = g_strdup (path);
    }

    g_clear_error (&error);

    /* remove trailing slashes */
    if (g_str_has_suffix (real_path, "/")) {
        slash_p = strrchr (real_path, '/');
        while (*slash_p == '/') {
            *slash_p = '\0';
            slash_p--;
        }
    }

    success = g_file_get_contents ("/proc/self/mountinfo", &file_contents, NULL, &error);
    if (!success) {
        g_warning ("Failed to read /proc/self/moutinfo: %s", error->message);
        g_clear_error (&error);
        g_free (real_path);
        return FALSE;
    }

    g_clear_error (&error);

    /* see if the real_path without trailing slashes is in the file_contents */
    pattern = g_strdup_printf ("\\s+%s\\s+", real_path);
    regex = g_regex_new (pattern, 0, 0, &error);
    if (!regex) {
        g_warning ("Failed to create new GRegex: %s", error->message);
        g_clear_error (&error);
        g_free (real_path);
        g_free (pattern);
        return FALSE;
    }
    ret = g_regex_match (regex, file_contents, 0, NULL);
    g_free (file_contents);
    g_free (real_path);
    g_free (pattern);
    g_regex_unref (regex);

    return ret;
}

/**
 * bd_btrfs_create_volume:
 * @devices: (array zero-terminated=1): list of devices to create btrfs volume from
 * @label: label for the volume
 * @data_level: (allow-none): RAID level for the data or %NULL to use the default
 * @md_level: (allow-none): RAID level for the metadata or %NULL to use the default
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the new btrfs volume was created from @devices or not
 *
 * See mkfs.btrfs(8) for details about @data_level, @md_level and btrfs in general.
 */
gboolean bd_btrfs_create_volume (gchar **devices, gchar *label, gchar *data_level, gchar *md_level, gchar **error_message) {
    gchar **device_p = NULL;
    guint8 num_args = 0;
    gchar **argv = NULL;
    guint8 next_arg = 1;
    gboolean success = FALSE;

    if (!devices) {
        *error_message = g_strdup ("No devices given");
        return FALSE;
    }

    for (device_p = devices; *device_p != NULL; device_p++) {
        if (access (*device_p, F_OK) != 0) {
            *error_message = g_strdup_printf ("Device %s does not exist", *device_p);
            return FALSE;
        }
        num_args++;
    }

    if (label)
        num_args += 2;
    if (data_level)
        num_args += 2;
    if (md_level)
        num_args += 2;

    argv = g_new (gchar*, num_args + 2);
    argv[0] = "mkfs.btrfs";
    if (label) {
        argv[next_arg] = "--label";
        next_arg++;
        argv[next_arg] = label;
        next_arg++;
    }
    if (data_level) {
        argv[next_arg] = "--data";
        next_arg++;
        argv[next_arg] = data_level;
        next_arg++;
    }
    if (md_level) {
        argv[next_arg] = "--metadata";
        next_arg++;
        argv[next_arg] = md_level;
        next_arg++;
    }

    for (device_p = devices; next_arg <= num_args; device_p++, next_arg++)
        argv[next_arg] = *device_p;
    argv[next_arg] = NULL;

    success = bd_utils_exec_and_report_error (argv, error_message);
    g_free (argv);
    return success;
}

/**
 * bd_btrfs_add_device:
 * @mountpoint: mountpoint of the btrfs volume to add new device to
 * @device: a device to add to the btrfs volume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @device was successfully added to the @mountpoint btrfs volume or not
 */
gboolean bd_btrfs_add_device (gchar *mountpoint, gchar *device, gchar **error_message) {
    gchar *argv[6] = {"btrfs", "device", "add", device, mountpoint, NULL};
    if (!path_is_mountpoint (mountpoint)) {
        *error_message = g_strdup_printf ("%s not mounted", mountpoint);
        return FALSE;
    }

    return bd_utils_exec_and_report_error (argv, error_message);
}

/**
 * bd_btrfs_remove_device:
 * @mountpoint: mountpoint of the btrfs volume to remove device from
 * @device: a device to remove from the btrfs volume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @device was successfully removed from the @mountpoint btrfs volume or not
 */
gboolean bd_btrfs_remove_device (gchar *mountpoint, gchar *device, gchar **error_message) {
    gchar *argv[6] = {"btrfs", "device", "delete", device, mountpoint, NULL};
    if (!path_is_mountpoint (mountpoint)) {
        *error_message = g_strdup_printf ("%s not mounted", mountpoint);
        return FALSE;
    }

    return bd_utils_exec_and_report_error (argv, error_message);
}

/**
 * bd_btrfs_create_subvolume:
 * @mountpoint: mountpoint of the btrfs volume to create subvolume under
 * @name: name of the subvolume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @mountpoint/@name subvolume was successfully created or not
 */
gboolean bd_btrfs_create_subvolume (gchar *mountpoint, gchar *name, gchar **error_message) {
    gchar *path = NULL;
    gboolean success = FALSE;
    gchar *argv[5] = {"btrfs", "subvol", "create", NULL, NULL};

    if (!path_is_mountpoint (mountpoint)) {
        *error_message = g_strdup_printf ("%s not mounted", mountpoint);
        return FALSE;
    }

    if (g_str_has_suffix (mountpoint, "/"))
        path = g_strdup_printf ("%s%s", mountpoint, name);
    else
        path = g_strdup_printf ("%s/%s", mountpoint, name);
    argv[3] = path;

    success = bd_utils_exec_and_report_error (argv, error_message);
    g_free (path);

    return success;
}

/**
 * bd_btrfs_delete_subvolume:
 * @mountpoint: mountpoint of the btrfs volume to delete subvolume from
 * @name: name of the subvolume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @mountpoint/@name subvolume was successfully deleted or not
 */
gboolean bd_btrfs_delete_subvolume (gchar *mountpoint, gchar *name, gchar **error_message) {
    gchar *path = NULL;
    gboolean success = FALSE;
    gchar *argv[5] = {"btrfs", "subvol", "delete", NULL, NULL};

    if (!path_is_mountpoint (mountpoint)) {
        *error_message = g_strdup_printf ("%s not mounted", mountpoint);
        return FALSE;
    }

    if (g_str_has_suffix (mountpoint, "/"))
        path = g_strdup_printf ("%s%s", mountpoint, name);
    else
        path = g_strdup_printf ("%s/%s", mountpoint, name);
    argv[3] = path;

    success = bd_utils_exec_and_report_error (argv, error_message);
    g_free (path);

    return success;
}

/**
 * bd_btrfs_get_default_subvolume_id:
 * @mountpoint: mountpoint of the volume to get the default subvolume ID of
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: ID of the @mountpoint volume's default subvolume. If 0,
 * @error_message may be set to indicate error
 */
guint64 bd_btrfs_get_default_subvolume_id (gchar *mountpoint, gchar **error_message) {
    GError *error = NULL;
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar *match = NULL;
    guint64 ret = 0;
    gchar *argv[5] = {"btrfs", "subvol", "get-default", mountpoint, NULL};

    regex = g_regex_new ("ID (\\d+) .*", 0, 0, &error);
    if (!regex) {
        g_warning ("Failed to create new GRegex");
        *error_message = g_strdup ("Failed to create new GRegex");
        return 0;
    }

    success = bd_utils_exec_and_capture_output (argv, &output, error_message);
    if (!success) {
        g_regex_unref (regex);
        return 0;
    }

    success = g_regex_match (regex, output, 0, &match_info);
    if (!success) {
        *error_message = g_strdup ("Failed to parse subvolume's ID");
        g_regex_unref (regex);
        g_match_info_free (match_info);
        g_free (output);
        return 0;
    }

    match = g_match_info_fetch (match_info, 1);
    ret = g_ascii_strtoull (match, NULL, 0);

    g_free (match);
    g_match_info_free (match_info);
    g_regex_unref (regex);
    g_free (output);

    return ret;
}

