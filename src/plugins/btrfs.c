/*
 * Copyright (C) 2014  Red Hat, Inc.
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
#include <string.h>
#include <unistd.h>
#include <blockdev/utils.h>
#include <bs_size.h>
#include <btrfsutil.h>

#include "btrfs.h"
#include "check_deps.h"

#define BTRFS_MIN_VERSION "3.18.2"
// HACK: btrfsutil issue?
#define BTRFS_FSID_SIZE 16

/**
 * SECTION: btrfs
 * @short_description: plugin for operations with BTRFS devices
 * @title: BTRFS
 * @include: btrfs.h
 *
 * A plugin for operations with btrfs devices.
 */

/**
 * bd_btrfs_error_quark: (skip)
 */
GQuark bd_btrfs_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-btrfs-error-quark");
}

BDBtrfsDeviceInfo* bd_btrfs_device_info_copy (BDBtrfsDeviceInfo *info) {
    if (info == NULL)
        return NULL;

    BDBtrfsDeviceInfo *new_info = g_new0 (BDBtrfsDeviceInfo, 1);

    new_info->id = info->id;
    new_info->path = g_strdup (info->path);
    new_info->size = info->size;
    new_info->used = info->used;

    return new_info;
}

void bd_btrfs_device_info_free (BDBtrfsDeviceInfo *info) {
    if (info == NULL)
        return;

    g_free (info->path);
    g_free (info);
}

BDBtrfsSubvolumeInfo* bd_btrfs_subvolume_info_copy (BDBtrfsSubvolumeInfo *info) {
    if  (info == NULL)
        return NULL;

    BDBtrfsSubvolumeInfo *new_info = g_new0 (BDBtrfsSubvolumeInfo, 1);

    new_info->id = info->id;
    new_info->parent_id = info->parent_id;
    new_info->path = g_strdup (info->path);

    return new_info;
}

void bd_btrfs_subvolume_info_free (BDBtrfsSubvolumeInfo *info) {
    if (info == NULL)
        return;

    g_free (info->path);
    g_free (info);
}

BDBtrfsFilesystemInfo* bd_btrfs_filesystem_info_copy (BDBtrfsFilesystemInfo *info) {
    if (info == NULL)
        return NULL;

    BDBtrfsFilesystemInfo *new_info = g_new0 (BDBtrfsFilesystemInfo, 1);

    new_info->label = g_strdup (info->label);
    new_info->uuid = g_strdup (info->uuid);
    new_info->num_devices = info->num_devices;
    new_info->used = info->used;

    return new_info;
}

void bd_btrfs_filesystem_info_free (BDBtrfsFilesystemInfo *info) {
    if (info == NULL)
        return;

    g_free (info->label);
    g_free (info->uuid);
    g_free (info);
}

static volatile guint avail_deps = 0;
static volatile guint avail_module_deps = 0;
static GMutex deps_check_lock;

#define DEPS_BTRFS 0
#define DEPS_BTRFS_MASK (1 << DEPS_BTRFS)
#define DEPS_LAST 1

static const UtilDep deps[DEPS_LAST] = {
    {"btrfs", BTRFS_MIN_VERSION, NULL, "[Bb]trfs.* v([\\d\\.]+)"},
};

#define MODULE_DEPS_BTRFS 0
#define MODULE_DEPS_BTRFS_MASK (1 << MODULE_DEPS_BTRFS)
#define MODULE_DEPS_LAST 1

static const gchar*const module_deps[MODULE_DEPS_LAST] = { "btrfs" };


/**
 * bd_btrfs_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_btrfs_init (void) {
    /* nothing to do here */
    return TRUE;
};

/**
 * bd_btrfs_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_btrfs_close (void) {
    /* nothing to do here */
}



/**
 * bd_btrfs_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_btrfs_is_tech_avail (BDBtrfsTech tech G_GNUC_UNUSED, guint64 mode G_GNUC_UNUSED, GError **error) {
    /* all tech-mode combinations are supported by this implementation of the plugin */
    return check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error) &&
           check_module_deps (&avail_module_deps, MODULE_DEPS_BTRFS_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error);
}

