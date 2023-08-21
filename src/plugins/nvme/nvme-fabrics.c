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


/* nvme-cli defaults */
#define PATH_NVMF_CONFIG  "/etc/nvme/config.json"
#define MAX_DISC_RETRIES  10


static void parse_extra_args (const BDExtraArg **extra, struct nvme_fabrics_config *cfg, const gchar **config_file, const gchar **hostkey, const gchar **ctrlkey, const gchar **hostsymname) {
    const BDExtraArg **extra_i;

    if (!extra)
        return;

#define SAFE_INT_CONV(target) { \
        gint64 v; \
        gchar *endptr = NULL; \
        \
        v = g_ascii_strtoll ((*extra_i)->val, &endptr, 0); \
        if (endptr != (*extra_i)->val) \
            target = v; \
    }
#define SAFE_BOOL_CONV(target) { \
        if (g_ascii_strcasecmp ((*extra_i)->val, "on") == 0 || \
            g_ascii_strcasecmp ((*extra_i)->val, "1") == 0 || \
            g_ascii_strcasecmp ((*extra_i)->val, "true") == 0) \
            target = TRUE; \
        else \
        if (g_ascii_strcasecmp ((*extra_i)->val, "off") == 0 || \
            g_ascii_strcasecmp ((*extra_i)->val, "0") == 0 || \
            g_ascii_strcasecmp ((*extra_i)->val, "false") == 0) \
            target = FALSE; \
    }

    for (extra_i = extra; *extra_i; extra_i++) {
        if (g_strcmp0 ((*extra_i)->opt, "config") == 0 && config_file) {
            if (g_ascii_strcasecmp ((*extra_i)->val, "none") == 0)
                *config_file = NULL;
            else
                *config_file = (*extra_i)->val;
        } else
        if (g_strcmp0 ((*extra_i)->opt, "dhchap_key") == 0 && hostkey)
            *hostkey = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "dhchap_ctrl_key") == 0 && ctrlkey)
            *ctrlkey = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "hostsymname") == 0 && hostsymname)
            *hostsymname = (*extra_i)->val;
        else
        if (g_strcmp0 ((*extra_i)->opt, "nr_io_queues") == 0)
            SAFE_INT_CONV (cfg->nr_io_queues)
        else
        if (g_strcmp0 ((*extra_i)->opt, "nr_write_queues") == 0)
            SAFE_INT_CONV (cfg->nr_write_queues)
        else
        if (g_strcmp0 ((*extra_i)->opt, "nr_poll_queues") == 0)
            SAFE_INT_CONV (cfg->nr_poll_queues)
        else
        if (g_strcmp0 ((*extra_i)->opt, "queue_size") == 0)
            SAFE_INT_CONV (cfg->queue_size)
        else
        if (g_strcmp0 ((*extra_i)->opt, "keep_alive_tmo") == 0)
            SAFE_INT_CONV (cfg->keep_alive_tmo)
        else
        if (g_strcmp0 ((*extra_i)->opt, "reconnect_delay") == 0)
            SAFE_INT_CONV (cfg->reconnect_delay)
        else
        if (g_strcmp0 ((*extra_i)->opt, "ctrl_loss_tmo") == 0)
            SAFE_INT_CONV (cfg->ctrl_loss_tmo)
        else
        if (g_strcmp0 ((*extra_i)->opt, "fast_io_fail_tmo") == 0)
            SAFE_INT_CONV (cfg->fast_io_fail_tmo)
        else
        if (g_strcmp0 ((*extra_i)->opt, "tos") == 0)
            SAFE_INT_CONV (cfg->tos)
        else
        if (g_strcmp0 ((*extra_i)->opt, "duplicate_connect") == 0)
            SAFE_BOOL_CONV (cfg->duplicate_connect)
        else
        if (g_strcmp0 ((*extra_i)->opt, "disable_sqflow") == 0)
            SAFE_BOOL_CONV (cfg->disable_sqflow)
        else
        if (g_strcmp0 ((*extra_i)->opt, "hdr_digest") == 0)
            SAFE_BOOL_CONV (cfg->hdr_digest)
        else
        if (g_strcmp0 ((*extra_i)->opt, "data_digest") == 0)
            SAFE_BOOL_CONV (cfg->data_digest)
        else
        if (g_strcmp0 ((*extra_i)->opt, "tls") == 0)
            SAFE_BOOL_CONV (cfg->tls)
#ifdef HAVE_LIBNVME_1_4
        else
        if (g_strcmp0 ((*extra_i)->opt, "keyring") == 0) {
            int keyring;

            keyring = nvme_lookup_keyring ((*extra_i)->val);
            if (keyring) {
                cfg->keyring = keyring;
                nvme_set_keyring (cfg->keyring);
            }
        } else
        if (g_strcmp0 ((*extra_i)->opt, "tls_key") == 0) {
            int key;

            key = nvme_lookup_key ("psk", (*extra_i)->val);
            if (key)
                cfg->tls_key = key;
        }
