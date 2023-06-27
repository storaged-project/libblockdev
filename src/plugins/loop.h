#include <glib.h>

#ifndef BD_LOOP
#define BD_LOOP

GQuark bd_loop_error_quark (void);
#define BD_LOOP_ERROR bd_loop_error_quark ()
typedef enum {
    BD_LOOP_ERROR_TECH_UNAVAIL,
    BD_LOOP_ERROR_FAIL,
    BD_LOOP_ERROR_DEVICE,
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

/**
 * BDLoopInfo:
 * @backing_file: backing file for the give loop device;
 * @offset: offset of the start of the device (in @backing_file);
 * @autoclear: whether the autoclear flag is set or not;
 * @direct_io: whether direct IO is enabled or not;
 * @part_scan: whether the partition scan is enforced or not;
 * @read_only: whether the device is read-only or not;
 */
typedef struct BDLoopInfo {
    gchar *backing_file;
    guint64 offset;
    gboolean autoclear;
    gboolean direct_io;
    gboolean part_scan;
    gboolean read_only;
} BDLoopInfo;


void bd_loop_info_free (BDLoopInfo *info);
BDLoopInfo* bd_loop_info_copy (BDLoopInfo *info);


/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_loop_init (void);
void bd_loop_close (void);

gboolean bd_loop_is_tech_avail (BDLoopTech tech, guint64 mode, GError **error);

BDLoopInfo* bd_loop_info (const gchar *loop, GError **error);

gchar* bd_loop_get_loop_name (const gchar *file, GError **error);
gboolean bd_loop_setup (const gchar *file, guint64 offset, guint64 size, gboolean read_only, gboolean part_scan, guint64 sector_size, const gchar **loop_name, GError **error);
gboolean bd_loop_setup_from_fd (gint fd, guint64 offset, guint64 size, gboolean read_only, gboolean part_scan, guint64 sector_size, const gchar **loop_name, GError **error);
gboolean bd_loop_teardown (const gchar *loop, GError **error);

gboolean bd_loop_set_autoclear (const gchar *loop, gboolean autoclear, GError **error);

#endif  /* BD_LOOP */
