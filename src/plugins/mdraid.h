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

#endif  /* BD_MD */
