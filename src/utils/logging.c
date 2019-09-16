/*
 * Copyright (C) 2019  Red Hat, Inc.
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

#include "logging.h"

static BDUtilsLogFunc log_func = NULL;

/**
 * bd_utils_init_logging:
 * @new_log_func: (allow-none) (scope notified): logging function to use or
 *                                               %NULL to reset to default
 * @error: (out): place to store error (if any)
 *
 * Returns: whether logging was successfully initialized or not
 */
gboolean bd_utils_init_logging (BDUtilsLogFunc new_log_func, GError **error __attribute__((unused))) {
    /* XXX: the error attribute will likely be used in the future when this
       function gets more complicated */

    log_func = new_log_func;

    return TRUE;
}

/**
 * bd_utils_log:
 * @level: log level
 * @msg: log message
 */
void bd_utils_log (gint level, const gchar *msg) {
    if (log_func)
        log_func (level, msg);
}
