#include <glib.h>

#ifndef BD_MD
#define BD_MD

#define BD_MD_ERROR bd_md_error_quark ()
typedef enum {
    BD_MD_ERROR_BASE,
} BDMDError;

/* taken from blivet */
// these defaults were determined empirically
#define MD_SUPERBLOCK_SIZE (2 MiB)
#define MD_CHUNK_SIZE (512 KiB)

guint64 bd_md_get_superblock_size (guint64 size, gchar *version);
gboolean bd_md_create (gchar *device_name, gchar *level, gchar **disks, guint64 spares, gchar *version, gboolean bitmap, GError **error);
gboolean bd_md_destroy (gchar *device, GError **error);
gboolean bd_md_deactivate (gchar *device_name, GError **error);
gboolean bd_md_activate (gchar *device_name, gchar **members, gchar *uuid, GError **error);

#endif  /* BD_MD */
