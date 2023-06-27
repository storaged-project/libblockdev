#include <glib.h>

#ifndef BD_DM
#define BD_DM

GQuark bd_dm_error_quark (void);
#define BD_DM_ERROR bd_dm_error_quark ()
typedef enum {
    BD_DM_ERROR_TECH_UNAVAIL,
    BD_DM_ERROR_SYS,
    BD_DM_ERROR_NOT_ROOT,
    BD_DM_ERROR_TASK,
    BD_DM_ERROR_RAID_FAIL,
    BD_DM_ERROR_RAID_NO_DEVS,
    BD_DM_ERROR_RAID_NO_EXIST,
} BDDMError;

typedef enum {
    BD_DM_TECH_MAP = 0,
} BDDMTech;

typedef enum {
    BD_DM_TECH_MODE_CREATE_ACTIVATE   = 1 << 0,
    BD_DM_TECH_MODE_REMOVE_DEACTIVATE = 1 << 1,
    BD_DM_TECH_MODE_QUERY             = 1 << 2,
} BDDMTechMode;

/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_dm_init (void);
void bd_dm_close (void);

gboolean bd_dm_is_tech_avail (BDDMTech tech, guint64 mode, GError **error);

gboolean bd_dm_create_linear (const gchar *map_name, const gchar *device, guint64 length, const gchar *uuid, GError **error);
gboolean bd_dm_remove (const gchar *map_name, GError **error);
gboolean bd_dm_map_exists (const gchar *map_name, gboolean live_only, gboolean active_only, GError **error);
gchar* bd_dm_name_from_node (const gchar *dm_node, GError **error);
gchar* bd_dm_node_from_name (const gchar *map_name, GError **error);
gchar* bd_dm_get_subsystem_from_name (const gchar *device_name, GError **error);

#endif  /* BD_DM */
