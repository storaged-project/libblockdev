#include <glib.h>
#include <parted/parted.h>

#ifndef BD_UTILS_PARTED
#define BD_UTILS_PARTED

PedExceptionOption bd_exc_handler (PedException *ex);
gchar * bd_get_error_msg (void);

#endif /* BD_UTILS_PARTED */
