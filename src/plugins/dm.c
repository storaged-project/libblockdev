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
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <glib.h>
#include <unistd.h>
#include <blockdev/utils.h>
#include <libdevmapper.h>
#include <stdarg.h>
#include <syslog.h>

#include "dm.h"
#include "check_deps.h"
#include "dm_logging.h"

#define DM_MIN_VERSION "1.02.93"


/**
 * SECTION: dm
 * @short_description: plugin for basic operations with device mapper
 * @title: DeviceMapper
 * @include: dm.h
 *
 * A plugin for basic operations with device mapper.
 */

/**
 * bd_dm_error_quark: (skip)
 */
GQuark bd_dm_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-dm-error-quark");
}

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_DMSETUP 0
#define DEPS_DMSETUP_MASK (1 << DEPS_DMSETUP)
#define DEPS_LAST 1

static const UtilDep deps[DEPS_LAST] = {
    {"dmsetup", DM_MIN_VERSION, NULL, "Library version:\\s+([\\d\\.]+)"},
};


/**
 * bd_dm_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_dm_init (void) {
    dm_log_with_errno_init ((dm_log_with_errno_fn) redirect_dm_log);
#ifdef DEBUG
    dm_log_init_verbose (LOG_DEBUG);
#else
    dm_log_init_verbose (LOG_INFO);
#endif

    return TRUE;
}

/**
 * bd_dm_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_dm_close (void) {
    dm_log_with_errno_init (NULL);
    dm_log_init_verbose (0);
}

/**
 * bd_dm_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDDMTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_dm_is_tech_avail (BDDMTech tech, guint64 mode G_GNUC_UNUSED, GError **error) {
    /* all combinations are supported by this implementation of the plugin, but
       BD_DM_TECH_MAP requires the 'dmsetup' utility */
    switch (tech) {
        case BD_DM_TECH_MAP:
            return check_deps (&avail_deps, DEPS_DMSETUP_MASK, deps, DEPS_LAST, &deps_check_lock, error);
        default:
            return TRUE;
    }
}

/**
 * bd_dm_create_linear:
 * @map_name: name of the map
 * @device: device to create map for
 * @length: length of the mapping in sectors
 * @uuid: (nullable): UUID for the new dev mapper device or %NULL if not specified
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the new linear mapping @map_name was successfully created
 * for the @device or not
 *
 * Tech category: %BD_DM_TECH_MAP-%BD_DM_TECH_MODE_CREATE_ACTIVATE
 */
