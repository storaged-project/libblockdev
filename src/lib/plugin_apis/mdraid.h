#include <glib.h>

/* BpG-skip */
#define BD_MD_ERROR bd_md_error_quark ()
typedef enum {
    BD_MD_ERROR_BASE,
} BDMDError;
/* BpG-skip-end */

/**
 * bd_md_get_superblock_size:
 * @size: size of the array
 * @version: (allow-none): metadata version or %NULL to use the current default version
 *
 * Returns: Calculated superblock size for given array @size and metadata @version
 * or default if unsupported @version is used.
 */
guint64 bd_md_get_superblock_size (guint64 size, gchar *version);
