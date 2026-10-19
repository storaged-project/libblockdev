/*
 * Copyright (C) 2026 SUSE LLC
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Daniel Wagner <daniel.wagner@suse.com>
 */

/*
 * Internal API between the NVMe plugin and the libnvme library backend.
 *
 * The plugin is built against exactly one libnvme flavour. The generic code
 * (nvme-info.c, nvme-op.c, nvme-fabrics.c, nvme-error.c) only uses the NVMe
 * specification types, constants and static inline helpers provided by the
 * libnvme headers. Every call into the library itself goes through the
 * functions declared here, implemented by nvme-libnvme1.c.
 *
 * Unless stated otherwise, functions returning int return 0 on success,
 * a negative errno value on failure or a positive NVMe status value.
 */

#include <glib.h>
#include <blockdev/utils.h>

#include "nvme-libnvme1.h"

#ifndef BD_NVME_BACKEND
#define BD_NVME_BACKEND

/* opaque handle of an open NVMe controller or namespace device */
typedef struct _NVMeDev NVMeDev;

/* device access */
G_GNUC_INTERNAL
NVMeDev *_nvme_be_open (const gchar *device, GError **error);
G_GNUC_INTERNAL
void _nvme_be_close (NVMeDev *dev);
G_GNUC_INTERNAL
int _nvme_be_get_nsid (NVMeDev *dev, __u32 *nsid);

/* admin commands */
G_GNUC_INTERNAL
int _nvme_be_identify_ctrl (NVMeDev *dev, struct nvme_id_ctrl *ctrl_id);
G_GNUC_INTERNAL
int _nvme_be_identify_ns (NVMeDev *dev, __u32 nsid, struct nvme_id_ns *ns_info);
G_GNUC_INTERNAL
int _nvme_be_identify_ns_descs (NVMeDev *dev, __u32 nsid, struct nvme_ns_id_desc *descs);
G_GNUC_INTERNAL
int _nvme_be_identify_independent_ns (NVMeDev *dev, __u32 nsid, struct nvme_id_independent_id_ns *ns_info);
G_GNUC_INTERNAL
int _nvme_be_get_log_smart (NVMeDev *dev, struct nvme_smart_log *smart_log);
G_GNUC_INTERNAL
int _nvme_be_get_log_error (NVMeDev *dev, guint entries, struct nvme_error_log_page *err_log);
G_GNUC_INTERNAL
int _nvme_be_get_log_self_test (NVMeDev *dev, struct nvme_self_test_log *self_test_log);
G_GNUC_INTERNAL
int _nvme_be_get_log_sanitize (NVMeDev *dev, struct nvme_sanitize_log_page *sanitize_log);
G_GNUC_INTERNAL
int _nvme_be_self_test (NVMeDev *dev, __u32 nsid, __u8 stc);
G_GNUC_INTERNAL
int _nvme_be_format (NVMeDev *dev, __u32 nsid, __u8 lbaf, __u8 ses);
G_GNUC_INTERNAL
int _nvme_be_sanitize (NVMeDev *dev, __u8 sanact, gboolean ause, __u8 owpass, gboolean oipbp, gboolean nodas, __u32 ovrpat);

/* kernel notifications after a format */
G_GNUC_INTERNAL
int _nvme_be_rescan_ns (NVMeDev *dev);
G_GNUC_INTERNAL
int _nvme_be_update_block_size (NVMeDev *dev, int block_size);
G_GNUC_INTERNAL
int _nvme_be_reread_partitions (NVMeDev *dev);

/* strings */
G_GNUC_INTERNAL
const gchar *_nvme_be_status_to_string (int status, gboolean fabrics);
G_GNUC_INTERNAL
const gchar *_nvme_be_fabrics_errno_to_string (int err);
G_GNUC_INTERNAL
gchar *_nvme_be_uuid_to_string (unsigned char uuid[NVME_UUID_LEN]);

/* host identity, all return newly allocated strings or %NULL when not available */
G_GNUC_INTERNAL
gchar *_nvme_be_read_hostnqn (void);
G_GNUC_INTERNAL
gchar *_nvme_be_read_hostid (void);
G_GNUC_INTERNAL
gchar *_nvme_be_generate_hostnqn (void);

/* topology snapshot */
typedef struct {
    gchar *name;            /* kernel name, e.g. "nvme0" */
    gchar *sysfs_dir;
    gchar **ns_sysfs_dirs;  /* private namespaces of the controller */
} NVMeTopoCtrl;

typedef struct {
    gchar *host_nqn;
    gchar *host_id;
    gchar *subsysnqn;
    gchar **ns_sysfs_dirs;  /* shared (multipath) namespaces of the subsystem */
    NVMeTopoCtrl **ctrls;   /* %NULL-terminated */
} NVMeTopoSubsys;

/* Returns a %NULL-terminated array of subsystems, as seen by all hosts.
 * A topology scan failure is logged and results in an incomplete array. */
G_GNUC_INTERNAL
NVMeTopoSubsys **_nvme_be_scan_topology (void);
G_GNUC_INTERNAL
void _nvme_topo_free (NVMeTopoSubsys **topo);

/* fabrics */
typedef struct {
    gboolean set;
    gint64 val;
} NVMeOptInt;

typedef struct {
    gboolean set;
    gboolean val;
} NVMeOptBool;

typedef struct {
    const gchar *subsysnqn;
    const gchar *transport;
    const gchar *transport_addr;
    const gchar *transport_svcid;
    const gchar *host_traddr;
    const gchar *host_iface;
    const gchar *host_nqn;
    const gchar *host_id;

    /* the extra arguments, %NULL when not specified */
    gboolean use_config;
    const gchar *config_file;  /* %NULL for the libnvme default */
    const gchar *hostkey;
    const gchar *ctrlkey;
    const gchar *hostsymname;
    const gchar *keyring;
    const gchar *tls_key;
    NVMeOptInt nr_io_queues;
    NVMeOptInt nr_write_queues;
    NVMeOptInt nr_poll_queues;
    NVMeOptInt queue_size;
    NVMeOptInt keep_alive_tmo;
    NVMeOptInt reconnect_delay;
    NVMeOptInt ctrl_loss_tmo;
    NVMeOptInt fast_io_fail_tmo;
    NVMeOptInt tos;
    NVMeOptBool duplicate_connect;
    NVMeOptBool disable_sqflow;
    NVMeOptBool hdr_digest;
    NVMeOptBool data_digest;
    NVMeOptBool tls;
} NVMeConnectParams;

G_GNUC_INTERNAL
gboolean _nvme_be_connect (const NVMeConnectParams *params, GError **error);

/* Disconnects all controllers matching @subsysnqn and/or the kernel controller @name.
 * Sets @found when at least one matching controller has been disconnected. */
G_GNUC_INTERNAL
gboolean _nvme_be_disconnect (const gchar *subsysnqn, const gchar *name, gboolean *found, GError **error);

#endif  /* BD_NVME_BACKEND */