static BDBtrfsDeviceInfo* get_device_info_from_match (GMatchInfo *match_info) {
    BDBtrfsDeviceInfo *ret = g_new(BDBtrfsDeviceInfo, 1);
    gchar *item = NULL;
    BSSize size = NULL;
    BSError *error = NULL;

    item = g_match_info_fetch_named (match_info, "id");
    ret->id = g_ascii_strtoull (item, NULL, 0);
    g_free (item);

    ret->path = g_match_info_fetch_named (match_info, "path");

    item = g_match_info_fetch_named (match_info, "size");
    if (item) {
        size = bs_size_new_from_str (item, &error);
        if (size) {
            ret->size = bs_size_get_bytes (size, NULL, &error);
            bs_size_free (size);
        }
        if (error)
            bd_utils_log_format (BD_UTILS_LOG_WARNING, "%s", error->msg);
        bs_clear_error (&error);
        g_free (item);
    }

    item = g_match_info_fetch_named (match_info, "used");
    if (item) {
        size = bs_size_new_from_str (item, &error);
        if (size) {
            ret->used = bs_size_get_bytes (size, NULL, &error);
            bs_size_free (size);
        }
        if (error)
            bd_utils_log_format (BD_UTILS_LOG_WARNING, "%s", error->msg);
        bs_clear_error (&error);
        g_free (item);
    }

    return ret;
}

static BDBtrfsFilesystemInfo* get_filesystem_info_from_match (GMatchInfo *match_info) {
    BDBtrfsFilesystemInfo *ret = g_new(BDBtrfsFilesystemInfo, 1);
    gchar *item = NULL;
    BSSize size = NULL;
    BSError *error = NULL;

    ret->label = g_match_info_fetch_named (match_info, "label");
    ret->uuid = g_match_info_fetch_named (match_info, "uuid");

    item = g_match_info_fetch_named (match_info, "num_devices");
    ret->num_devices = g_ascii_strtoull (item, NULL, 0);
    g_free (item);

    item = g_match_info_fetch_named (match_info, "used");
    if (item) {
        size = bs_size_new_from_str (item, &error);
        if (size) {
            ret->used = bs_size_get_bytes (size, NULL, &error);
            bs_size_free (size);
        }
        if (error)
            bd_utils_log_format (BD_UTILS_LOG_WARNING, "%s", error->msg);
        bs_clear_error (&error);
        g_free (item);
    }

    return ret;
}

/**
 * bd_btrfs_create_volume:
 * @devices: (array zero-terminated=1): list of devices to create btrfs volume from
 * @label: (nullable): label for the volume
 * @data_level: (nullable): RAID level for the data or %NULL to use the default
 * @md_level: (nullable): RAID level for the metadata or %NULL to use the default
 * @extra: (nullable) (array zero-terminated=1): extra options for the volume creation (right now
 *                                                 passed to the 'mkfs.btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the new btrfs volume was created from @devices or not
 *
 * See mkfs.btrfs(8) for details about @data_level, @md_level and btrfs in general.
 *
 * Tech category: %BD_BTRFS_TECH_MULTI_DEV-%BD_BTRFS_TECH_MODE_CREATE
 */
gboolean bd_btrfs_create_volume (const gchar **devices, const gchar *label, const gchar *data_level, const gchar *md_level, const BDExtraArg **extra, GError **error) {
    const gchar **device_p = NULL;
    guint8 num_args = 0;
    const gchar **argv = NULL;
    guint8 next_arg = 1;
    gboolean success = FALSE;

    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_BTRFS_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (!devices || (g_strv_length ((gchar **) devices) < 1)) {
        g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_DEVICE, "No devices given");
        return FALSE;
    }

    for (device_p = devices; *device_p != NULL; device_p++) {
        if (access (*device_p, F_OK) != 0) {
            g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_DEVICE, "Device %s does not exist", *device_p);
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

    argv = g_new0 (const gchar*, num_args + 2);
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

    success = bd_utils_exec_and_report_error (argv, extra, error);
    g_free (argv);
    return success;
}

