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
#include <string.h>
#include <unistd.h>
#include <utils.h>
#include "swap.h"

/**
 * SECTION: swap
 * @short_description: plugin for operations with swap space
 * @title: Swap
 * @include: swap.h
 *
 * A plugin for operations with swap space.
 */

/**
 * bd_swap_error_quark: (skip)
 */
GQuark bd_swap_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-swap-error-quark");
}

/**
 * check: (skip)
 */
gboolean check() {
    GError *error = NULL;
    gboolean ret = bd_utils_check_util_version ("mkswap", MKSWAP_MIN_VERSION, NULL, "mkswap from util-linux ([\\d\\.]+)", &error);

    if (!ret && error) {
        g_warning("Cannot load the swap plugin: %s" , error->message);
        g_clear_error (&error);
    }

    if (!ret)
        return FALSE;

    ret = bd_utils_check_util_version ("swapon", SWAPON_MIN_VERSION, NULL, "swapon from util-linux ([\\d\\.]+)", &error);
    if (!ret && error) {
        g_warning("Cannot load the swap plugin: %s" , error->message);
        g_clear_error (&error);
    }

    if (!ret)
        return FALSE;

    ret = bd_utils_check_util_version ("swapoff", SWAPOFF_MIN_VERSION, NULL, "swapoff from util-linux ([\\d\\.]+)", &error);
    if (!ret && error) {
        g_warning("Cannot load the swap plugin: %s" , error->message);
        g_clear_error (&error);
    }

    return ret;
}

/**
 * bd_swap_mkswap:
 * @device: a device to create swap space on
 * @label: (allow-none): a label for the swap space device
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkswap' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the swap space was successfully created or not
 */
gboolean bd_swap_mkswap (const gchar *device, const gchar *label, const BDExtraArg **extra, GError **error) {
    guint8 next_arg = 2;

    /* We use -f to force since mkswap tends to refuse creation on lvs with
       a message about erasing bootbits sectors on whole disks. Bah. */
    const gchar *argv[6] = {"mkswap", "-f", NULL, NULL, NULL, NULL};

    if (label) {
        argv[next_arg] = "-L";
        next_arg++;
        argv[next_arg] = label;
        next_arg++;
    }

    argv[next_arg] = device;

    return bd_utils_exec_and_report_error (argv, extra, error);
}

/**
 * bd_swap_swapon:
 * @device: swap device to activate
 * @priority: priority of the activated device or -1 to use the default
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the swap device was successfully activated or not
 */
