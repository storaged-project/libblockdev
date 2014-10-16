#include <glib.h>

#ifndef BD_UTILS_EXEC
#define BD_UTILS_EXEC

/**
 * BDUtilsLogFunc:
 * @level: log level (as understood by syslog(3))
 * @msg: log message
 *
 * Function type for logging function used by the libblockdev's exec utils to
 * log the information about program executing.
 */
typedef void (*BDUtilsLogFunc) (gint level, gchar *msg);

gboolean bd_utils_exec_and_report_error (gchar **argv, gchar **error_message);
gboolean bd_utils_exec_and_capture_output (gchar **argv, gchar **output, gchar **error_message);
gboolean bd_utils_init_logging (BDUtilsLogFunc new_log_func, gchar **error_message);

#endif  /* BD_UTILS_EXEC */
