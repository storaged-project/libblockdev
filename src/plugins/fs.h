#include <glib.h>

#ifndef BD_FS
#define BD_FS

GQuark bd_fs_error_quark (void);
#define BD_FS_ERROR bd_fs_error_quark ()
typedef enum {
    BD_FS_ERROR_INVAL,
    BD_FS_ERROR_PARSE,
    BD_FS_ERROR_FAIL,
} BDFsError;

typedef struct BDFSExt4Info {
    gchar *label;
    gchar *uuid;
    gchar *state;
    guint64 block_size;
    guint64 block_count;
    guint64 free_blocks;
} BDFSExt4Info;

BDFSExt4Info* bd_fs_ext4_info_copy (BDFSExt4Info *data);
void bd_fs_ext4_info_free (BDFSExt4Info *data);

typedef struct BDFSXfsInfo {
    gchar *label;
    gchar *uuid;
    guint64 block_size;
    guint64 block_count;
} BDFSXfsInfo;

BDFSXfsInfo* bd_fs_xfs_info_copy (BDFSXfsInfo *data);
void bd_fs_xfs_info_free (BDFSXfsInfo *data);



typedef struct BDFSVfatInfo {
    gchar *label;
    gchar *uuid;
    gchar *state;
    guint64 block_size;
    guint64 block_count;
    guint64 free_blocks;
} BDFSVfatInfo;

BDFSVfatInfo* bd_fs_vfat_info_copy (BDFSVfatInfo *data);
void bd_fs_vfat_info_free (BDFSVfatInfo *data);


gboolean bd_fs_wipe (const gchar *device, gboolean all, GError **error);

gboolean bd_fs_ext4_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ext4_wipe (const gchar *device, GError **error);
gboolean bd_fs_ext4_check (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ext4_repair (const gchar *device, gboolean unsafe, const BDExtraArg **extra, GError **error);
gboolean bd_fs_ext4_set_label (const gchar *device, const gchar *label, GError **error);
BDFSExt4Info* bd_fs_ext4_get_info (const gchar *device, GError **error);
gboolean bd_fs_ext4_resize (const gchar *device, guint64 new_size, const BDExtraArg **extra, GError **error);

gboolean bd_fs_xfs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_xfs_wipe (const gchar *device, GError **error);
gboolean bd_fs_xfs_check (const gchar *device, GError **error);
gboolean bd_fs_xfs_repair (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_xfs_set_label (const gchar *device, const gchar *label, GError **error);
BDFSXfsInfo* bd_fs_xfs_get_info (const gchar *device, GError **error);
gboolean bd_fs_xfs_resize (const gchar *mpoint, guint64 new_size, const BDExtraArg **extra, GError **error);

gboolean bd_fs_vfat_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_vfat_wipe (const gchar *device, GError **error);
gboolean bd_fs_vfat_check (const gchar *device, GError **error, const BDExtraArg **extra);
gboolean bd_fs_vfat_repair (const gchar *device, const BDExtraArg **extra, GError **error, gboolean unsafe);
gboolean bd_fs_vfat_set_label (const gchar *device, const gchar *label, GError **error);
BDFSVfatInfo* bd_fs_vfat_get_info (const gchar *device, GError **error);
gboolean bd_fs_vfat_resize (const gchar *mpoint, guint64 new_size, const BDExtraArg **extra, GError **error);


#endif  /* BD_PART */