/**
 * bd_btrfs_add_device:
 * @mountpoint: mountpoint of the btrfs volume to add new device to
 * @device: a device to add to the btrfs volume
 * @extra: (nullable) (array zero-terminated=1): extra options for the addition (right now
 *                                                 passed to the 'btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @device was successfully added to the @mountpoint btrfs volume or not
 *
 * Tech category: %BD_BTRFS_TECH_MULTI_DEV-%BD_BTRFS_TECH_MODE_MODIFY
 */
gboolean bd_btrfs_add_device (const gchar *mountpoint, const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *argv[6] = {"btrfs", "device", "add", device, mountpoint, NULL};
    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_BTRFS_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, extra, error);
}

/**
 * bd_btrfs_remove_device:
 * @mountpoint: mountpoint of the btrfs volume to remove device from
 * @device: a device to remove from the btrfs volume
 * @extra: (nullable) (array zero-terminated=1): extra options for the removal (right now
 *                                                 passed to the 'btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @device was successfully removed from the @mountpoint btrfs volume or not
 *
 * Tech category: %BD_BTRFS_TECH_MULTI_DEV-%BD_BTRFS_TECH_MODE_MODIFY
 */
gboolean bd_btrfs_remove_device (const gchar *mountpoint, const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *argv[6] = {"btrfs", "device", "delete", device, mountpoint, NULL};
    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_BTRFS_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, extra, error);
}

/**
 * bd_btrfs_create_subvolume:
 * @mountpoint: mountpoint of the btrfs volume to create subvolume under
 * @name: name of the subvolume
 * @extra: (nullable) (array zero-terminated=1): extra options for the subvolume creation (right now
 *                                                 passed to the 'btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @mountpoint/@name subvolume was successfully created or not
 *
 * Tech category: %BD_BTRFS_TECH_SUBVOL-%BD_BTRFS_TECH_MODE_CREATE
 */
gboolean bd_btrfs_create_subvolume (const gchar *mountpoint, const gchar *name, G_GNUC_UNUSED const BDExtraArg **extra, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    enum btrfs_util_error err;

    if (g_str_has_suffix (mountpoint, "/"))
        path = g_strdup_printf ("%s%s", mountpoint, name);
    else
        path = g_strdup_printf ("%s/%s", mountpoint, name);


    err = btrfs_util_create_subvolume (path, 0 ,NULL ,NULL);
    if (err) {
        g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_DEVICE, "%s: %m", btrfs_util_strerror (err));
    } else {
        success = TRUE;
    }

    g_free (path);

    return success;
}

/**
 * bd_btrfs_delete_subvolume:
 * @mountpoint: mountpoint of the btrfs volume to delete subvolume from
 * @name: name of the subvolume
 * @extra: (nullable) (array zero-terminated=1): extra options for the subvolume deletion (right now
 *                                                 passed to the 'btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @mountpoint/@name subvolume was successfully deleted or not
 *
 * Tech category: %BD_BTRFS_TECH_SUBVOL-%BD_BTRFS_TECH_MODE_DELETE
 */
gboolean bd_btrfs_delete_subvolume (const gchar *mountpoint, const gchar *name, G_GNUC_UNUSED const BDExtraArg **extra, GError **error) {
    gchar *path = NULL;
    gboolean success = FALSE;
    enum btrfs_util_error err;

    if (g_str_has_suffix (mountpoint, "/"))
        path = g_strdup_printf ("%s%s", mountpoint, name);
    else
        path = g_strdup_printf ("%s/%s", mountpoint, name);

    // TODO: support BTRFS_UTIL_DELETE_SUBVOLUME_RECURSIVE
    err = btrfs_util_delete_subvolume (path, 0);
    if (err) {
        g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_DEVICE, "%s: %m", btrfs_util_strerror (err));
    } else {
        success = TRUE;
    }

    g_free (path);

    return success;
}

/**
 * bd_btrfs_get_default_subvolume_id:
 * @mountpoint: mountpoint of the volume to get the default subvolume ID of
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: ID of the @mountpoint volume's default subvolume. If 0,
 * @error) may be set to indicate error
 *
 * Tech category: %BD_BTRFS_TECH_SUBVOL-%BD_BTRFS_TECH_MODE_QUERY
 */
