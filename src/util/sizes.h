#include <glib.h>

#ifndef BD_SIZES
#define BD_SIZES

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

gchar* bd_size_human_readable (guint64 size);

#endif /* BD_SIZES */
