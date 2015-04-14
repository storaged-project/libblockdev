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
#include <unistd.h>
#include <glob.h>
#include <utils.h>
#include "loop.h"

/**
 * SECTION: loop
 * @short_description: plugin for operations with loop devices
 * @title: Loop
 * @include: loop.h
 *
 * A plugin for operations with loop devices. All sizes passed
 * in/out to/from the functions are in bytes.
 */

/**
 * bd_loop_error_quark: (skip)
 */
GQuark bd_loop_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-loop-error-quark");
}

/**
 * check: (skip)
 */
gboolean check() {
    GError *error = NULL;
    gboolean ret = bd_utils_check_util_version ("losetup", LOSETUP_MIN_VERSION, NULL, "losetup from util-linux\\s+([\\d\\.]+)", &error);

    if (!ret && error) {
        g_warning("Cannot load the loop plugin: %s" , error->message);
        g_clear_error (&error);
    }
    return ret;
}

/**
 * bd_loop_get_backing_file:
 * @dev_name: name of the loop device to get backing file for (e.g. "loop0")
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): path of the device's backing file or %NULL if none
 *                           is found
 */
gchar* bd_loop_get_backing_file (gchar *dev_name, GError **error) {
    gchar *sys_path = g_strdup_printf ("/sys/class/block/%s/loop/backing_file", dev_name);
    gchar *ret = NULL;
    gboolean success = FALSE;

    if (access (sys_path, R_OK) != 0) {
        g_free (sys_path);
        return NULL;
    }

    success = g_file_get_contents (sys_path, &ret, NULL, error);
    if (!success) {
        /* error is alraedy populated */
        g_free (sys_path);
        return NULL;
    }

    g_free (sys_path);
    return g_strstrip (ret);
}

/**
 * bd_loop_get_loop_name:
 * @file: path of the backing file to get loop name for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): name of the loop device associated with the given
 * @file or %NULL if failed to determine
 */
gchar* bd_loop_get_loop_name (gchar *file, GError **error __attribute__((unused))) {
    glob_t globbuf;
    gchar **path_p;
    gboolean success = FALSE;
    GError *tmp_error = NULL;
    gchar *content;
    gboolean found = FALSE;
    gchar **parts;
    gchar *ret;

    if (glob ("/sys/block/loop*/loop/backing_file", GLOB_NOSORT, NULL, &globbuf) != 0) {
        return NULL;
    }

    for (path_p = globbuf.gl_pathv; *path_p && !found; path_p++) {
        success = g_file_get_contents (*path_p, &content, NULL, &tmp_error);
        if (!success) {
            g_clear_error (&tmp_error);
            continue;
        }

        g_strstrip (content);
        found = (g_strcmp0 (content, file) == 0);
        g_free (content);
    }

    if (!found) {
        globfree (&globbuf);
        return NULL;
    }

    parts = g_strsplit (*(path_p - 1), "/", 5);
    ret = g_strdup (parts[3]);
    g_strfreev (parts);

    globfree (&globbuf);
    return ret;
}

/**
 * bd_loop_setup:
 * @file: file to setup as a loop device
 * @loop_name: (out): if not %NULL, it is used to store the name of the loop device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @file was successfully setup as a loop device or not
 */
gboolean bd_loop_setup (gchar *file, gchar **loop_name, GError **error) {
    gchar *args[4] = {"losetup", "-f", file, NULL};
    gboolean success = FALSE;

    success = bd_utils_exec_and_report_error (args, error);
    if (!success)
        return FALSE;
    else {
        if (loop_name)
            *loop_name = bd_loop_get_loop_name (file, error);
        return TRUE;
    }
}

/**
 * bd_loop_teardown:
 * @loop: path or name of the loop device to tear down
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @loop device was successfully torn down or not
 */
gboolean bd_loop_teardown (gchar *loop, GError **error) {
    gboolean success = FALSE;
    gchar *dev_loop = NULL;

    gchar *args[4] = {"losetup", "-d", NULL, NULL};

    if (g_str_has_prefix (loop, "/dev/"))
        args[2] = loop;
    else {
        dev_loop = g_strdup_printf ("/dev/%s", loop);
        args[2] = dev_loop;
    }

    success = bd_utils_exec_and_report_error (args, error);
    g_free (dev_loop);

    return success;
}
