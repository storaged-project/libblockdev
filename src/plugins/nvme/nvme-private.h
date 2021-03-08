#include <glib.h>
#include <glib-object.h>
#include <blockdev/utils.h>

#ifndef BD_NVME_PRIVATE
#define BD_NVME_PRIVATE

void _nvme_status_to_error (gint status, gboolean fabrics, GError **error);

#endif  /* BD_NVME_PRIVATE */
