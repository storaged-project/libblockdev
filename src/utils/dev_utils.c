/*
 * Copyright (C) 2017  Red Hat, Inc.
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

#include "dev_utils.h"

/**
 * bd_utils_resolve_device:
 * @dev_spec: specification of the device (e.g. "/dev/sda", any symlink, or the name of a file
 *            under "/dev")
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): the full real path of the device (e.g. "/dev/md126"
 *                           for "/dev/md/my_raid") or %NULL in case of error
 */
gchar* bd_utils_resolve_device (const gchar *dev_spec, GError **error) {
    gchar *path = NULL;
    gchar *symlink = NULL;

    /* TODO: check that the resulting path is a block device? */

    if (!g_str_has_prefix (dev_spec, "/dev/"))
        path = g_strdup_printf ("/dev/%s", dev_spec);
    else
        path = g_strdup (dev_spec);

    symlink = g_file_read_link (path, error);
    if (!symlink) {
        if (g_error_matches (*error, G_FILE_ERROR, G_FILE_ERROR_INVAL)) {
            /* invalid argument -> not a symlink -> nothing to resolve */
            g_clear_error (error);
            return path;
        } else {
            /* some other error, just report it */
            g_free (path);
            return NULL;
        }
    }
    g_free (path);

    if (g_str_has_prefix (symlink, "../"))
        path = g_strdup_printf ("/dev/%s", symlink + 3);
    else
        path = g_strdup_printf ("/dev/%s", symlink);
    g_free (symlink);

    return path;
}
