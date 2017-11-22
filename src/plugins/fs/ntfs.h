#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_FS_NTFS
#define BD_FS_NTFS

typedef struct BDFSNtfsInfo {
    guint64 size;
    guint64 free_space;
} BDFSNtfsInfo;

BDFSNtfsInfo* bd_fs_ntfs_info_copy (BDFSNtfsInfo *data);
void bd_fs_ntfs_info_free (BDFSNtfsInfo *data);

gboolean bd_fs_ntfs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ntfs_wipe (const gchar *device, GError **error);
gboolean bd_fs_ntfs_check (const gchar *device, GError **error);
gboolean bd_fs_ntfs_repair (const gchar *device, GError **error);
gboolean bd_fs_ntfs_set_label (const gchar *device, const gchar *label, GError **error);
BDFSNtfsInfo* bd_fs_ntfs_get_info (const gchar *device, GError **error);
gboolean bd_fs_ntfs_resize (const gchar *device, guint64 new_size, GError **error);

#endif  /* BD_FS_NTFS */
