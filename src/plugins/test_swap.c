#include <glib/gprintf.h>

int main (int argc, char **argv) {
    gboolean succ = FALSE;
    gchar *err_msg = NULL;

    succ = bd_swap_mkswap ("/dev/xd1", "SWAP", &err_msg);
    if (succ)
        puts ("Succeded.");
    else
        g_printf ("Not succeded: %s\n", err_msg);

    return 0;
}
