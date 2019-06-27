#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_FS_MOUNT
#define BD_FS_MOUNT

gboolean bd_fs_unmount (const gchar *spec, gboolean lazy, gboolean force, const BDExtraArg **extra, GError **error);
gboolean bd_fs_mount (const gchar *device, const gchar *mountpoint, const gchar *fstype, const gchar *options, const BDExtraArg **extra, GError **error);
gchar* bd_fs_get_mountpoint (const gchar *device, GError **error);
gboolean bd_fs_is_mountpoint (const gchar *path, GError **error);

#endif  /* BD_FS_MOUNT */
