#include <dlfcn.h>
#include <utils.h>
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
#include "plugin_apis/mdraid.h"
#include "plugin_apis/mdraid.c"

/**
 * SECTION: blockdev
 * @short_description: a library for doing low-level operations with block devices
 * @title: blockdev library
 * @include: blockdev.h
 *
 */

static GMutex init_lock;
static gboolean initialized = FALSE;

typedef struct BDPluginStatus {
    BDPluginSpec spec;
    gpointer handle;
} BDPluginStatus;

/* KEEP THE ORDERING OF THESE ARRAYS MATCHING THE BDPluginName ENUM! */
static gchar * default_plugin_so[BD_PLUGIN_UNDEF] = {
    "libbd_lvm.so."MAJOR_VER, "libbd_btrfs.so."MAJOR_VER,
    "libbd_swap.so."MAJOR_VER, "libbd_loop.so."MAJOR_VER,
    "libbd_crypto.so."MAJOR_VER, "libbd_mpath.so."MAJOR_VER,
    "libbd_dm.so."MAJOR_VER, "libbd_mdraid.so."MAJOR_VER
};
static BDPluginStatus plugins[BD_PLUGIN_UNDEF] = {
    {{BD_PLUGIN_LVM, NULL}, NULL},
    {{BD_PLUGIN_BTRFS, NULL}, NULL},
    {{BD_PLUGIN_SWAP, NULL}, NULL},
    {{BD_PLUGIN_LOOP, NULL}, NULL},
    {{BD_PLUGIN_CRYPTO, NULL}, NULL},
    {{BD_PLUGIN_MPATH, NULL}, NULL},
    {{BD_PLUGIN_DM, NULL}, NULL},
    {{BD_PLUGIN_MDRAID, NULL}, NULL}
};
static gchar* plugin_names[BD_PLUGIN_UNDEF] = {
    "lvm", "btrfs", "swap", "loop", "crypto", "mpath", "dm", "mdraid"
};

static void set_plugin_so_name (BDPlugin name, gchar *so_name) {
    plugins[name].spec.so_name = so_name;
}

static gboolean load_plugins (BDPluginSpec **require_plugins, gboolean reload) {
    guint8 i = 0;
    gboolean requested_loaded = TRUE;

    if (reload)
        for (i=0; i < BD_PLUGIN_UNDEF; i++) {
            if (plugins[i].handle && (dlclose (plugins[i].handle) != 0))
                g_warning ("Failed to close %s plugin", plugin_names[i]);
            plugins[i].handle = NULL;
        }

    /* clean all so names and populate back those that are requested or the
       defaults */
    for (i=0; i < BD_PLUGIN_UNDEF; i++)
        plugins[i].spec.so_name = NULL;
    if (require_plugins) {
        /* set requested so names or defaults if so names are not specified */
        for (i=0; *(require_plugins + i); i++) {
            if (require_plugins[i]->so_name)
                set_plugin_so_name(require_plugins[i]->name, require_plugins[i]->so_name);
            else
                set_plugin_so_name(require_plugins[i]->name, default_plugin_so[require_plugins[i]->name]);
        }
    }
    else
        /* nothing requested, just use defaults for everything */
        for (i=0; i < BD_PLUGIN_UNDEF; i++)
            if (!plugins[i].spec.so_name)
                plugins[i].spec.so_name = default_plugin_so[i];

    if (!plugins[BD_PLUGIN_LVM].handle && plugins[BD_PLUGIN_LVM].spec.so_name)
        plugins[BD_PLUGIN_LVM].handle = load_lvm_from_plugin(plugins[BD_PLUGIN_LVM].spec.so_name);
    if (!plugins[BD_PLUGIN_BTRFS].handle && plugins[BD_PLUGIN_BTRFS].spec.so_name)
        plugins[BD_PLUGIN_BTRFS].handle = load_btrfs_from_plugin(plugins[BD_PLUGIN_BTRFS].spec.so_name);
    if (!plugins[BD_PLUGIN_SWAP].handle && plugins[BD_PLUGIN_SWAP].spec.so_name)
        plugins[BD_PLUGIN_SWAP].handle = load_swap_from_plugin(plugins[BD_PLUGIN_SWAP].spec.so_name);
    if (!plugins[BD_PLUGIN_LOOP].handle && plugins[BD_PLUGIN_LOOP].spec.so_name)
        plugins[BD_PLUGIN_LOOP].handle = load_loop_from_plugin(plugins[BD_PLUGIN_LOOP].spec.so_name);
    if (!plugins[BD_PLUGIN_CRYPTO].handle && plugins[BD_PLUGIN_CRYPTO].spec.so_name)
        plugins[BD_PLUGIN_CRYPTO].handle = load_crypto_from_plugin(plugins[BD_PLUGIN_CRYPTO].spec.so_name);
    if (!plugins[BD_PLUGIN_MPATH].handle && plugins[BD_PLUGIN_MPATH].spec.so_name)
        plugins[BD_PLUGIN_MPATH].handle = load_mpath_from_plugin(plugins[BD_PLUGIN_MPATH].spec.so_name);
    if (!plugins[BD_PLUGIN_DM].handle && plugins[BD_PLUGIN_DM].spec.so_name)
        plugins[BD_PLUGIN_DM].handle = load_dm_from_plugin(plugins[BD_PLUGIN_DM].spec.so_name);
    if (!plugins[BD_PLUGIN_MDRAID].handle && plugins[BD_PLUGIN_MDRAID].spec.so_name)
        plugins[BD_PLUGIN_MDRAID].handle = load_mdraid_from_plugin(plugins[BD_PLUGIN_MDRAID].spec.so_name);

    for (i=0; (i < BD_PLUGIN_UNDEF) && requested_loaded; i++)
        if (plugins[i].spec.so_name)
            requested_loaded = requested_loaded && plugins[i].handle;

    return requested_loaded;
}

