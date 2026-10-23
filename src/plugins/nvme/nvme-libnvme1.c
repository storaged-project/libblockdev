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

/* libnvme-1.x backend, see nvme-backend.h */

#include <glib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>

#include <libnvme.h>

#include <blockdev/utils.h>
#include "nvme.h"
#include "nvme-private.h"
#include "nvme-backend.h"


/* nvme-cli defaults */
#define PATH_NVMF_CONFIG  "/etc/nvme/config.json"

struct _NVMeDev {
    int fd;
};

/* libnvme-1.x returns -1 with errno set on failure */
static int _ret (int ret) {
    return ret < 0 ? -errno : ret;
}

NVMeDev *_nvme_be_open (const gchar *device, GError **error) {
    NVMeDev *dev;
    int fd;

    fd = open (device, O_RDONLY);
    if (fd < 0) {
        _nvme_status_to_error (-errno, FALSE, error);
        g_prefix_error (error, "Failed to open device '%s': ", device);
        return NULL;
    }

    dev = g_new0 (NVMeDev, 1);
    dev->fd = fd;
    return dev;
}

void _nvme_be_close (NVMeDev *dev) {
    close (dev->fd);
    g_free (dev);
}

int _nvme_be_get_nsid (NVMeDev *dev, __u32 *nsid) {
    return _ret (nvme_get_nsid (dev->fd, nsid));
}

int _nvme_be_identify_ctrl (NVMeDev *dev, struct nvme_id_ctrl *ctrl_id) {
    return _ret (nvme_identify_ctrl (dev->fd, ctrl_id));
}

int _nvme_be_identify_ns (NVMeDev *dev, __u32 nsid, struct nvme_id_ns *ns_info) {
    return _ret (nvme_identify_ns (dev->fd, nsid, ns_info));
}

int _nvme_be_identify_ns_descs (NVMeDev *dev, __u32 nsid, struct nvme_ns_id_desc *descs) {
    return _ret (nvme_identify_ns_descs (dev->fd, nsid, descs));
}

int _nvme_be_identify_independent_ns (NVMeDev *dev, __u32 nsid, struct nvme_id_independent_id_ns *ns_info) {
    return _ret (nvme_identify_independent_identify_ns (dev->fd, nsid, ns_info));
}

int _nvme_be_get_log_smart (NVMeDev *dev, struct nvme_smart_log *smart_log) {
    return _ret (nvme_get_log_smart (dev->fd, NVME_NSID_ALL, FALSE /* rae */, smart_log));
}

int _nvme_be_get_log_error (NVMeDev *dev, guint entries, struct nvme_error_log_page *err_log) {
    return _ret (nvme_get_log_error (dev->fd, entries, FALSE /* rae */, err_log));
}

int _nvme_be_get_log_self_test (NVMeDev *dev, struct nvme_self_test_log *self_test_log) {
    return _ret (nvme_get_log_device_self_test (dev->fd, self_test_log));
}

int _nvme_be_get_log_sanitize (NVMeDev *dev, struct nvme_sanitize_log_page *sanitize_log) {
    return _ret (nvme_get_log_sanitize (dev->fd, FALSE /* rae */, sanitize_log));
}

int _nvme_be_self_test (NVMeDev *dev, __u32 nsid, __u8 stc) {
    struct nvme_dev_self_test_args args = {
        .args_size = sizeof(args),
        .fd = dev->fd,
        .result = NULL,
        .timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
        .nsid = nsid,
        .stc = stc,
    };

    return _ret (nvme_dev_self_test (&args));
}

int _nvme_be_format (NVMeDev *dev, __u32 nsid, __u8 lbaf, __u8 ses) {
    struct nvme_format_nvm_args args = {
        .args_size = sizeof(args),
        .fd = dev->fd,
        .result = NULL,
        .timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
        .nsid = nsid,
        .mset = NVME_FORMAT_MSET_SEPARATE /* 0 */,
        .pi = NVME_FORMAT_PI_DISABLE /* 0 */,
        .pil = NVME_FORMAT_PIL_LAST /* 0 */,
        .ses = ses,
        .lbaf = lbaf,
    };

    return _ret (nvme_format_nvm (&args));
}

