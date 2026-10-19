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

/* libnvme3 backend, see nvme-backend.h */

#include <glib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>

#include <libnvme.h>

#include <blockdev/utils.h>
#include "nvme.h"
#include "nvme-private.h"
#include "nvme-backend.h"


struct _NVMeDev {
    struct libnvme_global_ctx *ctx;
    struct libnvme_transport_handle *hdl;
};

NVMeDev *_nvme_be_open (const gchar *device, GError **error) {
    NVMeDev *dev;
    gchar *realp;
    int ret;

    /* libnvme_open() only takes the kernel device name, resolve any symlinks first */
    realp = realpath (device, NULL);
    if (!realp) {
        _nvme_status_to_error (-errno, FALSE, error);
        g_prefix_error (error, "Failed to open device '%s': ", device);
        return NULL;
    }

    dev = g_new0 (NVMeDev, 1);
    dev->ctx = libnvme_create_global_ctx ();
    if (!dev->ctx) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                             "Failed to create libnvme context");
        free (realp);
        g_free (dev);
        return NULL;
    }

    ret = libnvme_open (dev->ctx, realp, O_RDONLY, &dev->hdl);
    free (realp);
    if (ret != 0) {
        _nvme_status_to_error (ret, FALSE, error);
        g_prefix_error (error, "Failed to open device '%s': ", device);
        libnvme_free_global_ctx (dev->ctx);
        g_free (dev);
        return NULL;
    }

    return dev;
}

void _nvme_be_close (NVMeDev *dev) {
    libnvme_close (dev->hdl);
    libnvme_free_global_ctx (dev->ctx);
    g_free (dev);
}

int _nvme_be_get_nsid (NVMeDev *dev, __u32 *nsid) {
    return libnvme_get_nsid (dev->hdl, nsid);
}

int _nvme_be_identify_ctrl (NVMeDev *dev, struct nvme_id_ctrl *ctrl_id) {
    struct libnvme_passthru_cmd cmd;

    nvme_init_identify_ctrl (&cmd, ctrl_id);
    return libnvme_exec_admin_passthru (dev->hdl, &cmd);
}

int _nvme_be_identify_ns (NVMeDev *dev, __u32 nsid, struct nvme_id_ns *ns_info) {
    struct libnvme_passthru_cmd cmd;

    nvme_init_identify_ns (&cmd, nsid, ns_info);
    return libnvme_exec_admin_passthru (dev->hdl, &cmd);
}

int _nvme_be_identify_ns_descs (NVMeDev *dev, __u32 nsid, struct nvme_ns_id_desc *descs) {
    struct libnvme_passthru_cmd cmd;

    nvme_init_identify_ns_descs_list (&cmd, nsid, descs);
    return libnvme_exec_admin_passthru (dev->hdl, &cmd);
}

int _nvme_be_identify_independent_ns (NVMeDev *dev, __u32 nsid, struct nvme_id_independent_id_ns *ns_info) {
    struct libnvme_passthru_cmd cmd;

    nvme_init_identify_csi_independent_identify_id_ns (&cmd, nsid, ns_info);
    return libnvme_exec_admin_passthru (dev->hdl, &cmd);
}

int _nvme_be_get_log_smart (NVMeDev *dev, struct nvme_smart_log *smart_log) {
    struct libnvme_passthru_cmd cmd;

    nvme_init_get_log_smart (&cmd, NVME_NSID_ALL, smart_log);
    return libnvme_get_log (dev->hdl, &cmd, FALSE /* rae */, NVME_LOG_PAGE_PDU_SIZE);
}

int _nvme_be_get_log_error (NVMeDev *dev, guint entries, struct nvme_error_log_page *err_log) {
    struct libnvme_passthru_cmd cmd;

    nvme_init_get_log_error (&cmd, entries, err_log);
    return libnvme_get_log (dev->hdl, &cmd, FALSE /* rae */, NVME_LOG_PAGE_PDU_SIZE);
}

int _nvme_be_get_log_self_test (NVMeDev *dev, struct nvme_self_test_log *self_test_log) {
    struct libnvme_passthru_cmd cmd;

    nvme_init_get_log_device_self_test (&cmd, self_test_log);
    return libnvme_get_log (dev->hdl, &cmd, FALSE /* rae */, NVME_LOG_PAGE_PDU_SIZE);
}

