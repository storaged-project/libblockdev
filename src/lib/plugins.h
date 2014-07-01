#include <glib.h>

#ifndef BD_PLUGINS
#define BD_PLUGINS

typedef enum {
    BD_PLUGIN_LVM = 0,
    BD_PLUGIN_BTRFS,
    BD_PLUGIN_SWAP,
    BD_PLUGIN_LOOP,
    BD_PLUGIN_CRYPTO,
    BD_PLUGIN_MPATH,
    BD_PLUGIN_DM,
    BD_PLUGIN_UNDEF
} BDPlugin;

typedef struct BDPluginSpec {
    BDPlugin name;
    gchar *so_name;
} BDPluginSpec;

gboolean bd_plugin_available (BDPlugin name);
BDPlugin* bd_available_plugins ();
gboolean bd_func_available (BDPlugin plugin, gchar *func_name);

#endif  /* BD_PLUGINS */
