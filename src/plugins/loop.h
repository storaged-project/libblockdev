#include <glib.h>

#ifndef BD_LOOP
#define BD_LOOP

gchar* bd_loop_get_backing_file (gchar *dev_name, gchar **error_message);
gchar* bd_loop_get_loop_name (gchar *file, gchar **error_message);
gboolean bd_loop_setup (gchar *file, gchar **loop_name, gchar **error_message);
gboolean bd_loop_teardown (gchar *loop, gchar **error_message);

#endif  /* BD_LOOP */
