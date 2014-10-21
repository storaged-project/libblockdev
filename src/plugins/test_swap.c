#include <glib/gprintf.h>

#include "swap.c"

int main (void) {
    gboolean succ = FALSE;
    GError *error = NULL;

    succ = bd_swap_mkswap ("/dev/xd1", "SWAP", &error);
    if (succ)
        puts ("Succeded.");
    else
        g_printf ("Not succeded: %s\n", error->message);
    g_clear_error(&error);

    succ = bd_swap_swapon ("/dev/xd1", 5, &error);
    if (succ)
        puts ("Succeded.");
    else
        g_printf ("Not succeded: %s\n", error->message);
    g_clear_error(&error);

    succ = bd_swap_swapoff ("/dev/xd1", &error);
    if (succ)
        puts ("Succeded.");
    else
        g_printf ("Not succeded: %s\n", error->message);
    g_clear_error(&error);

    succ = bd_swap_swapstatus ("/dev/xd1", &error);
    if (succ)
        puts ("Activated.");
    else {
        if (error)
            g_printf ("Error: %s\n", error->message);
        else
            puts ("Not activated.");
    }
    g_clear_error(&error);

    return 0;
}
