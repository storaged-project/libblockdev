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


gboolean bd_fs_wipe (gchar *device, gboolean all, GError **error);

gboolean bd_fs_ext4_mkfs (gchar *device, BDExtraArg **extra, GError **error);
gboolean bd_fs_ext4_wipe (gchar *device, GError **error);
gboolean bd_fs_ext4_check (gchar *device, BDExtraArg **extra, GError **error);
gboolean bd_fs_ext4_repair (gchar *device, gboolean unsafe, BDExtraArg **extra, GError **error);
gboolean bd_fs_ext4_set_label (gchar *device, gchar *label, GError **error);
BDFSExt4Info* bd_fs_ext4_get_info (gchar *device, GError **error);
gboolean bd_fs_ext4_resize (gchar *device, guint64 new_size, BDExtraArg **extra, GError **error);

gboolean bd_fs_xfs_mkfs (gchar *device, BDExtraArg **extra, GError **error);
gboolean bd_fs_xfs_wipe (gchar *device, GError **error);
gboolean bd_fs_xfs_check (gchar *device, GError **error);
gboolean bd_fs_xfs_repair (gchar *device, BDExtraArg **extra, GError **error);
gboolean bd_fs_xfs_set_label (gchar *device, gchar *label, GError **error);
BDFSXfsInfo* bd_fs_xfs_get_info (gchar *device, GError **error);
gboolean bd_fs_xfs_resize (gchar *mpoint, guint64 new_size, BDExtraArg **extra, GError **error);

#endif  /* BD_PART */
