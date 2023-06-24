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
 * SECTION: nvme
 * @short_description: plugin for NVMe device reporting and management
 * @title: NVMe
 * @include: nvme.h
 *
 * A plugin for NVMe device reporting and management, based around libnvme.
 */


/**
 * bd_nvme_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_nvme_init (void) {
    /* nothing to do here */
    return TRUE;
};

/**
 * bd_nvme_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_nvme_close (void) {
    /* nothing to do here */
}

/**
 * bd_nvme_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDNVMETechMode) for @tech
 * @error: (out) (nullable): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_nvme_is_tech_avail (BDNVMETech tech, G_GNUC_UNUSED guint64 mode, GError **error) {
    switch (tech) {
        case BD_NVME_TECH_NVME:
            return TRUE;
        case BD_NVME_TECH_FABRICS:
            return TRUE;
        default:
            g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_TECH_UNAVAIL, "Unknown technology");
            return FALSE;
    }
}
