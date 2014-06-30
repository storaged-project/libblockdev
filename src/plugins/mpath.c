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
#include <exec.h>

/**
 * SECTION: mpath
 * @short_description: libblockdev plugin for basic operations with multipath devices
 * @title: Mpath
 * @include: mpath.h
 *
 * A libblockdev plugin for basic operations with multipath devices.
 */

/**
 * bd_mpath_flush_mpaths:
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether multipath device maps were successfully flushed or not
 *
 * Flushes all unused multipath device maps.
 */
gboolean bd_mpath_flush_mpaths (gchar **error_message) {
    gchar *argv[3] = {"multipath", "-F", NULL};
    gboolean success = FALSE;
    gchar *output = NULL;

    /* try to flush the device maps */
    success = bd_utils_exec_and_report_error (argv, error_message);
    if (!success)
        return FALSE;

    /* list devices (there should be none) */
    argv[1] = "-ll";
    success = bd_utils_exec_and_capture_output (argv, &output, error_message);
    if (success && output && (g_strcmp0 (output, "") != 0)) {
        g_free (output);
        *error_message = g_strdup ("Some device cannot be flushed");
        return FALSE;
    }

    g_free (output);
    return TRUE;
}

/**
 * bd_mpath_is_mpath_member:
 * @device: device to test
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: %TRUE if the device is a multipath member, %FALSE if not or an error
 * appeared when queried (@error_message is set in those cases)
 */
gboolean bd_mpath_is_mpath_member (gchar *device, gchar **error_message) {
    gchar *argv[4] = {"multipath", "-c", device, NULL};

    return bd_utils_exec_and_report_error (argv, error_message);
}

/**
 * bd_mpath_set_friendly_names:
 * @enabled: whether friendly names should be enabled or not
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: if successfully set or not
 */
gboolean bd_mpath_set_friendly_names (gboolean enabled, gchar **error_message) {
    gchar *argv[6] = {"multipath", "--user_friendly_names", NULL, "--with_multipathd", "y", NULL};
    argv[2] = enabled ? "y" : "n";

    return bd_utils_exec_and_report_error (argv, error_message);
}
