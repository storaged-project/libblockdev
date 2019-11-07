#include <glib.h>

#ifndef BD_FS_GENERIC
#define BD_FS_GENERIC

gboolean bd_fs_wipe (const gchar *device, gboolean all, GError **error);
gboolean bd_fs_wipe_force (const gchar *device, gboolean all, gboolean force, GError **error);
gboolean bd_fs_clean (const gchar *device, GError **error);
gchar* bd_fs_get_fstype (const gchar *device,  GError **error);

gboolean bd_fs_freeze (const gchar *mountpoint, GError **error);
gboolean bd_fs_unfreeze (const gchar *mountpoint, GError **error);

gboolean bd_fs_resize (const gchar *device, guint64 new_size, GError **error);
gboolean bd_fs_repair (const gchar *device, GError **error);
gboolean bd_fs_check (const gchar *device, GError **error);
gboolean bd_fs_set_label (const gchar *device, const gchar *label, GError **error);

typedef enum {
    BD_FS_OFFLINE_SHRINK = 1 << 1,
    BD_FS_OFFLINE_GROW = 1 << 2,
    BD_FS_ONLINE_SHRINK = 1 << 3,
    BD_FS_ONLINE_GROW = 1 << 4
} BDFsResizeFlags;

gboolean bd_fs_can_resize (const gchar *type, BDFsResizeFlags *mode, gchar **required_utility, GError **error);
gboolean bd_fs_can_check (const gchar *type, gchar **required_utility, GError **error);
gboolean bd_fs_can_repair (const gchar *type, gchar **required_utility, GError **error);
gboolean bd_fs_can_set_label (const gchar *type, gchar **required_utility, GError **error);

#endif  /* BD_FS_GENERIC */
