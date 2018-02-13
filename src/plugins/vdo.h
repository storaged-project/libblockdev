#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_VDO
#define BD_VDO

GQuark bd_vdo_error_quark (void);
#define BD_VDO_ERROR bd_vdo_error_quark ()
typedef enum {
    BD_VDO_ERROR_FAIL,
    BD_VDO_ERROR_TECH_UNAVAIL,
} BDVDOError;

typedef enum {
    BD_VDO_TECH_VDO = 0,
} BDVDOTech;

typedef enum {
    BD_VDO_TECH_MODE_CREATE              = 1 << 0,
    BD_VDO_TECH_MODE_REMOVE              = 1 << 1,
    BD_VDO_TECH_MODE_ACTIVATE_DEACTIVATE = 1 << 2,
    BD_VDO_TECH_MODE_QUERY               = 1 << 3,
} BDVDOTechMode;

/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * check_deps() - check plugin's dependencies, returning TRUE if satisfied
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_vdo_check_deps ();
gboolean bd_vdo_init ();
void bd_vdo_close ();

gboolean bd_vdo_is_tech_avail (BDVDOTech tech, guint64 mode, GError **error);

#endif  /* BD_VDO */
