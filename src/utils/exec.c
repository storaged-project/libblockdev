/*
 * Copyright (C) 2014  Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <glib.h>
#include "exec.h"
#include "extra_arg.h"
#include <syslog.h>
#include <stdlib.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __clang__
#define ZERO_INIT {}
#else
#define ZERO_INIT {0}
#endif

extern char **environ;

static GMutex id_counter_lock;
static guint64 id_counter = 0;
static BDUtilsLogFunc log_func = NULL;

static GMutex task_id_counter_lock;
static guint64 task_id_counter = 0;
static BDUtilsProgFunc prog_func = NULL;
static __thread BDUtilsProgFunc thread_prog_func = NULL;

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
guint64 get_next_task_id (void) {
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
    gint exit_status = 0;
    guint i = 0;
    gchar **old_env = NULL;
    gchar **new_env = NULL;

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

    old_env = g_get_environ ();
    new_env = g_environ_setenv (old_env, "LC_ALL", "C", TRUE);

    task_id = log_running (args ? args : argv);
    success = g_spawn_sync (NULL, args ? (gchar **) args : (gchar **) argv, new_env, G_SPAWN_SEARCH_PATH,
                            NULL, NULL, &stdout_data, &stderr_data, &exit_status, error);
    if (!success) {
        /* error is already populated from the call */
        g_strfreev (new_env);
        g_free (stdout_data);
        g_free (stderr_data);
        return FALSE;
    }
    g_strfreev (new_env);

    /* g_spawn_sync set the status in the same way waitpid() does, we need
       to get the process exit code manually (this is similar to calling
       WEXITSTATUS but also sets the error for terminated processes */

    #if !GLIB_CHECK_VERSION(2, 69, 0)
    #define g_spawn_check_wait_status(x,y) (g_spawn_check_exit_status (x,y))
    #endif

    if (!g_spawn_check_wait_status (exit_status, error)) {
        if (g_error_matches (*error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED)) {
            /* process was terminated abnormally (e.g. using a signal) */
            g_free (stdout_data);
            g_free (stderr_data);
            return FALSE;
        }

        *status = (*error)->code;
        g_clear_error (error);
    } else
        *status = 0;

    log_out (task_id, stdout_data, stderr_data);
    log_done (task_id, *status);

    g_free (args);

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

    g_free (stdout_data);
    g_free (stderr_data);
    return TRUE;
}

/* buffer size in bytes used to read from stdout and stderr */
#define _EXEC_BUF_SIZE 64*1024

/* similar to g_strstr_len() yet treats 'null' byte as @needle. */
static gchar *bd_strchr_len_null (const gchar *haystack, gssize haystack_len, const gchar needle) {
    gchar *ret;
    gchar *ret_null;

    ret = memchr (haystack, needle, haystack_len);
    ret_null = memchr (haystack, 0, haystack_len);
    if (ret && ret_null)
        return MIN (ret, ret_null);
    else
        return MAX (ret, ret_null);
}

static gboolean
_process_fd_event (gint fd, struct pollfd *poll_fd, GString *read_buffer, GString *filtered_buffer, gsize *read_buffer_pos, gboolean *done,
                   guint64 progress_id, guint8 *progress, BDUtilsProgExtract prog_extract, GError **error) {
    gchar buf[_EXEC_BUF_SIZE] = { 0 };
    ssize_t num_read;
    gchar *line;
    gchar *newline_pos;
    int errno_saved;
    gboolean eof = FALSE;

    if (! *done && (poll_fd->revents & POLLIN)) {
        /* read until we get EOF (0) or error (-1), expecting EAGAIN */
        while ((num_read = read (fd, buf, _EXEC_BUF_SIZE)) > 0)
            g_string_append_len (read_buffer, buf, num_read);
        errno_saved = errno;

        /* process the fresh data by lines */
        if (read_buffer->len > *read_buffer_pos) {
            gchar *buf_ptr;
            gsize buf_len;

            while ((buf_ptr = read_buffer->str + *read_buffer_pos,
                    buf_len = read_buffer->len - *read_buffer_pos,
                    newline_pos = bd_strchr_len_null (buf_ptr, buf_len, '\n'))) {
                line = g_strndup (buf_ptr, newline_pos - buf_ptr + 1);
                if (prog_extract && prog_extract (line, progress))
                    bd_utils_report_progress (progress_id, *progress, NULL);
                else
                    g_string_append (filtered_buffer, line);
                g_free (line);
                *read_buffer_pos = newline_pos - read_buffer->str + 1;
            }
        }

        /* read error */
        if (num_read < 0 && errno_saved != EAGAIN && errno_saved != EINTR) {
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                         "Error reading from pipe: %s", g_strerror (errno_saved));
            return FALSE;
        }

        /* EOF */
        if (num_read == 0)
            eof = TRUE;
    }

    if (poll_fd->revents & POLLHUP || poll_fd->revents & POLLERR || poll_fd->revents & POLLNVAL)
        eof = TRUE;

    if (eof) {
        *done = TRUE;
        /* process the remaining buffer */
        line = read_buffer->str + *read_buffer_pos;
        /* GString guarantees the buffer is always NULL-terminated. */
        if (strlen (line) > 0) {
            if (prog_extract && prog_extract (line, progress))
                bd_utils_report_progress (progress_id, *progress, NULL);
            else
                g_string_append (filtered_buffer, line);
        }
    }

    return TRUE;
}

