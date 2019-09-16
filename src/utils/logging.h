#include <glib.h>

#ifndef BD_UTILS_LOGGING
#define BD_UTILS_LOGGING

/**
 * BDUtilsLogFunc:
 * @level: log level (as understood by syslog(3))
 * @msg: log message
 *
 * Function type for logging function used by the libblockdev's exec utils to
 * log the information about program executing.
 */
typedef void (*BDUtilsLogFunc) (gint level, const gchar *msg);

gboolean bd_utils_init_logging (BDUtilsLogFunc new_log_func, GError **error);

void bd_utils_log (gint level, const gchar *msg);

#endif  /* BD_UTILS_LOGGING */
