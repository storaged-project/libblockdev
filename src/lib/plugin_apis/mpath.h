#include <glib.h>

/**
 * bd_mpath_flush_mpaths:
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether multipath device maps were successfully flushed or not
 *
 * Flushes all unused multipath device maps.
 */
gboolean bd_mpath_flush_mpaths (gchar **error_message);

/**
 * bd_mpath_is_mpath_member:
 * @device: device to test
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: %TRUE if the device is a multipath member, %FALSE if not or an error
 * appeared when queried (@error_message is set in those cases)
 */
gboolean bd_mpath_is_mpath_member (gchar *device, gchar **error_message);

/**
 * bd_mpath_set_friendly_names:
 * @enabled: whether friendly names should be enabled or not
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: if successfully set or not
 */
gboolean bd_mpath_set_friendly_names (gboolean enabled, gchar **error_message);
