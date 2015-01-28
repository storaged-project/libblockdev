#include <glib.h>
#include <dmraid/dmraid.h>

#ifndef BD_DM
#define BD_DM

GQuark bd_dm_error_quark (void);
#define BD_DM_ERROR bd_dm_error_quark ()
typedef enum {
    BD_DM_ERROR_SYS,
    BD_DM_ERROR_NOT_ROOT,
    BD_DM_ERROR_TASK,
    BD_DM_ERROR_RAID_FAIL,
    BD_DM_ERROR_RAID_NO_DEVS,
    BD_DM_ERROR_RAID_NO_EXIST,
} BDDMError;

gboolean bd_dm_create_linear (gchar *map_name, gchar *device, guint64 length, gchar *uuid, GError **error);
gboolean bd_dm_remove (gchar *map_name, GError **error);
gchar* bd_dm_name_from_node (gchar *dm_node, GError **error);
gchar* bd_dm_node_from_name (gchar *map_name, GError **error);
gboolean bd_dm_map_exists (gchar *map_name, gboolean live_only, gboolean active_only, GError **error);
gchar** bd_dm_get_member_raid_sets (gchar *name, gchar *uuid, gint major, gint minor, GError **error);
gboolean bd_dm_activate_raid_set (gchar *name, GError **error);
gboolean bd_dm_deactivate_raid_set (gchar *name, GError **error);
gchar* bd_dm_get_raid_set_type (gchar *name, GError **error);

#endif  /* BD_DM */
