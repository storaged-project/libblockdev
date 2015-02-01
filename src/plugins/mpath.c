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
#include <utils.h>
#include "mpath.h"

/**
 * SECTION: mpath
 * @short_description: plugin for basic operations with multipath devices
 * @title: Mpath
 * @include: mpath.h
 *
 * A plugin for basic operations with multipath devices.
 */

/**
 * bd_mpath_error_quark: (skip)
 */
GQuark bd_mpath_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-mpath-error-quark");
}

/**
 * bd_mpath_flush_mpaths:
 * @error: (out): place to store error (if any)
 *
 * Returns: whether multipath device maps were successfully flushed or not
 *
 * Flushes all unused multipath device maps.
 */
gboolean bd_mpath_flush_mpaths (GError **error) {
    gchar *argv[3] = {"multipath", "-F", NULL};
    gboolean success = FALSE;
    gchar *output = NULL;

    /* try to flush the device maps */
    success = bd_utils_exec_and_report_error (argv, error);
    if (!success)
        return FALSE;

    /* list devices (there should be none) */
    argv[1] = "-ll";
    success = bd_utils_exec_and_capture_output (argv, &output, error);
    if (success && output && (g_strcmp0 (output, "") != 0)) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_FLUSH,
                     "Some device cannot be flushed: %s", output);
        g_free (output);
        return FALSE;
    }

    g_free (output);
    return TRUE;
}

/**
 * bd_mpath_is_mpath_member:
 * @device: device to test
 * @error: (out): place to store error (if any)
 *
 * Returns: %TRUE if the device is a multipath member, %FALSE if not or an error
 * appeared when queried (@error is set in those cases)
 */
gboolean bd_mpath_is_mpath_member (gchar *device, GError **error) {
    gchar *argv[4] = {"multipath", "-c", device, NULL};
    gboolean ret = FALSE;

    ret = bd_utils_exec_and_report_error (argv, error);
    if (!ret && g_error_matches (*error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_FAILED))
        /* multipath's exit code != 0 (e.g. 256) means that the device is not a
           multipath member so just clear the error (there's none really) and
           return FALSE */
        g_clear_error (error);

    return ret;
}

/**
 * bd_mpath_set_friendly_names:
 * @enabled: whether friendly names should be enabled or not
 * @error: (out): place to store error (if any)
 *
 * Returns: if successfully set or not
 */
gboolean bd_mpath_set_friendly_names (gboolean enabled, GError **error) {
    gchar *argv[8] = {"mpathconf", "--find_multipaths", "y", "--user_friendly_names", NULL, "--with_multipathd", "y", NULL};
    argv[4] = enabled ? "y" : "n";

    return bd_utils_exec_and_report_error (argv, error);
}
