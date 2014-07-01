#include <glib.h>

#ifndef BD_BTRFS
#define BD_BTRFS

gboolean bd_btrfs_create_volume (gchar **devices, gchar *label, gchar *data_level, gchar *md_level, gchar **error_message);
gboolean bd_btrfs_add_device (gchar *mountpoint, gchar *device, gchar **error_message);
gboolean bd_btrfs_remove_device (gchar *mountpoint, gchar *device, gchar **error_message);
gboolean bd_btrfs_create_subvolume (gchar *mountpoint, gchar *name, gchar **error_message);
gboolean bd_btrfs_delete_subvolume (gchar *mountpoint, gchar *name, gchar **error_message);
guint64 bd_btrfs_get_default_subvolume_id (gchar *mountpoint, gchar **error_message);

#endif  /* BD_BTRFS */
