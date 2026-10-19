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

#include <blockdev/utils.h>
#include <check_deps.h>
#include "nvme.h"
#include "nvme-private.h"
#include "nvme-backend.h"


void _nvme_topo_free (NVMeTopoSubsys **topo) {
    NVMeTopoSubsys **s;
    NVMeTopoCtrl **c;

    if (!topo)
        return;
    for (s = topo; *s; s++) {
        for (c = (*s)->ctrls; *c; c++) {
            g_free ((*c)->name);
            g_free ((*c)->sysfs_dir);
            g_strfreev ((*c)->ns_sysfs_dirs);
            g_free (*c);
        }
        g_free ((*s)->ctrls);
        g_free ((*s)->host_nqn);
        g_free ((*s)->host_id);
        g_free ((*s)->subsysnqn);
        g_strfreev ((*s)->ns_sysfs_dirs);
        g_free (*s);
    }
    g_free (topo);
}

static void parse_extra_args (const BDExtraArg **extra, NVMeConnectParams *params) {
    const BDExtraArg **extra_i;

    if (!extra)
        return;

#define SAFE_INT_CONV(target) { \
        gint64 v; \
        gchar *endptr = NULL; \
        \
        v = g_ascii_strtoll ((*extra_i)->val, &endptr, 0); \
        if (endptr != (*extra_i)->val) { \
            target.val = v; \
            target.set = TRUE; \
        } \
    }
#define SAFE_BOOL_CONV(target) { \
        if (g_ascii_strcasecmp ((*extra_i)->val, "on") == 0 || \
            g_ascii_strcasecmp ((*extra_i)->val, "1") == 0 || \
            g_ascii_strcasecmp ((*extra_i)->val, "true") == 0) { \
            target.val = TRUE; \
            target.set = TRUE; \
        } else \
        if (g_ascii_strcasecmp ((*extra_i)->val, "off") == 0 || \
            g_ascii_strcasecmp ((*extra_i)->val, "0") == 0 || \
            g_ascii_strcasecmp ((*extra_i)->val, "false") == 0) { \
            target.val = FALSE; \
            target.set = TRUE; \
        } \
    }

    for (extra_i = extra; *extra_i; extra_i++) {
        if (g_strcmp0 ((*extra_i)->opt, "config") == 0) {
            if (g_ascii_strcasecmp ((*extra_i)->val, "none") == 0) {
                params->use_config = FALSE;
                params->config_file = NULL;
            } else {
                params->use_config = TRUE;
                params->config_file = (*extra_i)->val;
            }
        } else
        if (g_strcmp0 ((*extra_i)->opt, "kxchap_secret") == 0 ||
            g_strcmp0 ((*extra_i)->opt, "dhchap_key") == 0 /* deprecated */)
            params->hostkey = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "kxchap_ctrl_secret") == 0 ||
            g_strcmp0 ((*extra_i)->opt, "dhchap_ctrl_key") == 0 /* deprecated */)
            params->ctrlkey = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "hostsymname") == 0)
            params->hostsymname = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "keyring") == 0)
            params->keyring = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "tls_key") == 0)
            params->tls_key = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "owner") == 0)
            params->owner = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "nr_io_queues") == 0)
            SAFE_INT_CONV (params->nr_io_queues)
        else
        if (g_strcmp0 ((*extra_i)->opt, "nr_write_queues") == 0)
            SAFE_INT_CONV (params->nr_write_queues)
        else
        if (g_strcmp0 ((*extra_i)->opt, "nr_poll_queues") == 0)
            SAFE_INT_CONV (params->nr_poll_queues)
        else
        if (g_strcmp0 ((*extra_i)->opt, "queue_size") == 0)
            SAFE_INT_CONV (params->queue_size)
        else
        if (g_strcmp0 ((*extra_i)->opt, "keep_alive_tmo") == 0)
            SAFE_INT_CONV (params->keep_alive_tmo)
        else
        if (g_strcmp0 ((*extra_i)->opt, "reconnect_delay") == 0)
            SAFE_INT_CONV (params->reconnect_delay)
        else
        if (g_strcmp0 ((*extra_i)->opt, "ctrl_loss_tmo") == 0)
            SAFE_INT_CONV (params->ctrl_loss_tmo)
        else
        if (g_strcmp0 ((*extra_i)->opt, "fast_io_fail_tmo") == 0)
            SAFE_INT_CONV (params->fast_io_fail_tmo)
        else
        if (g_strcmp0 ((*extra_i)->opt, "tos") == 0)
            SAFE_INT_CONV (params->tos)
        else
        if (g_strcmp0 ((*extra_i)->opt, "duplicate_connect") == 0)
            SAFE_BOOL_CONV (params->duplicate_connect)
        else
        if (g_strcmp0 ((*extra_i)->opt, "disable_sqflow") == 0)
            SAFE_BOOL_CONV (params->disable_sqflow)
        else
        if (g_strcmp0 ((*extra_i)->opt, "hdr_digest") == 0)
            SAFE_BOOL_CONV (params->hdr_digest)
        else
        if (g_strcmp0 ((*extra_i)->opt, "data_digest") == 0)
            SAFE_BOOL_CONV (params->data_digest)
        else
        if (g_strcmp0 ((*extra_i)->opt, "tls") == 0)
            SAFE_BOOL_CONV (params->tls)
    }

