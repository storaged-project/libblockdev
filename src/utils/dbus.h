#include <glib.h>
#include <gio/gio.h>

#ifndef BD_UTILS_DBUS
#define BD_UTILS_DBUS

GQuark bd_utils_dbus_error_quark (void);
#define BD_UTILS_DBUS_ERROR bd_utils_dbus_error_quark ()
typedef enum {
    BD_UTILS_DBUS_ERROR_FAIL,
    BD_UTILS_DBUS_ERROR_NOEXIST,
} BDUtilsDBusError;

gboolean bd_utils_dbus_service_available (GDBusConnection *connection, GBusType bus_type, const gchar *bus_name, const gchar *obj_prefix, GError **error);

#endif  /* BD_UTILS_DBUS */
