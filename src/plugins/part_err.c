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
 * Author: Vojtech Trefny <vtrefny@redhat.com>
 */

#include <parted/parted.h>

#include "part_err.h"

static __thread gchar *error_msg = NULL;

PedExceptionOption bd_exc_handler (PedException *ex) {
    error_msg = g_strdup (ex->message);
    return PED_EXCEPTION_UNHANDLED;
}

gchar * bd_get_error_msg () {
  return g_strdup (error_msg);
}
