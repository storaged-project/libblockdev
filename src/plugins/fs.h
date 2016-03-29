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

gboolean bd_fs_wipe (gchar *device, gboolean all, GError **error);
gboolean bd_fs_ext4_mkfs (gchar *device, GError **error);
gboolean bd_fs_ext4_wipe (gchar *device, GError **error);
gboolean bd_fs_ext4_check (gchar *device, GError **error);
gboolean bd_fs_ext4_repair (gchar *device, gboolean unsafe, GError **error);
gboolean bd_fs_ext4_set_label (gchar *device, gchar *label, GError **error);
BDFSExt4Info* bd_fs_ext4_get_info (gchar *device, GError **error);
gboolean bd_fs_ext4_resize (gchar *device, guint64 new_size, GError **error);

#endif  /* BD_PART */
