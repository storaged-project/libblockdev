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

#include <libnvme.h>
#include <uuid/uuid.h>

#include <blockdev/utils.h>
#include <check_deps.h>
#include "nvme.h"
#include "nvme-private.h"


/* nvme-cli defaults */
#define PATH_NVMF_CONFIG  "/etc/nvme/config.json"
#define MAX_DISC_RETRIES  10


static void parse_extra_args (const BDExtraArg **extra, struct nvme_fabrics_config *cfg, const gchar **config_file, const gchar **hostkey, const gchar **ctrlkey) {
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
    }

#undef SAFE_INT_CONV
#undef SAFE_BOOL_CONV
}


/**
 * bd_nvme_connect:
 * @subsysnqn: The name for the NVMe subsystem to connect to.
 * @transport: The network fabric used for a NVMe-over-Fabrics network.
 * @transport_addr: (nullable): The network address of the Controller. For transports using IP addressing (e.g. `rdma`) this should be an IP-based address.
 * @transport_svcid: (nullable): The transport service id.  For transports using IP addressing (e.g. `rdma`) this field is the port number. By default, the IP port number for the `RDMA` transport is %4420.
 * @host_traddr: (nullable): The network address used on the host to connect to the Controller. For TCP, this sets the source address on the socket.
 * @host_iface: (nullable): The network interface used on the host to connect to the Controller (e.g. IP `eth1`, `enp2s0`). This forces the connection to be made on a specific interface instead of letting the system decide.
 * @host_nqn: (nullable): Overrides the default Host NQN that identifies the NVMe Host. If this option is %NULL, the default is read from `/etc/nvme/hostnqn` first.
 *                        If that does not exist, the autogenerated NQN value from the NVMe Host kernel module is used next. The Host NQN uniquely identifies the NVMe Host.
 * @host_id: (nullable): User-defined host UUID or %NULL to use default (as defined in `/etc/nvme/hostid`)
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
 * - `"ctrl_loss_tmo"`: The controller loss timeout period (in seconds). A special value of %-1 will cause reconnecting forever.
 * - `"fast_io_fail_tmo"`: Fast I/O Fail timeout (in seconds).
 * - `"tos"`: Type of service.
 * - `"duplicate_connect"`: Allow duplicated connections between same transport host and subsystem port. Boolean value.
 * - `"disable_sqflow"`: Disables SQ flow control to omit head doorbell update for submission queues when sending nvme completions. Boolean value.
 * - `"hdr_digest"`: Generates/verifies header digest (TCP). Boolean value.
 * - `"data_digest"`: Generates/verifies data digest (TCP). Boolean value.
 * - `"tls"`: Enable TLS encryption (TCP). Boolean value.
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

    /* parse extra arguments */
    nvmf_default_config (&cfg);
    parse_extra_args (extra, &cfg, &config_file, &hostkey, &ctrlkey);

    host_nqn_val = g_strdup (host_nqn);
    if (host_nqn_val == NULL)
        host_nqn_val = nvmf_hostnqn_from_file ();
    host_id_val = g_strdup (host_id);
    if (host_id_val == NULL)
        host_id_val = nvmf_hostid_from_file ();

    root = nvme_scan (config_file);
    g_assert (root != NULL);
    nvme_init_logging (root, -1, false, false);
    host = nvme_lookup_host (root, host_nqn_val, host_id_val);
    if (host == NULL) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Unable to lookup host for nqn '%s' and id '%s'",
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

    ctrl = nvme_create_ctrl (root, subsysnqn, transport, transport_addr, host_traddr, host_iface, transport_svcid);
    if (ctrl == NULL) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Error creating the controller: %s",
                     strerror_l (errno, _C_LOCALE));
        nvme_free_tree (root);
        return FALSE;
    }
    if (ctrlkey)
        nvme_ctrl_set_dhchap_key (ctrl, ctrlkey);

    ret = nvmf_add_ctrl (host, ctrl, &cfg);
    if (ret != 0) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Error connecting the controller: %s",
                     (errno >= ENVME_CONNECT_RESOLVE) ? nvme_errno_to_string (errno) : strerror_l (errno, _C_LOCALE));
        nvme_free_ctrl (ctrl);
        nvme_free_tree (root);
        return FALSE;
    }
    nvme_free_ctrl (ctrl);
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
    nvme_root_t root;
    nvme_host_t host;
    nvme_subsystem_t subsys;
    nvme_ctrl_t ctrl;
    gboolean found = FALSE;

    root = nvme_scan (NULL);
    nvme_init_logging (root, -1, false, false);
    nvme_for_each_host (root, host)
        nvme_for_each_subsystem (host, subsys)
            if (g_strcmp0 (nvme_subsystem_get_nqn (subsys), subsysnqn) == 0)
                nvme_subsystem_for_each_ctrl (subsys, ctrl) {
                    int ret;

                    ret = nvme_disconnect_ctrl (ctrl);
                    if (ret != 0) {
                        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                                     "Error disconnecting the controller: %s",
                                     strerror_l (errno, _C_LOCALE));
                        nvme_free_tree (root);
                        return FALSE;
                    }
                    found = TRUE;
                }
    nvme_free_tree (root);
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
    nvme_root_t root;
    nvme_ctrl_t ctrl;
    int ret;

    root = nvme_scan (NULL);
    nvme_init_logging (root, -1, false, false);
    ctrl = nvme_scan_ctrl (root, path);
    if (!ctrl) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_NO_MATCH,
                     "Unable to match a NVMeoF controller for the specified block device %s.",
                     path);
        nvme_free_tree (root);
        return FALSE;
    }

    ret = nvme_disconnect_ctrl (ctrl);
    if (ret != 0) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Error disconnecting the controller: %s",
                     strerror_l (errno, _C_LOCALE));
        nvme_free_tree (root);
        return FALSE;
    }
    nvme_free_tree (root);

    return TRUE;
}


