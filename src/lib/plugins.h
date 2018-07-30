#include <glib.h>
#include <glib-object.h>

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
    BD_PLUGIN_MDRAID,
    BD_PLUGIN_KBD,
    BD_PLUGIN_S390,
    BD_PLUGIN_PART,
    BD_PLUGIN_FS,
    BD_PLUGIN_NVDIMM,
    BD_PLUGIN_VDO,
    BD_PLUGIN_UNDEF
} BDPlugin;

#define BD_TYPE_PLUGIN_SPEC (bd_plugin_spec_get_type ())
GType bd_plugin_spec_get_type(void);

typedef struct BDPluginSpec {
    BDPlugin name;
    const gchar *so_name;
} BDPluginSpec;

BDPluginSpec* bd_plugin_spec_copy (BDPluginSpec *spec);
void bd_plugin_spec_free (BDPluginSpec *spec);

gboolean bd_is_plugin_available (BDPlugin plugin);
gchar** bd_get_available_plugin_names (void);
gchar* bd_get_plugin_soname (BDPlugin plugin);
gchar* bd_get_plugin_name (BDPlugin plugin);

#endif  /* BD_PLUGINS */
