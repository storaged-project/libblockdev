#include <glib.h>

#ifndef BD_LOOP
#define BD_LOOP

gchar* bd_loop_get_backing_file (gchar *dev_name, gchar **error_message);

#endif  /* BD_LOOP */