GQuark bd_init_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-init-error-quark");
}

/**
 * bd_init:
 * @require_plugins: (allow-none) (array zero-terminated=1): %NULL-terminated list
 *                 of plugins that should be loaded (if no so_name is specified
 *                 for the plugin, the default is used) or %NULL to load all
 *                 plugins
 * @log_func: (allow-none) (scope notified): logging function to use
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the library was successfully initialized or not
 */
gboolean bd_init (BDPluginSpec **require_plugins, BDUtilsLogFunc log_func, GError **error) {
    gboolean success = TRUE;
    g_mutex_lock (&init_lock);
    if (initialized) {
        g_warning ("bd_init() called more than once! Use bd_reinit() to reinitialize "
                   "or bd_is_initialized() to get the current state.");
        g_mutex_unlock (&init_lock);
        return FALSE;
    }

    if (!load_plugins (require_plugins, FALSE)) {
        g_set_error (error, BD_INIT_ERROR, BD_INIT_ERROR_PLUGINS_FAILED,
                     "Failed to load plugins");
        /* the library is unusable without the plugins so we can just return here */
        success = FALSE;
    }

    if (log_func && !bd_utils_init_logging (log_func, error))
        /* the error is already populated */
        success = FALSE;

    initialized = success;
    g_mutex_unlock (&init_lock);

    return success;
}

/**
 * bd_reinit:
 * @require_plugins: (allow-none) (array zero-terminated=1): %NULL-terminated list
 *                 of plugins that should be loaded (if no so_name is specified
 *                 for the plugin, the default is used) or %NULL to load all
 *                 plugins
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
gboolean bd_reinit (BDPluginSpec **require_plugins, gboolean reload, BDUtilsLogFunc log_func, GError **error) {
    gboolean success = TRUE;
    g_mutex_lock (&init_lock);
    if (!load_plugins (require_plugins, reload)) {
        g_set_error (error, BD_INIT_ERROR, BD_INIT_ERROR_PLUGINS_FAILED,
                     "Failed to load plugins");
        /* the library is unusable without the plugins so we can just return here */
        success = FALSE;
    }

    if (log_func && !bd_utils_init_logging (log_func, error))
        /* the error is already populated */
        success = FALSE;

    g_mutex_unlock (&init_lock);
    return success;
}

/**
 * bd_is_initialized:
 *
 * Returns: whether the library is initialized (init() was called) or not
 */
gboolean bd_is_initialized () {
    gboolean is = FALSE;
    g_mutex_lock (&init_lock);
    is = initialized;
    g_mutex_unlock (&init_lock);
    return is;
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
