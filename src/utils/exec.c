/*
 * Copyright (C) 2014  Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <glib.h>
#include "exec.h"
#include <syslog.h>

static GMutex id_counter_lock;
static guint64 id_counter = 0;
static BDUtilsLogFunc log_func = NULL;

/**
 * bd_utils_exec_error_quark: (skip)
 */
GQuark bd_utils_exec_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-utils-exec-error-quark");
}

/**
 * log_running: (skip)
 *
 * Returns: id of the running task
 */
static guint64 log_running (gchar **argv) {
    guint64 task_id = 0;
    gchar *str_argv = NULL;
    gchar *log_msg = NULL;

    g_mutex_lock (&id_counter_lock);
    id_counter++;
    task_id = id_counter;
    g_mutex_unlock (&id_counter_lock);

    if (log_func) {
        str_argv = g_strjoinv (" ", argv);
        log_msg = g_strdup_printf ("Running [%"G_GUINT64_FORMAT"] %s ...", task_id, str_argv);
        log_func (LOG_INFO, log_msg);
        g_free (str_argv);
        g_free (log_msg);
    }

    return task_id;
}

/**
 * log_done: (skip)
 *
 */
static void log_done (guint64 task_id) {
    gchar *log_msg = NULL;

    if (log_func) {
        log_msg = g_strdup_printf ("...done [%"G_GUINT64_FORMAT"]", task_id);
        log_func (LOG_INFO, log_msg);
        g_free (log_msg);
    }

    return;
}

/**
 * bd_utils_exec_and_report_error:
 * @argv: (array zero-terminated=1): the argv array for the call
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @argv was successfully executed (no error and exit code 0) or not
 */
gboolean bd_utils_exec_and_report_error (gchar **argv, GError **error) {
    gboolean success = FALSE;
    gint status = 0;
    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;
    guint64 task_id = 0;

    task_id = log_running (argv);
    success = g_spawn_sync (NULL, argv, NULL, G_SPAWN_DEFAULT|G_SPAWN_SEARCH_PATH,
                            NULL, NULL, &stdout_data, &stderr_data, &status, error);
    log_done (task_id);

    if (!success) {
        /* error is already populated from the call */
        return FALSE;
    }

    if (status != 0) {
        if (stderr_data && (g_strcmp0 ("", stderr_data) != 0)) {
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                         "Process reported exit code %d: %s", status, stderr_data);
            g_free (stdout_data);
        } else {
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                         "Process reported exit code %d: %s", status, stdout_data);
            g_free (stderr_data);
        }

        return FALSE;
    }

    g_free (stdout_data);
    g_free (stderr_data);
    return TRUE;
}

/**
 * bd_utils_exec_and_capture_output:
 * @argv: (array zero-terminated=1): the argv array for the call
 * @output: (out): variable to store output to
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @argv was successfully executed capturing the output or not
 */
gboolean bd_utils_exec_and_capture_output (gchar **argv, gchar **output, GError **error) {
    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;
    gint status = 0;
    gboolean success = FALSE;
    guint64 task_id = 0;

    task_id = log_running (argv);
    success = g_spawn_sync (NULL, argv, NULL, G_SPAWN_DEFAULT|G_SPAWN_SEARCH_PATH,
                            NULL, NULL, &stdout_data, &stderr_data, &status, error);
    log_done (task_id);

    if (!success) {
        /* error is already populated */
        return FALSE;
    }

    if ((status != 0) || (g_strcmp0 ("", stdout_data) == 0)) {
        if (stderr_data && (g_strcmp0 ("", stderr_data) != 0)) {
            if (status != 0)
                g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                             "Process reported exit code %d: %s", status, stderr_data);
            else
                g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_NOOUT,
                             "Process didn't provide any data on standard output. "
                             "Error output: %s", stderr_data);
            g_free (stdout_data);
        }
        return FALSE;
    } else {
        *output = stdout_data;
        g_free (stderr_data);
        return TRUE;
    }
}

/**
 * bd_utils_init_logging:
 * @new_log_func: (allow-none) (scope notified): logging function to use or
 *                                               %NULL to reset to default
 * @error: (out): place to store error (if any)
 *
 * Returns: whether logging was successfully initialized or not
 */
gboolean bd_utils_init_logging (BDUtilsLogFunc new_log_func, GError **error __attribute__((unused))) {
    /* XXX: the error attribute will likely be used in the future when this
       function gets more complicated */

    log_func = new_log_func;

    return TRUE;
}
