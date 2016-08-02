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
#include "extra_arg.h"
#include <syslog.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

extern char **environ;

static GMutex id_counter_lock;
static guint64 id_counter = 0;
static BDUtilsLogFunc log_func = NULL;

static GMutex task_id_counter_lock;
static guint64 task_id_counter = 0;
static BDUtilsProgFunc prog_func = NULL;

/**
 * bd_utils_exec_error_quark: (skip)
 */
GQuark bd_utils_exec_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-utils-exec-error-quark");
}

/**
 * get_next_task_id: (skip)
 */
guint64 get_next_task_id () {
    guint64 task_id = 0;

    g_mutex_lock (&id_counter_lock);
    id_counter++;
    task_id = id_counter;
    g_mutex_unlock (&id_counter_lock);

    return task_id;
}

/**
 * log_task_status: (skip)
 * @task_id: ID of the task the status of which is being logged
 * @msg: log message
 */
void log_task_status (guint64 task_id, const gchar *msg) {
    gchar *log_msg = NULL;

    if (log_func) {
        log_msg = g_strdup_printf ("[%"G_GUINT64_FORMAT"] %s", task_id, msg);
        log_func (LOG_INFO, log_msg);
        g_free (log_msg);
    }
}

/**
 * log_running: (skip)
 *
 * Returns: id of the running task
 */
static guint64 log_running (const gchar **argv) {
    guint64 task_id = 0;
    gchar *str_argv = NULL;
    gchar *log_msg = NULL;

    task_id = get_next_task_id ();

    if (log_func) {
        str_argv = g_strjoinv (" ", (gchar **) argv);
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
static void log_out (guint64 task_id, const gchar *stdout, const gchar *stderr) {
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

static void set_c_locale(gpointer user_data __attribute__((unused))) {
    if (setenv ("LC_ALL", "C", 1) != 0)
        g_warning ("Failed to set LC_ALL=C for a child process!");
}

/**
 * bd_utils_exec_and_report_error:
 * @argv: (array zero-terminated=1): the argv array for the call
 * @extra: (allow-none) (array zero-terminated=1): extra arguments
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @argv was successfully executed (no error and exit code 0) or not
 */
gboolean bd_utils_exec_and_report_error (const gchar **argv, const BDExtraArg **extra, GError **error) {
    gint status = 0;
    /* just use the "stronger" function providing dumb progress reporting (just
       'started' and 'finished') and throw away the returned status */
    return bd_utils_exec_and_report_progress (argv, extra, NULL, &status, error);
}

/**
 * bd_utils_exec_and_report_error_no_progress:
 * @argv: (array zero-terminated=1): the argv array for the call
 * @extra: (allow-none) (array zero-terminated=1): extra arguments
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @argv was successfully executed (no error and exit code 0) or not
 */
gboolean bd_utils_exec_and_report_error_no_progress (const gchar **argv, const BDExtraArg **extra, GError **error) {
    gint status = 0;
    /* just use the "stronger" function and throw away the returned status */
    return bd_utils_exec_and_report_status_error (argv, extra, &status, error);
}

/**
 * bd_utils_exec_and_report_status_error:
 * @argv: (array zero-terminated=1): the argv array for the call
 * @extra: (allow-none) (array zero-terminated=1): extra arguments
 * @status: (out): place to store the status
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @argv was successfully executed (no error and exit code 0) or not
 */
gboolean bd_utils_exec_and_report_status_error (const gchar **argv, const BDExtraArg **extra, gint *status, GError **error) {
    gboolean success = FALSE;
    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;
    guint64 task_id = 0;
    const gchar **args = NULL;
    guint args_len = 0;
    const gchar **arg_p = NULL;
    const BDExtraArg **extra_p = NULL;
    guint i = 0;

    if (extra) {
        args_len = g_strv_length ((gchar **) argv);
        for (extra_p=extra; *extra_p; extra_p++) {
            if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "") != 0))
                args_len++;
            if ((*extra_p)->val && (g_strcmp0 ((*extra_p)->val, "") != 0))
                args_len++;
        }
        args = g_new0 (const gchar*, args_len + 1);
        for (arg_p=argv; *arg_p; arg_p++, i++)
            args[i] = *arg_p;
        for (extra_p=extra; *extra_p; extra_p++) {
            if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "") != 0)) {
                args[i] = (*extra_p)->opt;
                i++;
            }
            if ((*extra_p)->val && (g_strcmp0 ((*extra_p)->val, "") != 0)) {
                args[i] = (*extra_p)->val;
                i++;
            }
        }
        args[i] = NULL;
    }

    task_id = log_running (args ? args : argv);
    success = g_spawn_sync (NULL, args ? (gchar **) args : (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
                            (GSpawnChildSetupFunc) set_c_locale, NULL,
                            &stdout_data, &stderr_data, status, error);
    log_out (task_id, stdout_data, stderr_data);
    log_done (task_id, *status);

    if (!success) {
        /* error is already populated from the call */
        return FALSE;
    }

    if (*status != 0) {
        if (stderr_data && (g_strcmp0 ("", stderr_data) != 0)) {
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                         "Process reported exit code %d: %s", *status, stderr_data);
            g_free (stdout_data);
        } else {
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                         "Process reported exit code %d: %s", *status, stdout_data);
            g_free (stderr_data);
        }

        return FALSE;
    }

    g_free (args);
    g_free (stdout_data);
    g_free (stderr_data);
    return TRUE;
}

