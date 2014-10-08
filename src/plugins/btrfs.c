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
#include <sizes.h>

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

static BDBtrfsDeviceInfo* get_device_info_from_match (GMatchInfo *match_info) {
    BDBtrfsDeviceInfo *ret = g_new(BDBtrfsDeviceInfo, 1);
    gchar *item = NULL;
    gchar *error_message = NULL;

    item = g_match_info_fetch_named (match_info, "id");
    ret->id = g_ascii_strtoull (item, NULL, 0);
    g_free (item);

    ret->path = g_match_info_fetch_named (match_info, "path");

    item = g_match_info_fetch_named (match_info, "size");
    ret->size = bd_utils_size_from_spec (item, &error_message);
    g_free (item);
    if (error_message)
        g_warning (error_message);
    g_free (error_message);
    error_message = NULL;

    item = g_match_info_fetch_named (match_info, "used");
    ret->used = bd_utils_size_from_spec (item, &error_message);
    g_free (item);
    if (error_message)
        g_warning (error_message);
    g_free (error_message);
    error_message = NULL;

    return ret;
}

static BDBtrfsSubvolumeInfo* get_subvolume_info_from_match (GMatchInfo *match_info) {
    BDBtrfsSubvolumeInfo *ret = g_new(BDBtrfsSubvolumeInfo, 1);
    gchar *item = NULL;

    item = g_match_info_fetch_named (match_info, "id");
    ret->id = g_ascii_strtoull (item, NULL, 0);
    g_free (item);

    item = g_match_info_fetch_named (match_info, "parent_id");
    ret->parent_id = g_ascii_strtoull (item, NULL, 0);
    g_free (item);

    ret->path = g_match_info_fetch_named (match_info, "path");

    return ret;
}

static BDBtrfsFilesystemInfo* get_filesystem_info_from_match (GMatchInfo *match_info) {
    BDBtrfsFilesystemInfo *ret = g_new(BDBtrfsFilesystemInfo, 1);
    gchar *item = NULL;
    gchar *error_message = NULL;

    ret->label = g_match_info_fetch_named (match_info, "label");
    ret->uuid = g_match_info_fetch_named (match_info, "uuid");

    item = g_match_info_fetch_named (match_info, "num_devices");
    ret->num_devices = g_ascii_strtoull (item, NULL, 0);
    g_free (item);

    item = g_match_info_fetch_named (match_info, "used");
    ret->used = bd_utils_size_from_spec (item, &error_message);
    g_free (item);
    if (error_message)
        g_warning (error_message);
    g_free (error_message);
    error_message = NULL;

    return ret;
}

/**
 * bd_btrfs_create_volume:
 * @devices: (array zero-terminated=1): list of devices to create btrfs volume from
 * @label: (allow-none): label for the volume
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

/**
 * bd_btrfs_set_default_subvolume:
 * @mountpoint: mountpoint of the volume to set the default subvolume ID of
 * @subvol_id: ID of the subvolume to be set as the default subvolume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @mountpoint volume's default subvolume was correctly set
 * to @subvol_id or not
 */
gboolean bd_btrfs_set_default_subvolume (gchar *mountpoint, guint64 subvol_id, gchar **error_message) {
    gchar *argv[6] = {"btrfs", "subvol", "set-default", NULL, mountpoint, NULL};
    gboolean ret = FALSE;

    if (!path_is_mountpoint (mountpoint)) {
        *error_message = g_strdup_printf ("%s not mounted", mountpoint);
        return FALSE;
    }

    argv[3] = g_strdup_printf ("%"G_GUINT64_FORMAT, subvol_id);
    ret = bd_utils_exec_and_report_error (argv, error_message);
    g_free (argv[3]);

    return ret;
}

/**
 * bd_btrfs_create_snapshot:
 * @source: path to source subvolume
 * @dest: path to new snapshot volume
 * @ro: whether the snapshot should be read-only
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @dest snapshot of @source was successfully created or not
 */
gboolean bd_btrfs_create_snapshot (gchar *source, gchar *dest, gboolean ro, gchar **error_message) {
    gchar *argv[7] = {"btrfs", "subvol", "snapshot", NULL, NULL, NULL, NULL};
    guint next_arg = 3;

    if (ro) {
        argv[next_arg] = "-r";
        next_arg++;
    }
    argv[next_arg] = source;
    next_arg++;
    argv[next_arg] = dest;

    return bd_utils_exec_and_report_error (argv, error_message);
}

