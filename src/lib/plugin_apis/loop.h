#include <glib.h>

/**
 * bd_loop_get_backing_file:
 * @dev_name: name of the loop device to get backing file for (e.g. "loop0")
 * @error_message: (out): variable to store error message to (if any)
 *
 */
gchar* bd_loop_get_backing_file (gchar *dev_name, gchar **error_message);
