#include <glib.h>

#ifndef BD_LIB
#define BD_LIB

#include "plugins.h"
#include "exec.h"

gboolean bd_init (BDPluginSpec **force_plugins, BDUtilsLogFunc log_func, gchar **error_message);
gboolean bd_reinit (BDPluginSpec **force_plugins, gboolean reload, BDUtilsLogFunc log_func, gchar **error_message);

#endif  /* BD_LIB */
