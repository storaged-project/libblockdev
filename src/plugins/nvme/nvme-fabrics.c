/*
 * Copyright (C) 2014-2021 Red Hat, Inc.
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
 * Author: Tomas Bzatek <tbzatek@redhat.com>
 */

#include <glib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <linux/fs.h>
#include <glib/gstdio.h>

#include <libnvme.h>

#include <blockdev/utils.h>
#include <check_deps.h>
#include "nvme.h"
#include "nvme-private.h"


static const gchar *get_extra_config_file (const BDExtraArg **extra, gboolean *use_config) {
    const BDExtraArg **extra_i;
    const gchar *config_file = NULL;

    /* NULL means the libnvme default configuration file */
    *use_config = TRUE;
    if (!extra)
        return NULL;

    for (extra_i = extra; *extra_i; extra_i++)
        if (g_strcmp0 ((*extra_i)->opt, "config") == 0) {
            if (g_ascii_strcasecmp ((*extra_i)->val, "none") == 0) {
                *use_config = FALSE;
                config_file = NULL;
            } else {
                *use_config = TRUE;
                config_file = (*extra_i)->val;
            }
        }

    return config_file;
}

static void parse_extra_args (const BDExtraArg **extra, struct libnvmf_context *fctx, const gchar **hostkey, const gchar **ctrlkey, const gchar **hostsymname, const gchar **keyring, const gchar **tls_key) {
    const BDExtraArg **extra_i;

    if (!extra)
        return;

#define SAFE_INT_CONV(setter) { \
        gint64 v; \
        gchar *endptr = NULL; \
        \
        v = g_ascii_strtoll ((*extra_i)->val, &endptr, 0); \
        if (endptr != (*extra_i)->val) \
            setter (fctx, v); \
    }
#define SAFE_BOOL_CONV(setter) { \
        if (g_ascii_strcasecmp ((*extra_i)->val, "on") == 0 || \
            g_ascii_strcasecmp ((*extra_i)->val, "1") == 0 || \
            g_ascii_strcasecmp ((*extra_i)->val, "true") == 0) \
            setter (fctx, TRUE); \
        else \
        if (g_ascii_strcasecmp ((*extra_i)->val, "off") == 0 || \
            g_ascii_strcasecmp ((*extra_i)->val, "0") == 0 || \
            g_ascii_strcasecmp ((*extra_i)->val, "false") == 0) \
            setter (fctx, FALSE); \
    }

    for (extra_i = extra; *extra_i; extra_i++) {
        if ((g_strcmp0 ((*extra_i)->opt, "kxchap_secret") == 0 ||
             g_strcmp0 ((*extra_i)->opt, "dhchap_key") == 0 /* deprecated */) && hostkey)
            *hostkey = (*extra_i)->val;
        else
        if ((g_strcmp0 ((*extra_i)->opt, "kxchap_ctrl_secret") == 0 ||
             g_strcmp0 ((*extra_i)->opt, "dhchap_ctrl_key") == 0 /* deprecated */) && ctrlkey)
            *ctrlkey = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "hostsymname") == 0 && hostsymname)
            *hostsymname = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "keyring") == 0 && keyring)
            *keyring = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "tls_key") == 0 && tls_key)
            *tls_key = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "nr_io_queues") == 0)
            SAFE_INT_CONV (libnvmf_context_set_nr_io_queues)
        else
        if (g_strcmp0 ((*extra_i)->opt, "nr_write_queues") == 0)
            SAFE_INT_CONV (libnvmf_context_set_nr_write_queues)
        else
        if (g_strcmp0 ((*extra_i)->opt, "nr_poll_queues") == 0)
            SAFE_INT_CONV (libnvmf_context_set_nr_poll_queues)
        else
        if (g_strcmp0 ((*extra_i)->opt, "queue_size") == 0)
            SAFE_INT_CONV (libnvmf_context_set_queue_size)
        else
        if (g_strcmp0 ((*extra_i)->opt, "keep_alive_tmo") == 0)
            SAFE_INT_CONV (libnvmf_context_set_keep_alive_tmo)
        else
        if (g_strcmp0 ((*extra_i)->opt, "reconnect_delay") == 0)
            SAFE_INT_CONV (libnvmf_context_set_reconnect_delay)
        else
        if (g_strcmp0 ((*extra_i)->opt, "ctrl_loss_tmo") == 0)
            SAFE_INT_CONV (libnvmf_context_set_ctrl_loss_tmo)
        else
        if (g_strcmp0 ((*extra_i)->opt, "fast_io_fail_tmo") == 0)
            SAFE_INT_CONV (libnvmf_context_set_fast_io_fail_tmo)
        else
        if (g_strcmp0 ((*extra_i)->opt, "tos") == 0)
            SAFE_INT_CONV (libnvmf_context_set_tos)
        else
        if (g_strcmp0 ((*extra_i)->opt, "duplicate_connect") == 0)
            SAFE_BOOL_CONV (libnvmf_context_set_duplicate_connect)
        else
        if (g_strcmp0 ((*extra_i)->opt, "disable_sqflow") == 0)
            SAFE_BOOL_CONV (libnvmf_context_set_disable_sqflow)
        else
        if (g_strcmp0 ((*extra_i)->opt, "hdr_digest") == 0)
            SAFE_BOOL_CONV (libnvmf_context_set_hdr_digest)
        else
        if (g_strcmp0 ((*extra_i)->opt, "data_digest") == 0)
            SAFE_BOOL_CONV (libnvmf_context_set_data_digest)
        else
        if (g_strcmp0 ((*extra_i)->opt, "tls") == 0)
            SAFE_BOOL_CONV (libnvmf_context_set_tls)
    }

