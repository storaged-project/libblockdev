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

#include <blockdev/utils.h>
#include <check_deps.h>
#include "nvme.h"
#include "nvme-private.h"


/**
 * bd_nvme_error_quark: (skip)
 */
GQuark bd_nvme_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-nvme-error-quark");
}

void _nvme_status_to_error (gint status, gboolean fabrics, GError **error)
{
    gint code;

    if (error == NULL)
        return;
    if (status == 0) {
        g_clear_error (error);
        return;
    }

    if (status < 0) {
        /* generic errno errors */
        switch (errno) {
            case EWOULDBLOCK:
                code = BD_NVME_ERROR_BUSY;
                break;
            default:
                code = BD_NVME_ERROR_FAILED;
        }
        g_set_error_literal (error, BD_NVME_ERROR, code,
                             strerror_l (errno, _C_LOCALE));
        return;
    } else {
        /* NVMe status codes */
        switch (nvme_status_code_type (status)) {
            case NVME_SCT_GENERIC:
                code = BD_NVME_ERROR_SC_GENERIC;
                break;
            case NVME_SCT_CMD_SPECIFIC:
                code = BD_NVME_ERROR_SC_CMD_SPECIFIC;
                break;
            case NVME_SCT_MEDIA:
                code = BD_NVME_ERROR_SC_MEDIA;
                break;
            case NVME_SCT_PATH:
                code = BD_NVME_ERROR_SC_PATH;
                break;
            case NVME_SCT_VS:
                code = BD_NVME_ERROR_SC_VENDOR_SPECIFIC;
                break;
            default:
                code = BD_NVME_ERROR_SC_GENERIC;
        }
        g_set_error_literal (error, BD_NVME_ERROR, code, nvme_status_to_string (status, fabrics));
        return;
    }
    g_warn_if_reached ();
}

void _nvme_fabrics_errno_to_gerror (int result, int _errno, GError **error)
{
    gint code;

    if (error == NULL)
        return;
    if (result == 0) {
        g_clear_error (error);
        return;
    }

    if (_errno >= ENVME_CONNECT_RESOLVE) {
        switch (_errno) {
            case ENVME_CONNECT_ADDRFAM:
            case ENVME_CONNECT_TRADDR:
            case ENVME_CONNECT_TARG:
            case ENVME_CONNECT_AARG:
            case ENVME_CONNECT_INVAL_TR:
                code = BD_NVME_ERROR_INVALID_ARGUMENT;
                break;
            case ENVME_CONNECT_RESOLVE:
            case ENVME_CONNECT_OPEN:
            case ENVME_CONNECT_WRITE:
            case ENVME_CONNECT_READ:
            case ENVME_CONNECT_PARSE:
            case ENVME_CONNECT_LOOKUP_SUBSYS_NAME:
            case ENVME_CONNECT_LOOKUP_SUBSYS:
                code = BD_NVME_ERROR_CONNECT;
                break;
            case ENVME_CONNECT_ALREADY:
                code = BD_NVME_ERROR_CONNECT_ALREADY;
                break;
            case ENVME_CONNECT_INVAL:
                code = BD_NVME_ERROR_CONNECT_INVALID;
                break;
            case ENVME_CONNECT_ADDRINUSE:
                code = BD_NVME_ERROR_CONNECT_ADDRINUSE;
                break;
            case ENVME_CONNECT_NODEV:
                code = BD_NVME_ERROR_CONNECT_NODEV;
                break;
            case ENVME_CONNECT_OPNOTSUPP:
                code = BD_NVME_ERROR_CONNECT_OPNOTSUPP;
                break;
            case ENVME_CONNECT_CONNREFUSED:
                code = BD_NVME_ERROR_CONNECT_REFUSED;
                break;
            default:
                code = BD_NVME_ERROR_CONNECT;
        }
        g_set_error_literal (error, BD_NVME_ERROR, code, nvme_errno_to_string (_errno));
        return;
    } else {
        switch (errno) {
            case EWOULDBLOCK:
                code = BD_NVME_ERROR_BUSY;
                break;
            default:
                code = BD_NVME_ERROR_FAILED;
        }
        g_set_error_literal (error, BD_NVME_ERROR, code, strerror_l (errno, _C_LOCALE));
        return;
    }
    g_warn_if_reached ();
}
