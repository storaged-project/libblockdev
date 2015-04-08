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
 * log_out: (skip)
 *
 */
static void log_out (guint64 task_id, gchar *stdout, gchar *stderr) {
    gchar *log_msg = NULL;

    if (log_func) {
        log_msg = g_strdup_printf ("stdout[%"G_GUINT64_FORMAT"]: %s", task_id, stdout);
        log_func (LOG_INFO, log_msg);
        g_free (log_msg);

        log_msg = g_strdup_printf ("stderr[%"G_GUINT64_FORMAT"]: %s", task_id, stderr);
        log_func (LOG_INFO, log_msg);
        g_free (log_msg);
    }

    return;
}

/**
 * log_done: (skip)
 *
 */
static void log_done (guint64 task_id, gint exit_code) {
    gchar *log_msg = NULL;

    if (log_func) {
        log_msg = g_strdup_printf ("...done [%"G_GUINT64_FORMAT"] (exit code: %d)", task_id, exit_code);
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
    success = g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                            NULL, NULL, &stdout_data, &stderr_data, &status, error);
    log_out (task_id, stdout_data, stderr_data);
    log_done (task_id, status);

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
    success = g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                            NULL, NULL, &stdout_data, &stderr_data, &status, error);
    log_out (task_id, stdout_data, stderr_data);
    log_done (task_id, status);

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

/**
 * bd_utils_version_cmp:
 * @ver_string1: first version string
 * @ver_string2: second version string
 * @error: (out): place to store error (if any)
 *
 * Returns: -1, 0 or 1 if @ver_string1 is lower, the same or higher version as
 *          @ver_string2 respectively. If an error occurs, returns -2 and @error
 *          is set.
 *
 * **ONLY SUPPORTS VERSION STRINGS OF FORMAT X[.Y[.Z[.Z2[.Z3...[-R]]]]] where all components
 *   are natural numbers!**
 */
gint bd_utils_version_cmp (gchar *ver_string1, gchar *ver_string2, GError **error) {
    gchar **v1_fields = NULL;
    gchar **v2_fields = NULL;
    guint v1_fields_len = 0;
    guint v2_fields_len = 0;
    guint64 v1_value = 0;
    guint64 v2_value = 0;
    GRegex *regex = NULL;
    gboolean success = FALSE;
    guint i = 0;
    gint ret = -2;

    regex = g_regex_new ("^(\\d+)(\\.\\d+)*(-\\d)?$", 0, 0, error);
    if (!regex) {
        /* error is already populated */
        return -2;
    }

    success = g_regex_match (regex, ver_string1, 0, NULL);
    if (!success) {
        g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_INVAL_VER,
                     "Invalid or unsupported version (1) format: %s", ver_string1);
        return -2;
    }
    success = g_regex_match (regex, ver_string2, 0, NULL);
    if (!success) {
        g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_INVAL_VER,
                     "Invalid or unsupported version (2) format: %s", ver_string2);
        return -2;
    }
    g_regex_unref (regex);

    v1_fields = g_strsplit_set (ver_string1, ".-", 0);
    v2_fields = g_strsplit_set (ver_string2, ".-", 0);
    v1_fields_len = g_strv_length (v1_fields);
    v2_fields_len = g_strv_length (v2_fields);

    for (i=0; (i < v1_fields_len) && (i < v2_fields_len) && ret == -2; i++) {
        v1_value = g_ascii_strtoull (v1_fields[i], NULL, 0);
        v2_value = g_ascii_strtoull (v2_fields[i], NULL, 0);
        if (v1_value < v2_value)
            ret = -1;
        else if (v1_value > v2_value)
            ret = 1;
    }

    if (ret == -2) {
        if (v1_fields_len < v2_fields_len)
            ret = -1;
        else if (v1_fields_len > v2_fields_len)
            ret = 1;
        else
            ret = 0;
    }

    g_strfreev (v1_fields);
    g_strfreev (v2_fields);

    return ret;
}

/**
 * bd_utils_check_util_version:
 * @util: name of the utility to check
 * @version: (allow-none): minimum required version of the utility or %NULL
 *           if no version is required
 * @version_arg: (allow-none): argument to use with the @util to get version
 *               info or %NULL to use "--version"
 * @version_regexp: (allow-none): regexp to extract version from the version
 *                  info or %NULL if only version is printed by "$ @util @version_arg"
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @util is available in a version >= @version or not
 *          (@error is set in such case).
 */
gboolean bd_utils_check_util_version (gchar *util, gchar *version, gchar *version_arg, gchar *version_regexp, GError **error) {
    gchar *util_path = NULL;
    gchar *argv[] = {util, version_arg ? version_arg : "--version", NULL};
    gchar *output = NULL;
    gboolean succ = FALSE;
    GRegex *regex = NULL;
    GMatchInfo *match_info = NULL;
    gchar *version_str = NULL;

    util_path = g_find_program_in_path (util);
    if (!util_path) {
        g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_UNAVAILABLE,
                     "The '%s' utility is not available", util);
        return FALSE;
    }
    g_free (util_path);

    if (!version)
        /* nothing more to do here */
        return TRUE;

    succ = bd_utils_exec_and_capture_output (argv, &output, error);
    if (!succ) {
        /* error is already populated */
        return FALSE;
    }

    if (version_regexp) {
        regex = g_regex_new (version_regexp, 0, 0, error);
        if (!regex) {
            g_free (output);
            /* error is already populated */
            return FALSE;
        }

        succ = g_regex_match (regex, output, 0, &match_info);
        if (!succ) {
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_UNKNOWN_VER,
                         "Failed to determine %s's version from: %s", util, output);
            g_free (output);
            g_regex_unref (regex);
            return FALSE;
        }
        g_regex_unref (regex);

        version_str = g_match_info_fetch (match_info, 1);
        g_match_info_free (match_info);
        g_free (output);
    }
    else
        version_str = g_strstrip (output);

    if (!version_str || (g_strcmp0 (version_str, "") == 0)) {
        g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_UNKNOWN_VER,
                     "Failed to determine %s's version from: %s", util, output);
        g_free (version_str);
        return FALSE;
    }

    if (bd_utils_version_cmp (version_str, version, error) < 0) {
        /* smaller version or error */
        if (!(*error))
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_LOW_VER,
                         "Too low version of %s: %s. At least %s required.",
                         util, version_str, version);
        g_free (version_str);
        return FALSE;
    }

    g_free (version_str);
    return TRUE;
}
