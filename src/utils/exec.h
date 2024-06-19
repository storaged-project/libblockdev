#include <glib.h>
#include "extra_arg.h"

#ifndef BD_UTILS_EXEC
#define BD_UTILS_EXEC

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
 * @msg: (nullable): arbitrary progress message (for the user)
 */
typedef void (*BDUtilsProgFunc) (guint64 task_id, BDUtilsProgStatus status, guint8 completion, gchar *msg);

/**
 * BDUtilsProgExtract:
 * @line: line to extract progress from
 * @completion: (out): percentage of completion
 *
 * Callback function used to process a line captured from spawned command's standard
 * output and standard error output. Typically used to extract completion percentage
 * of a long-running job.
 *
 * Note that both outputs are read simultaneously with no guarantees of message order
 * this function is called with.
 *
 * The value the @completion points to may contain value previously returned from
 * this callback or zero when called for the first time. This is useful for extractors
 * where only some kind of a tick mark is printed out as a progress and previous value
 * is needed to compute an incremented value. It's important to keep in mind that this
 * function is only called over lines, i.e. progress reporting printing out tick marks
 * (e.g. dots) without a newline character might not work properly.
 *
 * The @line string usually contains trailing newline character, which may be absent
 * however in case the spawned command exits without printing one. It's guaranteed
 * this function is called over remaining buffer no matter what the trailing
 * character is.
 *
 * Returns: whether the line was a progress reporting line and should be excluded
 *          from the collected standard output string or not.
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
    BD_UTILS_EXEC_ERROR_UTIL_CHECK_ERROR,
    BD_UTILS_EXEC_ERROR_UTIL_FEATURE_CHECK_ERROR,
    BD_UTILS_EXEC_ERROR_UTIL_FEATURE_UNAVAILABLE,
} BDUtilsExecError;

gboolean bd_utils_exec_and_report_error (const gchar **argv, const BDExtraArg **extra, GError **error);
gboolean bd_utils_exec_and_report_error_no_progress (const gchar **argv, const BDExtraArg **extra, GError **error);
gboolean bd_utils_exec_and_report_status_error (const gchar **argv, const BDExtraArg **extra, gint *status, GError **error);
gboolean bd_utils_exec_and_capture_output (const gchar **argv, const BDExtraArg **extra, gchar **output, GError **error);
gboolean bd_utils_exec_and_capture_output_no_progress (const gchar **argv, const BDExtraArg **extra, gchar **output, gchar **stderr, gint *status, GError **error);
gboolean bd_utils_exec_and_report_progress (const gchar **argv, const BDExtraArg **extra, BDUtilsProgExtract prog_extract, gint *proc_status, GError **error);
gboolean bd_utils_exec_with_input (const gchar **argv, const gchar *input, const BDExtraArg **extra, GError **error);
gint bd_utils_version_cmp (const gchar *ver_string1, const gchar *ver_string2, GError **error);
gboolean bd_utils_check_util_version (const gchar *util, const gchar *version, const gchar *version_arg, const gchar *version_regexp, GError **error);

gboolean bd_utils_init_prog_reporting (BDUtilsProgFunc new_prog_func, GError **error);
gboolean bd_utils_init_prog_reporting_thread (BDUtilsProgFunc new_prog_func, GError **error);
gboolean bd_utils_mute_prog_reporting_thread (GError **error);
gboolean bd_utils_prog_reporting_initialized (void);
guint64 bd_utils_report_started (const gchar *msg);
void bd_utils_report_progress (guint64 task_id, guint64 completion, const gchar *msg);
void bd_utils_report_finished (guint64 task_id, const gchar *msg);

guint64 bd_utils_get_next_task_id (void);
void bd_utils_log_task_status (guint64 task_id, const gchar *msg);

gboolean bd_utils_echo_str_to_file (const gchar *str, const gchar *file_path, GError **error);

#endif  /* BD_UTILS_EXEC */