#undef SAFE_INT_CONV
#undef SAFE_BOOL_CONV
}

struct config_match_data {
    const gchar *subsysnqn;
    const gchar *transport;
    const gchar *transport_addr;
    const gchar *transport_svcid;
    const gchar *host_traddr;
    const gchar *host_iface;
    const gchar *host_nqn;
    const struct libnvmf_config_conn *conn;
};

static void config_match_conn (const struct libnvmf_config_conn *conn, void *user_data) {
    struct config_match_data *data = user_data;
    const gchar *val;

    /* first match wins */
    if (data->conn)
        return;

    if (g_strcmp0 (libnvmf_config_conn_get_subsysnqn (conn), data->subsysnqn) != 0 ||
        g_strcmp0 (libnvmf_config_conn_get_transport (conn), data->transport) != 0 ||
        g_strcmp0 (libnvmf_config_conn_get_traddr (conn), data->transport_addr) != 0 ||
        g_strcmp0 (libnvmf_config_conn_get_host_traddr (conn), data->host_traddr) != 0 ||
        g_strcmp0 (libnvmf_config_conn_get_host_iface (conn), data->host_iface) != 0)
        return;

    /* unset values in the config file match anything */
    val = libnvmf_config_conn_get_trsvcid (conn);
    if (val && data->transport_svcid && g_strcmp0 (val, data->transport_svcid) != 0)
        return;
    val = libnvmf_config_conn_get_hostnqn (conn);
    if (val && data->host_nqn && g_strcmp0 (val, data->host_nqn) != 0)
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


/**
 * bd_nvme_connect:
 * @subsysnqn: The name for the NVMe subsystem to connect to.
 * @transport: The network fabric used for a NVMe-over-Fabrics network.
 * @transport_addr: (nullable): The network address of the Controller. For transports using IP addressing (e.g. `rdma`) this should be an IP-based address.
 * @transport_svcid: (nullable): The transport service ID. For transports using IP addressing (e.g. `tcp`, `rdma`) this field is the port number. The default port number for the `tcp` and `rdma` transports is `4420` and `8009` respectively when the well-known Discovery NQN is specified.
 * @host_traddr: (nullable): The network address used on the host to connect to the Controller. For TCP, this sets the source address on the socket.
 * @host_iface: (nullable): The network interface used on the host to connect to the Controller (e.g. IP `eth1`, `enp2s0`). This forces the connection to be made on a specific interface instead of letting the system decide.
 * @host_nqn: (nullable): Overrides the default Host NQN that identifies the NVMe Host. If this option is %NULL, the default is read from `/etc/nvme/hostnqn` first.
 *                        If that does not exist, the autogenerated NQN value from the NVMe Host kernel module is used next. The Host NQN uniquely identifies the NVMe Host.
 * @host_id: (nullable): User-defined host UUID or %NULL to use default (as defined in `/etc/nvme/hostid`).
 * @extra: (nullable) (array zero-terminated=1): Additional arguments.
 * @error: (out) (nullable): Place to store error (if any).
 *
 * Creates a transport connection to a remote system (specified by @transport_addr and @transport_svcid)
 * and creates a NVMe over Fabrics controller for the NVMe subsystem specified by the @subsysnqn option.
 *
 * Valid values for @transport include:
 * - `"rdma"`: An rdma network (RoCE, iWARP, Infiniband, basic rdma, etc.)
 * - `"fc"`: A Fibre Channel network.
 * - `"tcp"`: A TCP/IP network.
 * - `"loop"`: A NVMe over Fabrics target on the local host.
 *
 * In addition to the primary options it's possible to supply @extra arguments:
 * - `"config"`: Use the specified configuration file instead of the default file (see below) or
 *               specify `"none"` to avoid reading any configuration file.
 * - `"kxchap_secret"`: NVMe In-band authentication (KX-HMAC-CHAP, formerly DH-HMAC-CHAP) host secret
 *                      in ASCII format as described in the NVMe specification. When not specified,
 *                      the secret is taken from the configuration file, if present. Otherwise
 *                      no in-band authentication is attempted.
 * - `"kxchap_ctrl_secret"`: NVMe In-band authentication controller secret for bi-directional authentication.
 *                           When not specified, the secret is taken from the configuration file, if present.
 *                           Otherwise no bi-directional authentication is attempted.
 * - `"dhchap_key"`, `"dhchap_ctrl_key"`: Deprecated aliases for `"kxchap_secret"` and `"kxchap_ctrl_secret"`.
 * - `"nr_io_queues"`: The number of I/O queues.
 * - `"nr_write_queues"`: Number of additional queues that will be used for write I/O.
 * - `"nr_poll_queues"`: Number of additional queues that will be used for polling latency sensitive I/O.
 * - `"queue_size"`: Number of elements in the I/O queues.
 * - `"keep_alive_tmo"`: The keep alive timeout (in seconds).
 * - `"reconnect_delay"`: The delay (in seconds) before reconnect is attempted after a connect loss.
 * - `"ctrl_loss_tmo"`: The controller loss timeout period (in seconds). A special value of `-1` will cause reconnecting forever.
 * - `"fast_io_fail_tmo"`: Fast I/O Fail timeout (in seconds).
 * - `"tos"`: Type of service.
 * - `"duplicate_connect"`: Allow duplicated connections between same transport host and subsystem port. Boolean value.
 * - `"disable_sqflow"`: Disables SQ flow control to omit head doorbell update for submission queues when sending nvme completions. Boolean value.
 * - `"hdr_digest"`: Generates/verifies header digest (TCP). Boolean value.
 * - `"data_digest"`: Generates/verifies data digest (TCP). Boolean value.
 * - `"tls"`: Enable TLS encryption (TCP). Boolean value.
 * - `"hostsymname"`: TP8010: NVMe host symbolic name.
 * - `"keyring"`: Keyring to store and lookup keys. String value.
 * - `"tls_key"`: TLS PSK for the connection. String value.
 *
 * Boolean values can be expressed by "0"/"1", "on"/"off" or "True"/"False" case-insensitive
 * strings. Failed numerical or boolean string conversions will result in the option being ignored.
 *
 * By default additional options are read from the default configuration file `/etc/nvme/nvme-fabrics.conf`
 * (and its `/etc/nvme/nvme-fabrics.conf.d/` drop-in directory). Parameters of the first connection
 * entry matching the @subsysnqn, @transport, @transport_addr, @transport_svcid, @host_traddr
 * and @host_iface arguments are applied, along with its host identity when @host_nqn or @host_id
 * are not specified. Use the @extra `"config"` argument to either specify a different config file
 * or disable use of it. The INI configuration file format is documented
 * in [https://github.com/linux-nvme/nvme-cli/blob/master/libnvme/design/CONFIG.md](https://github.com/linux-nvme/nvme-cli/blob/master/libnvme/design/CONFIG.md),
 * legacy JSON configuration files are not supported anymore.
 * Any @extra option generally overrides particular option specified in a configuration file.
 *
 * Returns: %TRUE if the subsystem was connected successfully, %FALSE otherwise with @error set.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gboolean bd_nvme_connect (const gchar *subsysnqn, const gchar *transport, const gchar *transport_addr, const gchar *transport_svcid, const gchar *host_traddr, const gchar *host_iface, const gchar *host_nqn, const gchar *host_id, const BDExtraArg **extra, GError **error) {
    int ret;
    const gchar *config_file;
    gboolean use_config;
    gchar *host_nqn_val = NULL;
    gchar *host_id_val = NULL;
    const gchar *hostkey = NULL;
    const gchar *ctrlkey = NULL;
    const gchar *hostsymname = NULL;
    const gchar *keyring = NULL;
    const gchar *tls_key = NULL;
    struct libnvme_global_ctx *ctx;
    struct libnvme_host *host;
    struct libnvmf_context *fctx = NULL;
    struct libnvmf_config *config = NULL;
    struct config_match_data match = ZERO_INIT;
    gboolean success = FALSE;

    if (subsysnqn == NULL) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                             "Invalid value specified for the subsysnqn argument");
        return FALSE;
    }
    if (transport == NULL) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                             "Invalid value specified for the transport argument");
        return FALSE;
    }
    if (transport_addr == NULL && !g_str_equal (transport, "loop") && !g_str_equal (transport, "pcie")) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                             "Invalid value specified for the transport address argument");
        return FALSE;
    }

    /* tr_svcid defaults */
    if (!transport_svcid) {
        if (g_strcmp0 (transport, "tcp") == 0) {
            if (g_strcmp0 (subsysnqn, NVME_DISC_SUBSYS_NAME) == 0)
                transport_svcid = G_STRINGIFY (NVME_DISC_IP_PORT);
            else
                transport_svcid = G_STRINGIFY (NVME_RDMA_IP_PORT);
        } else
        if (g_strcmp0(transport, "rdma") == 0)
            transport_svcid = G_STRINGIFY (NVME_RDMA_IP_PORT);
    }

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                             "Failed to create libnvme context");
        return FALSE;
    }

    /* look up the connection in the configuration file */
    config_file = get_extra_config_file (extra, &use_config);
    if (use_config) {
        ret = libnvmf_config_read (ctx, config_file, &config);
        if (ret != 0) {
            if (config_file) {
                g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                             "Failed to read configuration file '%s': %s",
                             config_file, libnvme_strerror (-ret));
                goto out;
            }
            /* not fatal for the default configuration file */
            bd_utils_log_format (BD_UTILS_LOG_WARNING,
                                 "Failed to read the default NVMe-oF configuration file: %s",
                                 libnvme_strerror (-ret));
            config = NULL;
        }
    }
    if (config) {
        match.subsysnqn = subsysnqn;
        match.transport = transport;
        match.transport_addr = transport_addr;
        match.transport_svcid = transport_svcid;
        match.host_traddr = host_traddr;
        match.host_iface = host_iface;
        match.host_nqn = host_nqn;
        libnvmf_config_conn_for_each (config, config_match_conn, &match);
        if (match.conn) {
            if (!host_nqn)
                host_nqn = libnvmf_config_conn_get_hostnqn (match.conn);
            if (!host_id)
                host_id = libnvmf_config_conn_get_hostid (match.conn);
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
        ret = libnvmf_context_set_connection (fctx, subsysnqn, transport, transport_addr, transport_svcid, host_traddr, host_iface);
    if (ret == 0)
        ret = libnvmf_context_set_hostnqn (fctx, host_nqn_val, host_id_val);
    if (ret == 0 && match.conn)
        ret = libnvmf_context_apply_params (fctx, libnvmf_config_conn_get_params (match.conn));
    if (ret != 0) {
        _nvme_fabrics_errno_to_gerror (ret, error);
        g_prefix_error (error, "Error creating the controller: ");
        goto out;
    }

    /* any extra argument overrides values from the configuration file */
    parse_extra_args (extra, fctx, &hostkey, &ctrlkey, &hostsymname, &keyring, &tls_key);
    if (hostkey || ctrlkey || keyring || tls_key) {
        ret = libnvmf_context_set_crypto (fctx,
                                          hostkey ? hostkey : get_param_nonempty (match.conn, "kxchap-secret"),
                                          ctrlkey ? ctrlkey : get_param_nonempty (match.conn, "kxchap-ctrl-secret"),
                                          keyring ? keyring : get_param_nonempty (match.conn, "keyring"),
                                          tls_key /* NULL keeps the current key */,
                                          get_param_nonempty (match.conn, "tls-key-identity"));
        if (ret != 0) {
            _nvme_fabrics_errno_to_gerror (ret, error);
            g_prefix_error (error, "Error setting the controller keys: ");
            goto out;
        }
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

static gboolean _disconnect (const gchar *subsysnqn, const gchar *path, GError **error, gboolean *found) {
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
                    if (!path || g_strcmp0 (libnvme_ctrl_get_name (ctrl), path) == 0) {
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

/**
 * bd_nvme_disconnect:
 * @subsysnqn: The name of the NVMe subsystem to disconnect.
 * @error: (out) (nullable): Place to store error (if any).
 *
 * Disconnects and removes one or more existing NVMe over Fabrics controllers.
 * This may disconnect multiple controllers with matching @subsysnqn and %TRUE
 * is only returned when all controllers were disconnected successfully.
 *
 * Returns: %TRUE if all matching controllers were disconnected successfully, %FALSE with @error
 *          set in case of a disconnect error or when no matching controllers were found.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gboolean bd_nvme_disconnect (const gchar *subsysnqn, GError **error) {
    gboolean found = FALSE;

    if (!_disconnect (subsysnqn, NULL, error, &found))
        return FALSE;

    if (!found) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_NO_MATCH,
                     "No subsystems matching '%s' NQN found.", subsysnqn);
        return FALSE;
    }
    return TRUE;
}

/**
 * bd_nvme_disconnect_by_path:
 * @path: NVMe controller device to disconnect (e.g. `/dev/nvme0`).
 * @error: (out) (nullable): Place to store error (if any).
 *
 * Disconnects and removes a NVMe over Fabrics controller represented
 * by a block device path.
 *
 * Returns: %TRUE if the controller was disconnected successfully,
 *          %FALSE otherwise with @error set.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gboolean bd_nvme_disconnect_by_path (const gchar *path, GError **error) {
    const gchar *p;
    gboolean found = FALSE;

    p = path;
    if (g_str_has_prefix (p, "/dev/"))
        p += 5;

    if (!_disconnect (NULL, p, error, &found))
        return FALSE;

    if (!found) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_NO_MATCH,
                     "No controllers matching the %s device name found.", path);
        return FALSE;
    }
    return TRUE;
}


/**
 * bd_nvme_find_ctrls_for_ns:
 * @ns_sysfs_path: NVMe namespace sysfs path.
 * @subsysnqn: (nullable): Limit matching to the specified subsystem NQN.
 * @host_nqn: (nullable): Limit matching to the specified host NQN.
 * @host_id: (nullable): Limit matching to the specified host ID.
 * @error: (out) (nullable): Place to store error (if any).
 *
 * A convenient utility function to look up all controllers associated
 *  with a NVMe subsystem the specified namespace is part of.
 *
 * Returns: (transfer full) (array zero-terminated=1): list of controller sysfs paths
 *          or %NULL in case of an error (with @error set).
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gchar ** bd_nvme_find_ctrls_for_ns (const gchar *ns_sysfs_path, const gchar *subsysnqn, const gchar *host_nqn, const gchar *host_id, GError **error G_GNUC_UNUSED) {
    GPtrArray *ptr_array;
    struct libnvme_global_ctx *ctx;
    struct libnvme_host *h;
    struct libnvme_subsystem *s;
    struct libnvme_ctrl *c;
    struct libnvme_ns *n;
    int ret;
    char realp[PATH_MAX];
    gchar *subsysnqn_p;

    /* libnvme strips trailing spaces and newlines when reading values from sysfs */
    subsysnqn_p = g_strdup (subsysnqn);
    if (subsysnqn_p)
        g_strchomp (subsysnqn_p);

    ptr_array = g_ptr_array_new ();

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL) {
        g_ptr_array_add (ptr_array, NULL);  /* trailing NULL element */
        g_free (subsysnqn_p);
        return (gchar **) g_ptr_array_free (ptr_array, FALSE);
    }
    ret = libnvme_scan_topology (ctx, NULL, NULL);
    if (ret < 0)
        bd_utils_log_format (BD_UTILS_LOG_WARNING, "Failed to scan topology: %s",
                             strerror_l (-ret, _C_LOCALE));

    libnvme_for_each_host (ctx, h) {
        if (host_nqn && g_strcmp0 (libnvme_host_get_hostnqn (h), host_nqn) != 0)
            continue;
        if (host_id && g_strcmp0 (libnvme_host_get_hostid (h), host_id) != 0)
            continue;

        libnvme_for_each_subsystem (h, s) {
            gboolean found = FALSE;

            if (subsysnqn && g_strcmp0 (libnvme_subsystem_get_subsysnqn (s), subsysnqn_p) != 0)
                continue;

            libnvme_subsystem_for_each_ctrl (s, c)
                libnvme_ctrl_for_each_ns (c, n)
                    if (realpath (libnvme_ns_get_sysfs_dir (n), realp) &&
                        g_strcmp0 (realp, ns_sysfs_path) == 0) {
                        if (realpath (libnvme_ctrl_get_sysfs_dir (c), realp)) {
                            g_ptr_array_add (ptr_array, g_strdup (realp));
                            break;
                        }
                    }

            libnvme_subsystem_for_each_ns (s, n)
                if (realpath (libnvme_ns_get_sysfs_dir (n), realp) &&
                    g_strcmp0 (realp, ns_sysfs_path) == 0) {
                    found = TRUE;
                    /* at least one of the namespaces match, don't care about the rest */
                    break;
                }

            if (found)
                /* add all controllers in the subsystem */
                libnvme_subsystem_for_each_ctrl (s, c) {
                    if (realpath (libnvme_ctrl_get_sysfs_dir (c), realp)) {
                        g_ptr_array_add (ptr_array, g_strdup (realp));
                    }
                }
        }
    }
    libnvme_free_global_ctx (ctx);
    g_free (subsysnqn_p);

    g_ptr_array_add (ptr_array, NULL);  /* trailing NULL element */
    return (gchar **) g_ptr_array_free (ptr_array, FALSE);
}


/**
 * bd_nvme_find_namespaces_for_ctrl:
 * @ctrl_sysfs_path: NVMe controller sysfs path.
 * @subsysnqn: (nullable): Limit matching to the specified subsystem NQN.
 * @host_nqn: (nullable): Limit matching to the specified host NQN.
 * @host_id: (nullable): Limit matching to the specified host ID.
 * @error: (out) (nullable): Place to store error (if any).
 *
 * A convenient utility function to look up all namespaces associated
 *  with the specified NVMe controller.
 *
 * Returns: (transfer full) (array zero-terminated=1): list of namespace sysfs paths
 *          or %NULL in case of an error (with @error set).
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gchar ** bd_nvme_find_namespaces_for_ctrl (const gchar *ctrl_sysfs_path, const gchar *subsysnqn, const gchar *host_nqn, const gchar *host_id, GError **error G_GNUC_UNUSED) {
    GPtrArray *ptr_array;
    struct libnvme_global_ctx *ctx;
    struct libnvme_host *h;
    struct libnvme_subsystem *s;
    struct libnvme_ctrl *c;
    struct libnvme_ns *n;
    int ret;
    char realp[PATH_MAX];
    gchar *subsysnqn_p;

    /* libnvme strips trailing spaces and newlines when reading values from sysfs */
    subsysnqn_p = g_strdup (subsysnqn);
    if (subsysnqn_p)
        g_strchomp (subsysnqn_p);

    ptr_array = g_ptr_array_new ();

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL) {
        g_ptr_array_add (ptr_array, NULL);  /* trailing NULL element */
        g_free (subsysnqn_p);
        return (gchar **) g_ptr_array_free (ptr_array, FALSE);
    }
    ret = libnvme_scan_topology (ctx, NULL, NULL);
    if (ret < 0)
        bd_utils_log_format (BD_UTILS_LOG_WARNING, "Failed to scan topology: %s",
                             strerror_l (-ret, _C_LOCALE));

    libnvme_for_each_host (ctx, h) {
        if (host_nqn && g_strcmp0 (libnvme_host_get_hostnqn (h), host_nqn) != 0)
            continue;
        if (host_id && g_strcmp0 (libnvme_host_get_hostid (h), host_id) != 0)
            continue;

        libnvme_for_each_subsystem (h, s) {
            if (subsysnqn && g_strcmp0 (libnvme_subsystem_get_subsysnqn (s), subsysnqn_p) != 0)
                continue;

            libnvme_subsystem_for_each_ctrl (s, c) {
                if (realpath (libnvme_ctrl_get_sysfs_dir (c), realp) &&
                    g_strcmp0 (realp, ctrl_sysfs_path) == 0) {
                    /* Found matching controller, add all its namespaces */
                    libnvme_ctrl_for_each_ns (c, n) {
                        if (realpath (libnvme_ns_get_sysfs_dir (n), realp)) {
                            g_ptr_array_add (ptr_array, g_strdup (realp));
                        }
                    }
                    /* Also check subsystem-level namespaces */
                    libnvme_subsystem_for_each_ns (s, n) {
                        if (realpath (libnvme_ns_get_sysfs_dir (n), realp)) {
                            g_ptr_array_add (ptr_array, g_strdup (realp));
                        }
                    }
                    break;
                }
            }
        }
    }
    libnvme_free_global_ctx (ctx);
    g_free (subsysnqn_p);

    g_ptr_array_add (ptr_array, NULL);  /* trailing NULL element */
    return (gchar **) g_ptr_array_free (ptr_array, FALSE);
}


/**
 * bd_nvme_get_host_nqn:
 * @error: (out) (nullable): Place to store error (if any).
 *
 * Reads the Host NQN (NVM Qualified Name) value from the global `/etc/nvme/hostnqn`
 * file. An empty string is an indication that no Host NQN has been set.
 *
 * Returns: (transfer full): the Host NQN string or an empty string if none set.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gchar * bd_nvme_get_host_nqn (GError **error) {
    struct libnvme_global_ctx *ctx;
    char *hostnqn;

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                             "Failed to create libnvme context");
        return NULL;
    }
    /* FIXME: libnvme SYSCONFDIR might be different from PACKAGE_SYSCONF_DIR */
    hostnqn = libnvmf_read_hostnqn (ctx);
    libnvme_free_global_ctx (ctx);
    return hostnqn ? hostnqn : g_strdup ("");
}

