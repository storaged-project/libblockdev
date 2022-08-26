#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_FS_F2FS
#define BD_FS_F2FS

/* this is taken from f2fs_fs.h */
typedef enum {
    BD_FS_F2FS_FEATURE_ENCRYPT =                1 << 0,
    BD_FS_F2FS_FEATURE_BLKZONED =               1 << 1,
    BD_FS_F2FS_FEATURE_ATOMIC_WRITE =           1 << 2,
    BD_FS_F2FS_FEATURE_EXTRA_ATTR =             1 << 3,
    BD_FS_F2FS_FEATURE_PRJQUOTA =               1 << 4,
    BD_FS_F2FS_FEATURE_INODE_CHKSUM =           1 << 5,
    BD_FS_F2FS_FEATURE_FLEXIBLE_INLINE_XATTR =  1 << 6,
    BD_FS_F2FS_FEATURE_QUOTA_INO =              1 << 7,
    BD_FS_F2FS_FEATURE_INODE_CRTIME =           1 << 8,
    BD_FS_F2FS_FEATURE_LOST_FOUND =             1 << 9,
    BD_FS_F2FS_FEATURE_VERITY =                 1 << 10,
    BD_FS_F2FS_FEATURE_SB_CHKSUM =              1 << 11,
} BDFSF2FSFeature;

typedef struct BDFSF2FSInfo {
    gchar *label;
    gchar *uuid;
    guint64 sector_size;
    guint64 sector_count;
    guint64 features;
} BDFSF2FSInfo;

BDFSF2FSInfo* bd_fs_f2fs_info_copy (BDFSF2FSInfo *data);
void bd_fs_f2fs_info_free (BDFSF2FSInfo *data);

gboolean bd_fs_f2fs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_f2fs_check (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_f2fs_repair (const gchar *device, const BDExtraArg **extra, GError **error);
BDFSF2FSInfo* bd_fs_f2fs_get_info (const gchar *device, GError **error);
gboolean bd_fs_f2fs_resize (const gchar *device, guint64 new_size, gboolean safe, const BDExtraArg **extra, GError **error);

#endif  /* BD_FS_F2FS */
