#include <glib.h>

#ifndef BD_KBD
#define BD_KBD

GQuark bd_kbd_error_quark (void);
#define BD_KBD_ERROR bd_kbd_error_quark ()
typedef enum {
    BD_KBD_ERROR_KMOD_INIT_FAIL,
    BD_KBD_ERROR_MODULE_FAIL,
    BD_KBD_ERROR_MODULE_NOEXIST,
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

gboolean bd_kbd_zram_create_devices (guint64 num_devices, guint64 *sizes, guint64 *nstreams, GError **error);
gboolean bd_kbd_zram_destroy_devices (GError **error);
BDKBDZramStats* bd_kbd_zram_get_stats (gchar *device, GError **error);

gboolean bd_kbd_bcache_create (gchar *backing_device, gchar *cache_device, gchar **bcache_device, GError **error);
gboolean bd_kbd_bcache_attach (gchar *c_set_uuid, gchar *bcache_device, GError **error);
gboolean bd_kbd_bcache_detach (gchar *bcache_device, gchar **c_set_uuid, GError **error);
gboolean bd_kbd_bcache_destroy (gchar *bcache_device, GError **error);
BDKBDBcacheMode bd_kbd_bcache_get_mode (gchar *bcache_device, GError **error);
const gchar* bd_kbd_bcache_get_mode_str (BDKBDBcacheMode mode, GError **error);
BDKBDBcacheMode bd_kbd_bcache_get_mode_from_str (gchar *mode_str, GError **error);
gboolean bd_kbd_bcache_set_mode (gchar *bcache_device, BDKBDBcacheMode mode, GError **error);
BDKBDBcacheStats* bd_kbd_bcache_status (gchar *bcache_device, GError **error);

#endif  /* BD_KBD */
