#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_FS_XFS
#define BD_FS_XFS

typedef struct BDFSXfsInfo {
    gchar *label;
    gchar *uuid;
    guint64 block_size;
    guint64 block_count;
} BDFSXfsInfo;

BDFSXfsInfo* bd_fs_xfs_info_copy (BDFSXfsInfo *data);
void bd_fs_xfs_info_free (BDFSXfsInfo *data);

gboolean bd_fs_xfs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_xfs_wipe (const gchar *device, GError **error);
gboolean bd_fs_xfs_check (const gchar *device, GError **error);
gboolean bd_fs_xfs_repair (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_xfs_set_label (const gchar *device, const gchar *label, GError **error);
BDFSXfsInfo* bd_fs_xfs_get_info (const gchar *device, GError **error);
gboolean bd_fs_xfs_resize (const gchar *mpoint, guint64 new_size, const BDExtraArg **extra, GError **error);

#endif  /* BD_FS_XFS */
