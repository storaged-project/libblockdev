#include <dlfcn.h>
#include "blockdev.h"
#include "plugins.h"
#include "exec.h"

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
#include "plugin_apis/mdraid.h"
#include "plugin_apis/mdraid.c"

/**
 * SECTION: libblockdev
 * @short_description: a library for doing low-level operations with block devices
 * @title: libblockdev library
 * @include: blockdev.h
 *
 */

typedef struct BDPluginStatus {
    BDPluginSpec spec;
    gpointer handle;
} BDPluginStatus;

/* KEEP THE ORDERING OF THESE ARRAYS MATCHING THE BDPluginName ENUM! */
static BDPluginStatus plugins[BD_PLUGIN_UNDEF] = {
    {{BD_PLUGIN_LVM, "libbd_lvm.so"}, NULL},
    {{BD_PLUGIN_BTRFS, "libbd_btrfs.so"}, NULL},
    {{BD_PLUGIN_SWAP, "libbd_swap.so"}, NULL},
    {{BD_PLUGIN_LOOP, "libbd_loop.so"}, NULL},
    {{BD_PLUGIN_CRYPTO, "libbd_crypto.so"}, NULL},
    {{BD_PLUGIN_MPATH, "libbd_mpath.so"}, NULL},
    {{BD_PLUGIN_DM, "libbd_dm.so"}, NULL},
    {{BD_PLUGIN_MDRAID, "libbd_mdraid.so"}, NULL}
};
static gchar* plugin_names[BD_PLUGIN_UNDEF] = {
    "lvm", "btrfs", "swap", "loop", "crypto", "mpath", "dm", "mdraid"
};

static void set_plugin_so_name (BDPlugin name, gchar *so_name) {
    plugins[name].spec.so_name = so_name;
}

static gboolean load_plugins (BDPluginSpec **force_plugins, gboolean reload) {
    guint8 i = 0;
    gboolean all_loaded = TRUE;

    if (reload)
        for (i=0; i < BD_PLUGIN_UNDEF; i++) {
            if (plugins[i].handle && (dlclose (plugins[i].handle) != 0))
                g_warning ("Failed to close %s plugin", plugin_names[i]);
            plugins[i].handle = NULL;
        }

    if (force_plugins)
        for (i=0; *(force_plugins + i); i++)
            set_plugin_so_name(force_plugins[i]->name, force_plugins[i]->so_name);

    if (!plugins[BD_PLUGIN_LVM].handle)
        plugins[BD_PLUGIN_LVM].handle = load_lvm_from_plugin(plugins[BD_PLUGIN_LVM].spec.so_name);
    if (!plugins[BD_PLUGIN_BTRFS].handle)
        plugins[BD_PLUGIN_BTRFS].handle = load_btrfs_from_plugin(plugins[BD_PLUGIN_BTRFS].spec.so_name);
    if (!plugins[BD_PLUGIN_SWAP].handle)
        plugins[BD_PLUGIN_SWAP].handle = load_swap_from_plugin(plugins[BD_PLUGIN_SWAP].spec.so_name);
    if (!plugins[BD_PLUGIN_LOOP].handle)
        plugins[BD_PLUGIN_LOOP].handle = load_loop_from_plugin(plugins[BD_PLUGIN_LOOP].spec.so_name);
    if (!plugins[BD_PLUGIN_CRYPTO].handle)
        plugins[BD_PLUGIN_CRYPTO].handle = load_crypto_from_plugin(plugins[BD_PLUGIN_CRYPTO].spec.so_name);
    if (!plugins[BD_PLUGIN_MPATH].handle)
        plugins[BD_PLUGIN_MPATH].handle = load_mpath_from_plugin(plugins[BD_PLUGIN_MPATH].spec.so_name);
    if (!plugins[BD_PLUGIN_DM].handle)
        plugins[BD_PLUGIN_DM].handle = load_dm_from_plugin(plugins[BD_PLUGIN_DM].spec.so_name);
    if (!plugins[BD_PLUGIN_MDRAID].handle)
        plugins[BD_PLUGIN_MDRAID].handle = load_mdraid_from_plugin(plugins[BD_PLUGIN_MDRAID].spec.so_name);

    for (i=0; (i < BD_PLUGIN_UNDEF) && all_loaded; i++)
        all_loaded = all_loaded && plugins[i].handle;

    return all_loaded;
}

GQuark bd_init_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-init-error-quark");
}

/**
 * bd_init:
 * @force_plugins: (allow-none) (array zero-terminated=1): null-terminated list
 *                 of plugins that should be loaded (even if
 *                 other plugins for the same technologies are found)
 * @log_func: (allow-none) (scope notified): logging function to use
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the library was successfully initialized or not
 */
gboolean bd_init (BDPluginSpec **force_plugins, BDUtilsLogFunc log_func, GError **error) {
    if (!load_plugins (force_plugins, FALSE)) {
        g_set_error (error, BD_INIT_ERROR, BD_INIT_ERROR_PLUGINS_FAILED,
                     "Failed to load plugins");
        /* the library is unusable without the plugins so we can just return here */
        return FALSE;
    }

    if (log_func && !bd_utils_init_logging (log_func, error))
        /* the error is already populated */
        return FALSE;

    /* everything went okay */
    return TRUE;
}

/**
 * bd_reinit:
 * @force_plugins: (allow-none) (array zero-terminated=1): null-terminated list
 *                 of plugins that should be loaded (even if
 *                 other plugins for the same technologies are found)
 * @reload: whether to reload the already loaded plugins or not
 * @log_func: (allow-none) (scope notified): logging function to use or %NULL
 *                                           to keep the old one
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the library was successfully initialized or not
 *
 * If @reload is %TRUE all the plugins are closed and reloaded otherwise only
 * the missing plugins are loaded.
 */
gboolean bd_reinit (BDPluginSpec **force_plugins, gboolean reload, BDUtilsLogFunc log_func, GError **error) {
    if (!load_plugins (force_plugins, reload)) {
        g_set_error (error, BD_INIT_ERROR, BD_INIT_ERROR_PLUGINS_FAILED,
                     "Failed to load plugins");
        /* the library is unusable without the plugins so we can just return here */
        return FALSE;
    }

    if (log_func && !bd_utils_init_logging (log_func, error))
        /* the error is already populated */
        return FALSE;

    /* everything went okay */
    return TRUE;
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
        if (plugins[i].handle)
            num_loaded++;

    gchar **ret_plugin_names = g_new (gchar*, num_loaded + 1);
    for (i=0; i < BD_PLUGIN_UNDEF; i++)
        if (plugins[i].handle) {
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
        return plugins[plugin].handle != NULL;
    else
        return FALSE;
}
