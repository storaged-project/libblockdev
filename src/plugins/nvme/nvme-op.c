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
