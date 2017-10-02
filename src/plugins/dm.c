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
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <glib.h>
#include <unistd.h>
#include <blockdev/utils.h>
#include <libdevmapper.h>
#include <dmraid/dmraid.h>
#include <libudev.h>

#include "dm.h"
#include "check_deps.h"

/* macros taken from the pyblock/dmraid.h file plus one more*/
#define for_each_raidset(_c, _n) list_for_each_entry(_n, LC_RS(_c), list)
#define for_each_subset(_rs, _n) list_for_each_entry(_n, &(_rs)->sets, list)
#define for_each_device(_rs, _d) list_for_each_entry(_d, &(_rs)->devs, devs)

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

typedef struct raid_set* (*RSEvalFunc) (struct raid_set *rs, gpointer data);


static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_DMSETUP 0
#define DEPS_DMSETUP_MASK (1 << DEPS_DMSETUP)
#define DEPS_LAST 1

static UtilDep deps[DEPS_LAST] = {
    {"dmsetup", DM_MIN_VERSION, NULL, "Library version:\\s+([\\d\\.]+)"},
};


/**
 * discard_dm_log: (skip)
 */
static void discard_dm_log (int level __attribute__((unused)), const char *file __attribute__((unused)), int line __attribute__((unused)),
                            int dm_errno_or_class __attribute__((unused)), const char *f __attribute__((unused)), ...) {
    return;
}

/**
 * bd_dm_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_dm_check_deps () {
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
        g_warning("Cannot load the DM plugin");

    return ret;
}

/**
 * bd_dm_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_dm_init () {
    dm_log_with_errno_init ((dm_log_with_errno_fn) discard_dm_log);
    dm_log_init_verbose (0);

    return TRUE;
}

/**
 * bd_dm_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_dm_close () {
    dm_log_with_errno_init (NULL);
    dm_log_init_verbose (0);
}

#define UNUSED __attribute__((unused))

/**
 * bd_dm_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDDMTechMode) for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is avaible -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_dm_is_tech_avail (BDDMTech tech, guint64 mode UNUSED, GError **error) {
    /* all combinations are supported by this implementation of the plugin, but
       BD_DM_TECH_MAP requires the 'dmsetup' utility */
    if (tech == BD_DM_TECH_MAP)
        return check_deps (&avail_deps, DEPS_DMSETUP_MASK, deps, DEPS_LAST, &deps_check_lock, error);
    else
        return TRUE;
}

/**
 * bd_dm_create_linear:
 * @map_name: name of the map
 * @device: device to create map for
 * @length: length of the mapping in sectors
 * @uuid: (allow-none): UUID for the new dev mapper device or %NULL if not specified
 * @error: (out): place to store error (if any)
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
 * @error: (out): place to store error (if any)
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
 * @error: (out): place to store error (if any)
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
        /* errror is already populated */
        return NULL;
    }

    return g_strstrip (ret);
}

/**
 * bd_dm_node_from_name:
 * @map_name: name of the queried DM map
 * @error: (out): place to store error (if any)
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
 * @error: (out): place to store error (if any)
 *
 * Returns: subsystem of the given device
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
 * @error: (out): place to store error (if any)
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

    task_list = dm_task_create(DM_DEVICE_LIST);
	if (!task_list) {
        g_warning ("Failed to create DM task");
        g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_TASK,
                     "Failed to create DM task");
        return FALSE;
    }

    dm_task_run(task_list);
	names = dm_task_get_names(task_list);

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
        task_info = dm_task_create(DM_DEVICE_INFO);
        if (!task_info) {
            g_warning ("Failed to create DM task");
            g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_TASK,
                         "Failed to create DM task");
            break;
        }

        dm_task_set_name(task_info, names->name);
        dm_task_run(task_info);
        dm_task_get_info(task_info, &info);

        if (!info.exists)
            /* doesn't exist, try next one */
            continue;

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

/**
 * init_dmraid_stack: (skip)
 *
 * Initializes the dmraid stack by creating the library context, discovering
 * devices, raid sets, etc.
 */
static struct lib_context* init_dmraid_stack (GError **error) {
    gint rc = 0;
    gchar *argv[] = {"blockdev.dmraid", NULL};
    struct lib_context *lc;

    /* the code for this function was cherry-picked from the pyblock code */
    /* XXX: do this all just once, store global lc and provide a reinit
     *      function? */

    /* initialize dmraid library context */
    lc = libdmraid_init (1, (gchar **)argv);

