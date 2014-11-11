/*
 * Copyright (C) 2014  Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <glib.h>
#include <exec.h>
#include <sizes.h>

#include "mdraid.h"

/**
 * SECTION: mdraid
 * @short_description: libblockdev plugin for basic operations with MD RAID
 * @title: MD RAID
 * @include: mdraid.h
 *
 * A libblockdev plugin for basic operations with MD RAID. Also sizes are in
 * bytes unless specified otherwise.
 */

GQuark bd_md_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-md-error-quark");
}

/**
 * bd_md_get_superblock_size:
 * @size: size of the array
 * @version: (allow-none): metadata version or %NULL to use the current default version
 *
 * Returns: Calculated superblock size for given array @size and metadata @version
 * or default if unsupported @version is used.
 */
guint64 bd_md_get_superblock_size (guint64 size, gchar *version) {
    guint64 headroom = MD_SUPERBLOCK_SIZE;
    guint64 min_headroom = (1 MiB);

    /* mdadm 3.2.4 made a major change in the amount of space used for 1.1 and
     * 1.2 in order to reserve space for reshaping. See commit 508a7f16 in the
     * upstream mdadm repository. */
    if (!version || (g_strcmp0 (version, "1.1") == 0) ||
        (g_strcmp0 (version, "1.2") == 0) || (g_strcmp0 (version, "default") == 0)) {
        /* MDADM: We try to leave 0.1% at the start for reshape
         * MDADM: operations, but limit this to 128Meg (0.1% of 10Gig)
         * MDADM: which is plenty for efficient reshapes
         * NOTE: In the mdadm code this is in 512b sectors. Converted to use MiB */
        headroom = (128 MiB);
        while (((headroom << 10) > size) && (headroom > min_headroom))
            headroom >>= 1;
    }

    return headroom;
}
