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
#include "loop.h"

/**
 * SECTION: loop
 * @short_description: libblockdev plugin for operations with loop devices
 * @title: Loop
 * @include: loop.h
 *
 * A libblockdev plugin for operations with loop devices. All sizes passed
 * in/out to/from the functions are in bytes.
 */

/**
 * bd_loop_get_backing_file:
 * @dev_name: name of the loop device to get backing file for (e.g. "loop0")
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: (transfer full): path of the device's backing file
 */
gchar* bd_loop_get_backing_file (gchar *dev_name, gchar **error_message) {
    gchar *sys_path = g_strdup_printf ("/sys/class/block/%s/loop/backing_file", dev_name);
    gchar *ret = NULL;
    gboolean success = FALSE;
    GError *error;

    if (access (sys_path, R_OK) != 0) {
        g_free (sys_path);
        *error_message = g_strdup ("Failed to access device's parameters under /sys");
        return NULL;
    }

    success = g_file_get_contents (sys_path, &ret, NULL, &error);
    if (!success) {
        *error_message = g_strdup (error->message);
        g_free (sys_path);
        g_error_free (error);
        return NULL;
    }

    g_free (sys_path);
    return g_strstrip (ret);
}

/**
 * bd_loop_get_loop_name:
 * @file: path of the backing file to get loop name for
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: (transfer full): name of the loop device associated with the given
 * @file or %NULL if failed to determine
 */
gchar* bd_loop_get_loop_name (gchar *file, gchar **error_message) {
    glob_t globbuf;
    gchar **path_p;
    gboolean success = FALSE;
    GError *error;
    gchar *content;
    gboolean found = FALSE;
    gchar **parts;
    gchar *ret;

    if (glob ("/sys/block/loop*/loop/backing_file", GLOB_NOSORT, NULL, &globbuf) != 0) {
        /* TODO: be more specific about the errors? */
        *error_message = g_strdup_printf ("The given file %s has no associated loop device",
                                          file);
        return NULL;
    }

    for (path_p = globbuf.gl_pathv; *path_p && !found; path_p++) {
        success = g_file_get_contents (*path_p, &content, NULL, &error);
        if (!success)
            continue;

        g_strstrip (content);
        found = (g_strcmp0 (content, file) == 0);
        g_free (content);
    }

    if (!found) {
        *error_message = g_strdup_printf ("The given file %s has no associated loop device",
                                          file);
        return NULL;
    }

    parts = g_strsplit (*(path_p - 1), "/", 5);
    ret = g_strdup (parts[3]);
    g_strfreev (parts);

    return ret;
}

/**
 * bd_loop_setup:
 * @file: file to setup as a loop device
 * @loop_name: (out): if not %NULL, it is used to store the name of the loop device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @file was successfully setup as a loop device or not
 */
gboolean bd_loop_setup (gchar *file, gchar **loop_name, gchar **error_message) {
    gboolean success = FALSE;
    GError *error = NULL;
    gint status = 0;
    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;

    gchar *args[4] = {"losetup", "-f", file, NULL};

    success = g_spawn_sync (NULL, args, NULL, G_SPAWN_DEFAULT|G_SPAWN_SEARCH_PATH,
                            NULL, NULL, &stdout_data, &stderr_data, &status, &error);

    if (!success) {
        *error_message = g_strdup (error->message);
        g_error_free(error);
        return FALSE;
    }

    if (status != 0) {
        if (stderr_data && (g_strcmp0 ("", stderr_data) != 0)) {
            *error_message = stderr_data;
            g_free (stdout_data);
        } else {
            *error_message = stdout_data;
            g_free (stderr_data);
        }

        return FALSE;
    } else {
        if (loop_name)
            *loop_name = bd_loop_get_loop_name (file, error_message);
        g_free (stdout_data);
        g_free (stderr_data);
        return TRUE;
    }
}


#ifdef TESTING_LOOP
#include "test_loop.c"
#endif
