/*
 * Copyright (C) 2017  Red Hat, Inc.
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
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <glib.h>

#ifndef BD_UTILS_DEV_UTILS
#define BD_UTILS_DEV_UTILS

GQuark bd_utils_dev_utils_error_quark (void);
#define BD_UTILS_DEV_UTILS_ERROR bd_utils_dev_utils_error_quark ()

typedef enum {
    BD_UTILS_DEV_UTILS_ERROR_FAILED,
} BDUtilsDevUtilsError;

gchar* bd_utils_resolve_device (const gchar *dev_spec, GError **error);
gchar** bd_utils_get_device_symlinks (const gchar *dev_spec, GError **error);

#endif  /* BD_UTILS_DEV_UTILS */
