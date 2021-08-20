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


/**
 * bd_nvme_device_self_test:
 * @device: a NVMe controller or namespace device (e.g. /dev/nvme0)
 * @action: self-test action to take.
 * @error: (out) (nullable): place to store error (if any)
 *
 * Initiates or aborts the Device Self-test operation on the controller or a namespace,
 * distinguished by the @device path specified. In case a controller device
 * is specified then the self-test operation would include all active namespaces.
 *
 * To abort a running operation, pass #BD_NVME_SELF_TEST_ACTION_ABORT as @action.
 * To retrieve progress of a current running operation, check the self-test log using
 * bd_nvme_get_self_test_log().
 *
 * Returns: %TRUE if the device self-test command was issued successfully,
 *          %FALSE otherwise with @error set.
 *
 * Tech category: %BD_NVME_TECH_NVME-%BD_NVME_TECH_MODE_MANAGE
 */
gboolean bd_nvme_device_self_test (const gchar *device, BDNVMESelfTestAction action, GError **error) {
    int ret;
    struct nvme_dev_self_test_args args = {
        .args_size = sizeof(args),
        .result = NULL,
        .timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
        .nsid = 0xffffffff,
    };

    switch (action) {
        case BD_NVME_SELF_TEST_ACTION_SHORT:
            args.stc = NVME_DST_STC_SHORT;
            break;
        case BD_NVME_SELF_TEST_ACTION_EXTENDED:
            args.stc = NVME_DST_STC_LONG;
            break;
        case BD_NVME_SELF_TEST_ACTION_VENDOR_SPECIFIC:
            args.stc = NVME_DST_STC_VS;
            break;
        case BD_NVME_SELF_TEST_ACTION_ABORT:
            args.stc = NVME_DST_STC_ABORT;
            break;
        default:
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                         "Invalid value specified for the self-test action: %d", action);
            return FALSE;
    }

    /* open the block device */
    args.fd = _open_dev (device, error);
    if (args.fd < 0)
        return FALSE;

    /* get Namespace Identifier (NSID) for the @device (NVME_IOCTL_ID) */
    ret = nvme_get_nsid (args.fd, &args.nsid);
    if (ret < 0 && errno == ENOTTY)
        /* not a block device, assuming controller character device */
        args.nsid = 0xffffffff;
    else if (ret < 0) {
        /* generic errno errors */
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Error getting Namespace Identifier (NSID): %s", strerror_l (-ret, _C_LOCALE));
        close (args.fd);
        return FALSE;
    }
    if (ret > 0) {
        /* NVMe status codes */
        _nvme_status_to_error (ret, FALSE, error);
        g_prefix_error (error, "Error getting Namespace Identifier (NSID): ");
        close (args.fd);
        return FALSE;
    }

    ret = nvme_dev_self_test (&args);
    if (ret < 0) {
        /* generic errno errors */
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "NVMe Device Self-test command error: %s", strerror_l (errno, _C_LOCALE));
        close (args.fd);
        return FALSE;
    }
    close (args.fd);
    if (ret > 0) {
        /* NVMe status codes */
        _nvme_status_to_error (ret, FALSE, error);
        g_prefix_error (error, "NVMe Device Self-test command error: ");
        return FALSE;
    }

    return TRUE;
}