#endif
    }

#undef SAFE_INT_CONV
#undef SAFE_BOOL_CONV
}


/**
 * bd_nvme_connect:
 * @subsysnqn: The name for the NVMe subsystem to connect to.
 * @transport: The network fabric used for a NVMe-over-Fabrics network.
 * @transport_addr: (nullable): The network address of the Controller. For transports using IP addressing (e.g. `rdma`) this should be an IP-based address.
 * @transport_svcid: (nullable): The transport service id.  For transports using IP addressing (e.g. `rdma`) this field is the port number. By default, the IP port number for the `RDMA` transport is `4420`.
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
 * - `"config"`: Use the specified JSON configuration file instead of the default file (see below) or
 *               specify `"none"` to avoid reading any configuration file.
 * - `"dhchap_key"`: NVMe In-band authentication secret in ASCII format as described
 *                      in the NVMe 2.0 specification. When not specified, the secret is by default read
 *                      from `/etc/nvme/hostkey`. In case that file does not exist no in-band authentication
 *                      is attempted.
 * - `"dhchap_ctrl_key"`: NVMe In-band authentication controller secret for bi-directional authentication.
 *                        When not specified, no bi-directional authentication is attempted.
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
 * By default additional options are read from the default configuration file `/etc/nvme/config.json`.
 * This follows the default behaviour of `nvme-cli`. Use the @extra `"config"` argument
 * to either specify a different config file or disable use of it. The JSON configuration
 * file format is documented in [https://raw.githubusercontent.com/linux-nvme/libnvme/master/doc/config-schema.json](https://raw.githubusercontent.com/linux-nvme/libnvme/master/doc/config-schema.json).
 * As a rule @extra key names are kept consistent with the JSON config file schema.
 * Any @extra option generally overrides particular option specified in a configuration file.
 *
 * Returns: %TRUE if the subsystem was connected successfully, %FALSE otherwise with @error set.
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
gboolean bd_nvme_connect (const gchar *subsysnqn, const gchar *transport, const gchar *transport_addr, const gchar *transport_svcid, const gchar *host_traddr, const gchar *host_iface, const gchar *host_nqn, const gchar *host_id, const BDExtraArg **extra, GError **error) {
    int ret;
    const gchar *config_file = PATH_NVMF_CONFIG;
    gchar *host_nqn_val;
    gchar *host_id_val;
    const gchar *hostkey = NULL;
    const gchar *ctrlkey = NULL;
    const gchar *hostsymname = NULL;
    nvme_root_t root;
    nvme_host_t host;
    nvme_ctrl_t ctrl;
    struct nvme_fabrics_config cfg;

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

    /* HostNQN checks */
    host_nqn_val = g_strdup (host_nqn);
    host_id_val = g_strdup (host_id);
    if (host_nqn_val == NULL)
        host_nqn_val = nvmf_hostnqn_from_file ();
    if (host_id_val == NULL)
        host_id_val = nvmf_hostid_from_file ();
    if (host_nqn_val == NULL)
        host_nqn_val = nvmf_hostnqn_generate ();
    if (host_nqn_val == NULL) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                             "Could not determine HostNQN");
        g_free (host_nqn_val);
        g_free (host_id_val);
        return FALSE;
    }
    if (host_id_val == NULL) {
        /* derive hostid from hostnqn, newer kernels refuse empty hostid */
        host_id_val = g_strrstr (host_nqn_val, "uuid:");
        if (host_id_val)
            host_id_val = g_strdup (host_id_val + strlen ("uuid:"));
        /* TODO: in theory generating arbitrary uuid might work as a fallback */
    }
    if (host_id_val == NULL) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                     "Could not determine HostID value from HostNQN '%s'",
                     host_nqn_val);
        g_free (host_nqn_val);
        g_free (host_id_val);
        return FALSE;
    }

    /* parse extra arguments */
    nvmf_default_config (&cfg);
    parse_extra_args (extra, &cfg, &config_file, &hostkey, &ctrlkey, &hostsymname);

    root = nvme_scan (config_file);
    g_assert (root != NULL);
    nvme_init_logging (root, -1, false, false);
    host = nvme_lookup_host (root, host_nqn_val, host_id_val);
    if (host == NULL) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Unable to lookup host for HostNQN '%s' and HostID '%s'",
                     host_nqn_val, host_id_val);
        g_free (host_nqn_val);
        g_free (host_id_val);
        nvme_free_tree (root);
        return FALSE;
    }
    g_free (host_nqn_val);
    g_free (host_id_val);
    if (hostkey)
        nvme_host_set_dhchap_key (host, hostkey);
    if (hostsymname)
        nvme_host_set_hostsymname (host, hostsymname);

    ctrl = nvme_create_ctrl (root, subsysnqn, transport, transport_addr, host_traddr, host_iface, transport_svcid);
    if (ctrl == NULL) {
        _nvme_fabrics_errno_to_gerror (-1, errno, error);
        g_prefix_error (error, "Error creating the controller: ");
        nvme_free_tree (root);
        return FALSE;
    }
    if (ctrlkey)
        nvme_ctrl_set_dhchap_key (ctrl, ctrlkey);

    ret = nvmf_add_ctrl (host, ctrl, &cfg);
    if (ret != 0) {
        _nvme_fabrics_errno_to_gerror (ret, errno, error);
        g_prefix_error (error, "Error connecting the controller: ");
        nvme_free_ctrl (ctrl);
        nvme_free_tree (root);
        return FALSE;
    }
    nvme_free_ctrl (ctrl);
    nvme_free_tree (root);

    return TRUE;
}

