#include <glib.h>

#ifndef BD_LOOP
#define BD_LOOP

#define BD_LOOP_ERROR bd_loop_error_quark ()
typedef enum {
    BD_LOOP_ERROR_SYS,
    BD_LOOP_ERROR_DEVICE,
} BDLoopError;

gchar* bd_loop_get_backing_file (gchar *dev_name, GError **error);
gchar* bd_loop_get_loop_name (gchar *file, GError **error);
gboolean bd_loop_setup (gchar *file, gchar **loop_name, GError **error);
gboolean bd_loop_teardown (gchar *loop, GError **error);

#endif  /* BD_LOOP */