guint64 bd_btrfs_get_default_subvolume_id (const gchar *mountpoint, GError **error) {
    enum btrfs_util_error err;
    guint64 ret = 0;

    err = btrfs_util_get_default_subvolume (mountpoint, &ret);
    if (err) {
        g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_NOT_FOUND, "%s: %m", btrfs_util_strerror (err));
    }

    return ret;
}

/**
 * bd_btrfs_set_default_subvolume:
 * @mountpoint: mountpoint of the volume to set the default subvolume ID of
 * @subvol_id: ID of the subvolume to be set as the default subvolume
 * @extra: (nullable) (array zero-terminated=1): extra options for the setting (right now
 *                                                 passed to the 'btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @mountpoint volume's default subvolume was correctly set
 * to @subvol_id or not
 *
 * Tech category: %BD_BTRFS_TECH_SUBVOL-%BD_BTRFS_TECH_MODE_MODIFY
 */
gboolean bd_btrfs_set_default_subvolume (const gchar *mountpoint, guint64 subvol_id, G_GNUC_UNUSED const BDExtraArg **extra, GError **error) {
    enum btrfs_util_error err;
    gboolean ret = FALSE;

    err = btrfs_util_set_default_subvolume (mountpoint, subvol_id);
    if (err)
        g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_NOT_FOUND, "%s: %m", btrfs_util_strerror (err));
    else
        ret = TRUE;

    return ret;
}

/**
 * bd_btrfs_create_snapshot:
 * @source: path to source subvolume
 * @dest: path to new snapshot volume
 * @ro: whether the snapshot should be read-only
 * @extra: (nullable) (array zero-terminated=1): extra options for the snapshot creation (right now
 *                                                 passed to the 'btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @dest snapshot of @source was successfully created or not
 *
 * Tech category: %BD_BTRFS_TECH_SNAPSHOT-%BD_BTRFS_TECH_MODE_CREATE
 */
gboolean bd_btrfs_create_snapshot (const gchar *source, const gchar *dest, gboolean ro, const BDExtraArg **extra, GError **error) {
    const gchar *argv[7] = {"btrfs", "subvol", "snapshot", NULL, NULL, NULL, NULL};
    guint next_arg = 3;

    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_BTRFS_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (ro) {
        argv[next_arg] = "-r";
        next_arg++;
    }
    argv[next_arg] = source;
    next_arg++;
    argv[next_arg] = dest;

    return bd_utils_exec_and_report_error (argv, extra, error);
}

/**
 * bd_btrfs_list_devices:
 * @device: a device that is part of the queried btrfs volume
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about the devices that are part of the btrfs volume
 * containing @device or %NULL in case of error
 *
 * Tech category: %BD_BTRFS_TECH_MULTI_DEV-%BD_BTRFS_TECH_MODE_QUERY
 */
