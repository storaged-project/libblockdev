#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_FS_VFAT
#define BD_FS_VFAT

typedef struct BDFSVfatInfo {
    gchar *label;
    gchar *uuid;
    guint64 cluster_size;
    guint64 cluster_count;
    guint64 free_cluster_count;
} BDFSVfatInfo;

BDFSVfatInfo* bd_fs_vfat_info_copy (BDFSVfatInfo *data);
void bd_fs_vfat_info_free (BDFSVfatInfo *data);

gboolean bd_fs_vfat_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_vfat_wipe (const gchar *device, GError **error);
gboolean bd_fs_vfat_check (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_vfat_repair (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_vfat_set_label (const gchar *device, const gchar *label, GError **error);
BDFSVfatInfo* bd_fs_vfat_get_info (const gchar *device, GError **error);
gboolean bd_fs_vfat_resize (const gchar *device, guint64 new_size, GError **error);

#endif  /* BD_FS_VFAT */
