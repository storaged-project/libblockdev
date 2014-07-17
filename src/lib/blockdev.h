#include <glib.h>

#ifndef BD_LIB
#define BD_LIB

#include "plugins.h"

gboolean bd_init (BDPluginSpec **force_plugins);
gboolean bd_reinit (BDPluginSpec **force_plugins, gboolean reload);

#endif  /* BD_LIB */