static gboolean _utils_exec_and_report_progress (const gchar **argv, const BDExtraArg **extra, BDUtilsProgExtract prog_extract, gint *proc_status, gchar **stdout, gchar **stderr, GError **error) {
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
    gint err_fd = 0;
    gint child_ret = -1;
    gint status = 0;
    gboolean ret = FALSE;
    gint poll_status = 0;
    guint i = 0;
    guint8 completion = 0;
    struct pollfd fds[2] = { ZERO_INIT, ZERO_INIT };
    int flags;
    gboolean out_done = FALSE;
    gboolean err_done = FALSE;
    GString *stdout_data;
    GString *stdout_buffer;
    GString *stderr_data;
    GString *stderr_buffer;
    gsize stdout_buffer_pos = 0;
    gsize stderr_buffer_pos = 0;
    gchar **old_env = NULL;
    gchar **new_env = NULL;
    gboolean success = TRUE;

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

    old_env = g_get_environ ();
    new_env = g_environ_setenv (old_env, "LC_ALL", "C", TRUE);

    ret = g_spawn_async_with_pipes (NULL, args ? (gchar**) args : (gchar**) argv, new_env,
                                    G_SPAWN_DEFAULT|G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD,
                                    NULL, NULL, &pid, NULL, &out_fd, &err_fd, error);

    g_strfreev (new_env);

    if (!ret) {
        /* error is already populated */
        g_free (args);
        return FALSE;
    }

    args_str = g_strjoinv (" ", args ? (gchar **) args : (gchar **) argv);
    msg = g_strdup_printf ("Started '%s'", args_str);
    progress_id = bd_utils_report_started (msg);
    g_free (args_str);
    g_free (args);
    g_free (msg);

    /* set both fds for non-blocking read */
    flags = fcntl (out_fd, F_GETFL, 0);
    if (fcntl (out_fd, F_SETFL, flags | O_NONBLOCK))
        g_warning ("_utils_exec_and_report_progress: Failed to set out_fd non-blocking: %m");
    flags = fcntl (err_fd, F_GETFL, 0);
    if (fcntl (err_fd, F_SETFL, flags | O_NONBLOCK))
        g_warning ("_utils_exec_and_report_progress: Failed to set err_fd non-blocking: %m");

    stdout_data = g_string_new (NULL);
    stdout_buffer = g_string_new (NULL);
    stderr_data = g_string_new (NULL);
    stderr_buffer = g_string_new (NULL);

    fds[0].fd = out_fd;
    fds[1].fd = err_fd;
    fds[0].events = POLLIN | POLLHUP | POLLERR;
    fds[1].events = POLLIN | POLLHUP | POLLERR;
    while (! (out_done && err_done)) {
        poll_status = poll (fds, 2, -1 /* timeout */);
        g_warn_if_fail (poll_status != 0);  /* no timeout specified, zero should never be returned */
        if (poll_status < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                         "Failed to poll output FDs: %m");
            bd_utils_report_finished (progress_id, (*error)->message);
            success = FALSE;
            break;
        }

        if (!out_done) {
            if (! _process_fd_event (out_fd, &fds[0], stdout_buffer, stdout_data, &stdout_buffer_pos, &out_done, progress_id, &completion, prog_extract, error)) {
                bd_utils_report_finished (progress_id, (*error)->message);
                success = FALSE;
                break;
            }
        }

        if (!err_done) {
            if (! _process_fd_event (err_fd, &fds[1], stderr_buffer, stderr_data, &stderr_buffer_pos, &err_done, progress_id, &completion, prog_extract, error)) {
                bd_utils_report_finished (progress_id, (*error)->message);
                success = FALSE;
                break;
            }
        }
    }

    g_string_free (stdout_buffer, TRUE);
    g_string_free (stderr_buffer, TRUE);
    close (out_fd);
    close (err_fd);

    child_ret = waitpid (pid, &status, 0);
    *proc_status = WEXITSTATUS (status);
    if (success) {
        if (child_ret > 0) {
            if (*proc_status != 0) {
                msg = stderr_data->len > 0 ? stderr_data->str : stdout_data->str;
                g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                             "Process reported exit code %d: %s", *proc_status, msg);
                bd_utils_report_finished (progress_id, (*error)->message);
                success = FALSE;
            } else if (WIFSIGNALED (status)) {
                g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                             "Process killed with a signal");
                bd_utils_report_finished (progress_id, (*error)->message);
                success = FALSE;
            }
        } else if (child_ret == -1) {
            if (errno != ECHILD) {
                errno = 0;
                g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                             "Failed to wait for the process");
                bd_utils_report_finished (progress_id, (*error)->message);
                success = FALSE;
            } else {
                /* no such process (the child exited before we tried to wait for it) */
                errno = 0;
            }
        }
        if (success)
            bd_utils_report_finished (progress_id, "Completed");
    }
    log_out (task_id, stdout_data->str, stderr_data->str);
    log_done (task_id, *proc_status);

    if (success && stdout)
        *stdout = g_string_free (stdout_data, FALSE);
    else
        g_string_free (stdout_data, TRUE);
    if (success && stderr)
        *stderr = g_string_free (stderr_data, FALSE);
    else
        g_string_free (stderr_data, TRUE);

    return success;
}

