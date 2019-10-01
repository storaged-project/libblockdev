#include <glib.h>
#include <blkid.h>

#ifndef BD_FS_COMMON
#define BD_FS_COMMON

gint synced_close (gint fd);
gboolean has_fs (blkid_probe probe, const gchar *device, const gchar *fs_type, GError **error);
gboolean wipe_fs (const gchar *device, const gchar *fs_type, gboolean wipe_all, GError **error);
gboolean get_uuid_label (const gchar *device, gchar **uuid, gchar **label, GError **error);

#endif  /* BD_FS_COMMON */
