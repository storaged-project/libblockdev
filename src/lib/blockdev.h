#include <utils.h>
#include <glib.h>

#ifndef BD_LIB
#define BD_LIB

#include "plugins.h"

#define BD_INIT_ERROR bd_init_error_quark ()
typedef enum {
    BD_INIT_ERROR_PLUGINS_FAILED,
} BDInitError;

gboolean bd_init (BDPluginSpec **force_plugins, BDUtilsLogFunc log_func, GError **error);
gboolean bd_reinit (BDPluginSpec **force_plugins, gboolean reload, BDUtilsLogFunc log_func, GError **error);

#endif  /* BD_LIB */
