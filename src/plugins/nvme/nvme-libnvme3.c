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

struct config_match_data {
    const NVMeConnectParams *params;
    const struct libnvmf_config_conn *conn;
};

static void config_match_conn (const struct libnvmf_config_conn *conn, void *user_data) {
    struct config_match_data *data = user_data;
    const NVMeConnectParams *params = data->params;
    const gchar *val;

    /* first match wins */
    if (data->conn)
        return;

    if (g_strcmp0 (libnvmf_config_conn_get_subsysnqn (conn), params->subsysnqn) != 0 ||
        g_strcmp0 (libnvmf_config_conn_get_transport (conn), params->transport) != 0 ||
        g_strcmp0 (libnvmf_config_conn_get_traddr (conn), params->transport_addr) != 0 ||
        g_strcmp0 (libnvmf_config_conn_get_host_traddr (conn), params->host_traddr) != 0 ||
        g_strcmp0 (libnvmf_config_conn_get_host_iface (conn), params->host_iface) != 0)
        return;

    /* unset values in the config file match anything */
    val = libnvmf_config_conn_get_trsvcid (conn);
    if (val && params->transport_svcid && g_strcmp0 (val, params->transport_svcid) != 0)
        return;
    val = libnvmf_config_conn_get_hostnqn (conn);
    if (val && params->host_nqn && g_strcmp0 (val, params->host_nqn) != 0)
        return;

    data->conn = conn;
}

static const gchar *get_param_nonempty (const struct libnvmf_config_conn *conn, const gchar *key) {
    const gchar *val;

    if (!conn)
        return NULL;
    val = libnvmf_params_get (libnvmf_config_conn_get_params (conn), key);
    if (val && *val == '\0')
        return NULL;
    return val;
}

/* applies the extra arguments on top of the configuration file entry @conn */
static int _apply_params (struct libnvmf_context *fctx, const NVMeConnectParams *params, const struct libnvmf_config_conn *conn) {
    int ret;

    if (conn) {
        ret = libnvmf_context_apply_params (fctx, libnvmf_config_conn_get_params (conn));
        if (ret != 0)
            return ret;
    }

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
        return libnvmf_context_set_crypto (fctx,
                                           params->hostkey ? params->hostkey : get_param_nonempty (conn, "kxchap-secret"),
                                           params->ctrlkey ? params->ctrlkey : get_param_nonempty (conn, "kxchap-ctrl-secret"),
                                           params->keyring ? params->keyring : get_param_nonempty (conn, "keyring"),
                                           params->tls_key /* NULL keeps the current key */,
                                           get_param_nonempty (conn, "tls-key-identity"));
    return 0;
}

/* Warns about a legacy JSON configuration that nvme-cli would convert to the INI format
 * on its first fabrics command. libnvme3 doesn't read it and libblockdev doesn't convert it,
 * its options and keys would be silently ignored otherwise. */
static void _warn_legacy_config (void) {
    /* FIXME: libnvme SYSCONFDIR might be different from PACKAGE_SYSCONF_DIR */
    gchar *ini = g_build_filename (PACKAGE_SYSCONF_DIR, "nvme", "nvme-fabrics.conf", NULL);
    gchar *json = g_build_filename (PACKAGE_SYSCONF_DIR, "nvme", "config.json", NULL);
    gchar *marker = g_strconcat (json, ".converted", NULL);

    if (!g_file_test (ini, G_FILE_TEST_EXISTS) &&
        g_file_test (json, G_FILE_TEST_EXISTS) &&
        !g_file_test (marker, G_FILE_TEST_EXISTS))
        bd_utils_log_format (BD_UTILS_LOG_WARNING,
                             "The legacy NVMe-oF configuration file %s is not supported by libnvme3 and is ignored, "
                             "run 'nvme config-convert' to convert it to %s", json, ini);

    g_free (ini);
    g_free (json);
    g_free (marker);
}

gboolean _nvme_be_connect (const NVMeConnectParams *params, GError **error) {
    int ret;
    const gchar *host_nqn;
    const gchar *host_id;
    const gchar *hostsymname;
    gchar *host_nqn_val = NULL;
    gchar *host_id_val = NULL;
    struct libnvme_global_ctx *ctx;
    struct libnvme_host *host;
    struct libnvmf_context *fctx = NULL;
    struct libnvmf_config *config = NULL;
    struct config_match_data match = ZERO_INIT;
    gboolean success = FALSE;

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                             "Failed to create libnvme context");
        return FALSE;
    }

    /* look up the connection in the configuration file */
    if (params->use_config) {
        /* libnvme treats a missing file as an empty configuration, which is only fine for the default */
        if (params->config_file && !g_file_test (params->config_file, G_FILE_TEST_EXISTS)) {
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                         "Failed to read configuration file '%s': %s",
                         params->config_file, strerror_l (ENOENT, _C_LOCALE));
            goto out;
        }
        if (!params->config_file)
            _warn_legacy_config ();
        /* NULL means the libnvme default configuration file */
        ret = libnvmf_config_read (ctx, params->config_file, &config);
        if (ret != 0) {
            if (params->config_file) {
                g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                             "Failed to read configuration file '%s': %s%s",
                             params->config_file, libnvme_strerror (-ret),
                             g_str_has_suffix (params->config_file, ".json") ?
                                 " (legacy JSON configuration files are not supported with libnvme3, "
                                 "convert it with 'nvme config-convert')" : "");
                goto out;
            }
            /* not fatal for the default configuration file */
            bd_utils_log_format (BD_UTILS_LOG_WARNING,
                                 "Failed to read the default NVMe-oF configuration file: %s",
                                 libnvme_strerror (-ret));
            config = NULL;
        }
    }
    host_nqn = params->host_nqn;
    host_id = params->host_id;
    hostsymname = params->hostsymname;
    if (config) {
        match.params = params;
        libnvmf_config_conn_for_each (config, config_match_conn, &match);
        if (match.conn) {
            if (!host_nqn)
                host_nqn = libnvmf_config_conn_get_hostnqn (match.conn);
            if (!host_id)
                host_id = libnvmf_config_conn_get_hostid (match.conn);
            if (!hostsymname)
                hostsymname = libnvmf_config_conn_get_hostsymname (match.conn);
        }
    }

    /* HostNQN and HostID, read from /etc/nvme/ or generated when not specified */
    ret = libnvmf_host_get_ids (ctx, host_nqn, host_id, &host_nqn_val, &host_id_val);
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
        ret = _apply_params (fctx, params, match.conn);
    if (ret != 0) {
        _nvme_fabrics_errno_to_gerror (ret, error);
        g_prefix_error (error, "Error creating the controller: ");
        goto out;
    }

    if (hostsymname) {
        ret = libnvme_get_host (ctx, host_nqn_val, host_id_val, &host);
        if (ret != 0) {
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                         "Unable to lookup host for HostNQN '%s' and HostID '%s': %s",
                         host_nqn_val, host_id_val, libnvme_strerror (-ret));
            goto out;
        }
        libnvme_host_set_hostsymname (host, hostsymname);
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
    libnvmf_config_free (config);
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