int _nvme_be_get_log_sanitize (NVMeDev *dev, struct nvme_sanitize_log_page *sanitize_log) {
    struct libnvme_passthru_cmd cmd;

    nvme_init_get_log_sanitize (&cmd, sanitize_log);
    return libnvme_get_log (dev->hdl, &cmd, FALSE /* rae */, NVME_LOG_PAGE_PDU_SIZE);
}

int _nvme_be_self_test (NVMeDev *dev, __u32 nsid, __u8 stc) {
    struct libnvme_passthru_cmd cmd;

    nvme_init_dev_self_test (&cmd, nsid, stc);
    return libnvme_exec_admin_passthru (dev->hdl, &cmd);
}

int _nvme_be_format (NVMeDev *dev, __u32 nsid, __u8 lbaf, __u8 ses) {
    struct libnvme_passthru_cmd cmd;

    nvme_init_format_nvm (&cmd, nsid, lbaf,
                          NVME_FORMAT_MSET_SEPARATE /* 0 */,
                          NVME_FORMAT_PI_DISABLE /* 0 */,
                          NVME_FORMAT_PIL_LAST /* 0 */,
                          ses);
    return libnvme_exec_admin_passthru (dev->hdl, &cmd);
}

int _nvme_be_sanitize (NVMeDev *dev, __u8 sanact, gboolean ause, __u8 owpass, gboolean oipbp, gboolean nodas, __u32 ovrpat) {
    struct libnvme_passthru_cmd cmd;

    nvme_init_sanitize_nvm (&cmd, sanact, ause, owpass, oipbp, nodas,
                            FALSE /* emvs */,
                            FALSE /* preq */,
                            ovrpat);
    return libnvme_exec_admin_passthru (dev->hdl, &cmd);
}

int _nvme_be_rescan_ns (NVMeDev *dev) {
    return libnvme_rescan_ns (dev->hdl);
}

int _nvme_be_update_block_size (NVMeDev *dev, int block_size) {
    return libnvme_update_block_size (dev->hdl, block_size);
}

int _nvme_be_reread_partitions (NVMeDev *dev) {
    return libnvme_reread_partitions (dev->hdl);
}

const gchar *_nvme_be_status_to_string (int status, gboolean fabrics) {
    return libnvme_status_to_string (status, fabrics);
}

const gchar *_nvme_be_fabrics_errno_to_string (int err) {
    return libnvme_errno_to_string (err);
}

gchar *_nvme_be_uuid_to_string (unsigned char uuid[NVME_UUID_LEN]) {
    gchar uuid_buf[NVME_UUID_LEN_STRING] = ZERO_INIT;

    if (libnvme_uuid_to_string (uuid, uuid_buf) == 0)
        return g_strdup (uuid_buf);
    return NULL;
}

gchar *_nvme_be_read_hostnqn (void) {
    struct libnvme_global_ctx *ctx;
    gchar *hostnqn;

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL)
        return NULL;
    /* FIXME: libnvme SYSCONFDIR might be different from PACKAGE_SYSCONF_DIR */
    hostnqn = libnvmf_read_hostnqn (ctx);
    libnvme_free_global_ctx (ctx);
    return hostnqn;
}

gchar *_nvme_be_read_hostid (void) {
    struct libnvme_global_ctx *ctx;
    gchar *hostid;

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL)
        return NULL;
    hostid = libnvmf_read_hostid (ctx);
    libnvme_free_global_ctx (ctx);
    return hostid;
}

gchar *_nvme_be_generate_hostnqn (void) {
    struct libnvme_global_ctx *ctx;
    gchar *nqn;

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL)
        return NULL;
    nqn = libnvmf_generate_hostnqn (ctx);
    libnvme_free_global_ctx (ctx);
    return nqn;
}

static void _add_sysfs_dir (GPtrArray *ptr_array, const char *dir) {
    if (dir)
        g_ptr_array_add (ptr_array, g_strdup (dir));
}