/**
 * bd_utils_exec_and_report_progress:
 * @argv: (array zero-terminated=1): the argv array for the call
 * @extra: (allow-none) (array zero-terminated=1): extra arguments
 * @prog_extract: (scope notified) (nullable): function for extracting progress information
 * @proc_status: (out): place to store the process exit status
 * @error: (out): place to store error (if any)
 *
 * Note that any NULL bytes read from standard output and standard error
 * output are treated as separators similar to newlines and @prog_extract
 * will be called with the respective chunk.
 *
 * Returns: whether the @argv was successfully executed (no error and exit code 0) or not
 */
gboolean bd_utils_exec_and_report_progress (const gchar **argv, const BDExtraArg **extra, BDUtilsProgExtract prog_extract, gint *proc_status, GError **error) {
    return _utils_exec_and_report_progress (argv, extra, prog_extract, proc_status, NULL, NULL, error);
}

/**
 * bd_utils_exec_and_capture_output:
 * @argv: (array zero-terminated=1): the argv array for the call
 * @extra: (allow-none) (array zero-terminated=1): extra arguments
 * @output: (out): variable to store output to
 * @error: (out): place to store error (if any)
 *
 * Note that any NULL bytes read from standard output and standard error
 * output will be discarded.
 *
 * Returns: whether the @argv was successfully executed capturing the output or not
 */
