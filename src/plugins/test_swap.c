#include <glib/gprintf.h>

int main (int argc, char **argv) {
    gboolean succ = FALSE;
    gchar *err_msg = NULL;

    succ = bd_swap_mkswap ("/dev/xd1", "SWAP", &err_msg);
    if (succ)
        puts ("Succeded.");
    else
        g_printf ("Not succeded: %s\n", err_msg);

    g_free (err_msg);
    err_msg = NULL;

    succ = bd_swap_swapon ("/dev/xd1", 5, &err_msg);
    if (succ)
        puts ("Succeded.");
    else
        g_printf ("Not succeded: %s\n", err_msg);

    g_free (err_msg);
    err_msg = NULL;

    succ = bd_swap_swapoff ("/dev/xd1", &err_msg);
    if (succ)
        puts ("Succeded.");
    else
        g_printf ("Not succeded: %s\n", err_msg);

    g_free (err_msg);
    err_msg = NULL;

    succ = bd_swap_swapstatus ("/dev/xd1", &err_msg);
    if (succ)
        puts ("Activated.");
    else {
        if (err_msg)
            g_printf ("Error: %s\n", err_msg);
        else
            puts ("Not activated.");
    }

    return 0;
}
