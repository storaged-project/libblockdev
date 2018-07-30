#include <blockdev/utils.h>
#include <glib.h>

#ifndef BD_LIB
#define BD_LIB

#include "plugins.h"

/**
 * bd_init_error_quark: (skip)
 */
GQuark bd_init_error_quark (void);
#define BD_INIT_ERROR bd_init_error_quark ()
typedef enum {
    BD_INIT_ERROR_PLUGINS_FAILED,
    BD_INIT_ERROR_NOT_IMPLEMENTED,
    BD_INIT_ERROR_FAILED,
} BDInitError;

gboolean bd_init (BDPluginSpec **require_plugins, BDUtilsLogFunc log_func, GError **error);
gboolean bd_ensure_init (BDPluginSpec **require_plugins, BDUtilsLogFunc log_func, GError **error);
gboolean bd_reinit (BDPluginSpec **require_plugins, gboolean reload, BDUtilsLogFunc log_func, GError **error);
gboolean bd_try_init(BDPluginSpec **request_plugins, BDUtilsLogFunc log_func,
                     gchar ***loaded_plugin_names, GError **error);
gboolean bd_try_reinit (BDPluginSpec **require_plugins, gboolean reload, BDUtilsLogFunc log_func,
                        gchar ***loaded_plugin_names, GError **error);
gboolean bd_is_initialized (void);
gboolean bd_switch_init_checks (gboolean enable, GError **error);

#endif  /* BD_LIB */
