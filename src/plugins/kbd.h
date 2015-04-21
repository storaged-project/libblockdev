#include <glib.h>

#ifndef BD_KBD
#define BD_KBD

GQuark bd_kbd_error_quark (void);
#define BD_KBD_ERROR bd_kbd_error_quark ()
typedef enum {
    BD_KBD_ERROR_KMOD_INIT_FAIL,
    BD_KBD_ERROR_MODULE_FAIL,
    BD_KBD_ERROR_MODULE_NOEXIST,
} BDKBDError;

gboolean bd_kbd_zram_create_devices (guint64 num_devices, guint64 *sizes, guint64 *nstreams, GError **error);

#endif  /* BD_KBD */
