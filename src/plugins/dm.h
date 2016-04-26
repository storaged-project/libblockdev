#include <glib.h>
#include <dmraid/dmraid.h>

#ifndef BD_DM
#define BD_DM

#define DM_MIN_VERSION "1.02.93"

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

gboolean bd_dm_create_linear (const gchar *map_name, const gchar *device, guint64 length, const gchar *uuid, GError **error);
gboolean bd_dm_remove (const gchar *map_name, GError **error);
gchar* bd_dm_name_from_node (const gchar *dm_node, GError **error);
gchar* bd_dm_node_from_name (const gchar *map_name, GError **error);
gboolean bd_dm_map_exists (const gchar *map_name, gboolean live_only, gboolean active_only, GError **error);
gchar** bd_dm_get_member_raid_sets (const gchar *name, const gchar *uuid, gint major, gint minor, GError **error);
gboolean bd_dm_activate_raid_set (const gchar *name, GError **error);
gboolean bd_dm_deactivate_raid_set (const gchar *name, GError **error);
gchar* bd_dm_get_raid_set_type (const gchar *name, GError **error);

#endif  /* BD_DM */
