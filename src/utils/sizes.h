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

gchar* bd_utils_size_human_readable (guint64 size);
guint64 bd_utils_size_from_spec (gchar *spec, gchar **error_message);

#endif /* BD_UTILS_SIZES */