/**
 * bd_btrfs_list_devices:
 * @device: a device that is part of the queried btrfs volume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: (array zero-terminated=1): information about the devices that are part of the btrfs volume
 * containing @device or %NULL in case of error
 */
BDBtrfsDeviceInfo** bd_btrfs_list_devices (gchar *device, gchar **error_message) {
    gchar *argv[5] = {"btrfs", "filesystem", "show", device, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar const * const pattern = "devid[ \\t]+(?P<id>\\d+)[ \\t]+" \
                                  "size[ \\t]+(?P<size>\\S+)[ \\t]+" \
                                  "used[ \\t]+(?P<used>\\S+)[ \\t]+" \
                                  "path[ \\t]+(?P<path>\\S+)\n";
    GError *error = NULL;
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    guint8 i = 0;
    GPtrArray *dev_infos = g_ptr_array_new ();
    BDBtrfsDeviceInfo** ret = NULL;

    regex = g_regex_new (pattern, G_REGEX_EXTENDED, 0, &error);
    if (!regex) {
        g_warning ("Failed to create new GRegex");
        *error_message = g_strdup ("Failed to create new GRegex");
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (argv, &output, error_message);
    if (!success)
        /* error_message is already populated from the previous call */
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (line_p = lines; *line_p; line_p++) {
        success = g_regex_match (regex, *line_p, 0, &match_info);
        if (!success) {
            g_match_info_free (match_info);
            continue;
        }

        g_ptr_array_add (dev_infos, get_device_info_from_match (match_info));
        g_match_info_free (match_info);
    }

    g_strfreev (lines);

    if (dev_infos->len == 0) {
        *error_message = g_strdup ("Failed to parse information about devices");
        return NULL;
    }

    /* now create the return value -- NULL-terminated array of BDBtrfsDeviceInfo */
    ret = g_new (BDBtrfsDeviceInfo*, dev_infos->len + 1);
    for (i=0; i < dev_infos->len; i++)
        ret[i] = (BDBtrfsDeviceInfo*) g_ptr_array_index (dev_infos, i);
    ret[i] = NULL;

    g_ptr_array_free (dev_infos, FALSE);

    return ret;
}

/**
 * bd_btrfs_list_subvolumes:
 * @mountpoint: a mountpoint of the queried btrfs volume
 * @snapshots_only: whether to list only snapshot subvolumes or not
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: (array zero-terminated=1): information about the subvolumes that are part of the btrfs volume
 * mounted at @mountpoint or %NULL in case of error
 */
BDBtrfsSubvolumeInfo** bd_btrfs_list_subvolumes (gchar *mountpoint, gboolean snapshots_only, gchar **error_message) {
    gchar *argv[7] = {"btrfs", "subvol", "list", "-p", NULL, NULL, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar const * const pattern = "ID\\s+(?P<id>\\d+)\\s+gen\\s+\\d+\\s+(cgen\\s+\\d+\\s+)?" \
                                  "parent\\s+(?P<parent_id>\\d+)\\s+top\\s+level\\s+\\d+\\s+" \
                                  "(otime\\s+\\d{4}-\\d{2}-\\d{2}\\s+\\d\\d:\\d\\d:\\d\\d\\s+)?"\
                                  "path\\s+(?P<path>\\S+)";
    GError *error = NULL;
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    guint8 i = 0;
    GPtrArray *subvol_infos = g_ptr_array_new ();
    BDBtrfsSubvolumeInfo** ret = NULL;

    if (snapshots_only) {
        argv[4] = "-s";
        argv[5] = mountpoint;
    } else
        argv[4] = mountpoint;

    regex = g_regex_new (pattern, G_REGEX_EXTENDED, 0, &error);
    if (!regex) {
        g_warning ("Failed to create new GRegex");
        *error_message = g_strdup ("Failed to create new GRegex");
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (argv, &output, error_message);
    if (!success)
        /* error_message is already populated from the call above or simply no output*/
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (line_p = lines; *line_p; line_p++) {
        success = g_regex_match (regex, *line_p, 0, &match_info);
        if (!success) {
            g_match_info_free (match_info);
            continue;
        }

        g_ptr_array_add (subvol_infos, get_subvolume_info_from_match (match_info));
        g_match_info_free (match_info);
    }

    g_strfreev (lines);

    if (subvol_infos->len == 0) {
        *error_message = g_strdup ("Failed to parse information about subvolumes");
        return NULL;
    }

    /* now create the return value -- NULL-terminated array of BDBtrfsSubvolumeInfo */
    ret = g_new (BDBtrfsSubvolumeInfo*, subvol_infos->len + 1);
    for (i=0; i < subvol_infos->len; i++)
        ret[i] = (BDBtrfsSubvolumeInfo*) g_ptr_array_index (subvol_infos, i);
    ret[i] = NULL;

    g_ptr_array_free (subvol_infos, FALSE);

    return ret;
}

/**
 * bd_btrfs_filesystem_info:
 * @device: a device that is part of the queried btrfs volume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: information about the @device's volume's filesystem or %NULL in case of error
 */
BDBtrfsFilesystemInfo* bd_btrfs_filesystem_info (gchar *device, gchar **error_message) {
    gchar *argv[5] = {"btrfs", "filesystem", "show", device, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    gchar const * const pattern = "Label:\\s+'(?P<label>\\S+)'\\s+" \
                                  "uuid:\\s+(?P<uuid>\\S+)\\s+" \
                                  "Total\\sdevices\\s+(?P<num_devices>\\d+)\\s+" \
                                  "FS\\sbytes\\sused\\s+(?P<used>\\S+)";
    GError *error = NULL;
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    BDBtrfsFilesystemInfo *ret = NULL;

    regex = g_regex_new (pattern, G_REGEX_EXTENDED, 0, &error);
    if (!regex) {
        g_warning ("Failed to create new GRegex");
        *error_message = g_strdup ("Failed to create new GRegex");
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (argv, &output, error_message);
    if (!success)
        /* error_message is already populated from the call above or just empty
           output */
        return NULL;

    success = g_regex_match (regex, output, 0, &match_info);
    if (!success) {
        g_regex_unref (regex);
        g_match_info_free (match_info);
        return NULL;
    }

    g_regex_unref (regex);
    ret = get_filesystem_info_from_match (match_info);
    g_match_info_free (match_info);

    return ret;
}

/**
 * bd_btrfs_mkfs:
 * @devices: (array zero-terminated=1): list of devices to create btrfs volume from
 * @label: (allow-none): label for the volume
 * @data_level: (allow-none): RAID level for the data or %NULL to use the default
 * @md_level: (allow-none): RAID level for the metadata or %NULL to use the default
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the new btrfs volume was created from @devices or not
 *
 * See mkfs.btrfs(8) for details about @data_level, @md_level and btrfs in general.
 */
gboolean bd_btrfs_mkfs (gchar **devices, gchar *label, gchar *data_level, gchar *md_level, gchar **error_message) {
    return bd_btrfs_create_volume (devices, label, data_level, md_level, error_message);
}

/**
 * bd_btrfs_resize:
 * @mountpoint: a mountpoint of the to be resized btrfs filesystem
 * @size: requested new size
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @mountpoint filesystem was successfully resized to @size
 * or not
 */
gboolean bd_btrfs_resize (gchar *mountpoint, guint64 size, gchar **error_message) {
    gchar *argv[6] = {"btrfs", "filesystem", "resize", NULL, mountpoint, NULL};
    gboolean ret = FALSE;

    argv[3] = g_strdup_printf ("%"G_GUINT64_FORMAT, size);
    ret = bd_utils_exec_and_report_error (argv, error_message);
    g_free (argv[3]);

    return ret;
}

/**
 * bd_btrfs_check:
 * @device: a device that is part of the checked btrfs volume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the filesystem was successfully checked or not
 */
gboolean bd_btrfs_check (gchar *device, gchar **error_message) {
    gchar *argv[4] = {"btrfs", "check", device, NULL};

    return bd_utils_exec_and_report_error (argv, error_message);
}

/**
 * bd_btrfs_repair:
 * @device: a device that is part of the to be repaired btrfs volume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the filesystem was successfully checked and repaired or not
 */
gboolean bd_btrfs_repair (gchar *device, gchar **error_message) {
    gchar *argv[5] = {"btrfs", "check", "--repair", device, NULL};

    return bd_utils_exec_and_report_error (argv, error_message);
}

/**
 * bd_btrfs_change_label:
 * @mountpoint: a mountpoint of the btrfs filesystem to change label of
 * @label: new label for the filesystem
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the label of the @mountpoint filesystem was successfully set
 * to @label or not
 */
gboolean bd_btrfs_change_label (gchar *mountpoint, gchar *label, gchar **error_message) {
    gchar *argv[6] = {"btrfs", "filesystem", "label", mountpoint, label, NULL};

    return bd_utils_exec_and_report_error (argv, error_message);
}