    rc = discover_devices (lc, NULL);
    if (!rc) {
        g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_RAID_FAIL,
                     "Failed to discover devices");
        libdmraid_exit (lc);
        return NULL;
    }
    discover_raid_devices (lc, NULL);

    if (!count_devices (lc, RAID)) {
        g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_RAID_NO_DEVS,
                     "No RAIDs discovered");
        libdmraid_exit (lc);
        return NULL;
    }

    argv[0] = NULL;
    if (!group_set (lc, argv)) {
        g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_RAID_FAIL,
                     "Failed to group_set");
        libdmraid_exit (lc);
        return NULL;
    }

    return lc;
}

/**
 * raid_dev_matches_spec: (skip)
 *
 * Returns: whether the device specified by @sysname matches the spec given by @name,
 *          @uuid, @major and @minor
 */
static gboolean raid_dev_matches_spec (struct raid_dev *raid_dev, const gchar *name, const gchar *uuid, gint major, gint minor) {
    gchar const *dev_name = NULL;
    gchar const *dev_uuid;
    gchar const *major_str;
    gchar const *minor_str;
    struct udev *context;
    struct udev_device *device;
    gboolean ret = TRUE;

    /* find the second '/' to get name (the rest of the string) */
    dev_name = strchr (raid_dev->di->path, '/');
    if (dev_name && strlen (dev_name) > 1) {
        dev_name++;
        dev_name = strchr (dev_name, '/');
    }
    if (dev_name && strlen (dev_name) > 1) {
        dev_name++;
    }
    else
        dev_name = NULL;

    /* if we don't have the name, we cannot check any match */
    g_return_val_if_fail (dev_name, FALSE);

    if (name && strcmp (dev_name, name) != 0) {
        return FALSE;
    }

    context = udev_new ();
    device = udev_device_new_from_subsystem_sysname (context, "block", dev_name);
    dev_uuid = udev_device_get_property_value (device, "UUID");
    major_str = udev_device_get_property_value (device, "MAJOR");
    minor_str = udev_device_get_property_value (device, "MINOR");

    if (uuid && (g_strcmp0 (uuid, "") != 0) && (g_strcmp0 (uuid, dev_uuid) != 0))
        ret = FALSE;

    if (major >= 0 && (atoi (major_str) != major))
        ret = FALSE;

    if (minor >= 0 && (atoi (minor_str) != minor))
        ret = FALSE;

    udev_device_unref (device);
    udev_unref (context);

    return ret;
}

/**
 * find_raid_sets_for_dev: (skip)
 */
static void find_raid_sets_for_dev (const gchar *name, const gchar *uuid, gint major, gint minor, struct lib_context *lc, struct raid_set *rs, GPtrArray *ret_sets) {
    struct raid_set *subset;
    struct raid_dev *dev;

    if (T_GROUP(rs) || !list_empty(&(rs->sets))) {
        for_each_subset (rs, subset)
            find_raid_sets_for_dev (name, uuid, major, minor, lc, subset, ret_sets);
    } else {
        for_each_device (rs, dev) {
            if (raid_dev_matches_spec (dev, name, uuid, major, minor))
                g_ptr_array_add (ret_sets, g_strdup (rs->name));
        }
    }
}

/**
 * bd_dm_get_member_raid_sets:
 * @name: (allow-none): name of the member
 * @uuid: (allow-none): uuid of the member
 * @major: major number of the device or -1 if not specified
 * @minor: minor number of the device or -1 if not specified
 * @error: (out): variable to store error (if any)
 *
 * Returns: (transfer full) (array zero-terminated=1): list of names of the RAID sets related to
 * the member or %NULL in case of error
 *
 * One of @name, @uuid or @major:@minor has to be given.
 *
 * Tech category: %BD_DM_TECH_RAID-%BD_DM_TECH_MODE_QUERY
 */
gchar** bd_dm_get_member_raid_sets (const gchar *name, const gchar *uuid, gint major, gint minor, GError **error) {
    guint64 i = 0;
    struct lib_context *lc = NULL;
    struct raid_set *rs = NULL;
    GPtrArray *ret_sets = g_ptr_array_new ();
    gchar **ret = NULL;

    lc = init_dmraid_stack (error);
    if (!lc)
        /* error is already populated */
        return NULL;

    for_each_raidset (lc, rs) {
        find_raid_sets_for_dev (name, uuid, major, minor, lc, rs, ret_sets);
    }

    /* now create the return value -- NULL-terminated array of strings */
    ret = g_new0 (gchar*, ret_sets->len + 1);
    for (i=0; i < ret_sets->len; i++)
        ret[i] = (gchar*) g_ptr_array_index (ret_sets, i);
    ret[i] = NULL;

    g_ptr_array_free (ret_sets, FALSE);

    libdmraid_exit (lc);
    return ret;
}

/**
 * find_in_raid_sets: (skip)
 *
 * Runs @eval_fn with @data on each set (traversing recursively) and returns the
 * first RAID set that @eval_fn returns. Thus the @eval_fn should return %NULL
 * on all RAID sets that don't fulfill the search criteria.
 */