#undef SAFE_INT_CONV
#undef SAFE_BOOL_CONV
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
 *                      the secret is taken from the configuration file or, with libnvme-1.x, read from
 *                      `/etc/nvme/hostkey`, if present. Otherwise no in-band authentication is attempted.
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
 * - `"owner"`: Register a newly created controller with this owner in the libnvme controller ownership
 *              registry (`/run/nvme/registry/`) instead of the default `"libblockdev"`, e.g. `"udisks"`.
 *              An empty string leaves the controller without an owner. An already connected controller
 *              keeps its owner. See bd_nvme_disconnect() and bd_nvme_disconnect_full().
 *              Only supported with libnvme3, ignored with libnvme-1.x.
 * - `"keyring"`: Keyring to store and lookup keys. String value.
 * - `"tls_key"`: TLS PSK for the connection. String value.
 *
 * Boolean values can be expressed by "0"/"1", "on"/"off" or "True"/"False" case-insensitive
 * strings. Failed numerical or boolean string conversions will result in the option being ignored.
 *
 * By default additional options are read from the default configuration file, following the
 * default behaviour of `nvme-cli`. Use the @extra `"config"` argument to either specify a different
 * config file or disable use of it. The configuration file format depends on the libnvme flavour
 * libblockdev has been built with:
 * - libnvme3: The INI configuration file `/etc/nvme/nvme-fabrics.conf` (and its `/etc/nvme/nvme-fabrics.conf.d/`
 *   drop-in directory). Parameters of the first connection entry matching the @subsysnqn, @transport,
 *   @transport_addr, @transport_svcid, @host_traddr and @host_iface arguments are applied, along with
 *   its host identity when @host_nqn or @host_id are not specified. The format is documented
 *   in [https://github.com/linux-nvme/nvme-cli/blob/master/libnvme/design/CONFIG.md](https://github.com/linux-nvme/nvme-cli/blob/master/libnvme/design/CONFIG.md),
 *   legacy JSON configuration files are not supported. Unlike `nvme-cli`, libblockdev doesn't convert
 *   an existing `/etc/nvme/config.json` automatically, a warning is logged instead. Use `nvme config-convert`
 *   to convert it.
 * - libnvme-1.x: The JSON configuration file `/etc/nvme/config.json`. The format is documented
 *   in [https://raw.githubusercontent.com/linux-nvme/libnvme/master/doc/config-schema.json](https://raw.githubusercontent.com/linux-nvme/libnvme/master/doc/config-schema.json).
 *   As a rule @extra key names are kept consistent with the JSON config file schema.
 *
 * Any @extra option generally overrides particular option specified in a configuration file.
 *
 * Returns: %TRUE if the subsystem was connected successfully, %FALSE otherwise with @error set.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gboolean bd_nvme_connect (const gchar *subsysnqn, const gchar *transport, const gchar *transport_addr, const gchar *transport_svcid, const gchar *host_traddr, const gchar *host_iface, const gchar *host_nqn, const gchar *host_id, const BDExtraArg **extra, GError **error) {
    NVMeConnectParams params = ZERO_INIT;

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

    params.subsysnqn = subsysnqn;
    params.transport = transport;
    params.transport_addr = transport_addr;
    params.transport_svcid = transport_svcid;
    params.host_traddr = host_traddr;
    params.host_iface = host_iface;
    params.host_nqn = host_nqn;
    params.host_id = host_id;

    /* parse extra arguments */
    params.use_config = TRUE;
    parse_extra_args (extra, &params);

    return _nvme_be_connect (&params, error);
}

static gboolean _disconnect (const gchar *subsysnqn, const gchar *path, const gchar *owner, BDNVMEDisconnectFlags flags, GError **error) {
    const gchar *name = NULL;
    gboolean found = FALSE;
    gboolean disconnected = FALSE;
    gchar *skipped_owner = NULL;
    gchar *what;

    if (path) {
        name = path;
        if (g_str_has_prefix (name, "/dev/"))
            name += 5;
    }
    if (!owner)
        owner = NVME_DEFAULT_OWNER;

    if (!_nvme_be_disconnect (subsysnqn, name, owner, flags & BD_NVME_DISCONNECT_FORCE,
                              &found, &disconnected, &skipped_owner, error))
        return FALSE;

    if (!found) {
        if (path)
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_NO_MATCH,
                         "No controllers matching the %s device name found.", path);
        else
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_NO_MATCH,
                         "No subsystems matching '%s' NQN found.", subsysnqn);
        return FALSE;
    }
    if (!disconnected) {
        what = path ? g_strdup_printf ("The %s controller is", path)
                    : g_strdup_printf ("Controllers of the '%s' subsystem are", subsysnqn);
        if (skipped_owner)
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_OWNED,
                         "%s owned by '%s' instead of '%s', not disconnecting.",
                         what, skipped_owner, owner);
        else
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_OWNED,
                         "%s not owned by '%s', not disconnecting.", what, owner);
        g_free (what);
        g_free (skipped_owner);
        return FALSE;
    }
    g_free (skipped_owner);
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
 * With libnvme3, only controllers owned by libblockdev according to the libnvme controller
 * ownership registry are disconnected, i.e. the ones connected by bd_nvme_connect() without
 * a specific `"owner"`. Controllers connected by other components, e.g. boot volumes
 * connected via NBFT or connections managed by a discovery daemon, are left untouched.
 * See bd_nvme_disconnect_full() to disconnect controllers of a different owner.
 * libnvme-1.x has no ownership registry, all matching controllers are disconnected.
 *
 * Returns: %TRUE if all matching controllers were disconnected successfully, %FALSE with @error
 *          set in case of a disconnect error or when no matching controllers were found.
 *          The #BD_NVME_ERROR_OWNED error is returned when none of the matching controllers
 *          are owned by libblockdev.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gboolean bd_nvme_disconnect (const gchar *subsysnqn, GError **error) {
    return _disconnect (subsysnqn, NULL, NULL, BD_NVME_DISCONNECT_NONE, error);
}

