/*
 * Copyright (C) 2019 Red Hat, Inc.
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

#include <glib/gprintf.h>
#include <syslog.h>
#include <stdarg.h>
#include <errno.h>
#include <blockdev/utils.h>

#include "dm_logging.h"

void __attribute__ ((visibility ("hidden")))
redirect_dm_log (int level, const char *file __attribute__((unused)), int line __attribute__((unused)),
                             int dm_errno_or_class __attribute__((unused)), const char *f, ...) {
    gchar *dm_msg = NULL;
    gchar *message = NULL;
    gint ret = 0;
    va_list args;

    va_start (args, f);
    ret = g_vasprintf (&dm_msg, f, args);
    va_end (args);

    if (ret < 0) {
        g_free (dm_msg);
        return;
    }


#ifdef DEBUG
    message = g_strdup_printf ("[libdevmapper] %s:%d %s", file, line, dm_msg);
#else
    message = g_strdup_printf ("[libdevmapper] %s", dm_msg);
#endif

    /* libdevmapper has some custom special log levels, these should be
       internal, but just to be sure mark everything weird as debug */
    if (level > LOG_DEBUG)
        level = LOG_DEBUG;

    bd_utils_log (level, message);

    g_free (dm_msg);
    g_free (message);

}
