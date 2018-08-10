/*
 * Copyright (C) 2014  Red Hat, Inc.
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
 * Author: Vojtech Trefny <vtrefny@redhat.com>
 */

#include <glib.h>
#include <parted/parted.h>

#include "part_err.h"

static __thread gchar *error_msg = NULL;

PedExceptionOption bd_exc_handler (PedException *ex) {
    if (ex->type <= PED_EXCEPTION_WARNING && (ex->options & PED_EXCEPTION_IGNORE) != 0) {
      g_warning ("[parted] %s", ex->message);
      return PED_EXCEPTION_IGNORE;
    }
    error_msg = g_strdup (ex->message);
    return PED_EXCEPTION_UNHANDLED;
}

gchar * bd_get_error_msg (void) {
  return g_strdup (error_msg);
}
