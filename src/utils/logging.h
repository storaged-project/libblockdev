#include <glib.h>
#include <syslog.h>

#ifndef BD_UTILS_LOGGING
#define BD_UTILS_LOGGING

/* These are same as syslog levels, unfortunately GObject Introspection
 * doesn't work with "redefined" constants so we can't use syslog
 * constants here.
 */
#define BD_UTILS_LOG_EMERG   0
#define BD_UTILS_LOG_ALERT   1
#define BD_UTILS_LOG_CRIT    2
#define BD_UTILS_LOG_ERR     3
#define BD_UTILS_LOG_WARNING 4
#define BD_UTILS_LOG_NOTICE  5
#define BD_UTILS_LOG_INFO    6
#define BD_UTILS_LOG_DEBUG   7

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

void bd_utils_set_log_level (gint level);

void bd_utils_log (gint level, const gchar *msg);
void bd_utils_log_format (gint level, const gchar *format, ...) G_GNUC_PRINTF (2, 3);
void bd_utils_log_stdout (gint level, const gchar *msg);

#endif  /* BD_UTILS_LOGGING */
