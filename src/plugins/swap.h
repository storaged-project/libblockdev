#include <glib.h>

#ifndef BD_SWAP
#define BD_SWAP

#define BD_SWAP_ERROR bd_swap_error_quark ()
typedef enum {
    BD_SWAP_ERROR_UNKNOWN_STATE,
    BD_SWAP_ERROR_ACTIVATE,
} BDSwapError;

gboolean bd_swap_mkswap (gchar *device, gchar *label, GError **error);
gboolean bd_swap_swapon (gchar *device, gint priority, GError **error);
gboolean bd_swap_swapoff (gchar *device, GError **error);
gboolean bd_swap_swapstatus (gchar *device, GError **error);

#endif  /* BD_SWAP */
