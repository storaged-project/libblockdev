#include <glib.h>

#ifndef BD_SWAP
#define BD_SWAP

/**
 * bd_swap_mkswap:
 * @device: a device to create swap space on
 * @label: (allow-none): a label for the swap space device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the swap space was successfully created or not
 */
gboolean bd_swap_mkswap (gchar *device, gchar *label, gchar **error_message);

/**
 * bd_swap_swapon:
 * @device: swap device to activate
 * @priority: priority of the activated device or -1 to use the default
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the swap device was successfully activated or not
 */
gboolean bd_swap_swapon (gchar *device, gint priority, gchar **error_message);

/**
 * bd_swap_swapoff:
 * @device: swap device to deactivate
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the swap device was successfully deactivated or not
 */
gboolean bd_swap_swapoff (gchar *device, gchar **error_message);

/**
 * bd_swap_swapstatus:
 * @device: swap device to get status of
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: #TRUE if the swap device is active, #FALSE if not active or failed
 * to determine (@error_message is set not a non-NULL value in such case)
 */
gboolean bd_swap_swapstatus (gchar *device, gchar **error_message);

#endif  /* BD_SWAP */
