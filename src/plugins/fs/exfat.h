#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_FS_EXFAT
#define BD_FS_EXFAT

typedef struct BDFSExfatInfo {
    gchar *label;
    gchar *uuid;
    guint64 sector_size;
    guint64 sector_count;
    guint64 cluster_count;
} BDFSExfatInfo;

BDFSExfatInfo* bd_fs_exfat_info_copy (BDFSExfatInfo *data);
void bd_fs_exfat_info_free (BDFSExfatInfo *data);

gboolean bd_fs_exfat_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_exfat_check (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_exfat_repair (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_exfat_set_label (const gchar *device, const gchar *label, GError **error);
gboolean bd_fs_exfat_check_label (const gchar *label, GError **error);
gboolean bd_fs_exfat_set_uuid (const gchar *device, const gchar *uuid, GError **error);
gboolean bd_fs_exfat_check_uuid (const gchar *uuid, GError **error);
BDFSExfatInfo* bd_fs_exfat_get_info (const gchar *device, GError **error);

#endif  /* BD_FS_EXFAT */
