#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_KBD
#define BD_KBD

GQuark bd_kbd_error_quark (void);
#define BD_KBD_ERROR bd_kbd_error_quark ()
typedef enum {
    BD_KBD_ERROR_INVAL,
    BD_KBD_ERROR_ZRAM_NOEXIST,
    BD_KBD_ERROR_ZRAM_INVAL,
    BD_KBD_ERROR_BCACHE_PARSE,
    BD_KBD_ERROR_BCACHE_SETUP_FAIL,
    BD_KBD_ERROR_BCACHE_DETACH_FAIL,
    BD_KBD_ERROR_BCACHE_NOT_ATTACHED,
    BD_KBD_ERROR_BCACHE_UUID,
    BD_KBD_ERROR_BCACHE_MODE_FAIL,
    BD_KBD_ERROR_BCACHE_MODE_INVAL,
    BD_KBD_ERROR_BCACHE_NOEXIST,
    BD_KBD_ERROR_BCACHE_INVAL,
    BD_KBD_ERROR_TECH_UNAVAIL,
} BDKBDError;

typedef enum {
    BD_KBD_MODE_WRITETHROUGH,
    BD_KBD_MODE_WRITEBACK,
    BD_KBD_MODE_WRITEAROUND,
    BD_KBD_MODE_NONE,
    BD_KBD_MODE_UNKNOWN,
} BDKBDBcacheMode;

/* see zRAM kernel documentation for details */
typedef struct BDKBDZramStats {
    guint64 disksize;
    guint64 num_reads;
    guint64 num_writes;
    guint64 invalid_io;
    guint64 zero_pages;
    guint64 max_comp_streams;
    gchar* comp_algorithm;
    guint64 orig_data_size;
    guint64 compr_data_size;
    guint64 mem_used_total;
} BDKBDZramStats;

BDKBDZramStats* bd_kbd_zram_stats_copy (BDKBDZramStats *data);
void bd_kbd_zram_stats_free (BDKBDZramStats *data);

typedef struct BDKBDBcacheStats {
    gchar *state;
    guint64 block_size;
    guint64 cache_size;
    guint64 cache_used;
    guint64 hits;
    guint64 misses;
    guint64 bypass_hits;
    guint64 bypass_misses;
} BDKBDBcacheStats;

BDKBDBcacheStats* bd_kbd_bcache_stats_copy (BDKBDBcacheStats *data);
void bd_kbd_bcache_stats_free (BDKBDBcacheStats *data);

typedef enum {
    BD_KBD_TECH_ZRAM = 0,
    BD_KBD_TECH_BCACHE,
} BDKBDTech;

typedef enum {
    BD_KBD_TECH_MODE_CREATE  = 1 << 0,
    BD_KBD_TECH_MODE_DESTROY = 1 << 1,
    BD_KBD_TECH_MODE_MODIFY  = 1 << 2,
    BD_KBD_TECH_MODE_QUERY   = 1 << 3,
} BDKBDTechMode;


/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * check_deps() - check plugin's dependencies, returning TRUE if satisfied
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_kbd_check_deps (void);
gboolean bd_kbd_init (void);
void bd_kbd_close (void);

gboolean bd_kbd_is_tech_avail (BDKBDTech tech, guint64 mode, GError **error);

gboolean bd_kbd_zram_create_devices (guint64 num_devices, const guint64 *sizes, const guint64 *nstreams, GError **error);
gboolean bd_kbd_zram_destroy_devices (GError **error);
gboolean bd_kbd_zram_add_device (guint64 size, guint64 nstreams, gchar **device, GError **error);
gboolean bd_kbd_zram_remove_device (const gchar *device, GError **error);
BDKBDZramStats* bd_kbd_zram_get_stats (const gchar *device, GError **error);

gboolean bd_kbd_bcache_create (const gchar *backing_device, const gchar *cache_device, const BDExtraArg **extra, const gchar **bcache_device, GError **error);
gboolean bd_kbd_bcache_attach (const gchar *c_set_uuid, const gchar *bcache_device, GError **error);
gboolean bd_kbd_bcache_detach (const gchar *bcache_device, gchar **c_set_uuid, GError **error);
gboolean bd_kbd_bcache_destroy (const gchar *bcache_device, GError **error);
BDKBDBcacheMode bd_kbd_bcache_get_mode (const gchar *bcache_device, GError **error);
const gchar* bd_kbd_bcache_get_mode_str (BDKBDBcacheMode mode, GError **error);
BDKBDBcacheMode bd_kbd_bcache_get_mode_from_str (const gchar *mode_str, GError **error);
gboolean bd_kbd_bcache_set_mode (const gchar *bcache_device, BDKBDBcacheMode mode, GError **error);
BDKBDBcacheStats* bd_kbd_bcache_status (const gchar *bcache_device, GError **error);
gchar* bd_kbd_bcache_get_backing_device (const gchar *bcache_device, GError **error);
gchar* bd_kbd_bcache_get_cache_device (const gchar *bcache_device, GError **error);

#endif  /* BD_KBD */