NVMeTopoSubsys **_nvme_be_scan_topology (void) {
    GPtrArray *subsystems;
    struct libnvme_global_ctx *ctx;
    struct libnvme_host *h;
    struct libnvme_subsystem *s;
    struct libnvme_ctrl *c;
    struct libnvme_ns *n;
    int ret;

    subsystems = g_ptr_array_new ();

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL) {
        bd_utils_log_format (BD_UTILS_LOG_WARNING, "Failed to create libnvme context");
        g_ptr_array_add (subsystems, NULL);
        return (NVMeTopoSubsys **) g_ptr_array_free (subsystems, FALSE);
    }
    ret = libnvme_scan_topology (ctx, NULL, NULL);
    if (ret < 0)
        bd_utils_log_format (BD_UTILS_LOG_WARNING, "Failed to scan topology: %s",
                             strerror_l (-ret, _C_LOCALE));

    libnvme_for_each_host (ctx, h)
        libnvme_for_each_subsystem (h, s) {
            NVMeTopoSubsys *subsys;
            GPtrArray *ctrls;
            GPtrArray *nss;

            subsys = g_new0 (NVMeTopoSubsys, 1);
            subsys->host_nqn = g_strdup (libnvme_host_get_hostnqn (h));
            subsys->host_id = g_strdup (libnvme_host_get_hostid (h));
            subsys->subsysnqn = g_strdup (libnvme_subsystem_get_subsysnqn (s));

            nss = g_ptr_array_new ();
            libnvme_subsystem_for_each_ns (s, n)
                _add_sysfs_dir (nss, libnvme_ns_get_sysfs_dir (n));
            g_ptr_array_add (nss, NULL);
            subsys->ns_sysfs_dirs = (gchar **) g_ptr_array_free (nss, FALSE);

            ctrls = g_ptr_array_new ();
            libnvme_subsystem_for_each_ctrl (s, c) {
                NVMeTopoCtrl *ctrl;

                ctrl = g_new0 (NVMeTopoCtrl, 1);
                ctrl->name = g_strdup (libnvme_ctrl_get_name (c));
                ctrl->sysfs_dir = g_strdup (libnvme_ctrl_get_sysfs_dir (c));
                nss = g_ptr_array_new ();
                libnvme_ctrl_for_each_ns (c, n)
                    _add_sysfs_dir (nss, libnvme_ns_get_sysfs_dir (n));
                g_ptr_array_add (nss, NULL);
                ctrl->ns_sysfs_dirs = (gchar **) g_ptr_array_free (nss, FALSE);
                g_ptr_array_add (ctrls, ctrl);
            }
            g_ptr_array_add (ctrls, NULL);
            subsys->ctrls = (NVMeTopoCtrl **) g_ptr_array_free (ctrls, FALSE);

            g_ptr_array_add (subsystems, subsys);
        }
    libnvme_free_global_ctx (ctx);

    g_ptr_array_add (subsystems, NULL);
    return (NVMeTopoSubsys **) g_ptr_array_free (subsystems, FALSE);
}

static int _apply_params (struct libnvmf_context *fctx, const NVMeConnectParams *params) {
#define APPLY_OPT(opt) { \
        if (params->opt.set) \
            libnvmf_context_set_##opt (fctx, params->opt.val); \
    }
    APPLY_OPT (nr_io_queues);
    APPLY_OPT (nr_write_queues);
    APPLY_OPT (nr_poll_queues);
    APPLY_OPT (queue_size);
    APPLY_OPT (keep_alive_tmo);
    APPLY_OPT (reconnect_delay);
    APPLY_OPT (ctrl_loss_tmo);
    APPLY_OPT (fast_io_fail_tmo);
    APPLY_OPT (tos);
    APPLY_OPT (duplicate_connect);
    APPLY_OPT (disable_sqflow);
    APPLY_OPT (hdr_digest);
    APPLY_OPT (data_digest);
    APPLY_OPT (tls);
#undef APPLY_OPT

    if (params->hostkey || params->ctrlkey || params->keyring || params->tls_key)
        return libnvmf_context_set_crypto (fctx, params->hostkey, params->ctrlkey,
                                           params->keyring, params->tls_key, NULL);
    return 0;
}