/**
 * bd_nvme_disconnect_by_path:
 * @path: NVMe controller device to disconnect (e.g. `/dev/nvme0`).
 * @error: (out) (nullable): Place to store error (if any).
 *
 * Disconnects and removes a NVMe over Fabrics controller represented
 * by a block device path.
 *
 * With libnvme3, only a controller owned by libblockdev according to the libnvme controller
 * ownership registry is disconnected, see bd_nvme_disconnect() and bd_nvme_disconnect_full().
 *
 * Returns: %TRUE if the controller was disconnected successfully,
 *          %FALSE otherwise with @error set.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gboolean bd_nvme_disconnect_by_path (const gchar *path, GError **error) {
    return _disconnect (NULL, path, NULL, BD_NVME_DISCONNECT_NONE, error);
}

/**
 * bd_nvme_disconnect_full:
 * @subsysnqn: (nullable): The name of the NVMe subsystem to disconnect.
 * @path: (nullable): NVMe controller device to disconnect (e.g. `/dev/nvme0`).
 * @owner: (nullable): The owner of the controllers to disconnect or %NULL for libblockdev.
 * @flags: Flags to control the disconnect.
 * @error: (out) (nullable): Place to store error (if any).
 *
 * Disconnects and removes existing NVMe over Fabrics controllers matching @subsysnqn
 * or the controller represented by @path. Exactly one of @subsysnqn and @path has to
 * be specified.
 *
 * Only controllers registered to @owner in the libnvme controller ownership registry
 * (`/run/nvme/registry/`) are disconnected, see the `"owner"` argument of bd_nvme_connect().
 * With the #BD_NVME_DISCONNECT_FORCE flag all matching controllers are disconnected
 * regardless of their owner, including controllers without any owner. Use with care,
 * this may disconnect devices other components depend on, e.g. the root filesystem.
 *
 * libnvme-1.x has no ownership registry, @owner and @flags are ignored and all matching
 * controllers are disconnected, same as with bd_nvme_disconnect().
 *
 * Returns: %TRUE if all matching controllers were disconnected successfully, %FALSE with @error
 *          set in case of a disconnect error or when no matching controllers were found.
 *          The #BD_NVME_ERROR_OWNED error is returned when none of the matching controllers
 *          are owned by @owner.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gboolean bd_nvme_disconnect_full (const gchar *subsysnqn, const gchar *path, const gchar *owner, BDNVMEDisconnectFlags flags, GError **error) {
    if ((subsysnqn == NULL) == (path == NULL)) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                             "Exactly one of the subsystem NQN and the controller path has to be specified");
        return FALSE;
    }

    return _disconnect (subsysnqn, path, owner, flags, error);
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
    NVMeTopoSubsys **topo;
    NVMeTopoSubsys **s;
    NVMeTopoCtrl **c;
    gchar **n;
    char realp[PATH_MAX];
    gchar *subsysnqn_p;

    /* libnvme strips trailing spaces and newlines when reading values from sysfs */
    subsysnqn_p = g_strdup (subsysnqn);
    if (subsysnqn_p)
        g_strchomp (subsysnqn_p);

    ptr_array = g_ptr_array_new ();

    topo = _nvme_be_scan_topology ();

    for (s = topo; *s; s++) {
        gboolean found = FALSE;

        if (host_nqn && g_strcmp0 ((*s)->host_nqn, host_nqn) != 0)
            continue;
        if (host_id && g_strcmp0 ((*s)->host_id, host_id) != 0)
            continue;
        if (subsysnqn && g_strcmp0 ((*s)->subsysnqn, subsysnqn_p) != 0)
            continue;

        for (c = (*s)->ctrls; *c; c++)
            for (n = (*c)->ns_sysfs_dirs; *n; n++)
                if (realpath (*n, realp) &&
                    g_strcmp0 (realp, ns_sysfs_path) == 0) {
                    if ((*c)->sysfs_dir && realpath ((*c)->sysfs_dir, realp)) {
                        g_ptr_array_add (ptr_array, g_strdup (realp));
                        break;
                    }
                }

        for (n = (*s)->ns_sysfs_dirs; *n; n++)
            if (realpath (*n, realp) &&
                g_strcmp0 (realp, ns_sysfs_path) == 0) {
                found = TRUE;
                /* at least one of the namespaces match, don't care about the rest */
                break;
            }

        if (found)
            /* add all controllers in the subsystem */
            for (c = (*s)->ctrls; *c; c++) {
                if ((*c)->sysfs_dir && realpath ((*c)->sysfs_dir, realp)) {
                    g_ptr_array_add (ptr_array, g_strdup (realp));
                }
            }
    }
    _nvme_topo_free (topo);
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
    NVMeTopoSubsys **topo;
    NVMeTopoSubsys **s;
    NVMeTopoCtrl **c;
    gchar **n;
    char realp[PATH_MAX];
    gchar *subsysnqn_p;

    /* libnvme strips trailing spaces and newlines when reading values from sysfs */
    subsysnqn_p = g_strdup (subsysnqn);
    if (subsysnqn_p)
        g_strchomp (subsysnqn_p);

    ptr_array = g_ptr_array_new ();

    topo = _nvme_be_scan_topology ();

    for (s = topo; *s; s++) {
        if (host_nqn && g_strcmp0 ((*s)->host_nqn, host_nqn) != 0)
            continue;
        if (host_id && g_strcmp0 ((*s)->host_id, host_id) != 0)
            continue;
        if (subsysnqn && g_strcmp0 ((*s)->subsysnqn, subsysnqn_p) != 0)
            continue;

        for (c = (*s)->ctrls; *c; c++) {
            if ((*c)->sysfs_dir && realpath ((*c)->sysfs_dir, realp) &&
                g_strcmp0 (realp, ctrl_sysfs_path) == 0) {
                /* Found matching controller, add all its namespaces */
                for (n = (*c)->ns_sysfs_dirs; *n; n++) {
                    if (realpath (*n, realp)) {
                        g_ptr_array_add (ptr_array, g_strdup (realp));
                    }
                }
                /* Also check subsystem-level namespaces */
                for (n = (*s)->ns_sysfs_dirs; *n; n++) {
                    if (realpath (*n, realp)) {
                        g_ptr_array_add (ptr_array, g_strdup (realp));
                    }
                }
                break;
            }
        }
    }
    _nvme_topo_free (topo);
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
gchar * bd_nvme_get_host_nqn (G_GNUC_UNUSED GError **error) {
    char *hostnqn;

    /* FIXME: libnvme SYSCONFDIR might be different from PACKAGE_SYSCONF_DIR */
    hostnqn = _nvme_be_read_hostnqn ();
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
    char *nqn;

    nqn = _nvme_be_generate_hostnqn ();
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
gchar * bd_nvme_get_host_id (G_GNUC_UNUSED GError **error) {
    char *hostid;

    hostid = _nvme_be_read_hostid ();
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
