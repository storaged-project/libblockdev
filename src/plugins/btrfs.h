#include <glib.h>

#ifndef BD_BTRFS
#define BD_BTRFS

gboolean bd_btrfs_create_volume (gchar **devices, gchar *label, gchar *data_level, gchar *md_level, gchar **error_message);

#endif  /* BD_BTRFS */
