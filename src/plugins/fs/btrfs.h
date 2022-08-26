#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_FS_BTRFS
#define BD_FS_BTRFS

typedef struct BDFSBtrfsInfo {
    gchar *label;
    gchar *uuid;
    guint64 size;
    guint64 free_space;
} BDFSBtrfsInfo;

BDFSBtrfsInfo* bd_fs_btrfs_info_copy (BDFSBtrfsInfo *data);
void bd_fs_btrfs_info_free (BDFSBtrfsInfo *data);

gboolean bd_fs_btrfs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_btrfs_check (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_btrfs_repair (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_btrfs_set_label (const gchar *mpoint, const gchar *label, GError **error);
gboolean bd_fs_btrfs_check_label (const gchar *label, GError **error);
gboolean bd_fs_btrfs_set_uuid (const gchar *device, const gchar *uuid, GError **error);
gboolean bd_fs_btrfs_check_uuid ( const gchar *uuid, GError **error);
BDFSBtrfsInfo* bd_fs_btrfs_get_info (const gchar *mpoint, GError **error);
gboolean bd_fs_btrfs_resize (const gchar *mpoint, guint64 new_size, const BDExtraArg **extra, GError **error);

#endif  /* BD_FS_BTRFS */
