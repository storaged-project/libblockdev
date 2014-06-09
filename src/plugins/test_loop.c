#include <glib/gprintf.h>

int main (int argc, char **argv) {
    gchar *ret = NULL;
    gchar *err_msg = NULL;

    ret = bd_loop_get_backing_file ("loop0", &err_msg);
    if (!ret)
        g_printf ("Failed to get backing device for loop0: %s\n", err_msg);
    else
        g_printf ("loop0's backing device: %s\n", ret);
    g_free (ret);
    g_free (err_msg);

    return 0;
}
