#include <glib.h>

#ifndef BD_UTILS_MODULE
#define BD_UTILS_MODULE

GQuark bd_utils_module_error_quark (void);
#define BD_UTILS_MODULE_ERROR bd_utils_module_error_quark ()
typedef enum {
    BD_UTILS_MODULE_ERROR_KMOD_INIT_FAIL,
    BD_UTILS_MODULE_ERROR_FAIL,
    BD_UTILS_MODULE_ERROR_NOEXIST,
} BDUtilsModuleError;

gboolean bd_utils_have_kernel_module (const gchar *module_name, GError **error);
gboolean bd_utils_load_kernel_module (const gchar *module_name, const gchar *options, GError **error);
gboolean bd_utils_unload_kernel_module (const gchar *module_name, GError **error);


#endif  /* BD_UTILS_MODULE */
