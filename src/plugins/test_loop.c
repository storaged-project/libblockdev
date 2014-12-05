#include <glib/gprintf.h>

#include "loop.c"

int main (void) {
    gchar *ret = NULL;
    GError *error = NULL;
    gboolean success = FALSE;

    ret = bd_loop_get_backing_file ("loop0", &error);
    if (!ret)
        g_printf ("Failed to get backing device for loop0: %s\n", error->message);
    else
        g_printf ("loop0's backing device: %s\n", ret);
    g_free (ret);
    g_clear_error (&error);

    ret = bd_loop_get_loop_name ("/tmp/loop_test", &error);
    if (!ret)
        g_printf ("Failed to get loop name for /test/loop_test: %s\n", error->message);
    else
        g_printf ("/test/loop_test's loop device: %s\n", ret);
    g_free (ret);
    ret = NULL;
    g_clear_error (&error);

    success = bd_loop_setup ("/tmp/loop_test", &ret, &error);
    if (!success)
        g_printf ("Failed to setup /tmp/loop_test as loop device: %s", error->message);
    else
        g_printf ("/tmp/loop_test setup at %s", ret);
    g_free (ret);
    g_clear_error (&error);

    success = bd_loop_teardown ("/dev/loop0", &error);
    if (!success)
        g_printf ("Failed to teardown /dev/loop0: %s", error->message);
    else
        puts ("/dev/loop0 torn down");
    g_clear_error (&error);

    return 0;
}
