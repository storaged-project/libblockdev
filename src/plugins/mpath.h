#include <glib.h>

#ifndef BD_MPATH
#define BD_MPATH

GQuark bd_mpath_error_quark (void);
#define BD_MPATH_ERROR bd_mpath_error_quark ()
typedef enum {
    BD_MPATH_ERROR_TECH_UNAVAIL,
    BD_MPATH_ERROR_INVAL,
    BD_MPATH_ERROR_FLUSH,
    BD_MPATH_ERROR_NOT_ROOT,
    BD_MPATH_ERROR_DM_ERROR,
} BDMpathError;

typedef enum {
    BD_MPATH_TECH_BASE = 0,
    BD_MPATH_TECH_FRIENDLY_NAMES,
} BDMpathTech;

typedef enum {
    BD_MPATH_TECH_MODE_QUERY  = 1 << 0,
    BD_MPATH_TECH_MODE_MODIFY = 1 << 1,
} BDMpathTechMode;


/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_mpath_init (void);
void bd_mpath_close (void);

gboolean bd_mpath_is_tech_avail (BDMpathTech tech, guint64 mode, GError **error);

gboolean bd_mpath_flush_mpaths (GError **error);
gboolean bd_mpath_is_mpath_member (const gchar *device, GError **error);
gchar** bd_mpath_get_mpath_members (GError **error);
gboolean bd_mpath_set_friendly_names (gboolean enabled, GError **error);

#endif  /* BD_MPATH */