static struct raid_set* find_in_raid_sets (struct raid_set *rs, RSEvalFunc eval_fn, gpointer data) {
    struct raid_set *subset = NULL;
    struct raid_set *ret = NULL;

    ret = eval_fn (rs, data);
    if (ret)
        return ret;

    if (T_GROUP(rs) || !list_empty(&(rs->sets))) {
        for_each_subset (rs, subset) {
            ret = find_in_raid_sets (subset, eval_fn, data);
            if (ret)
                return ret;
        }
    }

    return ret;
}

static struct raid_set* rs_matches_name (struct raid_set *rs, gpointer *name_data) {
    gchar *name = (gchar*) name_data;

    if (g_strcmp0 (rs->name, name) == 0)
        return rs;
    else
        return NULL;
}

static gboolean change_set_by_name (const gchar *name, enum activate_type action, GError **error) {
    gint rc = 0;
    struct lib_context *lc;
    struct raid_set *iter_rs;
    struct raid_set *match_rs = NULL;

    lc = init_dmraid_stack (error);
    if (!lc)
        /* error is already populated */
        return FALSE;

    for_each_raidset (lc, iter_rs) {
        match_rs = find_in_raid_sets (iter_rs, (RSEvalFunc)rs_matches_name, (gchar *)name);
        if (match_rs)
            break;
    }

    if (!match_rs) {
        g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_RAID_NO_EXIST,
                     "RAID set %s doesn't exist", name);
        libdmraid_exit (lc);
        return FALSE;
    }

    rc = change_set (lc, action, match_rs);
    if (!rc) {
        g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_RAID_FAIL,
                     "Failed to activate the RAID set '%s'", name);
        libdmraid_exit (lc);
        return FALSE;
    }

    libdmraid_exit (lc);
    return TRUE;
}

/**
 * bd_dm_activate_raid_set:
 * @name: name of the DM RAID set to activate
 * @error: (out): variable to store error (if any)
 *
 * Returns: whether the RAID set @name was successfully activate or not
 *
 * Tech category: %BD_DM_TECH_RAID-%BD_DM_TECH_CREATE_ACTIVATE
 */
gboolean bd_dm_activate_raid_set (const gchar *name, GError **error) {
    guint64 progress_id = 0;
    gchar *msg = NULL;
    gboolean ret = FALSE;

    msg = g_strdup_printf ("Activating DM RAID set '%s'", name);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);
    ret = change_set_by_name (name, A_ACTIVATE, error);
    bd_utils_report_finished (progress_id, "Completed");
    return ret;
}

/**
 * bd_dm_deactivate_raid_set:
 * @name: name of the DM RAID set to deactivate
 * @error: (out): variable to store error (if any)
 *
 * Returns: whether the RAID set @name was successfully deactivate or not
 *
 * Tech category: %BD_DM_TECH_RAID-%BD_DM_TECH_REMOVE_DEACTIVATE
 */
gboolean bd_dm_deactivate_raid_set (const gchar *name, GError **error) {
    guint64 progress_id = 0;
    gchar *msg = NULL;
    gboolean ret = FALSE;

    msg = g_strdup_printf ("Deactivating DM RAID set '%s'", name);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);
    ret = change_set_by_name (name, A_DEACTIVATE, error);
    bd_utils_report_finished (progress_id, "Completed");
    return ret;
}

/**
 * bd_dm_get_raid_set_type:
 * @name: name of the DM RAID set to get the type of
 * @error: (out): variable to store error (if any)
 *
 * Returns: string representation of the @name RAID set's type
 *
 * Tech category: %BD_DM_TECH_RAID-%BD_DM_TECH_QUERY
 */
gchar* bd_dm_get_raid_set_type (const gchar *name, GError **error) {
    struct lib_context *lc;
    struct raid_set *iter_rs;
    struct raid_set *match_rs = NULL;
    const gchar *type = NULL;

    lc = init_dmraid_stack (error);
    if (!lc)
        /* error is already populated */
        return NULL;

    for_each_raidset (lc, iter_rs) {
        match_rs = find_in_raid_sets (iter_rs, (RSEvalFunc)rs_matches_name, (gchar *)name);
        if (match_rs)
            break;
    }

    if (!match_rs) {
        g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_RAID_NO_EXIST,
                     "RAID set %s doesn't exist", name);
        libdmraid_exit (lc);
        return NULL;
    }

    type = get_set_type (lc, match_rs);
    if (!type) {
        g_set_error (error, BD_DM_ERROR, BD_DM_ERROR_RAID_FAIL,
                     "Failed to get RAID set's type");
        libdmraid_exit (lc);
        return NULL;
    }

    libdmraid_exit (lc);
    return g_strdup (type);
}
