/*
 * Copyright (C) 2017  Red Hat, Inc.
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
#include <libudev.h>

#include "dev_utils.h"

/**
 * bd_utils_dev_utils_error_quark: (skip)
 */
GQuark bd_utils_dev_utils_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-utils-dev_utils-error-quark");
}

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

/**
 * bd_utils_get_device_symlinks:
 * @dev_spec: specification of the device (e.g. "/dev/sda", any symlink, or the name of a file
 *            under "/dev")
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full) (array zero-terminated=1): a list of all symlinks (known to udev) for the
 *                                                     device specified with @dev_spec or %NULL in
 *                                                     case of error
 */
gchar** bd_utils_get_device_symlinks (const gchar *dev_spec, GError **error) {
    gchar *dev_path;
    struct udev *context;
    struct udev_device *device;
    struct udev_list_entry *entry = NULL;
    struct udev_list_entry *ent_it = NULL;
    guint64 n_links = 0;
    guint64 i = 0;
    gchar **ret = NULL;

    dev_path = bd_utils_resolve_device (dev_spec, error);
    if (!dev_path)
        return NULL;

    context = udev_new ();
    /* dev_path is the full path like "/dev/sda", we only need the device name ("sda") */
    device = udev_device_new_from_subsystem_sysname (context, "block", dev_path + 5);

    if (!device) {
        g_set_error (error, BD_UTILS_DEV_UTILS_ERROR, BD_UTILS_DEV_UTILS_ERROR_FAILED,
                     "Failed to get information about the device '%s' from udev database",
                     dev_path);
        g_free (dev_path);
        udev_unref (context);
        return NULL;
    }

    entry = udev_device_get_devlinks_list_entry (device);
    if (!entry) {
        g_set_error (error, BD_UTILS_DEV_UTILS_ERROR, BD_UTILS_DEV_UTILS_ERROR_FAILED,
                     "Failed to get symlinks for the device '%s'", dev_path);
        g_free (dev_path);
        udev_device_unref (device);
        udev_unref (context);
        return NULL;
    }
    g_free (dev_path);

    ent_it = entry;
    while (ent_it) {
        n_links++;
        ent_it = udev_list_entry_get_next (ent_it);
    }

    ret = g_new0 (gchar*, n_links + 1);
    ent_it = entry;
    while (ent_it) {
        ret[i++] = g_strdup (udev_list_entry_get_name (ent_it));
        ent_it = udev_list_entry_get_next (ent_it);
    }
    ret[i] = NULL;

    udev_device_unref (device);
    udev_unref (context);

    return ret;
}
