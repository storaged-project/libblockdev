#include <glib.h>

#ifndef BD_SWAP
#define BD_SWAP

#define MKSWAP_MIN_VERSION "2.25.2"
#define SWAPON_MIN_VERSION "2.25.2"
#define SWAPOFF_MIN_VERSION "2.25.2"

GQuark bd_swap_error_quark (void);
#define BD_SWAP_ERROR bd_swap_error_quark ()
typedef enum {
    BD_SWAP_ERROR_UNKNOWN_STATE,
    BD_SWAP_ERROR_ACTIVATE,
} BDSwapError;

gboolean bd_swap_mkswap (const gchar *device, const gchar *label, const BDExtraArg **extra, GError **error);
gboolean bd_swap_swapon (const gchar *device, gint priority, GError **error);
gboolean bd_swap_swapoff (const gchar *device, GError **error);
gboolean bd_swap_swapstatus (const gchar *device, GError **error);

#endif  /* BD_SWAP */
