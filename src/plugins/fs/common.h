#include <glib.h>
#include <blkid.h>

#ifndef BD_FS_COMMON
#define BD_FS_COMMON

gint synced_close (gint fd);
gboolean get_uuid_label (const gchar *device, gchar **uuid, gchar **label, GError **error);
gboolean check_uuid (const gchar *uuid, GError **error);

#endif  /* BD_FS_COMMON */
