#include <dlfcn.h>
#include "blockdev.h"
#include "plugins.h"

#include "plugin_apis/lvm.h"
#include "plugin_apis/lvm.c"
#include "plugin_apis/btrfs.h"
#include "plugin_apis/btrfs.c"
#include "plugin_apis/swap.h"
#include "plugin_apis/swap.c"
#include "plugin_apis/loop.h"
#include "plugin_apis/loop.c"
#include "plugin_apis/crypto.h"
#include "plugin_apis/crypto.c"
#include "plugin_apis/mpath.c"
#include "plugin_apis/mpath.h"
#include "plugin_apis/dm.c"
#include "plugin_apis/dm.h"

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

/* KEEP THE ORDERING OF THESE ARRAYS MATCHING THE BDPluginName ENUM! */
static BDPluginStatus plugins[BD_PLUGIN_UNDEF] = {
    {{BD_PLUGIN_LVM, "libbd_lvm.so"}, FALSE},
    {{BD_PLUGIN_BTRFS, "libbd_btrfs.so"}, FALSE},
    {{BD_PLUGIN_SWAP, "libbd_swap.so"}, FALSE},
    {{BD_PLUGIN_LOOP, "libbd_loop.so"}, FALSE},
    {{BD_PLUGIN_CRYPTO, "libbd_crypto.so"}, FALSE},
    {{BD_PLUGIN_MPATH, "libbd_mpath.so"}, FALSE},
    {{BD_PLUGIN_DM, "libbd_dm.so"}, FALSE}
};
static gchar* plugin_names[BD_PLUGIN_UNDEF] = {
    "lvm", "btrfs", "swap", "loop", "crypto", "mpath", "dm"
};

void set_plugin_so_name (BDPlugin name, gchar *so_name) {
    plugins[name].spec.so_name = so_name;
}

/**
 * bd_init:
 * @force_plugins: (allow-none): null-terminated list of plugins that should be loaded (even if
 *                 other plugins for the same technologies are found)
 *
 * Returns: whether the library was successfully initialized or not
 */
gboolean bd_init (BDPluginSpec *force_plugins) {
    guint8 i = 0;
    gboolean all_loaded = TRUE;

    if (force_plugins)
        for (i=0; force_plugins + i; i++)
            set_plugin_so_name(force_plugins[i].name, force_plugins[i].so_name);

    plugins[BD_PLUGIN_LVM].loaded = load_lvm_from_plugin(plugins[BD_PLUGIN_LVM].spec.so_name) != NULL;
    plugins[BD_PLUGIN_BTRFS].loaded = load_btrfs_from_plugin(plugins[BD_PLUGIN_BTRFS].spec.so_name) != NULL;
    plugins[BD_PLUGIN_SWAP].loaded = load_swap_from_plugin(plugins[BD_PLUGIN_SWAP].spec.so_name) != NULL;
    plugins[BD_PLUGIN_LOOP].loaded = load_loop_from_plugin(plugins[BD_PLUGIN_LOOP].spec.so_name) != NULL;
    plugins[BD_PLUGIN_CRYPTO].loaded = load_crypto_from_plugin(plugins[BD_PLUGIN_CRYPTO].spec.so_name) != NULL;
    plugins[BD_PLUGIN_MPATH].loaded = load_mpath_from_plugin(plugins[BD_PLUGIN_MPATH].spec.so_name) != NULL;
    plugins[BD_PLUGIN_DM].loaded = load_dm_from_plugin(plugins[BD_PLUGIN_DM].spec.so_name) != NULL;

    for (i=0; (i < BD_PLUGIN_UNDEF) && all_loaded; i++)
        all_loaded = all_loaded && plugins[i].loaded;

    return all_loaded;
}

/**
 * bd_get_available_plugin_names:
 *
 * Returns: (transfer container) (array zero-terminated=1): an array of string
 * names of plugins that are available
 */
gchar** bd_get_available_plugin_names () {
    guint8 i = 0;
    guint8 num_loaded = 0;
    guint8 next = 0;

    for (i=0; i < BD_PLUGIN_UNDEF; i++)
        if (plugins[i].loaded)
            num_loaded++;

    gchar **ret_plugin_names = g_new (gchar*, num_loaded + 1);
    for (i=0; i < BD_PLUGIN_UNDEF; i++)
        if (plugins[i].loaded) {
            ret_plugin_names[next] = plugin_names[i];
            next++;
        }
    ret_plugin_names[next] = NULL;

    return ret_plugin_names;
}

/**
 * bd_is_plugin_available:
 * @plugin: the queried plugin
 *
 * Returns: whether the given plugin is available or not
 */
gboolean bd_is_plugin_available (BDPlugin plugin) {
    if (plugin < BD_PLUGIN_UNDEF)
        return plugins[plugin].loaded;
    else
        return FALSE;
}