/**
 * bd_utils_exec_and_capture_output:
 * @argv: (array zero-terminated=1): the argv array for the call
 * @extra: (allow-none) (array zero-terminated=1): extra arguments
 * @output: (out): variable to store output to
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @argv was successfully executed capturing the output or not
 */
gboolean bd_utils_exec_and_capture_output (const gchar **argv, const BDExtraArg **extra, gchar **output, GError **error) {
    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;
    gint status = 0;
    gboolean success = FALSE;
    guint64 task_id = 0;
    const gchar **args = NULL;
    guint args_len = 0;
    const gchar **arg_p = NULL;
    const BDExtraArg **extra_p = NULL;
    guint i = 0;

    if (extra) {
        args_len = g_strv_length ((gchar **) argv);
        for (extra_p=extra; *extra_p; extra_p++) {
            if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "") != 0))
                args_len++;
            if ((*extra_p)->val && (g_strcmp0 ((*extra_p)->val, "") != 0))
                args_len++;
        }
        args = g_new0 (const gchar*, args_len + 1);
        for (arg_p=argv; *arg_p; arg_p++, i++)
            args[i] = *arg_p;
        for (extra_p=extra; *extra_p; extra_p++) {
            if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "") != 0)) {
                args[i] = (*extra_p)->opt;
                i++;
            }
            if ((*extra_p)->val && (g_strcmp0 ((*extra_p)->val, "") != 0)) {
                args[i] = (*extra_p)->val;
                i++;
            }
        }
        args[i] = NULL;
    }

    task_id = log_running (argv);
    success = g_spawn_sync (NULL, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
                            (GSpawnChildSetupFunc) set_c_locale, NULL,
                            &stdout_data, &stderr_data, &status, error);
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
                             "Process reported exit code %d: %s%s", status, stdout_data, stderr_data);
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
 * bd_exec_and_report_progress:
 * @argv: (array zero-terminated=1): the argv array for the call
 * @extra: (allow-none) (array zero-terminated=1): extra arguments
 * @prog_extract: (scope notified): function for extracting progress information
 * @proc_status: (out): place to store the process exit status
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @argv was successfully executed (no error and exit code 0) or not
 */
