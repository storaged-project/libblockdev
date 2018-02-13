/*
 * Copyright (C) 2018  Red Hat, Inc.
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

#include <glib.h>
#include <blockdev/utils.h>

#include "vdo.h"
#include "check_deps.h"

/**
 * SECTION: vdo
 * @short_description: plugin for operations with VDO devices
 * @title: VDO
 * @include: vdo.h
 *
 * A plugin for operations with VDO devices.
 */

/**
 * bd_vdo_error_quark: (skip)
 */
GQuark bd_vdo_error_quark (void) {
    return g_quark_from_static_string ("g-bd-vdo-error-quark");
}

static volatile guint avail_deps = 0;
static volatile guint avail_module_deps = 0;
static GMutex deps_check_lock;

#define DEPS_VDO 0
#define DEPS_VDO_MASK (1 << DEPS_VDO)
#define DEPS_LAST 1

static UtilDep deps[DEPS_LAST] = {
    {"vdo", NULL, NULL, NULL},
};

#define MODULE_DEPS_VDO 0
#define MODULE_DEPS_VDO_MASK (1 << MODULE_DEPS_VDO)
#define MODULE_DEPS_LAST 1

static gchar* module_deps[MODULE_DEPS_LAST] = { "kvdo" };

/**
 * bd_vdo_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_vdo_check_deps () {
    GError *error = NULL;
    guint i = 0;
    gboolean status = FALSE;
    gboolean ret = TRUE;

    for (i=0; i < DEPS_LAST; i++) {
        status = bd_utils_check_util_version (deps[i].name, deps[i].version,
                                              deps[i].ver_arg, deps[i].ver_regexp, &error);
        if (!status)
            g_warning ("%s", error->message);
        else
            g_atomic_int_or (&avail_deps, 1 << i);
        g_clear_error (&error);
        ret = ret && status;
    }

    for (i=0; i < MODULE_DEPS_LAST; i++) {
        status = check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, &error);
        if (!status) {
            g_warning ("%s", error->message);
            g_clear_error (&error);
        }
        ret = ret && status;
    }

    if (!ret)
        g_warning("Cannot load the VDO plugin");

    return ret;
}

/**
 * bd_vdo_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_vdo_init () {
    /* nothing to do here */
    return TRUE;
}

/**
 * bd_vdo_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_vdo_close () {
    /* nothing to do here */
    return;
}

#define UNUSED __attribute__((unused))

/**
 * bd_vdo_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDVDOTechMode) for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_vdo_is_tech_avail (BDVDOTech tech UNUSED, guint64 mode UNUSED, GError **error) {
  /* all tech-mode combinations are supported by this implementation of the
     plugin, but it requires the 'vdo' utility */
  if (tech == BD_VDO_TECH_VDO)
    return check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error);
  else {
    g_set_error (error, BD_VDO_ERROR, BD_VDO_ERROR_TECH_UNAVAIL, "Unknown technology");
    return FALSE;
  }

  return TRUE;
}