/* returns 0xff in case of error (the NVMe standard defines total of 16 flba records) */
static __u8 find_lbaf_for_size (int fd, __u32 nsid, guint16 lba_data_size, GError **error) {
    int ret;
    struct nvme_id_ns ns_info = ZERO_INIT;
    __u8 flbas = 0;
    guint i;

    /* TODO: find first attached namespace instead of hardcoding NSID = 1 */
    ret = nvme_identify_ns (fd, nsid == 0xffffffff ? 1 : nsid, &ns_info);
    if (ret < 0) {
        /* generic errno errors */
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "NVMe Identify Namespace command error: %s", strerror_l (errno, _C_LOCALE));
        return 0xff;
    }
    if (ret > 0) {
        /* NVMe status codes */
        _nvme_status_to_error (ret, FALSE, error);
        g_prefix_error (error, "NVMe Identify Namespace command error: ");
        return 0xff;
    }

    /* return currently used lbaf */
    if (lba_data_size == 0) {
       nvme_id_ns_flbas_to_lbaf_inuse (ns_info.flbas, &flbas);
       return flbas;
    }

    for (i = 0; i < ns_info.nlbaf; i++)
        if (1UL << ns_info.lbaf[i].ds == lba_data_size && ns_info.lbaf[i].ms == 0)
            return i;

    g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_INVALID_ARGUMENT,
                         "Couldn't match desired LBA data block size in a device supported LBA format data sizes");
    return 0xff;
}

/**
 * bd_nvme_format:
 * @device: NVMe namespace or controller device to format (e.g. /dev/nvme0n1)
 * @lba_data_size: desired LBA data size (i.e. a sector size) in bytes or %0 to keep current. See #BDNVMELBAFormat and bd_nvme_get_namespace_info().
 * @secure_erase: optional secure erase action to take.
 * @error: (out) (nullable): place to store error (if any)
 *
 * Performs low level format of the NVM media, destroying all data and metadata for either
 * a specific namespace or all attached namespaces to the controller. Use this command
 * to change LBA sector size. Optional secure erase method can be specified as well.
 *
 * Supported LBA data sizes for a given namespace can be listed using the bd_nvme_get_namespace_info()
 * call. In case of a special value %0 the current LBA format for a given namespace will be
 * retained. When called on a controller device the first namespace is used as a reference.
 *
 * Note that the NVMe controller may define a Format NVM attribute indicating that the format
 * operation would apply to all namespaces and a format (excluding secure erase) of any
 * namespace results in a format of all namespaces in the NVM subsystem. In such case and
 * when @device is a namespace block device the #BD_NVME_ERROR_WOULD_FORMAT_ALL_NS error
 * is returned to prevent further damage. This is then supposed to be handled by the caller
 * and bd_nvme_format() is supposed to be called on a controller device instead.
 *
 * This call blocks until the format operation has finished. To retrieve progress
 * of a current running operation, check the namespace info using bd_nvme_get_namespace_info().
 *
 * Returns: %TRUE if the format command finished successfully, %FALSE otherwise with @error set.
 *
 * Tech category: %BD_NVME_TECH_NVME-%BD_NVME_TECH_MODE_MANAGE
 */
