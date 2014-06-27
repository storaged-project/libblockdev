#include <glib/gprintf.h>

#include "loop.c"

int main (void) {
    gchar *ret = NULL;
    gchar *err_msg = NULL;
    gboolean success = FALSE;

    ret = bd_loop_get_backing_file ("loop0", &err_msg);
    if (!ret)
        g_printf ("Failed to get backing device for loop0: %s\n", err_msg);
    else
        g_printf ("loop0's backing device: %s\n", ret);
    g_free (ret);
    g_free (err_msg);

    ret = bd_loop_get_loop_name ("/tmp/loop_test", &err_msg);
    if (!ret)
        g_printf ("Failed to get loop name for /test/loop_test: %s\n", err_msg);
    else
        g_printf ("/test/loop_test's loop device: %s\n", ret);
    g_free (ret);
    ret = NULL;
    g_free (err_msg);

    success = bd_loop_setup ("/tmp/loop_test", &ret, &err_msg);
    if (!success)
        g_printf ("Failed to setup /tmp/loop_test as loop device: %s", err_msg);
    else
        g_printf ("/tmp/loop_test setup at %s", ret);
    g_free (ret);
    g_free (err_msg);

    success = bd_loop_teardown ("/dev/loop0", &err_msg);
    if (!success)
        g_printf ("Failed to teardown /dev/loop0: %s", err_msg);
    else
        puts ("/dev/loop0 torn down");
    g_free (err_msg);

    return 0;
}
