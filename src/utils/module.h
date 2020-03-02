#include <glib.h>

#ifndef BD_UTILS_MODULE
#define BD_UTILS_MODULE

GQuark bd_utils_module_error_quark (void);
#define BD_UTILS_MODULE_ERROR bd_utils_module_error_quark ()
typedef enum {
    BD_UTILS_MODULE_ERROR_KMOD_INIT_FAIL,
    BD_UTILS_MODULE_ERROR_FAIL,
    BD_UTILS_MODULE_ERROR_NOEXIST,
    BD_UTILS_MODULE_ERROR_MODULE_CHECK_ERROR,
    BD_UTILS_MODULE_ERROR_INVALID_PLATFORM,
} BDUtilsModuleError;

typedef struct {
    guint major;
    guint minor;
    guint micro;
} BDUtilsLinuxVersion;

gboolean bd_utils_have_kernel_module (const gchar *module_name, GError **error);
gboolean bd_utils_load_kernel_module (const gchar *module_name, const gchar *options, GError **error);
gboolean bd_utils_unload_kernel_module (const gchar *module_name, GError **error);

BDUtilsLinuxVersion * bd_utils_get_linux_version (GError **error);
gint bd_utils_check_linux_version (guint major, guint minor, guint micro);

#endif  /* BD_UTILS_MODULE */