gboolean bd_nvme_format (const gchar *device, guint16 lba_data_size, BDNVMEFormatSecureErase secure_erase, GError **error) {
    int ret;
    gboolean ctrl_device = FALSE;
    struct nvme_id_ctrl ctrl_id = ZERO_INIT;
    struct nvme_format_nvm_args args = {
        .args_size = sizeof(args),
        .result = NULL,
        .timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
        .nsid = 0xffffffff,
        .mset = NVME_FORMAT_MSET_SEPARATE /* 0 */,
        .pi = NVME_FORMAT_PI_DISABLE /* 0 */,
        .pil = NVME_FORMAT_PIL_LAST /* 0 */,
        .ses = NVME_FORMAT_SES_NONE,
    };

    /* open the block device */
    args.fd = _open_dev (device, error);
    if (args.fd < 0)
        return FALSE;

    ret = nvme_get_nsid (args.fd, &args.nsid);
    if (ret < 0 && errno == ENOTTY) {
        /* not a block device, assuming controller character device */
        args.nsid = 0xffffffff;
        ctrl_device = TRUE;
    } else if (ret < 0) {
        /* generic errno errors */
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Error getting Namespace Identifier (NSID): %s", strerror_l (-ret, _C_LOCALE));
        close (args.fd);
        return FALSE;
    }
    if (ret > 0) {
        /* NVMe status codes */
        _nvme_status_to_error (ret, FALSE, error);
        g_prefix_error (error, "Error getting Namespace Identifier (NSID): ");
        close (args.fd);
        return FALSE;
    }

    /* check the FNA controller bit when formatting a single namespace */
    if (! ctrl_device) {
        ret = nvme_identify_ctrl (args.fd, &ctrl_id);
        if (ret < 0) {
            /* generic errno errors */
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                         "NVMe Identify Controller command error: %s", strerror_l (-ret, _C_LOCALE));
            close (args.fd);
            return FALSE;
        }
        if (ret > 0) {
            /* NVMe status codes */
            _nvme_status_to_error (ret, FALSE, error);
            g_prefix_error (error, "NVMe Identify Controller command error: ");
            close (args.fd);
            return FALSE;
        }
        /* from nvme-cli:
         * FNA bit 0 set to 1: all namespaces ... shall be configured with the same
         * attributes and a format (excluding secure erase) of any namespace results in a
         * format of all namespaces.
         */
        if ((ctrl_id.fna & NVME_CTRL_FNA_FMT_ALL_NAMESPACES) == NVME_CTRL_FNA_FMT_ALL_NAMESPACES) {
            /* tell user that it would format other namespaces and that bd_nvme_format()
             * should be called on a controller device instead */
            g_set_error_literal (error, BD_NVME_ERROR, BD_NVME_ERROR_WOULD_FORMAT_ALL_NS,
                         "The NVMe controller indicates it would format all namespaces.");
            close (args.fd);
            return FALSE;
        }
    }

    /* find out the desired LBA data format index */
    args.lbaf = find_lbaf_for_size (args.fd, args.nsid, lba_data_size, error);
    if (args.lbaf == 0xff) {
        close (args.fd);
        return FALSE;
    }

    switch (secure_erase) {
        case BD_NVME_FORMAT_SECURE_ERASE_USER_DATA:
            args.ses = NVME_FORMAT_SES_USER_DATA_ERASE;
            break;
        case BD_NVME_FORMAT_SECURE_ERASE_CRYPTO:
            args.ses = NVME_FORMAT_SES_CRYPTO_ERASE;
            break;
        case BD_NVME_FORMAT_SECURE_ERASE_NONE:
        default:
            args.ses = NVME_FORMAT_SES_NONE;
    }

    ret = nvme_format_nvm (&args);
    if (ret < 0) {
        /* generic errno errors */
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Format NVM command error: %s", strerror_l (errno, _C_LOCALE));
        close (args.fd);
        return FALSE;
    }
    if (ret > 0) {
        /* NVMe status codes */
        _nvme_status_to_error (ret, FALSE, error);
        g_prefix_error (error, "Format NVM command error: ");
        close (args.fd);
        return FALSE;
    }

    /* rescan the namespaces if block size has changed */
    if (ctrl_device) {
        if (ioctl (args.fd, NVME_IOCTL_RESCAN) < 0) {
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                         "Failed to rescan namespaces after format: %s", strerror_l (errno, _C_LOCALE));
            close (args.fd);
            return FALSE;
        }
    } else {
        if (lba_data_size != 0) {
            /* from nvme-cli:
             * If block size has been changed by the format command up there, we should notify it to
             * kernel blkdev to update its own block size to the given one because blkdev will not
             * update by itself without re-opening fd.
             */
            int block_size = lba_data_size;

            if (ioctl (args.fd, BLKBSZSET, &block_size) < 0) {
                g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                             "Failed to set block size to %d after format: %s", block_size, strerror_l (errno, _C_LOCALE));
                close (args.fd);
                return FALSE;
            }

            if (ioctl (args.fd, BLKRRPART) < 0) {
                g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                             "Failed to re-read partition table after format: %s", strerror_l (errno, _C_LOCALE));
                close (args.fd);
                return FALSE;
            }
        }
    }

    close (args.fd);
    return TRUE;
}
