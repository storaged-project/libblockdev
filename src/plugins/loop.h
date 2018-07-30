#include <glib.h>

#ifndef BD_LOOP
#define BD_LOOP

#define LOSETUP_MIN_VERSION "2.23.2"

GQuark bd_loop_error_quark (void);
#define BD_LOOP_ERROR bd_loop_error_quark ()
typedef enum {
    BD_LOOP_ERROR_DEVICE,
    BD_LOOP_ERROR_FAIL,
    BD_LOOP_ERROR_TECH_UNAVAIL,
} BDLoopError;

typedef enum {
    BD_LOOP_TECH_LOOP = 0,
} BDLoopTech;

typedef enum {
    BD_LOOP_TECH_MODE_CREATE  = 1 << 0,
    BD_LOOP_TECH_MODE_DESTROY = 1 << 1,
    BD_LOOP_TECH_MODE_MODIFY  = 1 << 2,
    BD_LOOP_TECH_MODE_QUERY   = 1 << 3,
} BDLoopTechMode;


/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * check_deps() - check plugin's dependencies, returning TRUE if satisfied
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_loop_check_deps (void);
gboolean bd_loop_init (void);
void bd_loop_close (void);

gboolean bd_loop_is_tech_avail (BDLoopTech tech, guint64 mode, GError **error);

gchar* bd_loop_get_backing_file (const gchar *dev_name, GError **error);
gchar* bd_loop_get_loop_name (const gchar *file, GError **error);
gboolean bd_loop_setup (const gchar *file, guint64 offset, guint64 size, gboolean read_only, gboolean part_scan, const gchar **loop_name, GError **error);
gboolean bd_loop_setup_from_fd (gint fd, guint64 offset, guint64 size, gboolean read_only, gboolean part_scan, const gchar **loop_name, GError **error);
gboolean bd_loop_teardown (const gchar *loop, GError **error);

gboolean bd_loop_get_autoclear (const gchar *loop, GError **error);
gboolean bd_loop_set_autoclear (const gchar *loop, gboolean autoclear, GError **error);

#endif  /* BD_LOOP */
