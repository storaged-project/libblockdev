#include <glib.h>

#ifndef BD_SWAP
#define BD_SWAP

gboolean bd_swap_mkswap (gchar *device, gchar *label, gchar **error_message);
gboolean bd_swap_swapon (gchar *device, gint priority, gchar **error_message);
gboolean bd_swap_swapoff (gchar *device, gchar **error_message);
gboolean bd_swap_swapstatus (gchar *device, gchar **error_message);

#endif  /* BD_SWAP */
