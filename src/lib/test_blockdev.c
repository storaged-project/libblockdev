#include <glib.h>
#include <glib/gprintf.h>

#include "blockdev.c"

int main (int argc, char* argv[]) {
    GError *error = NULL;
    bd_init(NULL, NULL, &error);
    g_printf ("available plugins: %s\n", g_strjoinv (", ", bd_get_available_plugin_names()));

    if (bd_is_plugin_available (BD_PLUGIN_SWAP))
        g_printf ("'%s' plugin available\n", plugin_names[BD_PLUGIN_SWAP]);
    else
        g_printf ("'%s' plugin not available\n", plugin_names[BD_PLUGIN_SWAP]);

    /* the undef plugin has no name */
    if (bd_is_plugin_available (BD_PLUGIN_UNDEF))
        puts ("'undef' plugin available");
    else
        puts ("'undef' plugin not available");

    g_printf ("max LV size: %"G_GUINT64_FORMAT"\n", bd_lvm_get_max_lv_size(&error));

    return 0;
}
