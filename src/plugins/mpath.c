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
/* provides major and minor macros */
#include <sys/sysmacros.h>
#include <libdevmapper.h>
#include <unistd.h>
#include <blockdev/utils.h>

#include "mpath.h"
#include "check_deps.h"

/**
 * SECTION: mpath
 * @short_description: plugin for basic operations with multipath devices
 * @title: Mpath
 * @include: mpath.h
 *
 * A plugin for basic operations with multipath devices.
 */

/**
 * bd_mpath_error_quark: (skip)
 */
GQuark bd_mpath_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-mpath-error-quark");
}

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MPATH 0
#define DEPS_MPATH_MASK (1 << DEPS_MPATH)
#define DEPS_MPATHCONF 1
#define DEPS_MPATHCONF_MASK (1 << DEPS_MPATHCONF)
#define DEPS_LAST 2

static const UtilDep deps[DEPS_LAST] = {
    {"multipath", MULTIPATH_MIN_VERSION, NULL, "multipath-tools v([\\d\\.]+)"},
    {"mpathconf", NULL, NULL, NULL},
};


/**
 * bd_mpath_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_mpath_check_deps (void) {
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

    if (!ret)
        g_warning("Cannot load the mpath plugin");

    return ret;
}

/**
 * bd_mpath_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_mpath_init (void) {
    /* nothing to do here */
    return TRUE;
};

/**
 * bd_mpath_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_mpath_close (void) {
    /* nothing to do here */
}

/**
 * bd_mpath_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is avaible -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_mpath_is_tech_avail (BDMpathTech tech, guint64 mode, GError **error) {
    switch (tech) {
    case BD_MPATH_TECH_BASE:
        return check_deps (&avail_deps, DEPS_MPATH_MASK, deps, DEPS_LAST, &deps_check_lock, error);
    case BD_MPATH_TECH_FRIENDLY_NAMES:
        if (mode & ~BD_MPATH_TECH_MODE_MODIFY) {
            g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_TECH_UNAVAIL,
                         "Only 'modify' (setting) supported for friendly names");
            return FALSE;
        } else if (mode & BD_MPATH_TECH_MODE_MODIFY)
            return check_deps (&avail_deps, DEPS_MPATHCONF_MASK, deps, DEPS_LAST, &deps_check_lock, error);
        else {
            g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_TECH_UNAVAIL,
                         "Unknown mode");
            return FALSE;
        }
    default:
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_TECH_UNAVAIL, "Unknown technology");
        return FALSE;
    }
}


/**
 * bd_mpath_flush_mpaths:
 * @error: (out): place to store error (if any)
 *
 * Returns: whether multipath device maps were successfully flushed or not
 *
 * Flushes all unused multipath device maps.
 *
 * Tech category: %BD_MPATH_TECH_BASE-%BD_MPATH_TECH_MODE_MODIFY
 */
gboolean bd_mpath_flush_mpaths (GError **error) {
    const gchar *argv[3] = {"multipath", "-F", NULL};
    gboolean success = FALSE;
    gchar *output = NULL;

    if (!check_deps (&avail_deps, DEPS_MPATH_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    /* try to flush the device maps */
    success = bd_utils_exec_and_report_error (argv, NULL, error);
    if (!success)
        return FALSE;

    /* list devices (there should be none) */
    argv[1] = "-ll";
    success = bd_utils_exec_and_capture_output (argv, NULL, &output, error);
    if (success && output && (g_strcmp0 (output, "") != 0)) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_FLUSH,
                     "Some device cannot be flushed: %s", output);
        g_free (output);
        return FALSE;
    }

    g_free (output);
    return TRUE;
}

static gchar* get_device_name (const gchar *major_minor, GError **error) {
    gchar *path = NULL;
    gchar *link = NULL;
    gchar *ret = NULL;

    path = g_strdup_printf ("/dev/block/%s", major_minor);
    link = g_file_read_link (path, error);
    g_free (path);
    if (!link) {
        g_prefix_error (error, "Failed to determine device name for '%s'",
                        major_minor);
        return NULL;
    }

    /* 'link' should be something like "../sda" */
    /* get the last '/' */
    ret = strrchr (link, '/');
    if (!ret) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_INVAL,
                     "Failed to determine device name for '%s'",
                     major_minor);
        g_free (link);
        return NULL;
    }
    /* move right after the last '/' */
    ret++;

    /* create a new copy and free the whole link path */
    ret = g_strdup (ret);
    g_free (link);

    return ret;
}