gboolean bd_dm_create_linear (const gchar *map_name, const gchar *device, guint64 length, const gchar *uuid, GError **error) {
    gboolean success = FALSE;
    const gchar *argv[9] = {"dmsetup", "create", map_name, "--table", NULL, NULL, NULL, NULL, NULL};

    if (!check_deps (&avail_deps, DEPS_DMSETUP_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    gchar *table = g_strdup_printf ("0 %"G_GUINT64_FORMAT" linear %s 0", length, device);
    argv[4] = table;

    if (uuid) {
        argv[5] = "-u";
        argv[6] = uuid;
        argv[7] = device;
    } else
        argv[5] = device;

    success = bd_utils_exec_and_report_error (argv, NULL, error);
    g_free (table);

    return success;
}

/**
 * bd_dm_remove:
 * @map_name: name of the map to remove
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @map_name map was successfully removed or not
 *
 * Tech category: %BD_DM_TECH_MAP-%BD_DM_TECH_MODE_REMOVE_DEACTIVATE
 */
gboolean bd_dm_remove (const gchar *map_name, GError **error) {
    const gchar *argv[4] = {"dmsetup", "remove", map_name, NULL};

    if (!check_deps (&avail_deps, DEPS_DMSETUP_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, NULL, error);
}

/**
 * bd_dm_name_from_node:
 * @dm_node: name of the DM node (e.g. "dm-0")
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: map name of the map providing the @dm_node device or %NULL
 * (@error) contains the error in such cases)
 *
 * Tech category: %BD_DM_TECH_MAP-%BD_DM_TECH_MODE_QUERY
 */
gchar* bd_dm_name_from_node (const gchar *dm_node, GError **error) {
    gchar *ret = NULL;
    gboolean success = FALSE;

    gchar *sys_path = g_strdup_printf ("/sys/class/block/%s/dm/name", dm_node);

    if (access (sys_path, R_OK) != 0) {
        g_free (sys_path);
        g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_SYS,
                     "Failed to access dm node's parameters under /sys");
        return NULL;
    }

    success = g_file_get_contents (sys_path, &ret, NULL, error);
    g_free (sys_path);

    if (!success) {
        /* error is already populated */
        g_free (ret);
        return NULL;
    }

    return g_strstrip (ret);
}

/**
 * bd_dm_node_from_name:
 * @map_name: name of the queried DM map
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: DM node name for the @map_name map or %NULL (@error) contains
 * the error in such cases)
 *
 * Tech category: %BD_DM_TECH_MAP-%BD_DM_TECH_MODE_QUERY
 */
gchar* bd_dm_node_from_name (const gchar *map_name, GError **error) {
    gchar *dev_path = NULL;
    gchar *ret = NULL;
    gchar *dev_mapper_path = g_strdup_printf ("/dev/mapper/%s", map_name);

    dev_path = bd_utils_resolve_device (dev_mapper_path, error);
    g_free (dev_mapper_path);
    if (!dev_path)
        /* error is already populated */
        return NULL;

    ret = g_path_get_basename (dev_path);
    g_free (dev_path);

    return ret;
}

/**
 * bd_dm_get_subsystem_from_name:
 * @device_name: name of the device
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: subsystem of the given device
 *
 * Tech category: %BD_DM_TECH_MAP-%BD_DM_TECH_MODE_QUERY
 */
gchar* bd_dm_get_subsystem_from_name (const gchar *device_name, GError **error) {
    gchar *output = NULL;
    gboolean success = FALSE;
    const gchar *argv[] = {"dmsetup", "info", "-co", "subsystem", "--noheadings", device_name, NULL};

    success = bd_utils_exec_and_capture_output (argv, NULL, &output, error);
    if (!success)
        /* error is already populated */
        return NULL;

    output = g_strstrip (output);
    return output;
}

/**
 * bd_dm_map_exists:
 * @map_name: name of the queried map
 * @live_only: whether to go through the live maps only or not
 * @active_only: whether to ignore suspended maps or not
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given @map_name exists (and is live if @live_only is
 * %TRUE (and is active if @active_only is %TRUE)). If %FALSE is returned,
 * @error) indicates whether error appeared (non-%NULL) or not (%NULL).
 *
 * Tech category: %BD_DM_TECH_MAP-%BD_DM_TECH_MODE_QUERY
 */
gboolean bd_dm_map_exists (const gchar *map_name, gboolean live_only, gboolean active_only, GError **error) {
    struct dm_task *task_list = NULL;
    struct dm_task *task_info = NULL;
    struct dm_names *names = NULL;
    struct dm_info info;
    guint64 next = 0;
    gboolean ret = FALSE;

    if (geteuid () != 0) {
        g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_NOT_ROOT,
                     "Not running as root, cannot query DM maps");
        return FALSE;
    }

    task_list = dm_task_create (DM_DEVICE_LIST);
    if (!task_list) {
        g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_TASK,
                     "Failed to create DM task");
        return FALSE;
    }

    dm_task_run (task_list);
    names = dm_task_get_names (task_list);

    if (!names || !names->dev)
        return FALSE;

    do {
        names = (void *)names + next;
        next = names->next;
        /* we are searching for the particular map_name map */
        if (g_strcmp0 (map_name, names->name) != 0)
            /* not matching, skip */
            continue;

        /* get device info */
        task_info = dm_task_create (DM_DEVICE_INFO);
        if (!task_info) {
            g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_TASK,
                         "Failed to create DM task");
            break;
        }

        /* something failed, try next one */
        if (dm_task_set_name (task_info, names->name) == 0) {
            dm_task_destroy (task_info);
            continue;
        }
        if (dm_task_run (task_info) == 0) {
            dm_task_destroy (task_info);
            continue;
        }
        if (dm_task_get_info (task_info, &info) == 0) {
            dm_task_destroy (task_info);
            continue;
        }

        if (!info.exists) {
            /* doesn't exist, try next one */
            dm_task_destroy (task_info);
            continue;
        }

        /* found existing name match, let's test the restrictions */
        ret = TRUE;
        if (live_only)
            ret = info.live_table;
        if (active_only)
            ret = ret && !info.suspended;

        dm_task_destroy (task_info);
        if (ret)
            /* found match according to restrictions */
            break;
    } while (next);

    dm_task_destroy (task_list);

    return ret;
}
