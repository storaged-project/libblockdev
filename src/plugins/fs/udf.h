#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_FS_UDF
#define BD_FS_UDF

typedef struct BDFSUdfInfo {
    gchar *label;
    gchar *uuid;
    gchar *revision;
    gchar *lvid;
    gchar *vid;
    guint64 block_size;
    guint64 block_count;
    guint64 free_blocks;
} BDFSUdfInfo;

BDFSUdfInfo* bd_fs_udf_info_copy (BDFSUdfInfo *data);
void bd_fs_udf_info_free (BDFSUdfInfo *data);

gboolean bd_fs_udf_mkfs (const gchar *device, const gchar *media_type, gchar *revision, guint64 block_size, const BDExtraArg **extra, GError **error);
gboolean bd_fs_udf_set_label (const gchar *device, const gchar *label, GError **error);
gboolean bd_fs_udf_check_label (const gchar *label, GError **error);
gboolean bd_fs_udf_set_uuid (const gchar *device, const gchar *uuid, GError **error);
gboolean bd_fs_udf_check_uuid ( const gchar *uuid, GError **error);
BDFSUdfInfo* bd_fs_udf_get_info (const gchar *device, GError **error);

#endif  /* BD_FS_UDF */
