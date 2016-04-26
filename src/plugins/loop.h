#include <glib.h>

#ifndef BD_LOOP
#define BD_LOOP

#define LOSETUP_MIN_VERSION "2.25.2"

GQuark bd_loop_error_quark (void);
#define BD_LOOP_ERROR bd_loop_error_quark ()
typedef enum {
    BD_LOOP_ERROR_DEVICE,
} BDLoopError;

gchar* bd_loop_get_backing_file (const gchar *dev_name, GError **error);
gchar* bd_loop_get_loop_name (const gchar *file, GError **error);
gboolean bd_loop_setup (const gchar *file, const gchar **loop_name, GError **error);
gboolean bd_loop_teardown (const gchar *loop, GError **error);

#endif  /* BD_LOOP */
