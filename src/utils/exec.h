#include <glib.h>

#ifndef BD_UTILS_EXEC
#define BD_UTILS_EXEC

gboolean bd_utils_exec_and_report_error (gchar **argv, gchar **error_message);
gboolean bd_utils_exec_and_capture_output (gchar **argv, gchar **output, gchar **error_message);

#endif  /* BD_UTILS_EXEC */
