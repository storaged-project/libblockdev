#include <glib.h>
#include <glib-object.h>
#include <blockdev/utils.h>
#include <libnvme.h>

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
void _nvme_fabrics_errno_to_gerror (int result, GError **error);

/* nvme-info.c */
G_GNUC_INTERNAL
struct libnvme_transport_handle *_open_dev (const gchar *device, struct libnvme_global_ctx **ctx, GError **error);
G_GNUC_INTERNAL
void _close_dev (struct libnvme_global_ctx *ctx, struct libnvme_transport_handle *hdl);
G_GNUC_INTERNAL
int _nvme_identify_ctrl (struct libnvme_transport_handle *hdl, struct nvme_id_ctrl *ctrl_id);
G_GNUC_INTERNAL
int _nvme_identify_ns (struct libnvme_transport_handle *hdl, __u32 nsid, struct nvme_id_ns *ns_info);
G_GNUC_INTERNAL
void *_nvme_alloc (size_t len, GError **error);

#endif  /* BD_NVME_PRIVATE */
