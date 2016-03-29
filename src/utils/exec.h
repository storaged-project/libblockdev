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

GQuark bd_utils_exec_error_quark (void);
#define BD_UTILS_EXEC_ERROR bd_utils_exec_error_quark ()
typedef enum {
    BD_UTILS_EXEC_ERROR_FAILED,
    BD_UTILS_EXEC_ERROR_NOOUT,
    BD_UTILS_EXEC_ERROR_INVAL_VER,
    BD_UTILS_EXEC_ERROR_UTIL_UNAVAILABLE,
    BD_UTILS_EXEC_ERROR_UTIL_UNKNOWN_VER,
    BD_UTILS_EXEC_ERROR_UTIL_LOW_VER,
} BDUtilsExecError;

gboolean bd_utils_exec_and_report_error (gchar **argv, GError **error);
gboolean bd_utils_exec_and_report_status_error (gchar **argv, gint *status, GError **error);
gboolean bd_utils_exec_and_capture_output (gchar **argv, gchar **output, GError **error);
gboolean bd_utils_init_logging (BDUtilsLogFunc new_log_func, GError **error);
gint bd_utils_version_cmp (gchar *ver_string1, gchar *ver_string2, GError **error);
gboolean bd_utils_check_util_version (gchar *util, gchar *version, gchar *version_arg, gchar *version_regexp, GError **error);

#endif  /* BD_UTILS_EXEC */