static gboolean map_is_multipath (const gchar *map_name, GError **error) {
    struct dm_task *task = NULL;
    struct dm_info info;
    guint64 start = 0;
    guint64 length = 0;
    gchar *type = NULL;
    gchar *params = NULL;
    gboolean ret = FALSE;

    if (geteuid () != 0) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NOT_ROOT,
                     "Not running as root, cannot query DM maps");
        return FALSE;
    }

    task = dm_task_create (DM_DEVICE_STATUS);
    if (!task) {
        g_warning ("Failed to create DM task");
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_DM_ERROR,
                     "Failed to create DM task");
        return FALSE;
    }

    if (dm_task_set_name (task, map_name) == 0) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_DM_ERROR,
                     "Failed to create DM task");
        dm_task_destroy (task);
        return FALSE;
    }

    if (dm_task_run (task) == 0) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_DM_ERROR,
                     "Failed to run DM task");
        dm_task_destroy (task);
        return FALSE;
    }

    if (dm_task_get_info (task, &info) == 0) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_DM_ERROR,
                     "Failed to get task info");
        dm_task_destroy (task);
        return FALSE;
    }

    dm_get_next_target(task, NULL, &start, &length, &type, &params);
    if (g_strcmp0 (type, "multipath") == 0)
        ret = TRUE;
    else
        ret = FALSE;
    dm_task_destroy (task);

    return ret;
}

static gchar** get_map_deps (const gchar *map_name, guint64 *n_deps, GError **error) {
    struct dm_task *task;
    struct dm_deps *deps;
    guint64 dev_major = 0;
    guint64 dev_minor = 0;
    guint64 i = 0;
    gchar **dep_devs = NULL;
    gchar *major_minor = NULL;

    if (geteuid () != 0) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NOT_ROOT,
                     "Not running as root, cannot query DM maps");
        return NULL;
    }

    task = dm_task_create (DM_DEVICE_DEPS);
    if (!task) {
        g_warning ("Failed to create DM task");
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_DM_ERROR,
                     "Failed to create DM task");
        return NULL;
    }

    if (dm_task_set_name (task, map_name) == 0) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_DM_ERROR,
                     "Failed to create DM task");
        dm_task_destroy (task);
        return NULL;
    }

    if (dm_task_run (task) == 0) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_DM_ERROR,
                     "Failed to run DM task");
        dm_task_destroy (task);
        return NULL;
    }

    deps = dm_task_get_deps (task);
    if (!deps) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_DM_ERROR,
                     "Failed to device dependencies");
        dm_task_destroy (task);
        return NULL;
    }

    /* allocate space for the dependencies */
    dep_devs = g_new0 (gchar*, deps->count + 1);

    for (i = 0; i < deps->count; i++) {
        dev_major = (guint64) major (deps->device[i]);
        dev_minor = (guint64) minor (deps->device[i]);
        major_minor = g_strdup_printf ("%"G_GUINT64_FORMAT":%"G_GUINT64_FORMAT, dev_major, dev_minor);
        dep_devs[i] = get_device_name (major_minor, error);
        if (*error) {
            g_prefix_error (error, "Failed to resolve '%s' to device name",
                            major_minor);
            g_free (dep_devs);
            g_free (major_minor);
            return NULL;
        }
        g_free (major_minor);
    }
    dep_devs[deps->count] = NULL;
    if (n_deps)
        *n_deps = deps->count;

    dm_task_destroy (task);
    return dep_devs;
}

/**
 * bd_mpath_is_mpath_member:
 * @device: device to test
 * @error: (out): place to store error (if any)
 *
 * Returns: %TRUE if the device is a multipath member, %FALSE if not or an error
 * appeared when queried (@error is set in those cases)
 *
 * Tech category: %BD_MPATH_TECH_BASE-%BD_MPATH_TECH_MODE_QUERY
 */
