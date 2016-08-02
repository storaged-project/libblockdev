#include <glib.h>
#include "extra_arg.h"

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
typedef void (*BDUtilsLogFunc) (gint level, const gchar *msg);

typedef enum {
    BD_UTILS_PROG_STARTED,
    BD_UTILS_PROG_PROGRESS,
    BD_UTILS_PROG_FINISHED,
} BDUtilsProgStatus;

/**
 * BDUtilsProgFunc:
 * @task_id: ID of the task/action the progress is reported for
 * @status: progress status
 * @completion: percentage of completion
 * @msg: (allow-none): arbitrary progress message (for the user)
 */
typedef void (*BDUtilsProgFunc) (guint64 task_id, BDUtilsProgStatus status, guint8 completion, gchar *msg);

/**
 * BDUtilsProgExtract:
 * @line: line from extract progress from
 * @completion: (out): percentage of completion
 *
 * Returns: whether the line was a progress reporting line or not
 */
typedef gboolean (*BDUtilsProgExtract) (const gchar *line, guint8 *completion);

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

gboolean bd_utils_exec_and_report_error (const gchar **argv, const BDExtraArg **extra, GError **error);
gboolean bd_utils_exec_and_report_error_no_progress (const gchar **argv, const BDExtraArg **extra, GError **error);
gboolean bd_utils_exec_and_report_status_error (const gchar **argv, const BDExtraArg **extra, gint *status, GError **error);
gboolean bd_utils_exec_and_capture_output (const gchar **argv, const BDExtraArg **extra, gchar **output, GError **error);
gboolean bd_exec_and_report_progress (const gchar **argv, const BDExtraArg **extra, BDUtilsProgExtract prog_extract, gint *proc_status, GError **error);
gboolean bd_utils_init_logging (BDUtilsLogFunc new_log_func, GError **error);
gint bd_utils_version_cmp (const gchar *ver_string1, const gchar *ver_string2, GError **error);
gboolean bd_utils_check_util_version (const gchar *util, const gchar *version, const gchar *version_arg, const gchar *version_regexp, GError **error);

gboolean bd_utils_init_prog_reporting (BDUtilsProgFunc new_prog_func, GError **error);
guint64 bd_utils_report_started (gchar *msg);
void bd_utils_report_progress (guint64 task_id, guint64 completion, gchar *msg);
void bd_utils_report_finished (guint64 task_id, gchar *msg);

#endif  /* BD_UTILS_EXEC */
