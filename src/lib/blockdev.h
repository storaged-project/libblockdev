#include <glib.h>

#ifndef BD_LIB
#define BD_LIB

#include "plugins.h"
#include "plugin_apis/lvm.h"

gboolean bd_init (BDPluginSpec *force_plugins);
gboolean bd_reinit (BDPluginSpec *force_plugins, gboolean replace);
BDPluginName* bd_get_available_plugins();

#endif  /* BD_LIB */