gboolean bd_mpath_is_mpath_member (const gchar *device, GError **error) {
    struct dm_task *task_names = NULL;
	struct dm_names *names = NULL;
    gchar *dev_path = NULL;
    guint64 next = 0;
    gchar **deps = NULL;
    gchar **dev_name = NULL;
    gboolean ret = FALSE;

    if (geteuid () != 0) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NOT_ROOT,
                     "Not running as root, cannot query DM maps");
        return FALSE;
    }

    /* we check if the 'device' is a dependency of any multipath map  */
    /* get maps */
    task_names = dm_task_create(DM_DEVICE_LIST);
    if (!task_names) {
        g_warning ("Failed to create DM task");
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_DM_ERROR,
                     "Failed to create DM task");
        return FALSE;
    }

    dm_task_run(task_names);
    names = dm_task_get_names(task_names);

    if (!names || !names->dev)
        return FALSE;

    /* in case the device is dev_path, we need to resolve it because maps's deps
       are devices and not their dev_paths */
    if (g_str_has_prefix (device, "/dev/mapper/") || g_str_has_prefix (device, "/dev/md/")) {
        dev_path = bd_utils_resolve_device (device, error);
        if (!dev_path) {
            /* the device doesn't exist and thus is not an mpath member */
            g_clear_error (error);
            dm_task_destroy (task_names);
            return FALSE;
        }

        /* the dev_path starts with "../" */
        device = dev_path + 3;
    }

    if (g_str_has_prefix (device, "/dev/"))
        device += 5;

    /* check all maps */
    do {
        names = (void *)names + next;
        next = names->next;

        /* we are only interested in multipath maps */
        if (map_is_multipath (names->name, error)) {
            deps = get_map_deps (names->name, NULL, error);
            if (*error) {
                g_prefix_error (error, "Failed to determine deps for '%s'", names->name);
                g_free (dev_path);
                dm_task_destroy (task_names);
                return FALSE;
            }
            for (dev_name = deps; !ret && *dev_name; dev_name++)
                ret = (g_strcmp0 (*dev_name, device) == 0);
            g_strfreev (deps);
        } else if (*error) {
            g_prefix_error (error, "Failed to determine map's target for '%s'", names->name);
            g_free (dev_path);
            dm_task_destroy (task_names);
            return FALSE;
        }
    } while (!ret && next);

    g_free (dev_path);
    dm_task_destroy (task_names);
    return ret;
}

/**
 * bd_mpath_get_mpath_members:
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full) (array zero-terminated=1): list of names of all devices that are
 *                                                     members of the mpath mappings
 *                                                     (or %NULL in case of error)
 *
 * Tech category: %BD_MPATH_TECH_BASE-%BD_MPATH_TECH_MODE_QUERY
 */
gchar** bd_mpath_get_mpath_members (GError **error) {
    struct dm_task *task_names = NULL;
	struct dm_names *names = NULL;
    guint64 next = 0;
    gchar **deps = NULL;
    gchar **dev_name = NULL;
    guint64 n_deps = 0;
    guint64 n_devs = 0;
    guint64 top_dev = 0;
    gchar **ret = NULL;
    guint64 progress_id = 0;

    progress_id = bd_utils_report_started ("Started getting mpath members");

    if (geteuid () != 0) {
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_NOT_ROOT,
                     "Not running as root, cannot query DM maps");
        bd_utils_report_finished (progress_id, (*error)->message);
        return NULL;
    }

    /* we check if the 'device' is a dependency of any multipath map  */
    /* get maps */
    task_names = dm_task_create(DM_DEVICE_LIST);
	if (!task_names) {
        g_warning ("Failed to create DM task");
        g_set_error (error, BD_MPATH_ERROR, BD_MPATH_ERROR_DM_ERROR,
                     "Failed to create DM task");
        bd_utils_report_finished (progress_id, (*error)->message);
        return NULL;
    }

    dm_task_run(task_names);
	names = dm_task_get_names(task_names);

    if (!names || !names->dev) {
        bd_utils_report_finished (progress_id, "Completed");
        return NULL;
    }

    ret = g_new0 (gchar*, 1);
    n_devs = 1;

    /* check all maps */
    do {
        names = (void *)names + next;
        next = names->next;

        /* we are only interested in multipath maps */
        if (map_is_multipath (names->name, error)) {
            deps = get_map_deps (names->name, &n_deps, error);
            if (*error) {
                g_prefix_error (error, "Failed to determine deps for '%s'", names->name);
                dm_task_destroy (task_names);
                bd_utils_report_finished (progress_id, (*error)->message);
                g_free (deps);
                g_free (ret);
                return NULL;
            }
            if (deps) {
                n_devs += n_deps;
                ret = g_renew (gchar*, ret, n_devs);
                for (dev_name=deps; *dev_name; dev_name++) {
                    ret[top_dev] = *dev_name;
                    top_dev += 1;
                }
                g_free (deps);
            }
        }
    } while (next);

    ret[top_dev] = NULL;
    bd_utils_report_finished (progress_id, "Completed");

    return ret;
}


/**
 * bd_mpath_set_friendly_names:
 * @enabled: whether friendly names should be enabled or not
 * @error: (out): place to store error (if any)
 *
 * Returns: if successfully set or not
 *
 * Tech category: %BD_MPATH_TECH_FRIENDLY_NAMES-%BD_MPATH_TECH_MODE_MODIFY
 */
gboolean bd_mpath_set_friendly_names (gboolean enabled, GError **error) {
    const gchar *argv[8] = {"mpathconf", "--find_multipaths", "y", "--user_friendly_names", NULL, "--with_multipathd", "y", NULL};
    argv[4] = enabled ? "y" : "n";

    if (!check_deps (&avail_deps, DEPS_MPATHCONF_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, NULL, error);
}
