#include <glib.h>

/* BpG-skip */
#define BD_LOOP_ERROR bd_loop_error_quark ()
typedef enum {
    BD_LOOP_ERROR_SYS,
    BD_LOOP_ERROR_DEVICE,
} BDLoopError;
/* BpG-skip-end */

/**
 * bd_loop_get_backing_file:
 * @dev_name: name of the loop device to get backing file for (e.g. "loop0")
 * @error: (out): place to store error (if any)
 *
 */
gchar* bd_loop_get_backing_file (gchar *dev_name, GError **error);

/**
 * bd_loop_get_loop_name:
 * @file: path of the backing file to get loop name for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): name of the loop device associated with the given @file
 */
gchar* bd_loop_get_loop_name (gchar *file, GError **error);

/**
 * bd_loop_setup:
 * @file: file to setup as a loop device
 * @loop_name: (out): if not %NULL, it is used to store the name of the loop device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @file was successfully setup as a loop device or not
 */
gboolean bd_loop_setup (gchar *file, gchar **loop_name, GError **error);

/**
 * bd_loop_teardown:
 * @loop: path or name of the loop device to tear down
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @loop device was successfully torn down or not
 */
gboolean bd_loop_teardown (gchar *loop, GError **error);