static gboolean _disconnect (const gchar *subsysnqn, const gchar *path, GError **error, gboolean *found) {
    nvme_root_t root;
    nvme_host_t host;
    nvme_subsystem_t subsys;
    nvme_ctrl_t ctrl;
    int ret;

    root = nvme_create_root (NULL, -1);
    if (root == NULL) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Failed to create topology root: %s",
                     strerror_l (errno, _C_LOCALE));
        return FALSE;
    }
    ret = nvme_scan_topology (root, NULL, NULL);
    if (ret < 0) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Failed to scan topology: %s",
                     strerror_l (errno, _C_LOCALE));
        nvme_free_tree (root);
        return FALSE;
    }
    nvme_for_each_host (root, host)
        nvme_for_each_subsystem (host, subsys)
            if (!subsysnqn || g_strcmp0 (nvme_subsystem_get_nqn (subsys), subsysnqn) == 0)
                nvme_subsystem_for_each_ctrl (subsys, ctrl)
                    if (!path || g_strcmp0 (nvme_ctrl_get_name (ctrl), path) == 0) {
                        ret = nvme_disconnect_ctrl (ctrl);
                        if (ret != 0) {
                            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                                         "Error disconnecting the controller: %s",
                                         strerror_l (errno, _C_LOCALE));
                            nvme_free_tree (root);
                            return FALSE;
                        }
                        *found = TRUE;
                    }
    nvme_free_tree (root);
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
 * @ns_sysfs_path: NVMe namespace device file.
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
    nvme_root_t root;
    nvme_host_t h;
    nvme_subsystem_t s;
    nvme_ctrl_t c;
    nvme_ns_t n;
    char realp[PATH_MAX];
    gchar *subsysnqn_p;

    /* libnvme strips trailing spaces and newlines when reading values from sysfs */
    subsysnqn_p = g_strdup (subsysnqn);
    if (subsysnqn_p)
        g_strchomp (subsysnqn_p);

    ptr_array = g_ptr_array_new ();

    root = nvme_scan (NULL);
    g_warn_if_fail (root != NULL);

    nvme_for_each_host (root, h) {
        if (host_nqn && g_strcmp0 (nvme_host_get_hostnqn (h), host_nqn) != 0)
            continue;
        if (host_id && g_strcmp0 (nvme_host_get_hostid (h), host_id) != 0)
            continue;

        nvme_for_each_subsystem (h, s) {
            gboolean found = FALSE;

            if (subsysnqn && g_strcmp0 (nvme_subsystem_get_nqn (s), subsysnqn_p) != 0)
                continue;

            nvme_subsystem_for_each_ctrl (s, c)
                nvme_ctrl_for_each_ns (c, n)
                    if (realpath (nvme_ns_get_sysfs_dir (n), realp) &&
                        g_strcmp0 (realp, ns_sysfs_path) == 0) {
                        if (realpath (nvme_ctrl_get_sysfs_dir (c), realp)) {
                            g_ptr_array_add (ptr_array, g_strdup (realp));
                            break;
                        }
                    }

            nvme_subsystem_for_each_ns (s, n)
                if (realpath (nvme_ns_get_sysfs_dir (n), realp) &&
                    g_strcmp0 (realp, ns_sysfs_path) == 0) {
                    found = TRUE;
                    /* at least one of the namespaces match, don't care about the rest */
                    break;
                }

            if (found)
                /* add all controllers in the subsystem */
                nvme_subsystem_for_each_ctrl (s, c) {
                    if (realpath (nvme_ctrl_get_sysfs_dir (c), realp)) {
                        g_ptr_array_add (ptr_array, g_strdup (realp));
                    }
                }
        }
    }
    nvme_free_tree (root);
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
    hostnqn = nvmf_hostnqn_from_file ();
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

    nqn = nvmf_hostnqn_generate ();
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

    hostid = nvmf_hostid_from_file ();
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
    if (host_nqn[strlen (host_nqn) - 1] != '\n')
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
    if (host_id[strlen (host_id) - 1] != '\n')
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
