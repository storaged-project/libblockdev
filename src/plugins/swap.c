#include <glib.h>

/**
 * bd_swap_mkswap:
 * @device: a device to create swap space on
 * @label: (allow-none): a label for the swap space device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Return: whether the swap space was successfully created or not
 */
gboolean bd_swap_mkswap (gchar *device, gchar *label, gchar **error_message) {
    gboolean success = FALSE;
    GError *error = NULL;
    gint status = 0;
    guint8 next_arg = 2;

    /* We use -f to force since mkswap tends to refuse creation on lvs with
       a message about erasing bootbits sectors on whole disks. Bah. */
    gchar *argv[5] = {"mkswap", "-f", NULL, NULL, NULL};

    if (label) {
        argv[next_arg] = "-L";
        next_arg++;
        argv[next_arg] = label;
        next_arg++;
    }

    argv[next_arg] = device;

    success = g_spawn_sync (NULL, argv, NULL, G_SPAWN_DEFAULT|G_SPAWN_SEARCH_PATH,
                            NULL, NULL, NULL, NULL, &status, &error);

    if (!success) {
        *error_message = g_strdup (error->message);
        g_error_free(error);
        return FALSE;
    }

    /* TODO: check status == 0 and add stdout+stderr to error_message otherwise */

    return TRUE;
}
