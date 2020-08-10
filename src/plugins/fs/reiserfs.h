#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_FS_REISERFS
#define BD_FS_REISERFS

typedef struct BDFSReiserFSInfo {
    gchar *label;
    gchar *uuid;
    guint64 block_size;
    guint64 block_count;
    guint64 free_blocks;
} BDFSReiserFSInfo;

BDFSReiserFSInfo* bd_fs_reiserfs_info_copy (BDFSReiserFSInfo *data);
void bd_fs_reiserfs_info_free (BDFSReiserFSInfo *data);

gboolean bd_fs_reiserfs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_reiserfs_wipe (const gchar *device, GError **error);
gboolean bd_fs_reiserfs_check (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_reiserfs_repair (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_reiserfs_set_label (const gchar *device, const gchar *label, GError **error);
gboolean bd_fs_reiserfs_check_label (const gchar *label, GError **error);
gboolean bd_fs_reiserfs_set_uuid (const gchar *device, const gchar *uuid, GError **error);
BDFSReiserFSInfo* bd_fs_reiserfs_get_info (const gchar *device, GError **error);
gboolean bd_fs_reiserfs_resize (const gchar *device, guint64 new_size, GError **error);

#endif  /* BD_FS_REISERFS */