/**
 * bd_nvme_discovery_log_entry_free: (skip)
 * @entry: (nullable): %BDNVMEDiscoveryLogEntry to free
 *
 * Frees @entry.
 */
void bd_nvme_discovery_log_entry_free (BDNVMEDiscoveryLogEntry *entry) {
    if (entry == NULL)
        return;

    g_free (entry->transport_addr);
    g_free (entry->transport_svcid);
    g_free (entry->subsys_nqn);
    g_free (entry);
}

/**
 * bd_nvme_discovery_log_entry_copy: (skip)
 * @entry: (nullable): %BDNVMEDiscoveryLogEntry to copy
 *
 * Creates a new copy of @entry.
 */
BDNVMEDiscoveryLogEntry * bd_nvme_discovery_log_entry_copy (BDNVMEDiscoveryLogEntry *entry) {
    BDNVMEDiscoveryLogEntry *new_entry;

    if (entry == NULL)
        return NULL;

    new_entry = g_new0 (BDNVMEDiscoveryLogEntry, 1);
    memcpy (new_entry, entry, sizeof (BDNVMEDiscoveryLogEntry));
    new_entry->transport_addr = g_strdup (entry->transport_addr);
    new_entry->transport_svcid = g_strdup (entry->transport_svcid);
    new_entry->subsys_nqn = g_strdup (entry->subsys_nqn);

    return new_entry;
}

