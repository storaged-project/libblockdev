#include <glib.h>

#ifndef BD_PLUGINS
#define BD_PLUGINS

typedef enum {
    BD_PLUGIN_LVM = 0,
    BD_PLUGIN_SWAP,
    BD_PLUGIN_UNDEF
} BDPluginName;

typedef struct BDPluginSpec {
    BDPluginName name;
    gchar *so_name;
} BDPluginSpec;

gboolean bd_plugin_available (BDPluginName name);
BDPluginName* bd_available_plugins ();
gboolean bd_func_available (BDPluginName plugin, gchar *func_name);

#endif  /* BD_PLUGINS */
