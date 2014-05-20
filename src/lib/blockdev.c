#include <dlfcn.h>
#include "blockdev.h"
#include "plugins.h"

#include "plugin_apis/lvm.c"

/**
 * SECTION: libblockdev
 * @short_description: a library for doing low-level operations with block devices
 * @title: libblockdev library
 * @include: blockdev.h
 *
 */

typedef struct BDPluginStatus {
    BDPluginSpec spec;
    gboolean loaded;
} BDPluginStatus;

/* KEEP THE ORDERING OF THIS ARRAY MATCHING THE BDPluginName ENUM! */
static BDPluginStatus plugins[BD_PLUGIN_UNDEF] = {
    {{BD_PLUGIN_LVM, "libbd_lvm.so"}, FALSE},
    {{BD_PLUGIN_SWAP, "libbd_swap.so"}, FALSE},
};

void set_plugin_so_name (BDPluginName name, gchar *so_name) {
    plugins[name].spec.so_name = so_name;
}

/**
 * bd_init:
 * @force_plugins: (allow-none): null-terminated list of plugins that should be loaded (even if
 *                 other plugins for the same technologies are found)
 *
 * Return: whether the library was successfully initialized or not
 */
gboolean bd_init (BDPluginSpec *force_plugins) {
    guint8 i = 0;

    if (force_plugins)
        for (i=0; force_plugins + i; i++)
            set_plugin_so_name(force_plugins[i].name, force_plugins[i].so_name);

    plugins[BD_PLUGIN_LVM].loaded = load_lvm_from_plugin(plugins[BD_PLUGIN_LVM].spec.so_name);

    /* TODO: check that plugins are loaded */
    return TRUE;
}

int main (int argc, char* argv[]) {
    bd_init(NULL);
    g_printf ("max LV size: %lu\n", bd_lvm_get_max_lv_size());

    return 0;
}
