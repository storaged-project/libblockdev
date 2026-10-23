/*
 * Copyright (C) 2026 SUSE LLC
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
 * Author: Daniel Wagner <daniel.wagner@suse.com>
 */

/*
 * NVMe specification types for the libnvme-1.x backend.
 *
 * A few constants have been renamed to the NVMe specification names in libnvme3, while
 * newer libnvme-1.x releases use some of the new names for accessor macros. The generic
 * plugin code uses the BE_ prefixed names defined by each backend header instead.
 */

#include <libnvme.h>

#ifndef BD_NVME_LIBNVME1
#define BD_NVME_LIBNVME1

#define BE_NVME_CTRL_MEC_TWPME                     NVME_CTRL_MEC_SMBUSME
#define BE_NVME_CTRL_OACS_FNVMS                    NVME_CTRL_OACS_FORMAT
#define BE_NVME_CTRL_OACS_NMS                      NVME_CTRL_OACS_NS_MGMT
#define BE_NVME_CTRL_OACS_DSTS                     NVME_CTRL_OACS_SELF_TEST
#define BE_NVME_SANITIZE_SSTAT_STATUS_IN_PROGRESS  NVME_SANITIZE_SSTAT_STATUS_IN_PROGESS

#endif  /* BD_NVME_LIBNVME1 */
