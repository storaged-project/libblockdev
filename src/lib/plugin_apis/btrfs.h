#include <glib.h>

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
