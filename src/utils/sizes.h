#include <glib.h>

#ifndef BD_UTILS_SIZES
#define BD_UTILS_SIZES

#define KIBIBYTE *1024ULL
#define MEBIBYTE *1024ULL KIBIBYTE
#define GIBIBYTE *1024ULL MEBIBYTE
#define TEBIBYTE *1024ULL GIBIBYTE
#define PEBIBYTE *1024ULL TEBIBYTE
#define EXBIBYTE *1024ULL PEBIBYTE

#define KiB KIBIBYTE
#define MiB MEBIBYTE
#define GiB GIBIBYTE
#define TiB TEBIBYTE
#define PiB PEBIBYTE
#define EiB EXBIBYTE

#define BD_UTILS_SIZE_ERROR bd_utils_size_error_quark ()
typedef enum {
    BD_UTILS_SIZE_ERROR_INVALID_SPEC,
} BDUtilsSizeError;

gchar* bd_utils_size_human_readable (guint64 size);
guint64 bd_utils_size_from_spec (gchar *spec, GError **error);

#endif /* BD_UTILS_SIZES */
