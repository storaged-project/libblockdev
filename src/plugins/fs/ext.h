#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_FS_EXT
#define BD_FS_EXT

typedef struct BDFSExtInfo {
    gchar *label;
    gchar *uuid;
    gchar *state;
    guint64 block_size;
    guint64 block_count;
    guint64 free_blocks;
} BDFSExtInfo;

typedef struct BDFSExtInfo BDFSExt4Info;
typedef struct BDFSExtInfo BDFSExt3Info;
typedef struct BDFSExtInfo BDFSExt2Info;

BDFSExt2Info* bd_fs_ext2_info_copy (BDFSExt2Info *data);
void bd_fs_ext2_info_free (BDFSExt2Info *data);

BDFSExt3Info* bd_fs_ext3_info_copy (BDFSExt3Info *data);
void bd_fs_ext3_info_free (BDFSExt3Info *data);

BDFSExt4Info* bd_fs_ext4_info_copy (BDFSExt4Info *data);
void bd_fs_ext4_info_free (BDFSExt4Info *data);

gboolean bd_fs_ext2_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ext2_wipe (const gchar *device, GError **error);
gboolean bd_fs_ext2_check (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ext2_repair (const gchar *device, gboolean unsafe, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ext2_set_label (const gchar *device, const gchar *label, GError **error);
BDFSExt2Info* bd_fs_ext2_get_info (const gchar *device, GError **error);
gboolean bd_fs_ext2_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error);

gboolean bd_fs_ext3_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ext3_wipe (const gchar *device, GError **error);
gboolean bd_fs_ext3_check (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ext3_repair (const gchar *device, gboolean unsafe, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ext3_set_label (const gchar *device, const gchar *label, GError **error);
BDFSExt3Info* bd_fs_ext3_get_info (const gchar *device, GError **error);
gboolean bd_fs_ext3_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error);

gboolean bd_fs_ext4_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ext4_wipe (const gchar *device, GError **error);
gboolean bd_fs_ext4_check (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ext4_repair (const gchar *device, gboolean unsafe, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ext4_set_label (const gchar *device, const gchar *label, GError **error);
BDFSExt4Info* bd_fs_ext4_get_info (const gchar *device, GError **error);
gboolean bd_fs_ext4_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error);

#endif  /* BD_FS_EXT */
