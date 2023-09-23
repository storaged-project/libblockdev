#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_FS_BCACHEFS
#define BD_FS_BCACHEFS

typedef struct BDFSBcachefsInfo {
    gchar *uuid;
    guint64 size;
    guint64 free_space;
} BDFSBcachefsInfo;

BDFSBcachefsInfo* bd_fs_bcachefs_info_copy (BDFSBcachefsInfo *data);
void bd_fs_bcachefs_info_free (BDFSBcachefsInfo *data);

gboolean bd_fs_bcachefs_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
BDFSBcachefsInfo* bd_fs_bcachefs_get_info (const gchar *device, GError **error);

#endif  /* BD_FS_BCACHEFS */
