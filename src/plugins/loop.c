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

#ifdef TESTING_LOOP
#include "test_loop.c"
#endif