/**
 * bd_nvme_generate_host_nqn:
 * @error: (out) (nullable): Place to store error (if any).
 *
 * Compute new Host NQN (NVM Qualified Name) value for the current system. This
 * takes in account various system identifiers (DMI, device tree) with the goal
 * of a stable unique identifier whenever feasible.
 *
 * Returns: (transfer full): the Host NQN string or %NULL with @error set.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gchar * bd_nvme_generate_host_nqn (GError **error) {
    struct libnvme_global_ctx *ctx;
    char *nqn;

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                             "Failed to create libnvme context");
        return NULL;
    }
    nqn = libnvmf_generate_hostnqn (ctx);
    libnvme_free_global_ctx (ctx);
    if (!nqn)
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                             "Unable to generate Host NQN.");

    return nqn;
}

/**
 * bd_nvme_get_host_id:
 * @error: (out) (nullable): Place to store error (if any).
 *
 * Reads the Host ID value from the global `/etc/nvme/hostid` file. An empty
 * string is an indication that no Host ID has been set.
 *
 * Returns: (transfer full): the Host ID string or an empty string if none set.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gchar * bd_nvme_get_host_id (GError **error) {
    struct libnvme_global_ctx *ctx;
    char *hostid;

    ctx = libnvme_create_global_ctx ();
    if (ctx == NULL) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                             "Failed to create libnvme context");
        return NULL;
    }
    hostid = libnvmf_read_hostid (ctx);
    libnvme_free_global_ctx (ctx);
    return hostid ? hostid : g_strdup ("");
}

/**
 * bd_nvme_set_host_nqn:
 * @host_nqn: The Host NVM Qualified Name.
 * @error: (out) (nullable): Place to store error (if any).
 *
 * Writes the Host NQN (NVM Qualified Name) value to the system `/etc/nvme/hostnqn` file.
 * No validation of the string is performed.
 *
 * Returns: %TRUE if the value was set successfully or %FALSE otherwise with @error set.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gboolean bd_nvme_set_host_nqn (const gchar *host_nqn, GError **error) {
    gchar *path;
    gchar *filename;
    gchar *s;
    gboolean ret;

    g_return_val_if_fail (host_nqn != NULL, FALSE);

    path = g_build_path (G_DIR_SEPARATOR_S, PACKAGE_SYSCONF_DIR, "nvme", NULL);
    if (g_mkdir_with_parents (path, 0755) != 0 && errno != EEXIST) {
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                     "Error creating %s: %s",
                     path, strerror_l (errno, _C_LOCALE));
        g_free (path);
        return FALSE;
    }
    filename = g_build_filename (path, "hostnqn", NULL);
    if (strlen (host_nqn) == 0 || host_nqn[strlen (host_nqn) - 1] != '\n')
        s = g_strdup_printf ("%s\n", host_nqn);
    else
        s = g_strdup (host_nqn);
    ret = g_file_set_contents (filename, s, -1, error);
    if (ret)
        g_chmod (filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    g_free (s);
    g_free (path);
    g_free (filename);

    return ret;
}

/**
 * bd_nvme_set_host_id:
 * @host_id: The Host ID.
 * @error: (out) (nullable): Place to store error (if any).
 *
 * Writes the Host ID value to the system `/etc/nvme/hostid` file.
 * No validation of the string is performed.
 *
 * Returns: %TRUE if the value was set successfully or %FALSE otherwise with @error set.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gboolean bd_nvme_set_host_id (const gchar *host_id, GError **error) {
    gchar *path;
    gchar *filename;
    gchar *s;
    gboolean ret;

    g_return_val_if_fail (host_id != NULL, FALSE);

    path = g_build_path (G_DIR_SEPARATOR_S, PACKAGE_SYSCONF_DIR, "nvme", NULL);
    if (g_mkdir_with_parents (path, 0755) != 0 && errno != EEXIST) {
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                     "Error creating %s: %s",
                     path, strerror_l (errno, _C_LOCALE));
        g_free (path);
        return FALSE;
    }
    filename = g_build_filename (path, "hostid", NULL);
    if (strlen (host_id) == 0 || host_id[strlen (host_id) - 1] != '\n')
        s = g_strdup_printf ("%s\n", host_id);
    else
        s = g_strdup (host_id);
    ret = g_file_set_contents (filename, s, -1, error);
    if (ret)
        g_chmod (filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    g_free (s);
    g_free (path);
    g_free (filename);

    return ret;
}
