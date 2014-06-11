#include <glib.h>

#ifndef BD_LIB
#define BD_LIB

#include "plugins.h"
#include "plugin_apis/lvm.h"

gboolean bd_init (BDPluginSpec *force_plugins);
gboolean bd_reinit (BDPluginSpec *force_plugins, gboolean replace);
gchar** bd_get_available_plugin_names ();
gboolean bd_is_plugin_available (BDPlugin plugin);


#endif  /* BD_LIB */