BDBtrfsDeviceInfo** bd_btrfs_list_devices (const gchar *device, GError **error) {
    const gchar *argv[5] = {"btrfs", "filesystem", "show", device, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar const * const pattern = "devid[ \\t]+(?P<id>\\d+)[ \\t]+" \
                                  "size[ \\t]+(?P<size>\\S+)[ \\t]+" \
                                  "used[ \\t]+(?P<used>\\S+)[ \\t]+" \
                                  "path[ \\t]+(?P<path>\\S+)\n";
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    GPtrArray *dev_infos;

    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_BTRFS_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return NULL;

    regex = g_regex_new (pattern, G_REGEX_EXTENDED, 0, error);
    if (!regex) {
        bd_utils_log_format (BD_UTILS_LOG_WARNING, "Failed to create new GRegex");
        /* error is already populated */
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (argv, NULL, &output, error);
    if (!success) {
        g_regex_unref (regex);
        /* error is already populated from the previous call */
        return NULL;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    dev_infos = g_ptr_array_new ();
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
    g_regex_unref (regex);

    if (dev_infos->len == 0) {
        g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_PARSE, "Failed to parse information about devices");
        g_ptr_array_free (dev_infos, TRUE);
        return NULL;
    }

    g_ptr_array_add (dev_infos, NULL);
    return (BDBtrfsDeviceInfo **) g_ptr_array_free (dev_infos, FALSE);
}

/**
 * bd_btrfs_list_subvolumes:
 * @mountpoint: a mountpoint of the queried btrfs volume
 * @snapshots_only: whether to list only snapshot subvolumes or not
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about the subvolumes that are part of the btrfs volume
 * mounted at @mountpoint or %NULL in case of error
 *
 * The subvolumes are sorted in a way that no child subvolume appears in the
 * list before its parent (sub)volume.
 *
 * Tech category: %BD_BTRFS_TECH_SUBVOL-%BD_BTRFS_TECH_MODE_QUERY
 */
BDBtrfsSubvolumeInfo** bd_btrfs_list_subvolumes (const gchar *mountpoint, gboolean snapshots_only, GError **error) {
    struct btrfs_util_subvolume_iterator *iter;
    enum btrfs_util_error err;
    char *path;
    char empty_uuid[BTRFS_FSID_SIZE] = {0};
    struct btrfs_util_subvolume_info info;
    BDBtrfsSubvolumeInfo** ret = NULL;
    GPtrArray *subvol_infos;

    err = btrfs_util_create_subvolume_iterator (mountpoint, 5, 0, &iter);
    if (err) {
        g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_NOT_FOUND, "%s: %m", btrfs_util_strerror (err));
        return NULL;
    }

    subvol_infos = g_ptr_array_new ();
    while (!(err = btrfs_util_subvolume_iterator_next_info (iter, &path, &info))) {
        /* See uapi/linux/btrfs.h */
        if (snapshots_only && memcmp (info.parent_uuid, empty_uuid, BTRFS_FSID_SIZE) == 0)
            continue;

        BDBtrfsSubvolumeInfo *subvol = g_new (BDBtrfsSubvolumeInfo, 1);
        subvol->id = info.id;
        subvol->parent_id = info.parent_id;
        subvol->path = g_strdup (path);
        g_ptr_array_add (subvol_infos, subvol);

        free (path);
    }

    btrfs_util_destroy_subvolume_iterator (iter);

    // TODO: this can be Stop iteration: Success (3)
    // if (err) {
    //     g_set_error (error, BD_BTRFS_ERROR, BD_BTRFS_ERROR_NOT_FOUND, "%s: %m", btrfs_util_strerror (err));
    //     return NULL;
    // }

    /* now we know how much space to allocate for the result (subvols + NULL) */
    ret = g_new0 (BDBtrfsSubvolumeInfo*, subvol_infos->len + 1);

    guint64 i = 0;
    BDBtrfsSubvolumeInfo* item = NULL;
    for (i=0; i < subvol_infos->len; i++) {
        item = (BDBtrfsSubvolumeInfo*) g_ptr_array_index (subvol_infos, i);
        ret[i] = item;
    }

    /* now just free the pointer array */
    g_ptr_array_free (subvol_infos, TRUE);

    return ret;
}

/**
 * bd_btrfs_filesystem_info:
 * @device: a device that is part of the queried btrfs volume
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: information about the @device's volume's filesystem or %NULL in case of error
 *
 * Tech category: %BD_BTRFS_TECH_FS-%BD_BTRFS_TECH_MODE_QUERY
 */
BDBtrfsFilesystemInfo* bd_btrfs_filesystem_info (const gchar *device, GError **error) {
    const gchar *argv[5] = {"btrfs", "filesystem", "show", device, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    gchar const * const pattern = "Label:\\s+(none|'(?P<label>.+)')\\s+" \
                                  "uuid:\\s+(?P<uuid>\\S+)\\s+" \
                                  "Total\\sdevices\\s+(?P<num_devices>\\d+)\\s+" \
                                  "FS\\sbytes\\sused\\s+(?P<used>\\S+)";
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    BDBtrfsFilesystemInfo *ret = NULL;

    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_BTRFS_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return NULL;

    regex = g_regex_new (pattern, G_REGEX_EXTENDED, 0, error);
    if (!regex) {
        bd_utils_log_format (BD_UTILS_LOG_WARNING, "Failed to create new GRegex");
        /* error is already populated */
        return NULL;
    }

    success = bd_utils_exec_and_capture_output (argv, NULL, &output, error);
    if (!success) {
        /* error is already populated from the call above or just empty
           output */
        g_regex_unref (regex);
        return NULL;
    }

    success = g_regex_match (regex, output, 0, &match_info);
    if (!success) {
        g_regex_unref (regex);
        g_match_info_free (match_info);
        g_free (output);
        return NULL;
    }

    ret = get_filesystem_info_from_match (match_info);
    g_match_info_free (match_info);
    g_regex_unref (regex);

    g_free (output);

    return ret;
}

/**
 * bd_btrfs_mkfs:
 * @devices: (array zero-terminated=1): list of devices to create btrfs volume from
 * @label: (nullable): label for the volume
 * @data_level: (nullable): RAID level for the data or %NULL to use the default
 * @md_level: (nullable): RAID level for the metadata or %NULL to use the default
 * @extra: (nullable) (array zero-terminated=1): extra options for the volume creation (right now
 *                                                 passed to the 'btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the new btrfs volume was created from @devices or not
 *
 * See mkfs.btrfs(8) for details about @data_level, @md_level and btrfs in general.
 *
 * Tech category: %BD_BTRFS_TECH_FS-%BD_BTRFS_TECH_MODE_CREATE
 */
gboolean bd_btrfs_mkfs (const gchar **devices, const gchar *label, const gchar *data_level, const gchar *md_level, const BDExtraArg **extra, GError **error) {
    return bd_btrfs_create_volume (devices, label, data_level, md_level, extra, error);
}

/**
 * bd_btrfs_resize:
 * @mountpoint: a mountpoint of the to be resized btrfs filesystem
 * @size: requested new size
 * @extra: (nullable) (array zero-terminated=1): extra options for the volume resize (right now
 *                                                 passed to the 'btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @mountpoint filesystem was successfully resized to @size
 * or not
 *
 * Tech category: %BD_BTRFS_TECH_FS-%BD_BTRFS_TECH_MODE_MODIFY
 */
gboolean bd_btrfs_resize (const gchar *mountpoint, guint64 size, const BDExtraArg **extra, GError **error) {
    const gchar *argv[6] = {"btrfs", "filesystem", "resize", NULL, mountpoint, NULL};
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_BTRFS_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    argv[3] = g_strdup_printf ("%"G_GUINT64_FORMAT, size);
    ret = bd_utils_exec_and_report_error (argv, extra, error);
    g_free ((gchar *) argv[3]);

    return ret;
}

/**
 * bd_btrfs_check:
 * @device: a device that is part of the checked btrfs volume
 * @extra: (nullable) (array zero-terminated=1): extra options for the check (right now
 *                                                 passed to the 'btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the filesystem was successfully checked or not
 *
 * Tech category: %BD_BTRFS_TECH_FS-%BD_BTRFS_TECH_MODE_QUERY
 */
gboolean bd_btrfs_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *argv[4] = {"btrfs", "check", device, NULL};

    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_BTRFS_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, extra, error);
}

/**
 * bd_btrfs_repair:
 * @device: a device that is part of the to be repaired btrfs volume
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the filesystem was successfully checked and repaired or not
 *
 * Tech category: %BD_BTRFS_TECH_FS-%BD_BTRFS_TECH_MODE_MODIFY
 */
gboolean bd_btrfs_repair (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *argv[5] = {"btrfs", "check", "--repair", device, NULL};

    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_BTRFS_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, extra, error);
}

/**
 * bd_btrfs_change_label:
 * @mountpoint: a mountpoint of the btrfs filesystem to change label of
 * @label: new label for the filesystem
 * @extra: (nullable) (array zero-terminated=1): extra options for the volume creation (right now
 *                                                 passed to the 'btrfs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the label of the @mountpoint filesystem was successfully set
 * to @label or not
 *
 * Tech category: %BD_BTRFS_TECH_FS-%BD_BTRFS_TECH_MODE_MODIFY
 */
gboolean bd_btrfs_change_label (const gchar *mountpoint, const gchar *label, GError **error) {
    const gchar *argv[6] = {"btrfs", "filesystem", "label", mountpoint, label, NULL};

    if (!check_deps (&avail_deps, DEPS_BTRFS_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_BTRFS_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, NULL, error);
}
