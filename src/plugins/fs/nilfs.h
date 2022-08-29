#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_FS_NILFS
#define BD_FS_NILFS

typedef struct BDFSNILFS2Info {
    gchar *label;
    gchar *uuid;
    guint64 size;
    guint64 block_size;
    guint64 free_blocks;
} BDFSNILFS2Info;

BDFSNILFS2Info* bd_fs_nilfs2_info_copy (BDFSNILFS2Info *data);
void bd_fs_nilfs2_info_free (BDFSNILFS2Info *data);

gboolean bd_fs_nilfs2_mkfs (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_fs_nilfs2_set_label (const gchar *device, const gchar *label, GError **error);
gboolean bd_fs_nilfs2_check_label (const gchar *label, GError **error);
gboolean bd_fs_nilfs2_set_uuid (const gchar *device, const gchar *uuid, GError **error);
gboolean bd_fs_nilfs2_check_uuid (const gchar *uuid, GError **error);
BDFSNILFS2Info* bd_fs_nilfs2_get_info (const gchar *device, GError **error);
gboolean bd_fs_nilfs2_resize (const gchar *device, guint64 new_size, GError **error);

#endif  /* BD_FS_NILFS */