gboolean _nvme_be_connect (const NVMeConnectParams *params, GError **error) {
    int ret;
    gchar *host_nqn_val = NULL;
    gchar *host_id_val = NULL;
    struct libnvme_global_ctx *ctx;
    struct libnvme_host *host;
    struct libnvmf_context *fctx = NULL;
    gboolean success = FALSE;

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                             "Failed to create libnvme context");
        return FALSE;
    }

    /* HostNQN and HostID, read from /etc/nvme/ or generated when not specified */
    ret = libnvmf_host_get_ids (ctx, params->host_nqn, params->host_id, &host_nqn_val, &host_id_val);
    if (ret != 0 || host_nqn_val == NULL || host_id_val == NULL) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                     "Could not determine HostNQN or HostID: %s",
                     ret != 0 ? libnvme_strerror (-ret) : "unknown error");
        goto out;
    }

    /* existing controllers need to be known to detect duplicate connections */
    libnvme_skip_namespaces (ctx);
    ret = libnvme_scan_topology (ctx, NULL, NULL);
    if (ret < 0) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Failed to scan topology: %s",
                     strerror_l (-ret, _C_LOCALE));
        goto out;
    }

    ret = libnvmf_context_create (ctx, NULL, NULL, NULL, NULL, &fctx);
    if (ret == 0)
        ret = libnvmf_context_set_connection (fctx, params->subsysnqn, params->transport,
                                              params->transport_addr, params->transport_svcid,
                                              params->host_traddr, params->host_iface);
    if (ret == 0)
        ret = libnvmf_context_set_hostnqn (fctx, host_nqn_val, host_id_val);
    if (ret == 0)
        ret = _apply_params (fctx, params);
    if (ret != 0) {
        _nvme_fabrics_errno_to_gerror (ret, error);
        g_prefix_error (error, "Error creating the controller: ");
        goto out;
    }

    if (params->hostsymname) {
        ret = libnvme_get_host (ctx, host_nqn_val, host_id_val, &host);
        if (ret != 0) {
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                         "Unable to lookup host for HostNQN '%s' and HostID '%s': %s",
                         host_nqn_val, host_id_val, libnvme_strerror (-ret));
            goto out;
        }
        libnvme_host_set_hostsymname (host, params->hostsymname);
    }

    ret = libnvmf_connect (ctx, fctx);
    if (ret != 0) {
        _nvme_fabrics_errno_to_gerror (ret, error);
        g_prefix_error (error, "Error connecting the controller: ");
        goto out;
    }

    success = TRUE;

out:
    libnvmf_context_free (fctx);
    libnvme_free_global_ctx (ctx);
    free (host_nqn_val);
    free (host_id_val);

    return success;
}

gboolean _nvme_be_disconnect (const gchar *subsysnqn, const gchar *name, gboolean *found, GError **error) {
    struct libnvme_global_ctx *ctx;
    struct libnvme_host *host;
    struct libnvme_subsystem *subsys;
    struct libnvme_ctrl *ctrl;
    int ret;

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                             "Failed to create libnvme context");
        return FALSE;
    }
    ret = libnvme_scan_topology (ctx, NULL, NULL);
    if (ret < 0) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Failed to scan topology: %s",
                     strerror_l (-ret, _C_LOCALE));
        libnvme_free_global_ctx (ctx);
        return FALSE;
    }
    libnvme_for_each_host (ctx, host)
        libnvme_for_each_subsystem (host, subsys)
            if (!subsysnqn || g_strcmp0 (libnvme_subsystem_get_subsysnqn (subsys), subsysnqn) == 0)
                libnvme_subsystem_for_each_ctrl (subsys, ctrl)
                    if (!name || g_strcmp0 (libnvme_ctrl_get_name (ctrl), name) == 0) {
                        ret = libnvmf_disconnect_ctrl (ctrl);
                        if (ret != 0) {
                            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                                         "Error disconnecting the controller: %s",
                                         strerror_l (-ret, _C_LOCALE));
                            libnvme_free_global_ctx (ctx);
                            return FALSE;
                        }
                        *found = TRUE;
                    }
    libnvme_free_global_ctx (ctx);
    return TRUE;
}