int _nvme_be_sanitize (NVMeDev *dev, __u8 sanact, gboolean ause, __u8 owpass, gboolean oipbp, gboolean nodas, __u32 ovrpat) {
    struct nvme_sanitize_nvm_args args = {
        .args_size = sizeof(args),
        .fd = dev->fd,
        .result = NULL,
        .timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
        .sanact = sanact,
        .ause = ause,
        .owpass = owpass,
        .oipbp = oipbp,
        .nodas = nodas,
        .ovrpat = ovrpat,
    };

    return _ret (nvme_sanitize_nvm (&args));
}

int _nvme_be_rescan_ns (NVMeDev *dev) {
    return _ret (ioctl (dev->fd, NVME_IOCTL_RESCAN));
}

int _nvme_be_update_block_size (NVMeDev *dev, int block_size) {
    return _ret (ioctl (dev->fd, BLKBSZSET, &block_size));
}

int _nvme_be_reread_partitions (NVMeDev *dev) {
    return _ret (ioctl (dev->fd, BLKRRPART));
}

const gchar *_nvme_be_status_to_string (int status, gboolean fabrics) {
    return nvme_status_to_string (status, fabrics);
}

const gchar *_nvme_be_fabrics_errno_to_string (int err) {
    return nvme_errno_to_string (err);
}

gchar *_nvme_be_uuid_to_string (unsigned char uuid[NVME_UUID_LEN]) {
    gchar uuid_buf[NVME_UUID_LEN_STRING] = ZERO_INIT;

    if (nvme_uuid_to_string (uuid, uuid_buf) == 0)
        return g_strdup (uuid_buf);
    return NULL;
}

gchar *_nvme_be_read_hostnqn (void) {
    return nvmf_hostnqn_from_file ();
}

gchar *_nvme_be_read_hostid (void) {
    return nvmf_hostid_from_file ();
}

gchar *_nvme_be_generate_hostnqn (void) {
    return nvmf_hostnqn_generate ();
}

static void _add_sysfs_dir (GPtrArray *ptr_array, const char *dir) {
    if (dir)
        g_ptr_array_add (ptr_array, g_strdup (dir));
}

NVMeTopoSubsys **_nvme_be_scan_topology (void) {
    GPtrArray *subsystems;
    nvme_root_t root;
    nvme_host_t h;
    nvme_subsystem_t s;
    nvme_ctrl_t c;
    nvme_ns_t n;

    subsystems = g_ptr_array_new ();

    root = nvme_scan (NULL);
    g_warn_if_fail (root != NULL);
    if (root == NULL) {
        g_ptr_array_add (subsystems, NULL);
        return (NVMeTopoSubsys **) g_ptr_array_free (subsystems, FALSE);
    }

    nvme_for_each_host (root, h)
        nvme_for_each_subsystem (h, s) {
            NVMeTopoSubsys *subsys;
            GPtrArray *ctrls;
            GPtrArray *nss;

            subsys = g_new0 (NVMeTopoSubsys, 1);
            subsys->host_nqn = g_strdup (nvme_host_get_hostnqn (h));
            subsys->host_id = g_strdup (nvme_host_get_hostid (h));
            subsys->subsysnqn = g_strdup (nvme_subsystem_get_nqn (s));

            nss = g_ptr_array_new ();
            nvme_subsystem_for_each_ns (s, n)
                _add_sysfs_dir (nss, nvme_ns_get_sysfs_dir (n));
            g_ptr_array_add (nss, NULL);
            subsys->ns_sysfs_dirs = (gchar **) g_ptr_array_free (nss, FALSE);

            ctrls = g_ptr_array_new ();
            nvme_subsystem_for_each_ctrl (s, c) {
                NVMeTopoCtrl *ctrl;

                ctrl = g_new0 (NVMeTopoCtrl, 1);
                ctrl->name = g_strdup (nvme_ctrl_get_name (c));
                ctrl->sysfs_dir = g_strdup (nvme_ctrl_get_sysfs_dir (c));
                nss = g_ptr_array_new ();
                nvme_ctrl_for_each_ns (c, n)
                    _add_sysfs_dir (nss, nvme_ns_get_sysfs_dir (n));
                g_ptr_array_add (nss, NULL);
                ctrl->ns_sysfs_dirs = (gchar **) g_ptr_array_free (nss, FALSE);
                g_ptr_array_add (ctrls, ctrl);
            }
            g_ptr_array_add (ctrls, NULL);
            subsys->ctrls = (NVMeTopoCtrl **) g_ptr_array_free (ctrls, FALSE);

            g_ptr_array_add (subsystems, subsys);
        }
    nvme_free_tree (root);

    g_ptr_array_add (subsystems, NULL);
    return (NVMeTopoSubsys **) g_ptr_array_free (subsystems, FALSE);
}

