#include <glib.h>
#include <glib-object.h>

/* BpG-skip */
#define BD_BTRFS_TYPE_DEVICE_INFO (bd_btrfs_device_info_get_type ())
GType bd_btrfs_device_info_get_type();

typedef struct BDBtrfsDeviceInfo {
    guint64 id;
    gchar *path;
    guint64 size;
    guint64 used;
} BDBtrfsDeviceInfo;

/**
 * bd_btrfs_device_info_copy: (skip)
 *
 * Creates a new copy of @info.
 */
BDBtrfsDeviceInfo* bd_btrfs_device_info_copy (BDBtrfsDeviceInfo *info) {
    BDBtrfsDeviceInfo *new_info = g_new (BDBtrfsDeviceInfo, 1);

    new_info->id = info->id;
    new_info->path = g_strdup (info->path);
    new_info->size = info->size;
    new_info->used = info->used;

    return new_info;
}

/**
 * bd_btrfs_device_info_free: (skip)
 *
 * Frees @info.
 */
void bd_btrfs_device_info_free (BDBtrfsDeviceInfo *info) {
    g_free (info->path);
    g_free (info);
}

GType bd_btrfs_device_info_get_type () {
    static GType type = 0;

    if (G_UNLIKELY(type == 0)) {
        type = g_boxed_type_register_static("BDBtrfsDeviceInfo",
                                            (GBoxedCopyFunc) bd_btrfs_device_info_copy,
                                            (GBoxedFreeFunc) bd_btrfs_device_info_free);
    }

    return type;
}


#define BD_BTRFS_TYPE_SUBVOLUME_INFO (bd_btrfs_subvolume_info_get_type ())
GType bd_btrfs_subvolume_info_get_type();

typedef struct BDBtrfsSubvolumeInfo {
    guint64 id;
    guint64 parent_id;
    gchar *path;
} BDBtrfsSubvolumeInfo;

/**
 * bd_btrfs_subvolume_info_copy: (skip)
 *
 * Creates a new copy of @info.
 */
BDBtrfsSubvolumeInfo* bd_btrfs_subvolume_info_copy (BDBtrfsSubvolumeInfo *info) {
    BDBtrfsSubvolumeInfo *new_info = g_new (BDBtrfsSubvolumeInfo, 1);

    new_info->id = info->id;
    new_info->parent_id = info->parent_id;
    new_info->path = g_strdup (info->path);

    return new_info;
}

/**
 * bd_btrfs_subvolume_info_free: (skip)
 *
 * Frees @info.
 */
void bd_btrfs_subvolume_info_free (BDBtrfsSubvolumeInfo *info) {
    g_free (info->path);
    g_free (info);
}

GType bd_btrfs_subvolume_info_get_type () {
    static GType type = 0;

    if (G_UNLIKELY(type == 0)) {
        type = g_boxed_type_register_static("BDBtrfsSubvolumeInfo",
                                            (GBoxedCopyFunc) bd_btrfs_subvolume_info_copy,
                                            (GBoxedFreeFunc) bd_btrfs_subvolume_info_free);
    }

    return type;
}
/* BpG-skip-end */

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
gboolean bd_btrfs_create_volume (gchar **devices, gchar *label, gchar *data_level, gchar *md_level, gchar **error_message);

/**
 * bd_btrfs_add_device:
 * @mountpoint: mountpoint of the btrfs volume to add new device to
 * @device: a device to add to the btrfs volume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @device was successfully added to the @mountpoint btrfs volume or not
 */
gboolean bd_btrfs_add_device (gchar *mountpoint, gchar *device, gchar **error_message);

/**
 * bd_btrfs_remove_device:
 * @mountpoint: mountpoint of the btrfs volume to remove device from
 * @device: a device to remove from the btrfs volume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @device was successfully removed from the @mountpoint btrfs volume or not
 */
gboolean bd_btrfs_remove_device (gchar *mountpoint, gchar *device, gchar **error_message);

/**
 * bd_btrfs_create_subvolume:
 * @mountpoint: mountpoint of the btrfs volume to create subvolume under
 * @name: name of the subvolume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @mountpoint/@name subvolume was successfully created or not
 */
gboolean bd_btrfs_create_subvolume (gchar *mountpoint, gchar *name, gchar **error_message);

/**
 * bd_btrfs_delete_subvolume:
 * @mountpoint: mountpoint of the btrfs volume to delete subvolume from
 * @name: name of the subvolume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @mountpoint/@name subvolume was successfully deleted or not
 */
gboolean bd_btrfs_delete_subvolume (gchar *mountpoint, gchar *name, gchar **error_message);

/**
 * bd_btrfs_get_default_subvolume_id:
 * @mountpoint: mountpoint of the volume to get the default subvolume ID of
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: ID of the @mountpoint volume's default subvolume. If 0,
 * @error_message may be set to indicate error
 */
guint64 bd_btrfs_get_default_subvolume_id (gchar *mountpoint, gchar **error_message);

/**
 * bd_btrfs_create_snapshot:
 * @source: path to source subvolume
 * @dest: path to new snapshot volume
 * @ro: whether the snapshot should be read-only
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @dest snapshot of @source was successfully created or not
 */
gboolean bd_btrfs_create_snapshot (gchar *source, gchar *dest, gboolean ro, gchar **error_message);

/**
 * bd_btrfs_list_devices:
 * @device: a device that is part of the queried btrfs volume
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: (array zero-terminated=1): information about the devices that are part of the btrfs volume
 * containing @device
 */
BDBtrfsDeviceInfo** bd_btrfs_list_devices (gchar *device, gchar **error_message);

/**
 * bd_btrfs_list_subvolumes:
 * @mountpoint: a mountpoint of the queried btrfs volume
 * @snapshots_only: whether to list only snapshot subvolumes or not
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: (array zero-terminated=1): information about the subvolumes that are part of the btrfs volume
 * mounted at @mountpoint
 */
BDBtrfsSubvolumeInfo** bd_btrfs_list_subvolumes (gchar *mountpoint, gboolean snapshots_only, gchar **error_message);
