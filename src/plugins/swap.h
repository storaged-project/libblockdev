#include <glib.h>

/**
 * bd_swap_mkswap:
 * @device: a device to create swap space on
 * @label: (allow-none): a label for the swap space device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Return: whether the swap space was successfully created or not
 */
gboolean bd_swap_mkswap (gchar *device, gchar *label, gchar **error_message);