gboolean bd_exec_and_report_progress (const gchar **argv, const BDExtraArg **extra, BDUtilsProgExtract prog_extract, gint *proc_status, GError **error) {
    const gchar **args = NULL;
    guint args_len = 0;
    const gchar **arg_p = NULL;
    gchar *args_str = NULL;
    const BDExtraArg **extra_p = NULL;
    guint64 task_id = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GPid pid = 0;
    gint out_fd = 0;
    GIOChannel *out_pipe = NULL;
    gint err_fd = 0;
    GIOChannel *err_pipe = NULL;
    gchar *line = NULL;
    gint child_ret = -1;
    gint status = 0;
    gboolean ret = FALSE;
    GIOStatus io_status = G_IO_STATUS_NORMAL;
    guint i = 0;
    guint8 completion = 0;
    GPollFD fds[2] = { 0 };
    gboolean out_done = FALSE;
    gboolean err_done = FALSE;
    GString *stdout_data = g_string_new (NULL);
    GString *stderr_data = g_string_new (NULL);

    /* TODO: share this code between functions */
    if (extra) {
        args_len = g_strv_length ((gchar **) argv);
        for (extra_p=extra; *extra_p; extra_p++) {
            if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "") != 0))
                args_len++;
            if ((*extra_p)->val && (g_strcmp0 ((*extra_p)->val, "") != 0))
                args_len++;
        }
        args = g_new0 (const gchar*, args_len + 1);
        for (arg_p=argv; *arg_p; arg_p++, i++)
            args[i] = *arg_p;
        for (extra_p=extra; *extra_p; extra_p++) {
            if ((*extra_p)->opt && (g_strcmp0 ((*extra_p)->opt, "") != 0)) {
                args[i] = (*extra_p)->opt;
                i++;
            }
            if ((*extra_p)->val && (g_strcmp0 ((*extra_p)->val, "") != 0)) {
                args[i] = (*extra_p)->val;
                i++;
            }
        }
        args[i] = NULL;
    }

    task_id = log_running (args ? args : argv);

    ret = g_spawn_async_with_pipes (NULL, args ? (gchar**) args : (gchar**) argv, NULL,
                                    G_SPAWN_DEFAULT|G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD,
                                    NULL, NULL, &pid, NULL, &out_fd, &err_fd, error);

    if (!ret)
        /* error is already populated */
        return FALSE;

    args_str = g_strjoinv (" ", args ? (gchar **) args : (gchar **) argv);
    msg = g_strdup_printf ("Started '%s'", args_str);
    progress_id = bd_utils_report_started (msg);
    g_free (args_str);
    g_free (msg);

    out_pipe = g_io_channel_unix_new (out_fd);
    err_pipe = g_io_channel_unix_new (err_fd);

    fds[0].fd = out_fd;
    fds[1].fd = err_fd;
    fds[0].events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    fds[1].events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    while (!out_done || !err_done) {
        g_poll (fds, 2, -1);
        if (!out_done && (fds[0].revents & (G_IO_IN | G_IO_HUP | G_IO_ERR))) {
            if (fds[0].revents & G_IO_IN) {
                io_status = g_io_channel_read_line (out_pipe, &line, NULL, NULL, error);
                if (io_status == G_IO_STATUS_NORMAL) {
                    if (prog_extract && prog_extract (line, &completion))
                        bd_utils_report_progress (progress_id, completion, NULL);
                    else
                        g_string_append (stdout_data, line);
                    g_free (line);
                } else if (io_status == G_IO_STATUS_EOF) {
                    out_done = TRUE;
                } else if (error && (*error)) {
                    bd_utils_report_finished (progress_id, (*error)->message);
                    g_io_channel_shutdown (out_pipe, FALSE, error);
                    g_io_channel_unref (out_pipe);
                    g_io_channel_shutdown (err_pipe, FALSE, error);
                    g_io_channel_unref (err_pipe);
                    g_string_free (stdout_data, TRUE);
                    g_string_free (stderr_data, TRUE);
                    return FALSE;
                }
            } else
                /* ERR or HUP, nothing more to do here */
                out_done = TRUE;
        }
        if (!err_done && (fds[1].revents & (G_IO_IN | G_IO_HUP | G_IO_ERR))) {
            if (fds[1].revents & G_IO_IN) {
                io_status = g_io_channel_read_line (err_pipe, &line, NULL, NULL, error);
                if (io_status == G_IO_STATUS_NORMAL) {
                    g_string_append (stderr_data, line);
                    g_free (line);
                } else if (io_status == G_IO_STATUS_EOF) {
                    err_done = TRUE;
                } else if (error && (*error)) {
                    bd_utils_report_finished (progress_id, (*error)->message);
                    g_io_channel_shutdown (out_pipe, FALSE, error);
                    g_io_channel_unref (out_pipe);
                    g_io_channel_shutdown (err_pipe, FALSE, error);
                    g_io_channel_unref (err_pipe);
                    g_string_free (stdout_data, TRUE);
                    g_string_free (stderr_data, TRUE);
                    return FALSE;
                }
            } else
                /* ERR or HUP, nothing more to do here */
                err_done = TRUE;
        }
    }

    child_ret = waitpid (pid, &status, 0);
    *proc_status = WEXITSTATUS(status);
    if (child_ret > 0) {
        if (*proc_status != 0) {
            if (stderr_data->str && (g_strcmp0 ("", stderr_data->str) != 0))
                msg = stderr_data->str;
            else
                msg = stdout_data->str;
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                         "Process reported exit code %d: %s", *proc_status, stderr_data->str);
            bd_utils_report_finished (progress_id, (*error)->message);
            g_string_free (stdout_data, TRUE);
            g_string_free (stderr_data, TRUE);
            return FALSE;
        }
        if (WIFSIGNALED(status)) {
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                         "Process killed with a signal");
            bd_utils_report_finished (progress_id, (*error)->message);
            g_string_free (stdout_data, TRUE);
            g_string_free (stderr_data, TRUE);
            return FALSE;
        }
    } else if (child_ret == -1) {
        if (errno != ECHILD) {
            errno = 0;
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                         "Failed to wait for the process");
            bd_utils_report_finished (progress_id, (*error)->message);
            g_string_free (stdout_data, TRUE);
            g_string_free (stderr_data, TRUE);
            return FALSE;
        }
        /* no such process (the child exited before we tried to wait for it) */
        errno = 0;
    }

    bd_utils_report_finished (progress_id, "Completed");
    log_out (task_id, stdout_data->str, stderr_data->str);
    log_done (task_id, *proc_status);

    /* we don't care about the status here */
    g_io_channel_shutdown (out_pipe, FALSE, error);
    g_io_channel_unref (out_pipe);
    g_io_channel_shutdown (err_pipe, FALSE, error);
    g_io_channel_unref (err_pipe);
    g_string_free (stdout_data, TRUE);
    g_string_free (stderr_data, TRUE);

    return TRUE;
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
gint bd_utils_version_cmp (const gchar *ver_string1, const gchar *ver_string2, GError **error) {
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
gboolean bd_utils_check_util_version (const gchar *util, const gchar *version, const gchar *version_arg, const gchar *version_regexp, GError **error) {
    gchar *util_path = NULL;
    const gchar *argv[] = {util, version_arg ? version_arg : "--version", NULL};
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

    succ = bd_utils_exec_and_capture_output (argv, NULL, &output, error);
    if (!succ) {
        /* if we got nothing on STDOUT, try using STDERR data from error message */
        if (g_error_matches ((*error), BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_NOOUT)) {
            output = g_strdup ((*error)->message);
            g_clear_error (error);
        } else if (g_error_matches ((*error), BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED)) {
            /* exit status != 0, try using the output anyway */
            output = g_strdup ((*error)->message);
            g_clear_error (error);
        }
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
            g_match_info_free (match_info);
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

/**
 * bd_utils_init_prog_reporting:
 * @new_prog_func: (allow-none) (scope notified): progress reporting function to
 *                                                use or %NULL to reset to default
 * @error: (out): place to store error (if any)
 *
 * Returns: whether progress reporting was successfully initialized or not
 */
gboolean bd_utils_init_prog_reporting (BDUtilsProgFunc new_prog_func, GError **error __attribute__((unused))) {
    /* XXX: the error attribute will likely be used in the future when this
       function gets more complicated */

    prog_func = new_prog_func;

    return TRUE;
}

/**
 * bd_utils_report_started:
 * @msg: message describing the started task/action
 *
 * Returns: ID of the started task/action
 */
guint64 bd_utils_report_started (gchar *msg) {
    guint64 task_id = 0;

    g_mutex_lock (&task_id_counter_lock);
    task_id_counter++;
    task_id = task_id_counter;
    g_mutex_unlock (&task_id_counter_lock);

    if (prog_func)
        prog_func (task_id, BD_UTILS_PROG_STARTED, 0, msg);
    return task_id;
}

/**
 * bd_utils_report_progress:
 * @task_id: ID of the task/action
 * @completion: percentage of completion
 * @msg: message describing the status of the task/action
 */
void bd_utils_report_progress (guint64 task_id, guint64 completion, gchar *msg) {
    if (prog_func)
        prog_func (task_id, BD_UTILS_PROG_PROGRESS, completion, msg);
}

/**
 * bd_utils_report_finished:
 * @task_id: ID of the task/action
 * @msg: message describing the status of the task/action
 */
void bd_utils_report_finished (guint64 task_id, gchar *msg) {
    if (prog_func)
        prog_func (task_id, BD_UTILS_PROG_FINISHED, 100, msg);
}
