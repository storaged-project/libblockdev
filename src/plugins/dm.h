#include <glib.h>

#ifndef BD_DM
#define BD_DM

gboolean bd_dm_create_linear (gchar *map_name, gchar *device, guint64 length, gchar *uuid, gchar **error_message);
gboolean bd_dm_remove (gchar *map_name, gchar **error_message);
gchar* bd_dm_name_from_dm_node (gchar *dm_node, gchar **error_message);
gchar* bd_dm_node_from_name (gchar *map_name, gchar **error_message);
gboolean bd_dm_map_exists (gchar *map_name, gboolean live_only, gboolean active_only, gchar **error_message);

#endif  /* BD_DM */
