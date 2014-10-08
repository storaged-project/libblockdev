#include <glib.h>
#include <glib-object.h>

#ifndef BD_BTRFS
#define BD_BTRFS

typedef struct BDBtrfsDeviceInfo {
    guint64 id;
    gchar *path;
    guint64 size;
    guint64 used;
} BDBtrfsDeviceInfo;

BDBtrfsDeviceInfo* bd_btrfs_device_info_copy (BDBtrfsDeviceInfo *info) {
    BDBtrfsDeviceInfo *new_info = g_new (BDBtrfsDeviceInfo, 1);

    new_info->id = info->id;
    new_info->path = g_strdup (info->path);
    new_info->size = info->size;
    new_info->used = info->used;

    return new_info;
}

void bd_btrfs_device_info_free (BDBtrfsDeviceInfo *info) {
    g_free (info->path);
    g_free (info);
}

typedef struct BDBtrfsSubvolumeInfo {
    guint64 id;
    guint64 parent_id;
    gchar *path;
} BDBtrfsSubvolumeInfo;

BDBtrfsSubvolumeInfo* bd_btrfs_subvolume_info_copy (BDBtrfsSubvolumeInfo *info) {
    BDBtrfsSubvolumeInfo *new_info = g_new (BDBtrfsSubvolumeInfo, 1);

    new_info->id = info->id;
    new_info->parent_id = info->parent_id;
    new_info->path = g_strdup (info->path);

    return new_info;
}

void bd_btrfs_subvolume_info_free (BDBtrfsSubvolumeInfo *info) {
    g_free (info->path);
    g_free (info);
}

typedef struct BDBtrfsFilesystemInfo {
    gchar *label;
    gchar *uuid;
    guint64 num_devices;
    guint64 used;
} BDBtrfsFilesystemInfo;

BDBtrfsFilesystemInfo* bd_btrfs_filesystem_info_copy (BDBtrfsFilesystemInfo *info) {
    BDBtrfsFilesystemInfo *new_info = g_new (BDBtrfsFilesystemInfo, 1);

    new_info->label = g_strdup (info->label);
    new_info->uuid = g_strdup (info->uuid);
    new_info->num_devices = info->num_devices;
    new_info->used = info->used;

    return new_info;
}

void bd_btrfs_filesystem_info_free (BDBtrfsFilesystemInfo *info) {
    g_free (info->label);
    g_free (info->uuid);
    g_free (info);
}

gboolean bd_btrfs_create_volume (gchar **devices, gchar *label, gchar *data_level, gchar *md_level, gchar **error_message);
gboolean bd_btrfs_add_device (gchar *mountpoint, gchar *device, gchar **error_message);
gboolean bd_btrfs_remove_device (gchar *mountpoint, gchar *device, gchar **error_message);
gboolean bd_btrfs_create_subvolume (gchar *mountpoint, gchar *name, gchar **error_message);
gboolean bd_btrfs_delete_subvolume (gchar *mountpoint, gchar *name, gchar **error_message);
guint64 bd_btrfs_get_default_subvolume_id (gchar *mountpoint, gchar **error_message);
gboolean bd_btrfs_set_default_subvolume (gchar *mountpoint, guint64 subvol_id, gchar **error_message);
gboolean bd_btrfs_create_snapshot (gchar *source, gchar *dest, gboolean ro, gchar **error_message);
BDBtrfsDeviceInfo** bd_btrfs_list_devices (gchar *device, gchar **error_message);
BDBtrfsSubvolumeInfo** bd_btrfs_list_subvolumes (gchar *mountpoint, gboolean snapshots_only, gchar **error_message);
BDBtrfsFilesystemInfo* bd_btrfs_filesystem_info (gchar *device, gchar **error_message);

gboolean bd_btrfs_mkfs (gchar **devices, gchar *label, gchar *data_level, gchar *md_level, gchar **error_message);
gboolean bd_btrfs_resize (gchar *mountpoint, guint64 size, gchar **error_message);
gboolean bd_btrfs_check (gchar *device, gchar **error_message);
gboolean bd_btrfs_repair (gchar *device, gchar **error_message);
gboolean bd_btrfs_change_label (gchar *mountpoint, gchar *label, gchar **error_message);

#endif  /* BD_BTRFS */