/**
 * bd_nvme_discover:
 * @discovery_ctrl: (nullable): Use existing discovery controller device or %NULL to establish a new connection.
 * @persistent: Persistent discovery connection.
 * @transport: The network fabric used for a NVMe-over-Fabrics network.
 * @transport_addr: (nullable): The network address of the Controller. For transports using IP addressing (e.g. `rdma`) this should be an IP-based address.
 * @transport_svcid: (nullable): The transport service id.  For transports using IP addressing (e.g. `rdma`) this field is the port number. By default, the IP port number for the `RDMA` transport is %4420.
 * @host_traddr: (nullable): The network address used on the host to connect to the Controller. For TCP, this sets the source address on the socket.
 * @host_iface: (nullable): The network interface used on the host to connect to the Controller (e.g. IP `eth1`, `enp2s0`). This forces the connection to be made on a specific interface instead of letting the system decide.
 * @host_nqn: (nullable): Overrides the default Host NQN that identifies the NVMe Host. If this option is %NULL, the default is read from `/etc/nvme/hostnqn` first.
 *                        If that does not exist, the autogenerated NQN value from the NVMe Host kernel module is used next. The Host NQN uniquely identifies the NVMe Host.
 * @host_id: (nullable): User-defined host UUID or %NULL to use default (as defined in `/etc/nvme/hostid`)
 * @extra: (nullable) (array zero-terminated=1): Additional arguments.
 * @error: (out) (nullable): Place to store error (if any).
 *
 * Performs Discovery request on a Discovery Controller. If no connection to a Discovery Controller
 * exists (i.e. @discovery_ctrl is %NULL) a new connection is established as specified by the @transport,
 * @transport_addr and optionally @transport_svcid arguments.
 *
 * Note that the `nvme-cli`'s `/etc/nvme/discovery.conf` config file is not used at the moment.
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
 * - `"ctrl_loss_tmo"`: The controller loss timeout period (in seconds). A special value of %-1 will cause reconnecting forever.
 * - `"fast_io_fail_tmo"`: Fast I/O Fail timeout (in seconds).
 * - `"tos"`: Type of service.
 * - `"duplicate_connect"`: Allow duplicated connections between same transport host and subsystem port. Boolean value.
 * - `"disable_sqflow"`: Disables SQ flow control to omit head doorbell update for submission queues when sending nvme completions. Boolean value.
 * - `"hdr_digest"`: Generates/verifies header digest (TCP). Boolean value.
 * - `"data_digest"`: Generates/verifies data digest (TCP). Boolean value.
 * - `"tls"`: Enable TLS encryption (TCP). Boolean value.
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
 * Returns: (transfer full) (array zero-terminated=1): null-terminated list
 *          of discovery log entries or %NULL in case of an error (with @error set).
 *
 * Tech category: %BD_NVME_TECH_FABRICS-%BD_NVME_TECH_MODE_INITIATOR
 */