gboolean _nvme_be_connect (const NVMeConnectParams *params, GError **error) {
    int ret;
    const gchar *config_file;
    gchar *host_nqn_val;
    gchar *host_id_val;
    nvme_root_t root;
    nvme_host_t host;
    nvme_ctrl_t ctrl;
    struct nvme_fabrics_config cfg;

    /* HostNQN checks */
    host_nqn_val = g_strdup (params->host_nqn);
    host_id_val = g_strdup (params->host_id);
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
        return FALSE;
    }

    /* apply extra arguments */
    nvmf_default_config (&cfg);
#define APPLY_OPT(opt) { \
        if (params->opt.set) \
            cfg.opt = params->opt.val; \
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
#ifdef HAVE_LIBNVME_1_4
    if (params->keyring) {
        int keyring;

        keyring = nvme_lookup_keyring (params->keyring);
        if (keyring) {
            cfg.keyring = keyring;
            nvme_set_keyring (cfg.keyring);
        }
    }
    if (params->tls_key) {
        int key;

        key = nvme_lookup_key ("psk", params->tls_key);
        if (key)
            cfg.tls_key = key;
    }
#endif

    config_file = NULL;
    if (params->use_config)
        config_file = params->config_file ? params->config_file : PATH_NVMF_CONFIG;

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
    if (params->hostkey)
        nvme_host_set_dhchap_key (host, params->hostkey);
    if (params->hostsymname)
        nvme_host_set_hostsymname (host, params->hostsymname);

    ctrl = nvme_create_ctrl (root, params->subsysnqn, params->transport, params->transport_addr,
                             params->host_traddr, params->host_iface, params->transport_svcid);
    if (ctrl == NULL) {
        _nvme_fabrics_errno_to_gerror (-errno, error);
        g_prefix_error (error, "Error creating the controller: ");
        nvme_free_tree (root);
        return FALSE;
    }
    if (params->ctrlkey)
        nvme_ctrl_set_dhchap_key (ctrl, params->ctrlkey);

    ret = nvmf_add_ctrl (host, ctrl, &cfg);
    if (ret != 0) {
        _nvme_fabrics_errno_to_gerror (-errno, error);
        g_prefix_error (error, "Error connecting the controller: ");
        nvme_free_ctrl (ctrl);
        nvme_free_tree (root);
        return FALSE;
    }
    nvme_free_ctrl (ctrl);
    nvme_free_tree (root);

    return TRUE;
}

gboolean _nvme_be_disconnect (const gchar *subsysnqn, const gchar *name, const gchar *owner G_GNUC_UNUSED, gboolean force G_GNUC_UNUSED,
                              gboolean *found, gboolean *disconnected, gchar **skipped_owner G_GNUC_UNUSED, GError **error) {
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
                    if (!name || g_strcmp0 (nvme_ctrl_get_name (ctrl), name) == 0) {
                        ret = nvme_disconnect_ctrl (ctrl);
                        if (ret != 0) {
                            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                                         "Error disconnecting the controller: %s",
                                         strerror_l (errno, _C_LOCALE));
                            nvme_free_tree (root);
                            return FALSE;
                        }
                        *found = TRUE;
                        *disconnected = TRUE;
                    }
    nvme_free_tree (root);
    return TRUE;
}
