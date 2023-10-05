#include <glib.h>
#include <glib-object.h>
#include <blockdev/utils.h>

#ifndef BD_NVME_PRIVATE
#define BD_NVME_PRIVATE

/* TODO: move to a common libblockdev header */
#ifdef __clang__
#define ZERO_INIT {}
#else
#define ZERO_INIT {0}
#endif

/* "C" locale to get the locale-agnostic error messages */
#define _C_LOCALE (locale_t) 0

/* nvme-error.c */
G_GNUC_INTERNAL
void _nvme_status_to_error (gint status, gboolean fabrics, GError **error);
G_GNUC_INTERNAL
void _nvme_fabrics_errno_to_gerror (int result, int _errno, GError **error);

/* nvme-info.c */
G_GNUC_INTERNAL
gint _open_dev (const gchar *device, GError **error);
G_GNUC_INTERNAL
void *_nvme_alloc (size_t len);

#endif  /* BD_NVME_PRIVATE */
