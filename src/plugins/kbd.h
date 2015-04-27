#include <glib.h>

#ifndef BD_KBD
#define BD_KBD

GQuark bd_kbd_error_quark (void);
#define BD_KBD_ERROR bd_kbd_error_quark ()
typedef enum {
    BD_KBD_ERROR_KMOD_INIT_FAIL,
    BD_KBD_ERROR_MODULE_FAIL,
    BD_KBD_ERROR_MODULE_NOEXIST,
    BD_KBD_ERROR_BCACHE_PARSE,
    BD_KBD_ERROR_BCACHE_SETUP_FAIL,
    BD_KBD_ERROR_BCACHE_DETACH_FAIL,
    BD_KBD_ERROR_BCACHE_NOT_ATTACHED,
    BD_KBD_ERROR_BCACHE_UUID,
} BDKBDError;

gboolean bd_kbd_zram_create_devices (guint64 num_devices, guint64 *sizes, guint64 *nstreams, GError **error);
gboolean bd_kbd_zram_destroy_devices (GError **error);

gboolean bd_kbd_bcache_create (gchar *backing_device, gchar *cache_device, gchar **bcache_device, GError **error);
gboolean bd_kbd_bcache_attach (gchar *c_set_uuid, gchar *bcache_device, GError **error);
gboolean bd_kbd_bcache_detach (gchar *bcache_device, gchar **c_set_uuid, GError **error);
gboolean bd_kbd_bcache_destroy (gchar *bcache_device, GError **error);

#endif  /* BD_KBD */