BDNVMEDiscoveryLogEntry ** bd_nvme_discover (const gchar *discovery_ctrl, gboolean persistent, const gchar *transport, const gchar *transport_addr, const gchar *transport_svcid, const gchar *host_traddr, const gchar *host_iface, const gchar *host_nqn, const gchar *host_id, const BDExtraArg **extra, GError **error) {
    int ret;
    const gchar *config_file = PATH_NVMF_CONFIG;
    gchar *host_nqn_val;
    gchar *host_id_val;
    const gchar *hostkey = NULL;
    nvme_root_t root;
    nvme_host_t host;
    nvme_ctrl_t ctrl = NULL;
    struct nvme_fabrics_config cfg;
    struct nvmf_discovery_log *log = NULL;
    GPtrArray *ptr_array;
    guint64 i;

    if (discovery_ctrl && strncmp (discovery_ctrl, "/dev/", 5) != 0) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                             "Invalid discovery controller device specified");
        return NULL;
    }
    if (transport == NULL) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                             "Invalid value specified for the transport argument");
        return NULL;
    }
    if (transport_addr == NULL && !g_str_equal (transport, "loop") && !g_str_equal (transport, "pcie")) {
        g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                             "Invalid value specified for the transport address argument");
        return NULL;
    }
    /* TODO: nvme-cli defaults to parsing /etc/nvme/discovery.conf to retrieve missing arguments */

    /* parse extra arguments */
    nvmf_default_config (&cfg);
    parse_extra_args (extra, &cfg, &config_file, &hostkey, NULL);

    host_nqn_val = g_strdup (host_nqn);
    if (host_nqn_val == NULL)
        host_nqn_val = nvmf_hostnqn_from_file ();
    host_id_val = g_strdup (host_id);
    if (host_id_val == NULL)
        host_id_val = nvmf_hostid_from_file ();

    root = nvme_scan (config_file);
    g_assert (root != NULL);
    nvme_init_logging (root, -1, false, false);
    host = nvme_lookup_host (root, host_nqn_val, host_id_val);
    if (host == NULL) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Unable to lookup host for nqn '%s' and id '%s'",
                     host_nqn_val, host_id_val);
        g_free (host_nqn_val);
        g_free (host_id_val);
        nvme_free_tree (root);
        return NULL;
    }
    g_free (host_nqn_val);
    g_free (host_id_val);
    if (hostkey)
        nvme_host_set_dhchap_key (host, hostkey);

    if (persistent && !cfg.keep_alive_tmo)
        cfg.keep_alive_tmo = 30;

    /* check the supplied discovery controller validity */
    if (discovery_ctrl) {
        ctrl = nvme_scan_ctrl (root, discovery_ctrl + 5);
        if (!ctrl) {
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_NO_MATCH,
                         "Couldn't access the discovery controller device specified: %s",
                         strerror_l (errno, _C_LOCALE));
            nvme_free_tree (root);
            return NULL;
        }
        if (g_strcmp0 (nvme_ctrl_get_subsysnqn (ctrl), NVME_DISC_SUBSYS_NAME) != 0 ||
            g_strcmp0 (nvme_ctrl_get_transport (ctrl), transport) != 0 ||
            (transport_addr && g_strcmp0 (nvme_ctrl_get_traddr (ctrl), transport_addr) != 0) ||
            (host_traddr && g_strcmp0 (nvme_ctrl_get_host_traddr (ctrl), host_traddr) != 0) ||
            (host_iface && g_strcmp0 (nvme_ctrl_get_host_iface (ctrl), host_iface) != 0) ||
            (transport_svcid && g_strcmp0 (nvme_ctrl_get_trsvcid (ctrl), transport_svcid) != 0)) {
            g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_NO_MATCH,
                                 "The existing discovery controller device specified doesn't match the specified transport arguments");
            nvme_free_tree (root);
            return NULL;
        }
        /* existing discovery controllers need to be persistent */
        persistent = TRUE;
    }
    if (!ctrl) {
        ctrl = nvme_create_ctrl (root, NVME_DISC_SUBSYS_NAME, transport, transport_addr, host_traddr, host_iface, transport_svcid);
        if (ctrl == NULL) {
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                         "Error creating the controller: %s",
                         strerror_l (errno, _C_LOCALE));
            nvme_free_tree (root);
            return NULL;
        }
        nvme_ctrl_set_discovery_ctrl (ctrl, TRUE);
        ret = nvmf_add_ctrl (host, ctrl, &cfg);
        if (ret != 0) {
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                         "Error connecting the controller: %s",
                         (errno >= ENVME_CONNECT_RESOLVE) ? nvme_errno_to_string (errno) : strerror_l (errno, _C_LOCALE));
            nvme_free_ctrl (ctrl);
            nvme_free_tree (root);
            return NULL;
        }
    }

    /* connected, perform actual discovery */
    ret = nvmf_get_discovery_log (ctrl, &log, MAX_DISC_RETRIES);
    if (ret != 0) {
        _nvme_status_to_error (ret, TRUE, error);
        g_prefix_error (error, "NVMe Get Log Page - Discovery Log Page command error: ");
        if (!persistent)
            nvme_disconnect_ctrl (ctrl);
        nvme_free_ctrl (ctrl);
        nvme_free_tree (root);
        return NULL;
    }

    ptr_array = g_ptr_array_new ();
    for (i = 0; i < GUINT64_FROM_LE (log->numrec); i++) {
        BDNVMEDiscoveryLogEntry *entry;
        gchar *s;

        entry = g_new0 (BDNVMEDiscoveryLogEntry, 1);
        switch (log->entries[i].trtype) {
            case NVMF_TRTYPE_RDMA:
                entry->transport_type = BD_NVME_TRANSPORT_TYPE_RDMA;
                break;
            case NVMF_TRTYPE_FC:
                entry->transport_type = BD_NVME_TRANSPORT_TYPE_FC;
                break;
            case NVMF_TRTYPE_TCP:
                entry->transport_type = BD_NVME_TRANSPORT_TYPE_TCP;
                break;
            case NVMF_TRTYPE_LOOP:
                entry->transport_type = BD_NVME_TRANSPORT_TYPE_LOOP;
                break;
            case NVMF_TRTYPE_UNSPECIFIED:
            default:
                entry->transport_type = BD_NVME_TRANSPORT_TYPE_UNSPECIFIED;
        }
        switch (log->entries[i].adrfam) {
            case NVMF_ADDR_FAMILY_PCI:
                entry->address_family = BD_NVME_ADDRESS_FAMILY_PCI;
                break;
            case NVMF_ADDR_FAMILY_IP4:
                entry->address_family = BD_NVME_ADDRESS_FAMILY_INET;
                break;
            case NVMF_ADDR_FAMILY_IP6:
                entry->address_family = BD_NVME_ADDRESS_FAMILY_INET6;
                break;
            case NVMF_ADDR_FAMILY_IB:
                entry->address_family = BD_NVME_ADDRESS_FAMILY_IB;
                break;
            case NVMF_ADDR_FAMILY_FC:
                entry->address_family = BD_NVME_ADDRESS_FAMILY_FC;
                break;
            case NVMF_ADDR_FAMILY_LOOP:
                entry->address_family = BD_NVME_ADDRESS_FAMILY_LOOP;
                break;
        }
        entry->sq_flow_control_disable = (log->entries[i].treq & NVMF_TREQ_DISABLE_SQFLOW) == NVMF_TREQ_DISABLE_SQFLOW;
        entry->sq_flow_control_required = (log->entries[i].treq & NVMF_TREQ_REQUIRED) == NVMF_TREQ_REQUIRED;
        entry->port_id = GUINT16_FROM_LE (log->entries[i].portid);
        entry->ctrl_id = GUINT16_FROM_LE (log->entries[i].cntlid);
        s = g_strndup (log->entries[i].trsvcid, NVMF_TRSVCID_SIZE);
        entry->transport_svcid = g_strdup (g_strstrip (s));
        g_free (s);
        s = g_strndup (log->entries[i].traddr, NVMF_TRADDR_SIZE);
        entry->transport_addr = g_strdup (g_strstrip (s));
        g_free (s);
        s = g_strndup (log->entries[i].subnqn, NVME_NQN_LENGTH);
        entry->subsys_nqn = g_strdup (g_strstrip (s));
        g_free (s);

        if (entry->transport_type == BD_NVME_TRANSPORT_TYPE_RDMA) {
            /* TODO: expose any of the struct nvmf_disc_log_entry.tsas.rdma attributes? */
        }

        if (entry->transport_type == BD_NVME_TRANSPORT_TYPE_TCP)
            switch (log->entries[i].tsas.tcp.sectype) {
                case NVMF_TCP_SECTYPE_NONE:
                    entry->tcp_security = BD_NVME_TCP_SECURITY_NONE;
                    break;
                case NVMF_TCP_SECTYPE_TLS:
                    entry->tcp_security = BD_NVME_TCP_SECURITY_TLS12;
                    break;
                case NVMF_TCP_SECTYPE_TLS13:
                    entry->tcp_security = BD_NVME_TCP_SECURITY_TLS13;
                    break;
            }

        g_ptr_array_add (ptr_array, entry);
    }
    g_free (log);
    g_ptr_array_add (ptr_array, NULL);  /* trailing NULL element */

    if (!persistent)
        nvme_disconnect_ctrl (ctrl);
    nvme_free_ctrl (ctrl);
    nvme_free_tree (root);

    return (BDNVMEDiscoveryLogEntry **) g_ptr_array_free (ptr_array, FALSE);
}
