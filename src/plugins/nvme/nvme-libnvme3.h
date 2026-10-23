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
 * NVMe specification types for the libnvme3 backend.
 *
 * See nvme-libnvme1.h for the BE_ prefixed names used by the generic plugin code.
 */

#include <libnvme.h>

#ifndef BD_NVME_LIBNVME3
#define BD_NVME_LIBNVME3

#define BE_NVME_CTRL_MEC_TWPME                     NVME_CTRL_MEC_TWPME
#define BE_NVME_CTRL_OACS_FNVMS                    NVME_CTRL_OACS_FNVMS
#define BE_NVME_CTRL_OACS_NMS                      NVME_CTRL_OACS_NMS
#define BE_NVME_CTRL_OACS_DSTS                     NVME_CTRL_OACS_DSTS
#define BE_NVME_SANITIZE_SSTAT_STATUS_IN_PROGRESS  NVME_SANITIZE_SSTAT_STATUS_IN_PROGRESS

#endif  /* BD_NVME_LIBNVME3 */
