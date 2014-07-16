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
#include <exec.h>

#include "dm.h"

/**
 * SECTION: dm
 * @short_description: libblockdev plugin for basic operations with device mapper
 * @title: DeviceMapper
 * @include: dm.h
 *
 * A libblockdev plugin for basic operations with device mapper.
 */

/**
 * bd_dm_create_linear:
 * @map_name: name of the map
 * @device: device to create map for
 * @length: length of the mapping in sectors
 * @uuid: (allow-none): UUID for the new dev mapper device or %NULL if not specified
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the new linear mapping @map_name was successfully created
 * for the @device or not
 */
gboolean bd_dm_create_linear (gchar *map_name, gchar *device, guint64 length, gchar *uuid, gchar **error_message) {
    gboolean success = FALSE;
    gchar *argv[9] = {"dmsetup", "create", map_name, "--table", NULL, NULL, NULL, NULL, NULL};

    gchar *table = g_strdup_printf ("0 %"G_GUINT64_FORMAT" linear %s 0", length, device);
    argv[4] = table;

    if (uuid) {
        argv[5] = "-u";
        argv[6] = uuid;
        argv[7] = device;
    } else
        argv[5] = device;

    success = bd_utils_exec_and_report_error (argv, error_message);
    g_free (table);

    return success;
}

/**
 * bd_dm_remove:
 * @map_name: name of the map to remove
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @map_name map was successfully removed or not
 */
gboolean bd_dm_remove (gchar *map_name, gchar **error_message) {
    gchar *argv[4] = {"dmsetup", "remove", map_name, NULL};

    return bd_utils_exec_and_report_error (argv, error_message);
}

/**
 * bd_dm_name_from_dm_node:
 * @dm_node: name of the DM node (e.g. "dm-0")
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: map name of the map providing the @dm_node device or %NULL
 * (@error_message contains the error in such cases)
 */
gchar* bd_dm_name_from_dm_node (gchar *dm_node, gchar **error_message) {
    gchar *ret = NULL;
    gboolean success = FALSE;
    GError *error;

    gchar *sys_path = g_strdup_printf ("/sys/class/block/%s/dm/name", dm_node);

    if (access (sys_path, R_OK) != 0) {
        g_free (sys_path);
        *error_message = g_strdup ("Failed to access dm node's parameters under /sys");
        return NULL;
    }

    success = g_file_get_contents (sys_path, &ret, NULL, &error);
    g_free (sys_path);

    if (!success) {
        *error_message = g_strdup (error->message);
         g_clear_error (&error);
        return NULL;
    }

    return g_strstrip (ret);
}

/**
 * bd_dm_node_from_name:
 * @map_name: name of the queried DM map
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: DM node name for the @map_name map or %NULL (@error_message contains
 * the error in such cases)
 */
gchar* bd_dm_node_from_name (gchar *map_name, gchar **error_message) {
    GError *error = NULL;
    gchar *symlink = NULL;
    gchar *ret = NULL;
    gchar *dev_mapper_path = g_strdup_printf ("/dev/mapper/%s", map_name);

    symlink = g_file_read_link (dev_mapper_path, &error);
    if (!symlink) {
        *error_message = g_strdup (error->message);
        g_error_free(error);
        g_free (dev_mapper_path);
        return FALSE;
    }

    g_strstrip (symlink);
    ret = g_path_get_basename (symlink);

    g_free (symlink);
    g_free (dev_mapper_path);

    return ret;
}
