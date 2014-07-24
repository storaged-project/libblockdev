#include <glib.h>
#include <dmraid/dmraid.h>

#ifndef BD_DM
#define BD_DM

/* macros taken from the pyblock/dmraid.h file plus one more*/
#define for_each_raidset(_c, _n) list_for_each_entry(_n, LC_RS(_c), list)
#define for_each_subset(_rs, _n) list_for_each_entry(_n, &(_rs)->sets, list)
#define for_each_device(_rs, _d) list_for_each_entry(_d, &(_rs)->devs, devs)

gboolean bd_dm_create_linear (gchar *map_name, gchar *device, guint64 length, gchar *uuid, gchar **error_message);
gboolean bd_dm_remove (gchar *map_name, gchar **error_message);
gchar* bd_dm_name_from_node (gchar *dm_node, gchar **error_message);
gchar* bd_dm_node_from_name (gchar *map_name, gchar **error_message);
gboolean bd_dm_map_exists (gchar *map_name, gboolean live_only, gboolean active_only, gchar **error_message);
gchar** bd_dm_get_member_raid_sets (gchar *name, gchar *uuid, gint major, gint minor, gchar **error_message);

#endif  /* BD_DM */
