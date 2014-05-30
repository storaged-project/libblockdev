#include <glib.h>
#include <string.h>
#include <unistd.h>

/**
 * run_and_report_error:
 * @argv: (array zero-terminated=1): the argv array for the call
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the call was successfull (no error and exit code 0) or not
 */
/* XXX: should this be moved somewhere else so that it is available for the
   other plugins as well? */
static gboolean run_and_report_error (gchar **argv, gchar **error_message) {
    gboolean success = FALSE;
    GError *error = NULL;
    gint status = 0;
    guint8 next_arg = 2;
    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;

    success = g_spawn_sync (NULL, argv, NULL, G_SPAWN_DEFAULT|G_SPAWN_SEARCH_PATH,
                            NULL, NULL, &stdout_data, &stderr_data, &status, &error);

    if (!success) {
        *error_message = g_strdup (error->message);
        g_error_free(error);
        return FALSE;
    }

    if (status != 0) {
        if (stderr_data) {
            *error_message = stderr_data;
            g_free (stdout_data);
        } else
            *error_message = stdout_data;

        return FALSE;
    }

    return TRUE;
}

/**
 * bd_swap_mkswap:
 * @device: a device to create swap space on
 * @label: (allow-none): a label for the swap space device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the swap space was successfully created or not
 */
gboolean bd_swap_mkswap (gchar *device, gchar *label, gchar **error_message) {
    guint8 next_arg = 2;

    /* We use -f to force since mkswap tends to refuse creation on lvs with
       a message about erasing bootbits sectors on whole disks. Bah. */
    gchar *argv[6] = {"mkswap", "-f", NULL, NULL, NULL, NULL};

    if (label) {
        argv[next_arg] = "-L";
        next_arg++;
        argv[next_arg] = label;
        next_arg++;
    }

    argv[next_arg] = device;

    return run_and_report_error (argv, error_message);
}

/**
 * bd_swap_swapon:
 * @device: swap device to activate
 * @priority: priority of the activated device or -1 to use the default
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the swap device was successfully activated or not
 */
gboolean bd_swap_swapon (gchar *device, gint priority, gchar **error_message) {
    gboolean success = FALSE;
    guint8 next_arg = 1;
    guint8 to_free_idx = 0;
    GIOChannel *dev_file = NULL;
    GIOStatus io_status = G_IO_STATUS_ERROR;
    gsize num_read = 0;
    GError *error = NULL;
    gchar dev_status[11];
    dev_status[10] = '\0';
    gint page_size;

    gchar *argv[5] = {"swapon", NULL, NULL, NULL, NULL};

    /* check the device if it is an activatable swap */
    dev_file = g_io_channel_new_file (device, "r", &error);
    if (!dev_file) {
        *error_message = g_strdup (error->message);
        g_error_free (error);
        return FALSE;
    }

    page_size = getpagesize ();
    page_size = CLAMP (page_size, 2048, page_size);
    io_status = g_io_channel_seek_position (dev_file, page_size - 10, G_SEEK_SET, &error);
    if (io_status != G_IO_STATUS_NORMAL) {
        *error_message = g_strdup_printf ("Failed to determine device's state: %s", error->message);
        g_error_free (error);
        g_io_channel_shutdown (dev_file, FALSE, &error);
        return FALSE;
    }

    io_status = g_io_channel_read_chars (dev_file, dev_status, 10, &num_read, &error);
    if ((io_status != G_IO_STATUS_NORMAL) || (num_read != 10)) {
        *error_message = g_strdup_printf ("Failed to determine device's state: %s", error->message);
        g_error_free (error);
        g_io_channel_shutdown (dev_file, FALSE, &error);
        return FALSE;
    }

    g_io_channel_shutdown (dev_file, FALSE, &error);

    if (g_str_has_prefix (dev_status, "SWAP-SPACE")) {
        *error_message = "Old swap format, cannot activate.";
        return FALSE;
    } else if (g_str_has_prefix (dev_status, "S1SUSPEND") || g_str_has_prefix (dev_status, "S2SUSPEND")) {
        *error_message = "Suspended system on the swap device, cannot activate.";
        return FALSE;
    } else if (!g_str_has_prefix (dev_status, "SWAPSPACE2")) {
        *error_message = "Unknown swap space format, cannot activate.";
        return FALSE;
    }

    if (priority >= 0) {
        argv[next_arg] = "-p";
        next_arg++;
        to_free_idx = next_arg;
        argv[next_arg] = g_strdup_printf ("%d", priority);
        next_arg++;
    }

    argv[next_arg] = device;

    success = run_and_report_error (argv, error_message);

    if (to_free_idx > 0)
        g_free (argv[to_free_idx]);

    return success;
}

/**
 * bd_swap_swapoff:
 * @device: swap device to deactivate
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the swap device was successfully deactivated or not
 */
gboolean bd_swap_swapoff (gchar *device, gchar **error_message) {
    gchar *argv[3] = {"swapoff", NULL, NULL};
    argv[1] = device;

    return run_and_report_error (argv, error_message);
}

/**
 * bd_swap_swapstatus:
 * @device: swap device to get status of
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: #TRUE if the swap device is active, #FALSE if not active or failed
 * to determine (@error_message is set not a non-NULL value in such case)
 */
gboolean bd_swap_swapstatus (gchar *device, gchar **error_message) {
    gchar *file_content;
    gchar *real_device = NULL;
    gchar *symlink = NULL;
    gsize length;
    gchar *next_line;
    gboolean success;
    GError *error = NULL;

    success = g_file_get_contents ("/proc/swaps", &file_content, &length, &error);
    if (!success) {
        *error_message = g_strdup (error->message);
        g_error_free(error);
        return FALSE;
    }

    /* get the real device node for device-mapper devices since the ones
       with meaningful names are just symlinks */
    if (g_str_has_prefix (device, "/dev/mapper")) {
        symlink = g_file_read_link (device, &error);
        if (!symlink) {
            *error_message = g_strdup (error->message);
            g_error_free(error);
            return FALSE;
        }

        /* the symlink starts with "../" */
        real_device = g_strdup_printf ("/dev/%s", symlink + 3);
    }

    /* no error, set *error_message to NULL to show it */
    *error_message = NULL;

    next_line = file_content;
    if (g_str_has_prefix (next_line, real_device ? real_device : device)) {
        g_free (symlink);
        g_free (real_device);
        g_free (file_content);
        return TRUE;
    }

    next_line = (strchr (next_line, '\n') + 1);
    while (next_line && ((next_line - file_content) < length)) {
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


#ifdef TESTING_SWAP
#include "test_swap.c"
#endif