gboolean bd_utils_exec_and_capture_output (const gchar **argv, const BDExtraArg **extra, gchar **output, GError **error) {
    gint status = 0;
    gchar *stdout = NULL;
    gchar *stderr = NULL;
    gboolean ret = FALSE;

    ret = _utils_exec_and_report_progress (argv, extra, NULL, &status, &stdout, &stderr, error);
    if (!ret)
        return ret;

    if ((status != 0) || (g_strcmp0 ("", stdout) == 0)) {
        if (status != 0)
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED,
                         "Process reported exit code %d: %s%s", status,
                         stdout ? stdout : "",
                         stderr ? stderr : "");
        else
            g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_NOOUT,
                         "Process didn't provide any data on standard output. "
                         "Error output: %s", stderr ? stderr : "");
        g_free (stderr);
        g_free (stdout);
        return FALSE;
    } else {
        *output = stdout;
        g_free (stderr);
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
gint bd_utils_version_cmp (const gchar *ver_string1, const gchar *ver_string2, GError **error) {
    gchar **v1_fields = NULL;
    gchar **v2_fields = NULL;
    guint v1_fields_len = 0;
    guint v2_fields_len = 0;
    guint64 v1_value = 0;
    guint64 v2_value = 0;
    GRegex *regex = NULL;
    gboolean success = FALSE;
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

    for (guint i=0; (i < v1_fields_len) && (i < v2_fields_len) && ret == -2; i++) {
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
    }
    else
        version_str = g_strstrip (g_strdup (output));

    if (!version_str || (g_strcmp0 (version_str, "") == 0)) {
        g_set_error (error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_UTIL_UNKNOWN_VER,
                     "Failed to determine %s's version from: %s", util, output);
        g_free (version_str);
        g_free (output);
        return FALSE;
    }

    g_free (output);

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
 * bd_utils_init_prog_reporting_thread:
 * @new_prog_func: (allow-none) (scope notified): progress reporting function to
 *                                                use on current thread or %NULL
 *                                                to reset to default or global
 * @error: (out): place to store error (if any)
 *
 * Returns: whether progress reporting was successfully initialized or not
 */
gboolean bd_utils_init_prog_reporting_thread (BDUtilsProgFunc new_prog_func, GError **error __attribute__((unused))) {
    /* XXX: the error attribute will likely be used in the future when this
       function gets more complicated */

    thread_prog_func = new_prog_func;

    return TRUE;
}

static void thread_progress_muted (guint64 task_id __attribute__((unused)), BDUtilsProgStatus status __attribute__((unused)), guint8 completion __attribute__((unused)), gchar *msg __attribute__((unused))) {
    /* This function serves as a special value for the progress reporting
     * function to detect that nothing is done here. If clients use their own
     * empty function then bd_utils_prog_reporting_initialized will return TRUE
     * but with this function here it returns FALSE.
     */
}

/**
 * bd_utils_mute_prog_reporting_thread:
 * @error: (out): place to store error (if any)
 *
 * Returns: whether progress reporting for the current thread was successfully
 * muted (deinitialized even in presence of a global reporting function) or not
 */
gboolean bd_utils_mute_prog_reporting_thread (GError **error __attribute__((unused))) {
    /* XXX: the error attribute will likely be used in the future when this
       function gets more complicated */

    thread_prog_func = thread_progress_muted;

    return TRUE;
}

/**
 * bd_utils_prog_reporting_initialized:
 *
 * Returns: TRUE if progress reporting has been initialized, i.e. a reporting
 * function was set up with either bd_utils_init_prog_reporting or
 * bd_utils_init_prog_reporting_thread (takes precedence). FALSE if
 * bd_utils_mute_prog_reporting_thread was used to mute the thread.
 */
gboolean bd_utils_prog_reporting_initialized (void) {
    return (thread_prog_func != NULL || prog_func != NULL) && thread_prog_func != thread_progress_muted;
}

/**
 * bd_utils_report_started:
 * @msg: message describing the started task/action
 *
 * Returns: ID of the started task/action
 */
guint64 bd_utils_report_started (const gchar *msg) {
    guint64 task_id = 0;
    BDUtilsProgFunc current_prog_func;

    current_prog_func = thread_prog_func != NULL ? thread_prog_func : prog_func;

    g_mutex_lock (&task_id_counter_lock);
    task_id_counter++;
    task_id = task_id_counter;
    g_mutex_unlock (&task_id_counter_lock);

    if (current_prog_func)
        current_prog_func (task_id, BD_UTILS_PROG_STARTED, 0, (gchar *)msg);
    return task_id;
}

/**
 * bd_utils_report_progress:
 * @task_id: ID of the task/action
 * @completion: percentage of completion
 * @msg: message describing the status of the task/action
 */
void bd_utils_report_progress (guint64 task_id, guint64 completion, const gchar *msg) {
    BDUtilsProgFunc current_prog_func;

    current_prog_func = thread_prog_func != NULL ? thread_prog_func : prog_func;
    if (current_prog_func)
        current_prog_func (task_id, BD_UTILS_PROG_PROGRESS, completion, (gchar *)msg);
}

/**
 * bd_utils_report_finished:
 * @task_id: ID of the task/action
 * @msg: message describing the status of the task/action
 */
void bd_utils_report_finished (guint64 task_id, const gchar *msg) {
    BDUtilsProgFunc current_prog_func;

    current_prog_func = thread_prog_func != NULL ? thread_prog_func : prog_func;
    if (current_prog_func)
        current_prog_func (task_id, BD_UTILS_PROG_FINISHED, 100, (gchar *)msg);
}

/**
 * bd_utils_echo_str_to_file:
 * @str: string to write to @file_path
 * @file_path: path to file
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @str was successfully written to @file_path
 * or not.
 */
gboolean bd_utils_echo_str_to_file (const gchar *str, const gchar *file_path, GError **error) {
    GIOChannel *out_file = NULL;
    gsize bytes_written = 0;

    out_file = g_io_channel_new_file (file_path, "w", error);
    if (!out_file || g_io_channel_write_chars (out_file, str, -1, &bytes_written, error) != G_IO_STATUS_NORMAL) {
        g_prefix_error (error, "Failed to write '%s' to file '%s': ", str, file_path);
        return FALSE;
    }
    if (g_io_channel_shutdown (out_file, TRUE, error) != G_IO_STATUS_NORMAL) {
        g_prefix_error (error, "Failed to flush and close the file '%s': ", file_path);
        g_io_channel_unref (out_file);
        return FALSE;
    }
    g_io_channel_unref (out_file);
    return TRUE;
}

/**
 * bd_utils_log:
 * @level: log level
 * @msg: log message
 */
void bd_utils_log (gint level, const gchar *msg) {
    if (log_func)
        log_func (level, msg);
}
