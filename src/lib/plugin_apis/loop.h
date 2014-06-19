#include <glib.h>

/**
 * bd_loop_get_backing_file:
 * @dev_name: name of the loop device to get backing file for (e.g. "loop0")
 * @error_message: (out): variable to store error message to (if any)
 *
 */
gchar* bd_loop_get_backing_file (gchar *dev_name, gchar **error_message);

/**
 * bd_loop_get_loop_name:
 * @file: path of the backing file to get loop name for
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: (transfer full): name of the loop device associated with the given @file
 */
gchar* bd_loop_get_loop_name (gchar *file, gchar **error_message);

/**
 * bd_loop_setup:
 * @file: file to setup as a loop device
 * @loop_name: (out): if not %NULL, it is used to store the name of the loop device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @file was successfully setup as a loop device or not
 */
gboolean bd_loop_setup (gchar *file, gchar **loop_name, gchar **error_message);

/**
 * bd_loop_teardown:
 * @loop: path or name of the loop device to tear down
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @loop device was successfully torn down or not
 */
gboolean bd_loop_teardown (gchar *loop, gchar **error_message);
