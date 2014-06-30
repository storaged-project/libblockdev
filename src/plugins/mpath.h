#include <glib.h>

#ifndef BD_MPATH
#define BD_MPATH

gboolean bd_mpath_flush_mpaths (gchar **error_message);
gboolean bd_mpath_is_mpath_member (gchar *device, gchar **error_message);
gboolean bd_mpath_set_friendly_names (gboolean enabled, gchar **error_message);

#endif  /* BD_MPATH */