gboolean bd_swap_swapon (const gchar *device, gint priority, GError **error) {
    gboolean success = FALSE;
    guint8 next_arg = 1;
    guint8 to_free_idx = 0;
    GIOChannel *dev_file = NULL;
    GIOStatus io_status = G_IO_STATUS_ERROR;
    GError *tmp_error = NULL;
    gsize num_read = 0;
    gchar dev_status[11];
    dev_status[10] = '\0';
    gint page_size;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    const gchar *argv[5] = {"swapon", NULL, NULL, NULL, NULL};
    msg = g_strdup_printf ("Started 'swapon %s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);


    bd_utils_report_progress (progress_id, 0, "Analysing the swap device");
    /* check the device if it is an activatable swap */
    dev_file = g_io_channel_new_file (device, "r", error);
    if (!dev_file) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    page_size = getpagesize ();
    page_size = MAX (2048, page_size);
    io_status = g_io_channel_seek_position (dev_file, page_size - 10, G_SEEK_SET, &tmp_error);
    if (io_status != G_IO_STATUS_NORMAL) {
        g_set_error (error, BD_SWAP_ERROR, BD_SWAP_ERROR_UNKNOWN_STATE,
                     "Failed to determine device's state: %s", tmp_error->message);
        g_clear_error (&tmp_error);
        g_io_channel_shutdown (dev_file, FALSE, &tmp_error);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    io_status = g_io_channel_read_chars (dev_file, dev_status, 10, &num_read, &tmp_error);
    if ((io_status != G_IO_STATUS_NORMAL) || (num_read != 10)) {
        g_set_error (error, BD_SWAP_ERROR, BD_SWAP_ERROR_UNKNOWN_STATE,
                     "Failed to determine device's state: %s", tmp_error->message);
        g_clear_error (&tmp_error);
        g_io_channel_shutdown (dev_file, FALSE, &tmp_error);
        g_clear_error (&tmp_error);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    g_io_channel_shutdown (dev_file, FALSE, &tmp_error);
    g_clear_error (&tmp_error);

    if (g_str_has_prefix (dev_status, "SWAP-SPACE")) {
        g_set_error (error, BD_SWAP_ERROR, BD_SWAP_ERROR_ACTIVATE,
                     "Old swap format, cannot activate.");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    } else if (g_str_has_prefix (dev_status, "S1SUSPEND") || g_str_has_prefix (dev_status, "S2SUSPEND")) {
        g_set_error (error, BD_SWAP_ERROR, BD_SWAP_ERROR_ACTIVATE,
                     "Suspended system on the swap device, cannot activate.");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    } else if (!g_str_has_prefix (dev_status, "SWAPSPACE2")) {
        g_set_error (error, BD_SWAP_ERROR, BD_SWAP_ERROR_ACTIVATE,
                     "Unknown swap space format, cannot activate.");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    bd_utils_report_progress (progress_id, 10, "Swap device analysed, enabling");
    if (priority >= 0) {
        argv[next_arg] = "-p";
        next_arg++;
        to_free_idx = next_arg;
        argv[next_arg] = g_strdup_printf ("%d", priority);
        next_arg++;
    }

    argv[next_arg] = device;

    success = bd_utils_exec_and_report_error_no_progress (argv, NULL, error);

    if (to_free_idx > 0)
        g_free ((gchar *) argv[to_free_idx]);

    bd_utils_report_finished (progress_id, "Completed");
    return success;
}

/**
 * bd_swap_swapoff:
 * @device: swap device to deactivate
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the swap device was successfully deactivated or not
 */
gboolean bd_swap_swapoff (const gchar *device, GError **error) {
    const gchar *argv[3] = {"swapoff", NULL, NULL};
    argv[1] = device;

    return bd_utils_exec_and_report_error (argv, NULL, error);
}

/**
 * bd_swap_swapstatus:
 * @device: swap device to get status of
 * @error: (out): place to store error (if any)
 *
 * Returns: %TRUE if the swap device is active, %FALSE if not active or failed
 * to determine (@error) is set not a non-NULL value in such case)
 */
gboolean bd_swap_swapstatus (const gchar *device, GError **error) {
    gchar *file_content;
    gchar *real_device = NULL;
    gchar *symlink = NULL;
    gsize length;
    gchar *next_line;
    gboolean success;

    success = g_file_get_contents ("/proc/swaps", &file_content, &length, error);
    if (!success) {
        /* error is already populated */
        return FALSE;
    }

    /* get the real device node for device-mapper devices since the ones
       with meaningful names are just symlinks */
    if (g_str_has_prefix (device, "/dev/mapper/") || g_str_has_prefix (device, "/dev/md/")) {
        symlink = g_file_read_link (device, error);
        if (!symlink) {
            /* the device doesn't exist and thus is not an active swap */
            g_clear_error (error);
            return FALSE;
        }

        /* the symlink starts with "../" */
        real_device = g_strdup_printf ("/dev/%s", symlink + 3);
    }

    if (g_str_has_prefix (file_content, real_device ? real_device : device)) {
        g_free (symlink);
        g_free (real_device);
        g_free (file_content);
        return TRUE;
    }

    next_line = (strchr (file_content, '\n') + 1);
    while (next_line && ((gsize)(next_line - file_content) < length)) {
        if (g_str_has_prefix (next_line, real_device ? real_device : device)) {
            g_free (symlink);
            g_free (real_device);
            g_free (file_content);
            return TRUE;
        }

        next_line = (strchr (next_line, '\n') + 1);
    }

    g_free (symlink);
    g_free (real_device);
    g_free (file_content);
    return FALSE;
}
