/*
 * Copyright (C) 2015-2016  Red Hat, Inc.
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
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <blockdev/utils.h>
#include <gio/gio.h>

#include "lvm.h"
#include "check_deps.h"
#include "vdo_stats.h"

#define INT_FLOAT_EPS 1e-5
#define SECTOR_SIZE 512
#define VDO_POOL_SUFFIX "vpool"

static GMutex global_config_lock;
static gchar *global_config_str = NULL;

#define LVM_BUS_NAME "com.redhat.lvmdbus1"
#define LVM_OBJ_PREFIX "/com/redhat/lvmdbus1"
#define MANAGER_OBJ "/com/redhat/lvmdbus1/Manager"
#define MANAGER_INTF "com.redhat.lvmdbus1.Manager"
#define JOB_OBJ_PREFIX "/com/redhat/lvmdbus1/Job/"
#define JOB_INTF "com.redhat.lvmdbus1.Job"
#define PV_OBJ_PREFIX LVM_OBJ_PREFIX"/Pv"
#define VG_OBJ_PREFIX LVM_OBJ_PREFIX"/Vg"
#define LV_OBJ_PREFIX LVM_OBJ_PREFIX"/Lv"
#define HIDDEN_LV_OBJ_PREFIX LVM_OBJ_PREFIX"/HiddenLv"
#define THIN_POOL_OBJ_PREFIX LVM_OBJ_PREFIX"/ThinPool"
#define CACHE_POOL_OBJ_PREFIX LVM_OBJ_PREFIX"/CachePool"
#define VDO_POOL_OBJ_PREFIX LVM_OBJ_PREFIX"/VdoPool"
#define PV_INTF LVM_BUS_NAME".Pv"
#define VG_INTF LVM_BUS_NAME".Vg"
#define VG_VDO_INTF LVM_BUS_NAME".VgVdo"
#define LV_CMN_INTF LVM_BUS_NAME".LvCommon"
#define LV_INTF LVM_BUS_NAME".Lv"
#define CACHED_LV_INTF LVM_BUS_NAME".CachedLv"
#define SNAP_INTF LVM_BUS_NAME".Snapshot"
#define THPOOL_INTF LVM_BUS_NAME".ThinPool"
#define CACHE_POOL_INTF LVM_BUS_NAME".CachePool"
#define VDO_POOL_INTF LVM_BUS_NAME".VdoPool"
#define DBUS_PROPS_IFACE "org.freedesktop.DBus.Properties"
#define DBUS_INTRO_IFACE "org.freedesktop.DBus.Introspectable"
#define METHOD_CALL_TIMEOUT 5000
#define PROGRESS_WAIT 500 * 1000 /* microseconds */

#define UNUSED __attribute__((unused))

static GDBusConnection *bus = NULL;

/* "friend" functions from the utils library */
guint64 get_next_task_id (void);
void log_task_status (guint64 task_id, const gchar *msg);

/**
 * SECTION: lvm
 * @short_description: plugin for operations with LVM
 * @title: LVM
 * @include: lvm.h
 *
 * A plugin for operations with LVM. All sizes passed in/out to/from
 * the functions are in bytes.
 */

/**
 * bd_lvm_error_quark: (skip)
 */
GQuark bd_lvm_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-lvm-error-quark");
}

BDLVMPVdata* bd_lvm_pvdata_copy (BDLVMPVdata *data) {
    if (data == NULL)
        return NULL;

    BDLVMPVdata *new_data = g_new0 (BDLVMPVdata, 1);

    new_data->pv_name = g_strdup (data->pv_name);
    new_data->pv_uuid = g_strdup (data->pv_uuid);
    new_data->pv_free = data->pv_free;
    new_data->pe_start = data->pe_start;
    new_data->vg_name = g_strdup (data->vg_name);
    new_data->vg_uuid = g_strdup (data->vg_uuid);
    new_data->vg_size = data->vg_size;
    new_data->vg_free = data->vg_free;
    new_data->vg_extent_size = data->vg_extent_size;
    new_data->vg_extent_count = data->vg_extent_count;
    new_data->vg_free_count = data->vg_free_count;
    new_data->vg_pv_count = data->vg_pv_count;

    return new_data;
}

void bd_lvm_pvdata_free (BDLVMPVdata *data) {
    if (data == NULL)
        return;

    g_free (data->pv_name);
    g_free (data->pv_uuid);
    g_free (data->vg_name);
    g_free (data->vg_uuid);
    g_free (data);
}

BDLVMVGdata* bd_lvm_vgdata_copy (BDLVMVGdata *data) {
    if (data == NULL)
        return NULL;

    BDLVMVGdata *new_data = g_new0 (BDLVMVGdata, 1);

    new_data->name = g_strdup (data->name);
    new_data->uuid = g_strdup (data->uuid);
    new_data->size = data->size;
    new_data->free = data->free;
    new_data->extent_size = data->extent_size;
    new_data->extent_count = data->extent_count;
    new_data->free_count = data->free_count;
    new_data->pv_count = data->pv_count;
    return new_data;
}

void bd_lvm_vgdata_free (BDLVMVGdata *data) {
    if (data == NULL)
        return;

    g_free (data->name);
    g_free (data->uuid);
    g_free (data);
}

BDLVMLVdata* bd_lvm_lvdata_copy (BDLVMLVdata *data) {
    if (data == NULL)
        return NULL;

    BDLVMLVdata *new_data = g_new0 (BDLVMLVdata, 1);

    new_data->lv_name = g_strdup (data->lv_name);
    new_data->vg_name = g_strdup (data->vg_name);
    new_data->uuid = g_strdup (data->uuid);
    new_data->size = data->size;
    new_data->attr = g_strdup (data->attr);
    new_data->segtype = g_strdup (data->segtype);
    new_data->origin = g_strdup (data->origin);
    new_data->pool_lv = g_strdup (data->pool_lv);
    new_data->data_lv = g_strdup (data->data_lv);
    new_data->metadata_lv = g_strdup (data->metadata_lv);
    new_data->roles = g_strdup (data->roles);
    new_data->move_pv = g_strdup (data->move_pv);
    new_data->data_percent = data->data_percent;
    new_data->metadata_percent = data->metadata_percent;
    new_data->copy_percent = data->copy_percent;
    return new_data;
}

void bd_lvm_lvdata_free (BDLVMLVdata *data) {
    if (data == NULL)
        return;

    g_free (data->lv_name);
    g_free (data->vg_name);
    g_free (data->uuid);
    g_free (data->attr);
    g_free (data->segtype);
    g_free (data->origin);
    g_free (data->pool_lv);
    g_free (data->data_lv);
    g_free (data->metadata_lv);
    g_free (data->roles);
    g_free (data->move_pv);
    g_free (data);
}

BDLVMCacheStats* bd_lvm_cache_stats_copy (BDLVMCacheStats *data) {
    if (data == NULL)
        return NULL;

    BDLVMCacheStats *new = g_new0 (BDLVMCacheStats, 1);

    new->block_size = data->block_size;
    new->cache_size = data->cache_size;
    new->cache_used = data->cache_used;
    new->md_block_size = data->md_block_size;
    new->md_size = data->md_size;
    new->md_used = data->md_used;
    new->read_hits = data->read_hits;
    new->read_misses = data->read_misses;
    new->write_hits = data->write_hits;
    new->write_misses = data->write_misses;
    new->mode = data->mode;

    return new;
}

void bd_lvm_cache_stats_free (BDLVMCacheStats *data) {
    g_free (data);
}

static gboolean setup_dbus_connection (GError **error) {
    gchar *addr = NULL;

    addr = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SYSTEM, NULL, error);
    if (!addr) {
        g_critical ("Failed to get system bus address: %s\n", (*error)->message);
        return FALSE;
    }

    bus = g_dbus_connection_new_for_address_sync (addr,
                                                  G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|
                                                  G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                                  NULL, NULL, error);

    g_free (addr);

    if (!bus || g_dbus_connection_is_closed (bus)) {
        g_critical ("Failed to create a new connection for the system bus: %s\n", (*error)->message);
        return FALSE;
    }

    g_dbus_connection_set_exit_on_close (bus, FALSE);

    return TRUE;
}

static volatile guint avail_dbus_deps = 0;
static volatile guint avail_features = 0;
static volatile guint avail_module_deps = 0;
static GMutex deps_check_lock;

#define DBUS_DEPS_LVMDBUSD 0
#define DBUS_DEPS_LVMDBUSD_MASK (1 << DBUS_DEPS_LVMDBUSD)
#define DBUS_DEPS_LAST 1

static const DBusDep dbus_deps[DBUS_DEPS_LAST] = {
    {LVM_BUS_NAME, LVM_OBJ_PREFIX, G_BUS_TYPE_SYSTEM},
};

#define FEATURES_VDO 0
#define FEATURES_VDO_MASK (1 << FEATURES_VDO)
#define FEATURES_LAST 1

static const UtilFeatureDep features[FEATURES_LAST] = {
    {"lvm", "vdo", "segtypes", NULL},
};

#define MODULE_DEPS_VDO 0
#define MODULE_DEPS_VDO_MASK (1 << MODULE_DEPS_VDO)
#define MODULE_DEPS_LAST 1

static const gchar*const module_deps[MODULE_DEPS_LAST] = { "kvdo" };

/**
 * bd_lvm_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_lvm_check_deps (void) {
    GError *error = NULL;
    guint i = 0;
    gboolean success = FALSE;
    gboolean check_ret = TRUE;

    for (i=0; i < DBUS_DEPS_LAST; i++) {
        success = check_dbus_deps (&avail_dbus_deps, DBUS_DEPS_LVMDBUSD_MASK, dbus_deps, DBUS_DEPS_LAST, &deps_check_lock, &error);
        if (!success) {
            g_warning ("%s", error->message);
            g_clear_error (&error);
        }
        check_ret = check_ret && success;
    }

    if (!check_ret)
        g_warning("Cannot load the LVM plugin");

    return check_ret;
}

/**
 * discard_dm_log: (skip)
 */
static void discard_dm_log (int level __attribute__((unused)), const char *file __attribute__((unused)), int line __attribute__((unused)),
                            int dm_errno_or_class __attribute__((unused)), const char *f __attribute__((unused)), ...) {
    return;
}

/**
 * bd_lvm_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_lvm_init (void) {
    GError *error = NULL;

    /* the check() call should create the DBus connection for us, but let's not
       completely rely on it */
    if (G_UNLIKELY(!bus) && !setup_dbus_connection (&error)) {
        g_critical ("Failed to setup DBus connection: %s", error->message);
        return FALSE;
    }

    dm_log_with_errno_init ((dm_log_with_errno_fn) discard_dm_log);
    dm_log_init_verbose (0);

    return TRUE;
}

/**
 * bd_lvm_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_lvm_close (void) {
    GError *error = NULL;

    /* the check() call should create the DBus connection for us, but let's not
       completely rely on it */
    if (!g_dbus_connection_flush_sync (bus, NULL, &error))
        g_critical ("Failed to flush DBus connection: %s", error->message);
    if (!g_dbus_connection_close_sync (bus, NULL, &error))
        g_critical ("Failed to close DBus connection: %s", error->message);

    dm_log_with_errno_init (NULL);
    dm_log_init_verbose (0);
}

/**
 * bd_lvm_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDLVMTechMode) for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is avaible -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_lvm_is_tech_avail (BDLVMTech tech, guint64 mode, GError **error) {
    switch (tech) {
    case BD_LVM_TECH_THIN_CALCS:
        if (mode & ~BD_LVM_TECH_MODE_QUERY) {
            g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_TECH_UNAVAIL,
                         "Only 'query' supported for thin calculations");
            return FALSE;
        } else
            return TRUE;
    case BD_LVM_TECH_CALCS:
        if (mode & ~BD_LVM_TECH_MODE_QUERY) {
            g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_TECH_UNAVAIL,
                         "Only 'query' supported for calculations");
            return FALSE;
        } else
            return TRUE;
    case BD_LVM_TECH_VDO:
        if (mode & BD_LVM_TECH_MODE_MODIFY) {
            g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_TECH_UNAVAIL,
                         "Modifying existing LVM VDO devices is not supported by this plugin implementation.");
            return FALSE;

        } else
            return check_dbus_deps (&avail_dbus_deps, DBUS_DEPS_LVMDBUSD_MASK, dbus_deps, DBUS_DEPS_LAST, &deps_check_lock, error) &&
                   check_features (&avail_features, FEATURES_VDO_MASK, features, FEATURES_LAST, &deps_check_lock, error) &&
                   check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error);
    default:
        /* everything is supported by this implementation of the plugin */
        return check_dbus_deps (&avail_dbus_deps, DBUS_DEPS_LVMDBUSD_MASK, dbus_deps, DBUS_DEPS_LAST, &deps_check_lock, error);
    }
}

static gchar** get_existing_objects (const gchar *obj_prefix, GError **error) {
    GVariant *intro_v = NULL;
    gchar *intro_data = NULL;
    GDBusNodeInfo *info = NULL;
    gchar **ret = NULL;
    GDBusNodeInfo **nodes;
    guint64 n_nodes = 0;
    guint64 i = 0;

    intro_v = g_dbus_connection_call_sync (bus, LVM_BUS_NAME, obj_prefix, DBUS_INTRO_IFACE,
                                           "Introspect", NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
                                           -1, NULL, error);
    if (!intro_v)
        /* no introspection data, something went wrong (error must be set) */
        return NULL;

    g_variant_get (intro_v, "(s)", &intro_data);
    g_variant_unref (intro_v);
    info = g_dbus_node_info_new_for_xml (intro_data, error);
    g_free (intro_data);

    for (nodes = info->nodes; (*nodes); nodes++)
        n_nodes++;

    ret = g_new0 (gchar*, n_nodes + 1);
    for (nodes = info->nodes, i=0; (*nodes); nodes++, i++) {
        ret[i] = g_strdup_printf ("%s/%s", obj_prefix, ((*nodes)->path));
    }
    ret[i] = NULL;

    g_dbus_node_info_unref (info);

    return ret;
}

static gchar* get_object_path (const gchar *obj_id, GError **error) {
    GVariant *args = NULL;
    GVariant *ret = NULL;
    gchar *obj_path = NULL;

    args = g_variant_new ("(s)", obj_id);
    /* consumes (frees) the 'args' parameter */
    ret = g_dbus_connection_call_sync (bus, LVM_BUS_NAME, MANAGER_OBJ, MANAGER_INTF,
                                       "LookUpByLvmId", args, NULL, G_DBUS_CALL_FLAGS_NONE,
                                       -1, NULL, error);
    if (!ret)
        /* error is already set */
        return NULL;

    g_variant_get (ret, "(o)", &obj_path);
    g_variant_unref (ret);

    if (g_strcmp0 (obj_path, "/") == 0) {
        /* not a valid path (at least for us) */
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_NOEXIST,
                     "The object with LVM ID '%s' doesn't exist", obj_id);
        g_free (obj_path);
        return NULL;
    }

    return obj_path;
}

static GVariant* get_object_property (const gchar *obj_path, const gchar *iface, const gchar *property, GError **error) {
    GVariant *args = NULL;
    GVariant *ret = NULL;
    GVariant *real_ret = NULL;

    args = g_variant_new ("(ss)", iface, property);

    /* consumes (frees) the 'args' parameter */
    ret = g_dbus_connection_call_sync (bus, LVM_BUS_NAME, obj_path, DBUS_PROPS_IFACE,
                                       "Get", args, NULL, G_DBUS_CALL_FLAGS_NONE,
                                       -1, NULL, error);
    if (!ret) {
        g_prefix_error (error, "Failed to get %s property of the %s object: ", property, obj_path);
        return NULL;
    }

    g_variant_get (ret, "(v)", &real_ret);
    g_variant_unref (ret);

    return real_ret;
}

static GVariant* get_lvm_object_property (const gchar *obj_id, const gchar *iface, const gchar *property, GError **error) {
    gchar *obj_path = NULL;
    GVariant *ret = NULL;

    obj_path = get_object_path (obj_id, error);
    if (!obj_path)
        /* error is already set */
        return NULL;
    else {
        ret = get_object_property (obj_path, iface, property, error);
        g_free (obj_path);
        return ret;
    }
}

static gboolean unbox_params_and_add (GVariant *params, GVariantBuilder *builder) {
    GVariantIter iter;
    GVariant *param = NULL;
    gboolean ret = FALSE;

    if (g_variant_is_of_type (params, G_VARIANT_TYPE_DICTIONARY)) {
        g_variant_iter_init (&iter, params);
        while ((param = g_variant_iter_next_value (&iter))) {
            g_variant_builder_add_value (builder, param);
            ret = TRUE;
        }
        return ret;
    }

    if (g_variant_is_of_type (params, G_VARIANT_TYPE_VARIANT)) {
        param = g_variant_get_variant (params);
        return unbox_params_and_add (param, builder);
    }

    if (g_variant_is_container (params)) {
        g_variant_iter_init (&iter, params);
        while ((param = g_variant_iter_next_value (&iter)))
            ret = unbox_params_and_add (param, builder);
        return ret;
    }

    return FALSE;
}

static GVariant* call_lvm_method (const gchar *obj, const gchar *intf, const gchar *method, GVariant *params, GVariant *extra_params, const BDExtraArg **extra_args, guint64 *task_id, guint64 *progress_id, gboolean lock_config, GError **error) {
    GVariant *config = NULL;
    GVariant *param = NULL;
    GVariantIter iter;
    GVariantBuilder builder;
    GVariantBuilder extra_builder;
    GVariant *config_extra_params = NULL;
    GVariant *tmo = NULL;
    GVariant *all_params = NULL;
    GVariant *ret = NULL;
    gchar *params_str = NULL;
    gchar *log_msg = NULL;
    gchar *prog_msg = NULL;
    const BDExtraArg **extra_p = NULL;
    gboolean added_extra = FALSE;

    if (!check_dbus_deps (&avail_dbus_deps, DBUS_DEPS_LVMDBUSD_MASK, dbus_deps, DBUS_DEPS_LAST, &deps_check_lock, error))
        return NULL;

    /* don't allow global config string changes during the run */
    if (lock_config)
        g_mutex_lock (&global_config_lock);

    if (global_config_str || extra_params || extra_args) {
        if (global_config_str || extra_args) {
            /* add the global config to the extra_params */
            g_variant_builder_init (&extra_builder, G_VARIANT_TYPE_DICTIONARY);

            if (extra_params)
                added_extra = unbox_params_and_add (extra_params, &extra_builder);

            if (extra_args) {
                for (extra_p=extra_args; *extra_p; extra_p++) {
                    g_variant_builder_add (&extra_builder, "{sv}",
                                           (*extra_p)->opt ? (*extra_p)->opt : "",
                                           g_variant_new ("s",
                                                          (*extra_p)->val ? (*extra_p)->val : ""));
                    added_extra = TRUE;
                }
            }
            if (global_config_str) {
                config = g_variant_new ("s", global_config_str);
                g_variant_builder_add (&extra_builder, "{sv}", "--config", config);
                added_extra = TRUE;
            }

            if (added_extra)
                config_extra_params = g_variant_builder_end (&extra_builder);
            g_variant_builder_clear (&extra_builder);
        } else
            /* just use the extra_params */
            config_extra_params = extra_params;
    }

    if (!config_extra_params)
        /* create an empty dictionary with the extra arguments */
        config_extra_params = g_variant_new_array (G_VARIANT_TYPE("{sv}"), NULL, 0);

    /* create new GVariant holding the given parameters with the global
       config and extra_params merged together appended */
    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);

    if (params) {
        /* add parameters */
        g_variant_iter_init (&iter, params);
        while ((param = g_variant_iter_next_value (&iter))) {
            g_variant_builder_add_value (&builder, param);
            g_variant_unref (param);
        }
    }

    /* add the timeout spec (in seconds) */
    tmo = g_variant_new ("i", 1);
    g_variant_builder_add_value (&builder, tmo);

    /* add extra parameters including config */
    g_variant_builder_add_value (&builder, config_extra_params);

    all_params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    params_str = g_variant_print (all_params, FALSE);

    *task_id = get_next_task_id ();
    log_msg = g_strdup_printf ("Calling the '%s.%s' method on the '%s' object with the following parameters: '%s'",
                               intf, method, obj, params_str);
    log_task_status (*task_id, log_msg);
    g_free (log_msg);

    /* now do the call with all the parameters */
    ret = g_dbus_connection_call_sync (bus, LVM_BUS_NAME, obj, intf, method, all_params,
                                       NULL, G_DBUS_CALL_FLAGS_NONE, METHOD_CALL_TIMEOUT, NULL, error);

    if (lock_config)
         g_mutex_unlock (&global_config_lock);
    prog_msg = g_strdup_printf ("Started the '%s.%s' method on the '%s' object with the following parameters: '%s'",
                               intf, method, obj, params_str);
    g_free (params_str);
    *progress_id = bd_utils_report_started (prog_msg);
    g_free (prog_msg);

    if (!ret) {
        g_prefix_error (error, "Failed to call the '%s' method on the '%s' object: ", method, obj);
        return NULL;
    }

    return ret;
}

static void call_lvm_method_sync (const gchar *obj, const gchar *intf, const gchar *method, GVariant *params, GVariant *extra_params, const BDExtraArg **extra_args, gboolean lock_config, GError **error) {
    GVariant *ret = NULL;
    gchar *obj_path = NULL;
    gchar *task_path = NULL;
    guint64 log_task_id = 0;
    guint64 prog_id = 0;
    gdouble progress = 0.0;
    gchar *log_msg = NULL;
    gboolean completed = FALSE;
    gint64 error_code = 0;
    gchar *error_msg = NULL;

    ret = call_lvm_method (obj, intf, method, params, extra_params, extra_args, &log_task_id, &prog_id, lock_config, error);
    log_task_status (log_task_id, "Done.");
    if (!ret) {
        if (*error) {
            log_msg = g_strdup_printf ("Got error: %s", (*error)->message);
            log_task_status (log_task_id, log_msg);
            bd_utils_report_finished (prog_id, log_msg);
            g_free (log_msg);
        } else {
            log_task_status (log_task_id, "Got unknown error");
            bd_utils_report_finished (prog_id, "Got unknown error");
        }
        return;
    }
    if (g_variant_check_format_string (ret, "((oo))", TRUE)) {
        g_variant_get (ret, "((oo))", &obj_path, &task_path);
        if (g_strcmp0 (obj_path, "/") != 0) {
            log_msg = g_strdup_printf ("Got result: %s", obj_path);
            log_task_status (log_task_id, log_msg);
            g_free (log_msg);
            /* got a valid result, just return */
            g_variant_unref (ret);
            g_free (task_path);
            g_free (obj_path);
            bd_utils_report_finished (prog_id, "Completed");
            return;
        } else {
            g_variant_unref (ret);
            g_free (obj_path);
        }
    } else if (g_variant_check_format_string (ret, "(o)", TRUE)) {
        g_variant_get (ret, "(o)", &task_path);
        if (g_strcmp0 (task_path, "/") != 0) {
            g_variant_unref (ret);
        } else {
            log_task_status (log_task_id, "No result, no job started");
            g_free (task_path);
            bd_utils_report_finished (prog_id, "Completed");
            g_variant_unref (ret);
            return;
        }
    } else {
        g_variant_unref (ret);
        log_task_status (log_task_id, "Failed to parse the returned value!");
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                     "Failed to parse the returned value!");
        bd_utils_report_finished (prog_id, (*error)->message);
        return;
    }

    log_msg = g_strdup_printf ("Waiting for job '%s' to finish", task_path);
    log_task_status (log_task_id, log_msg);
    g_free (log_msg);

    ret = NULL;
    while (!completed && !(*error)) {
        g_usleep (PROGRESS_WAIT);
        ret = get_object_property (task_path, JOB_INTF, "Complete", error);
        if (ret) {
            g_variant_get (ret, "b", &completed);
            g_variant_unref (ret);
            ret = NULL;
        }
        if (!completed && !(*error)) {
            /* let's report progress and wait longer */
            ret = get_object_property (task_path, JOB_INTF, "Percent", error);
            if (ret) {
                g_variant_get (ret, "d", &progress);
                bd_utils_report_progress (prog_id, (gint) progress, NULL);
                g_variant_unref (ret);
                ret = NULL;
            } else {
                g_debug ("Got error when getting progress: %s", (*error)->message);
                g_clear_error (error);
            }
            log_msg = g_strdup_printf ("Still waiting for job '%s' to finish", task_path);
            log_task_status (log_task_id, log_msg);
            g_free (log_msg);
        }

    }
    log_msg = g_strdup_printf ("Job '%s' finished", task_path);
    log_task_status (log_task_id, log_msg);
    g_free (log_msg);

    obj_path = NULL;
    if (!(*error)) {
        ret = get_object_property (task_path, JOB_INTF, "Result", error);
        if (!ret) {
            g_prefix_error (error, "Getting result after waiting for '%s' method of the '%s' object failed: ",
                            method, obj);
            bd_utils_report_finished (prog_id, (*error)->message);
            g_free (task_path);
            return;
        } else {
            g_variant_get (ret, "o", &obj_path);
            g_variant_unref (ret);
            if (g_strcmp0 (obj_path, "/") != 0) {
                log_msg = g_strdup_printf ("Got result: %s", obj_path);
                log_task_status (log_task_id, log_msg);
                g_free (log_msg);
            } else {
                ret = get_object_property (task_path, JOB_INTF, "GetError", error);
                g_variant_get (ret, "(is)", &error_code, &error_msg);
                if (error_code != 0) {
                    if (error_msg) {
                        log_msg = g_strdup_printf ("Got error: %s", error_msg);
                        log_task_status (log_task_id, log_msg);
                        bd_utils_report_finished (prog_id, log_msg);
                        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_FAIL,
                                     "Running '%s' method on the '%s' object failed: %s",
                                     method, obj, error_msg);
                        g_free (log_msg);
                        g_free (error_msg);
                    } else {
                        log_task_status (log_task_id, "Got unknown error");
                        bd_utils_report_finished (prog_id, "Got unknown error");
                        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_FAIL,
                                     "Got unknown error when running '%s' method on the '%s' object.",
                                     method, obj);
                    }

                } else
                    log_task_status (log_task_id, "No result");
            }
            bd_utils_report_finished (prog_id, "Completed");
            g_free (obj_path);

            /* remove the job object and clean after ourselves */
            ret = g_dbus_connection_call_sync (bus, LVM_BUS_NAME, task_path, JOB_INTF, "Remove", NULL,
                                               NULL, G_DBUS_CALL_FLAGS_NONE, METHOD_CALL_TIMEOUT, NULL, NULL);
            if (ret)
                g_variant_unref (ret);

            g_free (task_path);
            return;
        }
    } else {
        /* some real error */
        g_prefix_error (error, "Waiting for '%s' method of the '%s' object to finish failed: ",
                        method, obj);
        bd_utils_report_finished (prog_id, "Completed");
    }
    g_free (task_path);
}

static void call_lvm_obj_method_sync (const gchar *obj_id, const gchar *intf, const gchar *method, GVariant *params, GVariant *extra_params, const BDExtraArg **extra_args, gboolean lock_config, GError **error) {
    gchar *obj_path = get_object_path (obj_id, error);
    if (!obj_path)
        return;

    call_lvm_method_sync (obj_path, intf, method, params, extra_params, extra_args, lock_config, error);
    g_free (obj_path);
}

static void call_lv_method_sync (const gchar *vg_name, const gchar *lv_name, const gchar *method, GVariant *params, GVariant *extra_params, const BDExtraArg **extra_args, gboolean lock_config, GError **error) {
    gchar *obj_id = g_strdup_printf ("%s/%s", vg_name, lv_name);

    call_lvm_obj_method_sync (obj_id, LV_INTF, method, params, extra_params, extra_args, lock_config, error);
    g_free (obj_id);
}

static void call_thpool_method_sync (const gchar *vg_name, const gchar *pool_name, const gchar *method, GVariant *params, GVariant *extra_params, const BDExtraArg **extra_args, gboolean lock_config, GError **error) {
    gchar *obj_id = g_strdup_printf ("%s/%s", vg_name, pool_name);

    call_lvm_obj_method_sync (obj_id, THPOOL_INTF, method, params, extra_params, extra_args, lock_config, error);
    g_free (obj_id);
}

static GVariant* get_lv_property (const gchar *vg_name, const gchar *lv_name, const gchar *property, GError **error) {
    gchar *lv_spec = NULL;
    GVariant *ret = NULL;

    lv_spec = g_strdup_printf ("%s/%s", vg_name, lv_name);

    ret = get_lvm_object_property (lv_spec, LV_CMN_INTF, property, error);
    g_free (lv_spec);

    return ret;
}

static GVariant* get_object_properties (const gchar *obj_path, const gchar *iface, GError **error) {
    GVariant *args = NULL;
    GVariant *ret = NULL;
    GVariant *real_ret = NULL;

    args = g_variant_new ("(s)", iface);

    /* consumes (frees) the 'args' parameter */
    ret = g_dbus_connection_call_sync (bus, LVM_BUS_NAME, obj_path, DBUS_PROPS_IFACE,
                                       "GetAll", args, NULL, G_DBUS_CALL_FLAGS_NONE,
                                       -1, NULL, error);
    if (!ret) {
        g_prefix_error (error, "Failed to get properties of the %s object: ", obj_path);
        return NULL;
    }

    real_ret = g_variant_get_child_value (ret, 0);
    g_variant_unref (ret);

    return real_ret;
}

static GVariant* get_lvm_object_properties (const gchar *obj_id, const gchar *iface, GError **error) {
    GVariant *args = NULL;
    GVariant *ret = NULL;
    gchar *obj_path = NULL;

    args = g_variant_new ("(s)", obj_id);
    /* consumes (frees) the 'args' parameter */
    ret = g_dbus_connection_call_sync (bus, LVM_BUS_NAME, MANAGER_OBJ, MANAGER_INTF,
                                       "LookUpByLvmId", args, NULL, G_DBUS_CALL_FLAGS_NONE,
                                       -1, NULL, error);
    g_variant_get (ret, "(o)", &obj_path);
    g_variant_unref (ret);

    if (g_strcmp0 (obj_path, "/") == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_NOEXIST,
                     "The object with LVM ID '%s' doesn't exist", obj_id);
        g_free (obj_path);
        return NULL;
    }

    ret = get_object_properties (obj_path, iface, error);
    g_free (obj_path);
    return ret;
}


static GVariant* get_pv_properties (const gchar *pv_name, GError **error) {
    gchar *obj_id = NULL;
    GVariant *ret = NULL;

    if (!g_str_has_prefix (pv_name, "/dev/")) {
        obj_id = g_strdup_printf ("/dev/%s", pv_name);
        ret = get_lvm_object_properties (obj_id, PV_INTF, error);
        g_free (obj_id);
    } else
        ret = get_lvm_object_properties (pv_name, PV_INTF, error);

    return ret;
}

static GVariant* get_vg_properties (const gchar *vg_name, GError **error) {
    GVariant *ret = NULL;

    ret = get_lvm_object_properties (vg_name, VG_INTF, error);

    return ret;
}

static GVariant* get_lv_properties (const gchar *vg_name, const gchar *lv_name, GError **error) {
    gchar *lvm_spec = NULL;
    GVariant *ret = NULL;

    lvm_spec = g_strdup_printf ("%s/%s", vg_name, lv_name);

    ret = get_lvm_object_properties (lvm_spec, LV_CMN_INTF, error);
    g_free (lvm_spec);

    return ret;
}

static GVariant* get_vdo_properties (const gchar *vg_name, const gchar *pool_name, GError **error) {
    gchar *lvm_spec = NULL;
    GVariant *ret = NULL;

    lvm_spec = g_strdup_printf ("%s/%s", vg_name, pool_name);

    ret = get_lvm_object_properties (lvm_spec, VDO_POOL_INTF, error);
    g_free (lvm_spec);

    return ret;
}

static BDLVMPVdata* get_pv_data_from_props (GVariant *props, GError **error) {
    BDLVMPVdata *data = g_new0 (BDLVMPVdata, 1);
    GVariantDict dict;
    gchar *value = NULL;
    GVariant *vg_props = NULL;

    g_variant_dict_init (&dict, props);

    g_variant_dict_lookup (&dict, "Name", "s", &(data->pv_name));
    g_variant_dict_lookup (&dict, "Uuid", "s", &(data->pv_uuid));
    g_variant_dict_lookup (&dict, "FreeBytes", "t", &(data->pv_free));
    g_variant_dict_lookup (&dict, "SizeBytes", "t", &(data->pv_size));
    g_variant_dict_lookup (&dict, "PeStart", "t", &(data->pe_start));

    /* returns an object path for the VG */
    g_variant_dict_lookup (&dict, "Vg", "&o", &value);
    if (g_strcmp0 (value, "/") == 0) {
        /* no VG, the PV is not part of any VG */
        g_variant_dict_clear (&dict);
        return data;
    }

    vg_props = get_object_properties (value, VG_INTF, error);
    g_variant_dict_clear (&dict);
    if (!vg_props)
        return data;

    g_variant_dict_init (&dict, vg_props);
    g_variant_dict_lookup (&dict, "Name", "s", &(data->vg_name));
    g_variant_dict_lookup (&dict, "Uuid", "s", &(data->vg_uuid));
    g_variant_dict_lookup (&dict, "SizeBytes", "t", &(data->vg_size));
    g_variant_dict_lookup (&dict, "FreeBytes", "t", &(data->vg_free));
    g_variant_dict_lookup (&dict, "ExtentSizeBytes", "t", &(data->vg_extent_size));
    g_variant_dict_lookup (&dict, "ExtentCount", "t", &(data->vg_extent_count));
    g_variant_dict_lookup (&dict, "FreeCount", "t", &(data->vg_free_count));
    g_variant_dict_lookup (&dict, "PvCount", "t", &(data->vg_pv_count));

    g_variant_dict_clear (&dict);
    g_variant_unref (vg_props);

    return data;
}

static BDLVMVGdata* get_vg_data_from_props (GVariant *props, GError **error UNUSED) {
    BDLVMVGdata *data = g_new0 (BDLVMVGdata, 1);
    GVariantDict dict;

    g_variant_dict_init (&dict, props);

    g_variant_dict_lookup (&dict, "Name", "s", &(data->name));
    g_variant_dict_lookup (&dict, "Uuid", "s", &(data->uuid));

    g_variant_dict_lookup (&dict, "SizeBytes", "t", &(data->size));
    g_variant_dict_lookup (&dict, "FreeBytes", "t", &(data->free));
    g_variant_dict_lookup (&dict, "ExtentSizeBytes", "t", &(data->extent_size));
    g_variant_dict_lookup (&dict, "ExtentCount", "t", &(data->extent_count));
    g_variant_dict_lookup (&dict, "FreeCount", "t", &(data->free_count));
    g_variant_dict_lookup (&dict, "PvCount", "t", &(data->pv_count));

    g_variant_dict_clear (&dict);

    return data;
}

static BDLVMLVdata* get_lv_data_from_props (GVariant *props, GError **error) {
    BDLVMLVdata *data = g_new0 (BDLVMLVdata, 1);
    GVariantDict dict;
    GVariant *value = NULL;
    gchar *path = NULL;
    GVariant *name = NULL;
    gsize n_children = 0;
    gsize i = 0;
    gchar **roles = NULL;

    g_variant_dict_init (&dict, props);

    g_variant_dict_lookup (&dict, "Name", "s", &(data->lv_name));
    g_variant_dict_lookup (&dict, "Uuid", "s", &(data->uuid));
    g_variant_dict_lookup (&dict, "Attr", "s", &(data->attr));
    g_variant_dict_lookup (&dict, "SizeBytes", "t", &(data->size));
    g_variant_dict_lookup (&dict, "DataPercent", "u", &(data->data_percent));
    g_variant_dict_lookup (&dict, "MetaDataPercent", "u", &(data->metadata_percent));
    g_variant_dict_lookup (&dict, "CopyPercent", "u", &(data->copy_percent));

    /* XXX: how to deal with LVs with multiple segment types? We are just taking
            the first one now. */
    value = g_variant_dict_lookup_value (&dict, "SegType", (GVariantType*) "as");
    if (value) {
        g_variant_get_child (value, 0, "s", &(data->segtype));
        g_variant_unref (value);
    }

    value = g_variant_dict_lookup_value (&dict, "Roles", (GVariantType*) "as");
    if (value) {
        n_children = g_variant_n_children (value);
        roles = g_new0 (gchar*, n_children + 1);
        for (i=0; i < n_children; i++)
            g_variant_get_child (value, i, "&s", roles+i);
        data->roles = g_strjoinv (",", roles);
        g_free (roles);
        g_variant_unref (value);
    }

    /* returns an object path for the VG */
    g_variant_dict_lookup (&dict, "Vg", "o", &path);
    name = get_object_property (path, VG_INTF, "Name", error);
    g_free (path);
    g_variant_get (name, "s", &(data->vg_name));
    g_variant_unref (name);

    g_variant_dict_lookup (&dict, "OriginLv", "o", &path);
    if (g_strcmp0 (path, "/") != 0) {
        name = get_object_property (path, LV_CMN_INTF, "Name", error);
        g_variant_get (name, "s", &(data->origin));
        g_variant_unref (name);
    }
    g_free (path);
    path = NULL;

    g_variant_dict_lookup (&dict, "PoolLv", "o", &path);
    if (g_strcmp0 (path, "/") != 0) {
        name = get_object_property (path, LV_CMN_INTF, "Name", error);
        g_variant_get (name, "s", &(data->pool_lv));
        g_variant_unref (name);
    }
    g_free (path);
    path = NULL;

    g_variant_dict_lookup (&dict, "MovePv", "o", &path);
    if (path && g_strcmp0 (path, "/") != 0) {
        g_debug ("Have path");
        g_debug ("  %s", path);
        name = get_object_property (path, PV_INTF, "Name", error);
        g_variant_get (name, "s", &(data->move_pv));
        g_variant_unref (name);
    }
    g_free (path);
    path = NULL;

    g_variant_dict_clear (&dict);
    g_variant_unref (props);

    return data;
}

static BDLVMVDOPooldata* get_vdo_data_from_props (GVariant *props, GError **error UNUSED) {
    BDLVMVDOPooldata *data = g_new0 (BDLVMVDOPooldata, 1);
    GVariantDict dict;
    gchar *value = NULL;

    g_variant_dict_init (&dict, props);

    g_variant_dict_lookup (&dict, "OperatingMode", "s", &value);
    if (g_strcmp0 (value, "recovering") == 0)
        data->operating_mode = BD_LVM_VDO_MODE_RECOVERING;
    else if (g_strcmp0 (value, "read-only") == 0)
        data->operating_mode = BD_LVM_VDO_MODE_READ_ONLY;
    else if (g_strcmp0 (value, "normal") == 0)
        data->operating_mode = BD_LVM_VDO_MODE_NORMAL;
    else {
        g_debug ("Unknown VDO operating mode: %s", value);
        data->operating_mode = BD_LVM_VDO_MODE_UNKNOWN;
    }
    g_free (value);

    g_variant_dict_lookup (&dict, "CompressionState", "s", &value);
    if (g_strcmp0 (value, "online") == 0)
        data->compression_state = BD_LVM_VDO_COMPRESSION_ONLINE;
    else if (g_strcmp0 (value, "offline") == 0)
        data->compression_state = BD_LVM_VDO_COMPRESSION_OFFLINE;
    else {
        g_debug ("Unknown VDO compression state: %s", value);
        data->compression_state = BD_LVM_VDO_COMPRESSION_UNKNOWN;
    }
    g_free (value);

    g_variant_dict_lookup (&dict, "IndexState", "s", &value);
    if (g_strcmp0 (value, "error") == 0)
        data->index_state = BD_LVM_VDO_INDEX_ERROR;
    else if (g_strcmp0 (value, "closed") == 0)
        data->index_state = BD_LVM_VDO_INDEX_CLOSED;
    else if (g_strcmp0 (value, "opening") == 0)
        data->index_state = BD_LVM_VDO_INDEX_OPENING;
    else if (g_strcmp0 (value, "closing") == 0)
        data->index_state = BD_LVM_VDO_INDEX_CLOSING;
    else if (g_strcmp0 (value, "offline") == 0)
        data->index_state = BD_LVM_VDO_INDEX_OFFLINE;
    else if (g_strcmp0 (value, "online") == 0)
        data->index_state = BD_LVM_VDO_INDEX_ONLINE;
    else {
        g_debug ("Unknown VDO index state: %s", value);
        data->index_state = BD_LVM_VDO_INDEX_UNKNOWN;
    }
    g_free (value);

    g_variant_dict_lookup (&dict, "WritePolicy", "s", &value);
    if (g_strcmp0 (value, "auto") == 0)
        data->write_policy = BD_LVM_VDO_WRITE_POLICY_AUTO;
    else if (g_strcmp0 (value, "sync") == 0)
        data->write_policy = BD_LVM_VDO_WRITE_POLICY_SYNC;
    else if (g_strcmp0 (value, "async") == 0)
        data->write_policy = BD_LVM_VDO_WRITE_POLICY_ASYNC;
    else {
        g_debug ("Unknown VDO write policy: %s", value);
        data->write_policy = BD_LVM_VDO_WRITE_POLICY_UNKNOWN;
    }
    g_free (value);

    g_variant_dict_lookup (&dict, "UsedSize", "t", &(data->used_size));
    g_variant_dict_lookup (&dict, "SavingPercent", "d", &(data->saving_percent));

    g_variant_dict_lookup (&dict, "IndexMemorySize", "t", &(data->index_memory_size));

    g_variant_dict_lookup (&dict, "Compression", "s", &value);
    if (value && g_strcmp0 (value, "enabled") == 0)
        data->compression = TRUE;
    else
        data->compression = FALSE;
    g_free (value);

    g_variant_dict_lookup (&dict, "Deduplication", "s", &value);
    if (value && g_strcmp0 (value, "enabled") == 0)
        data->deduplication = TRUE;
    else
        data->deduplication = FALSE;
    g_free (value);

    g_variant_dict_clear (&dict);
    g_variant_unref (props);

    return data;
}

static GVariant* create_size_str_param (guint64 size, const gchar *unit) {
    gchar *str = NULL;

    str = g_strdup_printf ("%"G_GUINT64_FORMAT"%s", size, unit ? unit : "");
    return g_variant_new_take_string (str);
}

/**
 * bd_lvm_is_supported_pe_size:
 * @size: size (in bytes) to test
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given size is supported physical extent size or not
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
gboolean bd_lvm_is_supported_pe_size (guint64 size, GError **error UNUSED) {
    return (((size % 2) == 0) && (size >= (BD_LVM_MIN_PE_SIZE)) && (size <= (BD_LVM_MAX_PE_SIZE)));
}

/**
 * bd_lvm_get_supported_pe_sizes:
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full) (array zero-terminated=1): list of supported PE sizes
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
guint64 *bd_lvm_get_supported_pe_sizes (GError **error UNUSED) {
    guint8 i;
    guint64 val = BD_LVM_MIN_PE_SIZE;
    guint8 num_items = ((guint8) round (log2 ((double) BD_LVM_MAX_PE_SIZE))) - ((guint8) round (log2 ((double) BD_LVM_MIN_PE_SIZE))) + 2;
    guint64 *ret = g_new0 (guint64, num_items);

    for (i=0; (val <= BD_LVM_MAX_PE_SIZE); i++, val = val * 2)
        ret[i] = val;

    ret[num_items-1] = 0;

    return ret;
}

/**
 * bd_lvm_get_max_lv_size:
 * @error: (out): place to store error (if any)
 *
 * Returns: maximum LV size in bytes
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_get_max_lv_size (GError **error UNUSED) {
    return BD_LVM_MAX_LV_SIZE;
}

/**
 * bd_lvm_round_size_to_pe:
 * @size: size to be rounded
 * @pe_size: physical extent (PE) size or 0 to use the default
 * @roundup: whether to round up or down (ceil or floor)
 * @error: (out): place to store error (if any)
 *
 * Returns: @size rounded to @pe_size according to the @roundup
 *
 * Rounds given @size up/down to a multiple of @pe_size according to the value
 * of the @roundup parameter. If the rounded value is too big to fit in the
 * return type, the result is rounded down (floored) regardless of the @roundup
 * parameter.
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_round_size_to_pe (guint64 size, guint64 pe_size, gboolean roundup, GError **error UNUSED) {
    pe_size = RESOLVE_PE_SIZE(pe_size);
    guint64 delta = size % pe_size;
    if (delta == 0)
        return size;

    if (roundup && (((G_MAXUINT64 - (pe_size - delta)) >= size)))
        return size + (pe_size - delta);
    else
        return size - delta;
}

/**
 * bd_lvm_get_lv_physical_size:
 * @lv_size: LV size
 * @pe_size: PE size
 * @error: (out): place to store error (if any)
 *
 * Returns: space taken on disk(s) by the LV with given @size
 *
 * Gives number of bytes needed for an LV with the size @lv_size on an LVM stack
 * using given @pe_size.
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_get_lv_physical_size (guint64 lv_size, guint64 pe_size, GError **error) {
    pe_size = RESOLVE_PE_SIZE(pe_size);

    /* the LV just takes space rounded up the the a multiple of extent size */
    return bd_lvm_round_size_to_pe (lv_size, pe_size, TRUE, error);
}

/**
 * bd_lvm_get_thpool_padding:
 * @size: size of the thin pool
 * @pe_size: PE size or 0 if the default value should be used
 * @included: if padding is already included in the size
 * @error: (out): place to store error (if any)
 *
 * Returns: size of the padding needed for a thin pool with the given @size
 *         according to the @pe_size and @included
 *
 * Tech category: %BD_LVM_TECH_THIN_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_get_thpool_padding (guint64 size, guint64 pe_size, gboolean included, GError **error) {
    guint64 raw_md_size;
    pe_size = RESOLVE_PE_SIZE(pe_size);

    if (included)
        raw_md_size = (guint64) ceil (size * THPOOL_MD_FACTOR_EXISTS);
    else
        raw_md_size = (guint64) ceil (size * THPOOL_MD_FACTOR_NEW);

    return MIN (bd_lvm_round_size_to_pe (raw_md_size, pe_size, TRUE, error),
                bd_lvm_round_size_to_pe (BD_LVM_MAX_THPOOL_MD_SIZE, pe_size, TRUE, error));
}

/**
 * bd_lvm_get_thpool_meta_size:
 * @size: size of the thin pool
 * @chunk_size: chunk size of the thin pool or 0 to use the default (%BD_LVM_DEFAULT_CHUNK_SIZE)
 * @n_snapshots: ignored
 * @error: (out): place to store error (if any)
 *
 * Note: This function will be changed in 3.0: the @n_snapshots parameter
 *       is currently not used and will be removed.
 *
 * Returns: recommended size of the metadata space for the specified pool
 *
 * Tech category: %BD_LVM_TECH_THIN_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_get_thpool_meta_size (guint64 size, guint64 chunk_size, guint64 n_snapshots UNUSED, GError **error UNUSED) {
    guint64 md_size = 0;

    /* based on lvcreate metadata size calculation */
    md_size = UINT64_C(64) * size / (chunk_size ? chunk_size : BD_LVM_DEFAULT_CHUNK_SIZE);

    if (md_size > BD_LVM_MAX_THPOOL_MD_SIZE)
        md_size = BD_LVM_MAX_THPOOL_MD_SIZE;
    else if (md_size < BD_LVM_MIN_THPOOL_MD_SIZE)
        md_size = BD_LVM_MIN_THPOOL_MD_SIZE;

    return md_size;
}

/**
 * bd_lvm_is_valid_thpool_md_size:
 * @size: the size to be tested
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given size is a valid thin pool metadata size or not
 *
 * Tech category: %BD_LVM_TECH_THIN_CALCS no mode (it is ignored)
 */
gboolean bd_lvm_is_valid_thpool_md_size (guint64 size, GError **error UNUSED) {
    return ((BD_LVM_MIN_THPOOL_MD_SIZE <= size) && (size <= BD_LVM_MAX_THPOOL_MD_SIZE));
}

/**
 * bd_lvm_is_valid_thpool_chunk_size:
 * @size: the size to be tested
 * @discard: whether discard/TRIM is required to be supported or not
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given size is a valid thin pool chunk size or not
 *
 * Tech category: %BD_LVM_TECH_THIN_CALCS no mode (it is ignored)
 */
gboolean bd_lvm_is_valid_thpool_chunk_size (guint64 size, gboolean discard, GError **error UNUSED) {
    gdouble size_log2 = 0.0;

    if ((size < BD_LVM_MIN_THPOOL_CHUNK_SIZE) || (size > BD_LVM_MAX_THPOOL_CHUNK_SIZE))
        return FALSE;

    /* To support discard, chunk size must be a power of two. Otherwise it must be a
       multiple of 64 KiB. */
    if (discard) {
        size_log2 = log2 ((double) size);
        return ABS (((int) round (size_log2)) - size_log2) <= INT_FLOAT_EPS;
    } else
        return (size % (64 KiB)) == 0;
}

/**
 * bd_lvm_pvcreate:
 * @device: the device to make PV from
 * @data_alignment: data (first PE) alignment or 0 to use the default
 * @metadata_size: size of the area reserved for metadata or 0 to use the default
 * @extra: (allow-none) (array zero-terminated=1): extra options for the PV creation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the PV was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_pvcreate (const gchar *device, guint64 data_alignment, guint64 metadata_size, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariant *param = NULL;
    GVariant *params = NULL;
    GVariant *extra_params = NULL;

    if (data_alignment != 0 || metadata_size != 0) {
        g_variant_builder_init (&builder, G_VARIANT_TYPE_DICTIONARY);
        if (data_alignment != 0) {
            param = create_size_str_param (data_alignment, "b");
            g_variant_builder_add (&builder, "{sv}", "dataalignment", param);
        }

        if (metadata_size != 0) {
            param = create_size_str_param (metadata_size, "b");
            g_variant_builder_add (&builder, "{sv}", "metadatasize", param);
        }
        extra_params = g_variant_builder_end (&builder);
        g_variant_builder_clear (&builder);
    }

    params = g_variant_new ("(s)", device);

    call_lvm_method_sync (MANAGER_OBJ, MANAGER_INTF, "PvCreate", params, extra_params, extra, TRUE, error);
    return ((*error) == NULL);
}

/**
 * bd_lvm_pvresize:
 * @device: the device to resize
 * @size: the new requested size of the PV or 0 if it should be adjusted to device's size
 * @extra: (allow-none) (array zero-terminated=1): extra options for the PV resize
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the PV's size was successfully changed or not
 *
 * If given @size different from 0, sets the PV's size to the given value (see
 * pvresize(8)). If given @size 0, adjusts the PV's size to the underlaying
 * block device's size.
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_pvresize (const gchar *device, guint64 size, const BDExtraArg **extra, GError **error) {
    GVariant *params = NULL;
    gchar *obj_path = get_object_path (device, error);
    if (!obj_path)
        return FALSE;

    params = g_variant_new ("(u)", size);
    call_lvm_method_sync (obj_path, PV_INTF, "ReSize", params, NULL, extra, TRUE, error);

    return (*error == NULL);
}

/**
 * bd_lvm_pvremove:
 * @device: the PV device to be removed/destroyed
 * @extra: (allow-none) (array zero-terminated=1): extra options for the PV removal
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the PV was successfully removed/destroyed or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_REMOVE
 */
gboolean bd_lvm_pvremove (const gchar *device, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariant *params = NULL;

    if (access (device, F_OK) != 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_NOEXIST,
                     "The device '%s' doesn't exist", device);
        return FALSE;
    }

    /* one has to be really persuasive to remove a PV (the double --force is not
       bug, at least not in this code) */
    g_variant_builder_init (&builder, G_VARIANT_TYPE_DICTIONARY);
    g_variant_builder_add (&builder, "{sv}", "-ff", g_variant_new ("s", ""));
    g_variant_builder_add (&builder, "{sv}", "--yes", g_variant_new ("s", ""));

    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);
    call_lvm_obj_method_sync (device, PV_INTF, "Remove", NULL, params, extra, TRUE, error);
    if (*error && g_error_matches (*error, BD_LVM_ERROR, BD_LVM_ERROR_NOEXIST))
        /* if the object doesn't exist, the given device is not a PV and thus
           this function should be a noop */
        g_clear_error (error);

    return (*error == NULL);
}

/**
 * bd_lvm_pvmove:
 * @src: the PV device to move extents off of
 * @dest: (allow-none): the PV device to move extents onto or %NULL
 * @extra: (allow-none) (array zero-terminated=1): extra options for the PV move
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the extents from the @src PV where successfully moved or not
 *
 * If @dest is %NULL, VG allocation rules are used for the extents from the @src
 * PV (see pvmove(8)).
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_pvmove (const gchar *src, const gchar *dest, const BDExtraArg **extra, GError **error) {
    GVariant *prop = NULL;
    gchar *src_path = NULL;
    gchar *dest_path = NULL;
    gchar *vg_obj_path = NULL;
    GVariantBuilder builder;
    GVariantType *type = NULL;
    GVariant *dest_var = NULL;
    GVariant *params = NULL;

    src_path = get_object_path (src, error);
    if (!src_path || (g_strcmp0 (src_path, "/") == 0)) {
        if (!(*error))
            g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_NOEXIST,
                         "The source PV '%s' doesn't exist", src);
        return FALSE;
    }
    if (dest) {
        dest_path = get_object_path (dest, error);
        if (!dest_path || (g_strcmp0 (dest_path, "/") == 0)) {
            if (!(*error))
                g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_NOEXIST,
                             "The destination PV '%s' doesn't exist", dest);
            return FALSE;
        }
    }
    prop = get_object_property (src_path, PV_INTF, "Vg", error);
    if (!prop) {
        g_free (src_path);
        return FALSE;
    }
    g_variant_get (prop, "o", &vg_obj_path);

    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("o", src_path));
    g_variant_builder_add_value (&builder, g_variant_new ("(tt)", (guint64) 0, (guint64) 0));
    if (dest) {
        dest_var = g_variant_new ("(ott)", dest_path, (guint64) 0, (guint64) 0);
        g_variant_builder_add_value (&builder, g_variant_new_array (NULL, &dest_var, 1));
    } else {
        type = g_variant_type_new ("a(ott)");
        g_variant_builder_add_value (&builder, g_variant_new_array (type, NULL, 0));
        g_variant_type_free (type);
    }
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    call_lvm_method_sync (vg_obj_path, VG_INTF, "Move", params, NULL, extra, TRUE, error);

    g_free (src_path);
    g_free (dest_path);
    g_free (vg_obj_path);
    return ((*error) == NULL);
}

/**
 * bd_lvm_pvscan:
 * @device: (allow-none): the device to scan for PVs or %NULL
 * @update_cache: whether to update the lvmetad cache or not
 * @extra: (allow-none) (array zero-terminated=1): extra options for the PV scan
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the system or @device was successfully scanned for PVs or not
 *
 * The @device argument is used only if @update_cache is %TRUE. Otherwise the
 * whole system is scanned for PVs.
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
gboolean bd_lvm_pvscan (const gchar *device, gboolean update_cache, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariantType *type = NULL;
    GVariant *params = NULL;
    GVariant *device_var = NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    /* update the cache and specify the device (if any) */
    g_variant_builder_add_value (&builder, g_variant_new_boolean (FALSE));
    g_variant_builder_add_value (&builder, g_variant_new_boolean (update_cache));
    if (update_cache && device) {
        device_var = g_variant_new ("s", device);
        g_variant_builder_add_value (&builder, g_variant_new_array (NULL, &device_var, 1));
    } else {
        type = g_variant_type_new ("as");
        g_variant_builder_add_value (&builder, g_variant_new_array (type, NULL, 0));
        g_variant_type_free (type);
    }
    /* (major, minor)`s, we never specify them */
    type = g_variant_type_new ("a(ii)");
    g_variant_builder_add_value (&builder, g_variant_new_array (type, NULL, 0));
    g_variant_type_free (type);

    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    call_lvm_method_sync (MANAGER_OBJ, MANAGER_INTF, "PvScan", params, NULL, extra, TRUE, error);
    return ((*error) == NULL);
}

/**
 * bd_lvm_pvinfo:
 * @device: a PV to get information about or %NULL
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the PV on the given @device or
 * %NULL in case of error (the @error) gets populated in those cases)
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMPVdata* bd_lvm_pvinfo (const gchar *device, GError **error) {
    GVariant *props = NULL;
    BDLVMPVdata *ret = NULL;

    props = get_pv_properties (device, error);
    if (!props)
        /* the error is already populated */
        return NULL;

    ret = get_pv_data_from_props (props, error);
    g_variant_unref (props);

    return ret;
}

/**
 * bd_lvm_pvs:
 * @error: (out): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about PVs found in the system
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMPVdata** bd_lvm_pvs (GError **error) {
    gchar **objects = NULL;
    guint64 n_pvs = 0;
    GVariant *props = NULL;
    BDLVMPVdata **ret = NULL;
    guint64 i = 0;

    objects = get_existing_objects (PV_OBJ_PREFIX, error);
    if (!objects) {
        if (!(*error)) {
            /* no PVs */
            ret = g_new0 (BDLVMPVdata*, 1);
            ret[0] = NULL;
            return ret;
        } else
            /* error is already populated */
            return NULL;
    }

    n_pvs = g_strv_length ((gchar **) objects);

    /* now create the return value -- NULL-terminated array of BDLVMPVdata */
    ret = g_new0 (BDLVMPVdata*, n_pvs + 1);
    for (i=0; i < n_pvs; i++) {
        props = get_object_properties (objects[i], PV_INTF, error);
        if (!props) {
            g_strfreev (objects);
            g_free (ret);
            return NULL;
        }
        ret[i] = get_pv_data_from_props (props, error);
        g_variant_unref (props);
        if (!(ret[i])) {
            g_strfreev (objects);
            g_free (ret);
            return NULL;
        }
    }
    ret[i] = NULL;

    g_strfreev (objects);
    return ret;
}

/**
 * bd_lvm_vgcreate:
 * @name: name of the newly created VG
 * @pv_list: (array zero-terminated=1): list of PVs the newly created VG should use
 * @pe_size: PE size or 0 if the default value should be used
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VG creation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG @name was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_vgcreate (const gchar *name, const gchar **pv_list, guint64 pe_size, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    gchar *path = NULL;
    const gchar **pv = NULL;
    GVariant *pvs = NULL;
    GVariant *params = NULL;
    GVariant *extra_params = NULL;

    /* build the array of PVs (object paths) */
    g_variant_builder_init (&builder, G_VARIANT_TYPE_OBJECT_PATH_ARRAY);
    for (pv=pv_list; *pv; pv++) {
        path = get_object_path (*pv, error);
        if (!path) {
            g_variant_builder_clear (&builder);
            return FALSE;
        }
        g_variant_builder_add_value (&builder, g_variant_new ("o", path));
    }
    pvs = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    /* build the params tuple */
    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("s", name));
    g_variant_builder_add_value (&builder, pvs);
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    /* pe_size needs to go to extra_params params */
    pe_size = RESOLVE_PE_SIZE (pe_size);
    g_variant_builder_init (&builder, G_VARIANT_TYPE_DICTIONARY);
    g_variant_builder_add_value (&builder, g_variant_new ("{sv}", "--physicalextentsize", create_size_str_param (pe_size, "b")));
    extra_params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    call_lvm_method_sync (MANAGER_OBJ, MANAGER_INTF, "VgCreate", params, extra_params, extra, TRUE, error);
    return ((*error) == NULL);
}

/**
 * bd_lvm_vgremove:
 * @vg_name: name of the to be removed VG
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VG removal
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG was successfully removed or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_REMOVE
 */
gboolean bd_lvm_vgremove (const gchar *vg_name, const BDExtraArg **extra, GError **error) {
    call_lvm_obj_method_sync (vg_name, VG_INTF, "Remove", NULL, NULL, extra, TRUE, error);
    return ((*error) == NULL);
}

/**
 * bd_lvm_vgrename:
 * @old_vg_name: old name of the VG to rename
 * @new_vg_name: new name for the @old_vg_name VG
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VG rename
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG was successfully renamed or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vgrename (const gchar *old_vg_name, const gchar *new_vg_name, const BDExtraArg **extra, GError **error) {
    GVariant *params = g_variant_new ("(s)", new_vg_name);
    call_lvm_obj_method_sync (old_vg_name, VG_INTF, "Rename", params, NULL, extra, TRUE, error);
    return ((*error) == NULL);
}

/**
 * bd_lvm_vgactivate:
 * @vg_name: name of the to be activated VG
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VG activation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG was successfully activated or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vgactivate (const gchar *vg_name, const BDExtraArg **extra, GError **error) {
    GVariant *params = g_variant_new ("(t)", (guint64) 0);
    call_lvm_obj_method_sync (vg_name, VG_INTF, "Activate", params, NULL, extra, TRUE, error);
    return ((*error) == NULL);
}

/**
 * bd_lvm_vgdeactivate:
 * @vg_name: name of the to be deactivated VG
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VG deactivation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG was successfully deactivated or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vgdeactivate (const gchar *vg_name, const BDExtraArg **extra, GError **error) {
    GVariant *params = g_variant_new ("(t)", (guint64) 0);
    call_lvm_obj_method_sync (vg_name, VG_INTF, "Deactivate", params, NULL, extra, TRUE, error);
    return ((*error) == NULL);
}

/**
 * bd_lvm_vgextend:
 * @vg_name: name of the to be extended VG
 * @device: PV device to extend the @vg_name VG with
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VG extension
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG @vg_name was successfully extended with the given @device or not.
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vgextend (const gchar *vg_name, const gchar *device, const BDExtraArg **extra, GError **error) {
    gchar *pv = NULL;
    GVariant *pv_var = NULL;
    GVariant *pvs = NULL;
    GVariant *params = NULL;

    pv = get_object_path (device, error);
    if (!pv)
        return FALSE;

    pv_var = g_variant_new ("o", pv);
    pvs = g_variant_new_array (NULL, &pv_var, 1);
    params = g_variant_new_tuple (&pvs, 1);
    call_lvm_obj_method_sync (vg_name, VG_INTF, "Extend", params, NULL, extra, TRUE, error);
    g_free (pv);
    return ((*error) == NULL);
}

/**
 * bd_lvm_vgreduce:
 * @vg_name: name of the to be reduced VG
 * @device: (allow-none): PV device the @vg_name VG should be reduced of or %NULL
 *                        if the VG should be reduced of the missing PVs
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VG reduction
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VG @vg_name was successfully reduced of the given @device or not
 *
 * Note: This function does not move extents off of the PV before removing
 *       it from the VG. You must do that first by calling #bd_lvm_pvmove.
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vgreduce (const gchar *vg_name, const gchar *device, const BDExtraArg **extra, GError **error) {
    gchar *pv = NULL;
    GVariantBuilder builder;
    GVariantType *type = NULL;
    GVariant *pv_var = NULL;
    GVariant *params = NULL;
    GVariant *extra_params = NULL;

    if (device) {
        pv = get_object_path (device, error);
        if (!pv)
            return FALSE;
    }

    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    if (device) {
        /* do not remove missing */
        pv_var = g_variant_new ("o", pv);
        g_variant_builder_add_value (&builder, g_variant_new_boolean (FALSE));
        g_variant_builder_add_value (&builder, g_variant_new_array (NULL, &pv_var, 1));
        params = g_variant_builder_end (&builder);
        g_variant_builder_clear (&builder);
    } else {
        /* remove missing */
        g_variant_builder_add_value (&builder, g_variant_new_boolean (TRUE));
        type = g_variant_type_new ("ao");
        g_variant_builder_add_value (&builder, g_variant_new_array (type, NULL, 0));
        g_variant_type_free (type);
        params = g_variant_builder_end (&builder);
        g_variant_builder_clear (&builder);

        g_variant_builder_init (&builder, G_VARIANT_TYPE_DICTIONARY);
        g_variant_builder_add_value (&builder, g_variant_new ("{sv}", "--force", g_variant_new ("s", "")));
        extra_params = g_variant_builder_end (&builder);
        g_variant_builder_clear (&builder);
    }

    call_lvm_obj_method_sync (vg_name, VG_INTF, "Reduce", params, extra_params, extra, TRUE, error);
    g_free (pv);
    return ((*error) == NULL);
}

/**
 * bd_lvm_vginfo:
 * @vg_name: a VG to get information about
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the @vg_name VG or %NULL in case
 * of error (the @error) gets populated in those cases)
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMVGdata* bd_lvm_vginfo (const gchar *vg_name, GError **error) {
    GVariant *props = NULL;
    BDLVMVGdata *ret = NULL;

    props = get_vg_properties (vg_name, error);
    if (!props)
        /* the error is already populated */
        return NULL;

    ret = get_vg_data_from_props (props, error);
    g_variant_unref (props);

    return ret;
}

/**
 * bd_lvm_vgs:
 * @error: (out): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about VGs found in the system
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMVGdata** bd_lvm_vgs (GError **error) {
    gchar **objects = NULL;
    guint64 n_vgs = 0;
    GVariant *props = NULL;
    BDLVMVGdata **ret = NULL;
    guint64 i = 0;

    objects = get_existing_objects (VG_OBJ_PREFIX, error);
    if (!objects) {
        if (!(*error)) {
            /* no VGs */
            ret = g_new0 (BDLVMVGdata*, 1);
            ret[0] = NULL;
            return ret;
        } else
            /* error is already populated */
            return NULL;
    }

    n_vgs = g_strv_length ((gchar **) objects);

    /* now create the return value -- NULL-terminated array of BDLVMVGdata */
    ret = g_new0 (BDLVMVGdata*, n_vgs + 1);
    for (i=0; i < n_vgs; i++) {
        props = get_object_properties (objects[i], VG_INTF, error);
        if (!props) {
            g_strfreev (objects);
            g_free (ret);
            return NULL;
        }
        ret[i] = get_vg_data_from_props (props, error);
        g_variant_unref (props);
        if (!(ret[i])) {
            g_strfreev (objects);
            g_free (ret);
            return NULL;
        }
    }
    ret[i] = NULL;

    g_strfreev (objects);
    return ret;
}

/**
 * bd_lvm_lvorigin:
 * @vg_name: name of the VG containing the queried LV
 * @lv_name: name of the queried LV
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): the origin volume for the @vg_name/@lv_name LV or
 * %NULL if failed to determine (@error) is set in those cases)
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
gchar* bd_lvm_lvorigin (const gchar *vg_name, const gchar *lv_name, GError **error) {
    GVariant *prop = NULL;
    gchar *obj_path = NULL;
    gchar *ret = NULL;

    prop = get_lv_property (vg_name, lv_name, "OriginLv", error);
    if (!prop)
        return NULL;
    g_variant_get (prop, "o", &obj_path);
    g_variant_unref (prop);

    if (g_strcmp0 (obj_path, "/") == 0) {
        /* no origin LV */
        g_free (obj_path);
        return NULL;
    }
    prop = get_object_property (obj_path, LV_CMN_INTF, "Name", error);
    if (!prop) {
        g_free (obj_path);
        return NULL;
    }

    g_variant_get (prop, "s", &ret);
    g_variant_unref (prop);

    return ret;
}

/**
 * bd_lvm_lvcreate:
 * @vg_name: name of the VG to create a new LV in
 * @lv_name: name of the to-be-created LV
 * @size: requested size of the new LV
 * @type: (allow-none): type of the new LV ("striped", "raid1",..., see lvcreate (8))
 * @pv_list: (allow-none) (array zero-terminated=1): list of PVs the newly created LV should use or %NULL
 * if not specified
 * @extra: (allow-none) (array zero-terminated=1): extra options for the LV creation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given @vg_name/@lv_name LV was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_lvcreate (const gchar *vg_name, const gchar *lv_name, guint64 size, const gchar *type, const gchar **pv_list, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    gchar *path = NULL;
    const gchar **pv = NULL;
    GVariant *pvs = NULL;
    GVariantType *var_type = NULL;
    GVariant *params = NULL;
    GVariant *extra_params = NULL;

    /* build the array of PVs (object paths) */
    if (pv_list) {
        g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
        for (pv=pv_list; *pv; pv++) {
            path = get_object_path (*pv, error);
            if (!path) {
                g_variant_builder_clear (&builder);
                return FALSE;
            }
            g_variant_builder_add_value (&builder, g_variant_new ("(ott)", path, (guint64) 0, (guint64) 0));
        }
        pvs = g_variant_builder_end (&builder);
        g_variant_builder_clear (&builder);
    } else {
        var_type = g_variant_type_new ("a(ott)");
        pvs = g_variant_new_array (var_type, NULL, 0);
        g_variant_type_free (var_type);
    }

    /* build the params tuple */
    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("s", lv_name));
    g_variant_builder_add_value (&builder, g_variant_new ("t", size));
    g_variant_builder_add_value (&builder, pvs);
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    if (type) {
        /* and now the extra_params params */
        g_variant_builder_init (&builder, G_VARIANT_TYPE_DICTIONARY);
        if (pv_list && g_strcmp0 (type, "striped") == 0)
            g_variant_builder_add_value (&builder, g_variant_new ("{sv}", "stripes", g_variant_new ("i", g_strv_length ((gchar **) pv_list))));
        else
            g_variant_builder_add_value (&builder, g_variant_new ("{sv}", "type", g_variant_new ("s", type)));
        extra_params = g_variant_builder_end (&builder);
        g_variant_builder_clear (&builder);
    }

    call_lvm_obj_method_sync (vg_name, VG_INTF, "LvCreate", params, extra_params, extra, TRUE, error);

    return ((*error) == NULL);
}

/**
 * bd_lvm_lvremove:
 * @vg_name: name of the VG containing the to-be-removed LV
 * @lv_name: name of the to-be-removed LV
 * @force: whether to force removal or not
 * @extra: (allow-none) (array zero-terminated=1): extra options for the LV removal
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully removed or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_REMOVE
 */
gboolean bd_lvm_lvremove (const gchar *vg_name, const gchar *lv_name, gboolean force, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariant *extra_params = NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_DICTIONARY);
    /* '--yes' is needed if DISCARD is enabled */
    g_variant_builder_add (&builder, "{sv}", "--yes", g_variant_new ("s", ""));
    if (force) {
        g_variant_builder_add (&builder, "{sv}", "--force", g_variant_new ("s", ""));
    }
    extra_params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    call_lv_method_sync (vg_name, lv_name, "Remove", NULL, extra_params, extra, TRUE, error);

    return (*error == NULL);
}

/**
 * bd_lvm_lvrename:
 * @vg_name: name of the VG containing the to-be-renamed LV
 * @lv_name: name of the to-be-renamed LV
 * @new_name: new name for the @vg_name/@lv_name LV
 * @extra: (allow-none) (array zero-terminated=1): extra options for the LV rename
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully renamed to
 * @vg_name/@new_name or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_lvrename (const gchar *vg_name, const gchar *lv_name, const gchar *new_name, const BDExtraArg **extra, GError **error) {
    GVariant *params = NULL;

    params = g_variant_new ("(s)", new_name);
    call_lv_method_sync (vg_name, lv_name, "Rename", params, NULL, extra, TRUE, error);
    return (*error == NULL);
}

/**
 * bd_lvm_lvresize:
 * @vg_name: name of the VG containing the to-be-resized LV
 * @lv_name: name of the to-be-resized LV
 * @size: the requested new size of the LV
 * @extra: (allow-none) (array zero-terminated=1): extra options for the LV resize
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully resized or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_lvresize (const gchar *vg_name, const gchar *lv_name, guint64 size, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariantType *type = NULL;
    GVariant *params = NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("t", size));
    type = g_variant_type_new ("a(ott)");
    g_variant_builder_add_value (&builder, g_variant_new_array (type, NULL, 0));
    g_variant_type_free (type);
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    call_lv_method_sync (vg_name, lv_name, "Resize", params, NULL, extra, TRUE, error);
    return (*error == NULL);
}

/**
 * bd_lvm_lvactivate:
 * @vg_name: name of the VG containing the to-be-activated LV
 * @lv_name: name of the to-be-activated LV
 * @ignore_skip: whether to ignore the skip flag or not
 * @extra: (allow-none) (array zero-terminated=1): extra options for the LV activation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully activated or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_lvactivate (const gchar *vg_name, const gchar *lv_name, gboolean ignore_skip, const BDExtraArg **extra, GError **error) {
    GVariant *params = g_variant_new ("(t)", (guint64) 0);
    GVariantBuilder builder;
    GVariant *extra_params = NULL;

    if (ignore_skip) {
        g_variant_builder_init (&builder, G_VARIANT_TYPE_DICTIONARY);
        g_variant_builder_add (&builder, "{sv}", "-K", g_variant_new ("s", ""));
        extra_params = g_variant_builder_end (&builder);
        g_variant_builder_clear (&builder);
    }
    call_lv_method_sync (vg_name, lv_name, "Activate", params, extra_params, extra, TRUE, error);

    return (*error == NULL);
}

/**
 * bd_lvm_lvdeactivate:
 * @vg_name: name of the VG containing the to-be-deactivated LV
 * @lv_name: name of the to-be-deactivated LV
 * @extra: (allow-none) (array zero-terminated=1): extra options for the LV deactivation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully deactivated or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_lvdeactivate (const gchar *vg_name, const gchar *lv_name, const BDExtraArg **extra, GError **error) {
    GVariant *params = g_variant_new ("(t)", (guint64) 0);
    call_lv_method_sync (vg_name, lv_name, "Deactivate", params, NULL, extra, TRUE, error);
    return (*error == NULL);
}

/**
 * bd_lvm_lvsnapshotcreate:
 * @vg_name: name of the VG containing the LV a new snapshot should be created of
 * @origin_name: name of the LV a new snapshot should be created of
 * @snapshot_name: name fo the to-be-created snapshot
 * @size: requested size for the snapshot
 * @extra: (allow-none) (array zero-terminated=1): extra options for the LV snapshot creation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @snapshot_name snapshot of the @vg_name/@origin_name LV
 * was successfully created or not.
 *
 * Tech category: %BD_LVM_TECH_BASIC_SNAP-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_lvsnapshotcreate (const gchar *vg_name, const gchar *origin_name, const gchar *snapshot_name, guint64 size, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariant *params = NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("s", snapshot_name));
    g_variant_builder_add_value (&builder, g_variant_new ("t", size));
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    call_lv_method_sync (vg_name, origin_name, "Snapshot", params, NULL, extra, TRUE, error);

    return (*error == NULL);
}

/**
 * bd_lvm_lvsnapshotmerge:
 * @vg_name: name of the VG containing the to-be-merged LV snapshot
 * @snapshot_name: name of the to-be-merged LV snapshot
 * @extra: (allow-none) (array zero-terminated=1): extra options for the LV snapshot merge
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@snapshot_name LV snapshot was successfully merged or not
 *
 * Tech category: %BD_LVM_TECH_BASIC_SNAP-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_lvsnapshotmerge (const gchar *vg_name, const gchar *snapshot_name, const BDExtraArg **extra, GError **error) {
    gchar *obj_id = NULL;
    gchar *obj_path = NULL;

    /* get object path for vg_name/snapshot_name and call SNAP_INTF, "Merge" */
    obj_id = g_strdup_printf ("%s/%s", vg_name, snapshot_name);
    obj_path = get_object_path (obj_id, error);
    g_free (obj_id);
    if (!obj_path)
        return FALSE;

    call_lvm_method_sync (obj_path, SNAP_INTF, "Merge", NULL, NULL, extra, TRUE, error);
    return (*error == NULL);
}

/**
 * bd_lvm_lvinfo:
 * @vg_name: name of the VG that contains the LV to get information about
 * @lv_name: name of the LV to get information about
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the @vg_name/@lv_name LV or %NULL in case
 * of error (the @error) gets populated in those cases)
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMLVdata* bd_lvm_lvinfo (const gchar *vg_name, const gchar *lv_name, GError **error) {
    GVariant *props = NULL;
    BDLVMLVdata* ret = NULL;

    props = get_lv_properties (vg_name, lv_name, error);
    if (!props)
        /* the error is already populated */
        return NULL;

    ret = get_lv_data_from_props (props, error);
    if (ret && ((g_strcmp0 (ret->segtype, "thin-pool") == 0) ||
                (g_strcmp0 (ret->segtype, "cache-pool") == 0))) {
        ret->data_lv = bd_lvm_data_lv_name (vg_name, lv_name, error);
        ret->metadata_lv = bd_lvm_metadata_lv_name (vg_name, lv_name, error);
    }
    if (ret && g_strcmp0 (ret->segtype, "vdo-pool") == 0) {
        ret->data_lv = bd_lvm_data_lv_name (vg_name, lv_name, error);
    }

    return ret;
}

static gchar* get_lv_vg_name (const gchar *lv_obj_path, GError **error) {
    GVariant *value = NULL;
    gchar *vg_obj_path = NULL;
    gchar *ret = NULL;

    value = get_object_property (lv_obj_path, LV_CMN_INTF, "Vg", error);
    g_variant_get (value, "o", &vg_obj_path);
    g_variant_unref (value);

    value = get_object_property (vg_obj_path, VG_INTF, "Name", error);
    g_variant_get (value, "s", &ret);
    g_free (vg_obj_path);
    g_variant_unref (value);

    return ret;
}

/**
 * filter_lvs_by_vg: (skip)
 *
 * Filter LVs by VG name and prepend the matching ones to the @out list.
 */
static gboolean filter_lvs_by_vg (gchar **lvs, const gchar *vg_name, GSList **out, guint64 *n_lvs, GError **error) {
    gchar **lv_p = NULL;
    gchar *lv_vg_name = NULL;
    gboolean success = TRUE;

    if (!lvs)
        /* nothing to do */
        return TRUE;

    for (lv_p=lvs; *lv_p; lv_p++) {
        if (vg_name) {
            lv_vg_name = get_lv_vg_name (*lv_p, error);
            if (!lv_vg_name) {
                g_free (*lv_p);
                success = FALSE;
                continue;
            }

            if (g_strcmp0 (lv_vg_name, vg_name) == 0) {
                *out = g_slist_prepend (*out, *lv_p);
                (*n_lvs)++;
            } else {
                g_free (*lv_p);
            }

            g_free (lv_vg_name);
        } else {
            *out = g_slist_prepend (*out, *lv_p);
            (*n_lvs)++;
        }
    }
    return success;
}

/**
 * bd_lvm_lvs:
 * @vg_name: (allow-none): name of the VG to get information about LVs from
 * @error: (out): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about LVs found in the given
 * @vg_name VG or in system if @vg_name is %NULL
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMLVdata** bd_lvm_lvs (const gchar *vg_name, GError **error) {
    gchar **lvs = NULL;
    guint64 n_lvs = 0;
    GVariant *props = NULL;
    BDLVMLVdata **ret = NULL;
    guint64 j = 0;
    GSList *matched_lvs = NULL;
    GSList *lv = NULL;
    gboolean success = FALSE;

    lvs = get_existing_objects (LV_OBJ_PREFIX, error);
    if (!lvs && (*error)) {
        /* error is already populated */
        return NULL;
    }
    success = filter_lvs_by_vg (lvs, vg_name, &matched_lvs, &n_lvs, error);
    g_free (lvs);
    if (!success) {
        g_slist_free_full (matched_lvs, g_free);
        return NULL;
    }

    lvs = get_existing_objects (THIN_POOL_OBJ_PREFIX, error);
    if (!lvs && (*error)) {
        /* error is already populated */
        g_slist_free_full (matched_lvs, g_free);
        return NULL;
    }
    success = filter_lvs_by_vg (lvs, vg_name, &matched_lvs, &n_lvs, error);
    g_free (lvs);
    if (!success) {
        g_slist_free_full (matched_lvs, g_free);
        return NULL;
    }

    lvs = get_existing_objects (CACHE_POOL_OBJ_PREFIX, error);
    if (!lvs && (*error)) {
        /* error is already populated */
        g_slist_free_full (matched_lvs, g_free);
        return NULL;
    }
    success = filter_lvs_by_vg (lvs, vg_name, &matched_lvs, &n_lvs, error);
    g_free (lvs);
    if (!success) {
        g_slist_free_full (matched_lvs, g_free);
        return NULL;
    }

    lvs = get_existing_objects (VDO_POOL_OBJ_PREFIX, error);
    if (!lvs && (*error)) {
        /* error is already populated */
        g_slist_free_full (matched_lvs, g_free);
        return NULL;
    }
    success = filter_lvs_by_vg (lvs, vg_name, &matched_lvs, &n_lvs, error);
    g_free (lvs);
    if (!success) {
        g_slist_free_full (matched_lvs, g_free);
        return NULL;
    }

    lvs = get_existing_objects (HIDDEN_LV_OBJ_PREFIX, error);
    if (!lvs && (*error)) {
        /* error is already populated */
        g_slist_free_full (matched_lvs, g_free);
        return NULL;
    }
    success = filter_lvs_by_vg (lvs, vg_name, &matched_lvs, &n_lvs, error);
    g_free (lvs);
    if (!success) {
        g_slist_free_full (matched_lvs, g_free);
        return NULL;
    }

    if (n_lvs == 0) {
        /* no LVs */
        ret = g_new0 (BDLVMLVdata*, 1);
        ret[0] = NULL;
        g_slist_free_full (matched_lvs, g_free);
        return ret;
    }

    /* we have been prepending to the list so far, but it will be nicer if we
       reverse it (to get back the original order) */
    matched_lvs = g_slist_reverse (matched_lvs);

    /* now create the return value -- NULL-terminated array of BDLVMLVdata */
    ret = g_new0 (BDLVMLVdata*, n_lvs + 1);

    lv = matched_lvs;
    while (lv) {
        props = get_object_properties (lv->data, LV_CMN_INTF, error);
        if (!props) {
            g_slist_free_full (matched_lvs, g_free);
            g_free (ret);
            return NULL;
        }
        ret[j] = get_lv_data_from_props (props, error);
        if (!(ret[j])) {
            g_slist_free_full (matched_lvs, g_free);
            for (guint64 i = 0; i < j; i++)
                bd_lvm_lvdata_free (ret[i]);
            g_free (ret);
            return NULL;
        } else if ((g_strcmp0 (ret[j]->segtype, "thin-pool") == 0) ||
                   (g_strcmp0 (ret[j]->segtype, "cache-pool") == 0)) {
            ret[j]->data_lv = bd_lvm_data_lv_name (ret[j]->vg_name, ret[j]->lv_name, error);
            ret[j]->metadata_lv = bd_lvm_metadata_lv_name (ret[j]->vg_name, ret[j]->lv_name, error);
        } else if (g_strcmp0 (ret[j]->segtype, "vdo-pool") == 0) {
            ret[j]->data_lv = bd_lvm_data_lv_name (ret[j]->vg_name, ret[j]->lv_name, error);
        }
        if (error && *error) {
            g_slist_free_full (matched_lvs, g_free);
            for (guint64 i = 0; i <= j; i++)
                bd_lvm_lvdata_free (ret[i]);
            g_free (ret);
            return NULL;
        }
        j++;
        lv = g_slist_next (lv);
    }
    g_slist_free_full (matched_lvs, g_free);

    ret[j] = NULL;
    return ret;
}

/**
 * bd_lvm_thpoolcreate:
 * @vg_name: name of the VG to create a thin pool in
 * @lv_name: name of the to-be-created pool LV
 * @size: requested size of the to-be-created pool
 * @md_size: requested metadata size or 0 to use the default
 * @chunk_size: requested chunk size or 0 to use the default
 * @profile: (allow-none): profile to use (see lvm(8) for more information) or %NULL to use
 *                         the default
 * @extra: (allow-none) (array zero-terminated=1): extra options for the thin pool creation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name thin pool was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_THIN-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_thpoolcreate (const gchar *vg_name, const gchar *lv_name, guint64 size, guint64 md_size, guint64 chunk_size, const gchar *profile, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariant *params = NULL;
    GVariant *extra_params = NULL;
    GVariant *param = NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("s", lv_name));
    g_variant_builder_add_value (&builder, g_variant_new_uint64 (size));
    g_variant_builder_add_value (&builder, g_variant_new_boolean (TRUE));
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    g_variant_builder_init (&builder, G_VARIANT_TYPE_DICTIONARY);
    if (md_size != 0) {
        param = create_size_str_param (md_size, "b");
        g_variant_builder_add (&builder, "{sv}", "poolmetadatasize", param);
    }
    if (chunk_size != 0) {
        param = create_size_str_param (chunk_size, "b");
        g_variant_builder_add (&builder, "{sv}", "chunksize", param);
    }
    if (profile) {
        g_variant_builder_add (&builder, "{sv}", "profile", g_variant_new ("s", profile));
    }
    extra_params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    call_lvm_obj_method_sync (vg_name, VG_INTF, "LvCreateLinear", params, extra_params, extra, TRUE, error);
    return ((*error) == NULL);
}

/**
 * bd_lvm_thlvcreate:
 * @vg_name: name of the VG containing the thin pool providing extents for the to-be-created thin LV
 * @pool_name: name of the pool LV providing extents for the to-be-created thin LV
 * @lv_name: name of the to-be-created thin LV
 * @size: requested virtual size of the to-be-created thin LV
 * @extra: (allow-none) (array zero-terminated=1): extra options for the thin LV creation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name thin LV was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_THIN-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_thlvcreate (const gchar *vg_name, const gchar *pool_name, const gchar *lv_name, guint64 size, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariant *params = NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("s", lv_name));
    g_variant_builder_add_value (&builder, g_variant_new ("t", size));
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    call_thpool_method_sync (vg_name, pool_name, "LvCreate", params, NULL, extra, TRUE, error);

    return (*error == NULL);
}

/**
 * bd_lvm_thlvpoolname:
 * @vg_name: name of the VG containing the queried thin LV
 * @lv_name: name of the queried thin LV
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): the name of the pool volume for the @vg_name/@lv_name
 * thin LV or %NULL if failed to determine (@error) is set in those cases)
 *
 * Tech category: %BD_LVM_TECH_THIN-%BD_LVM_TECH_MODE_QUERY
 */
gchar* bd_lvm_thlvpoolname (const gchar *vg_name, const gchar *lv_name, GError **error) {
    GVariant *prop = NULL;
    gboolean is_thin = FALSE;
    gchar *pool_obj_path = NULL;
    gchar *ret = NULL;

    prop = get_lv_property (vg_name, lv_name, "IsThinVolume", error);
    if (!prop)
        return NULL;
    is_thin = g_variant_get_boolean (prop);
    g_variant_unref (prop);

    if (!is_thin) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_NOEXIST,
                     "The LV '%s' is not a thin LV and thus have no thin pool", lv_name);
        return NULL;
    }
    prop = get_lv_property (vg_name, lv_name, "PoolLv", error);
    if (!prop)
        return NULL;
    g_variant_get (prop, "o", &pool_obj_path);
    g_variant_unref (prop);

    prop = get_object_property (pool_obj_path, LV_CMN_INTF, "Name", error);
    g_free (pool_obj_path);
    if (!prop)
        return NULL;
    g_variant_get (prop, "s", &ret);
    g_variant_unref (prop);

    return ret;
}

/**
 * bd_lvm_thsnapshotcreate:
 * @vg_name: name of the VG containing the thin LV a new snapshot should be created of
 * @origin_name: name of the thin LV a new snapshot should be created of
 * @snapshot_name: name fo the to-be-created snapshot
 * @pool_name: (allow-none): name of the thin pool to create the snapshot in or %NULL if not specified
 * @extra: (allow-none) (array zero-terminated=1): extra options for the thin LV snapshot creation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @snapshot_name snapshot of the @vg_name/@origin_name
 * thin LV was successfully created or not.
 *
 * Tech category: %BD_LVM_TECH_THIN-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_thsnapshotcreate (const gchar *vg_name, const gchar *origin_name, const gchar *snapshot_name, const gchar *pool_name, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariant *params = NULL;
    GVariant *extra_params = NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("s", snapshot_name));
    g_variant_builder_add_value (&builder, g_variant_new ("t", (guint64) 0));
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    if (pool_name) {
        g_variant_builder_init (&builder, G_VARIANT_TYPE_DICTIONARY);
        g_variant_builder_add (&builder, "{sv}", "thinpool", g_variant_new ("s", pool_name));
        extra_params = g_variant_builder_end (&builder);
        g_variant_builder_clear (&builder);
    }

    call_lv_method_sync (vg_name, origin_name, "Snapshot", params, extra_params, extra, TRUE, error);

    return (*error == NULL);
}

/**
 * bd_lvm_set_global_config:
 * @new_config: (allow-none): string representation of the new global LVM
 *                            configuration to set or %NULL to reset to default
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the new requested global config @new_config was successfully
 *          set or not
 *
 * Tech category: %BD_LVM_TECH_GLOB_CONF no mode (it is ignored)
 */
gboolean bd_lvm_set_global_config (const gchar *new_config, GError **error UNUSED) {
    /* XXX: the error attribute will likely be used in the future when
       some validation comes into the game */

    g_mutex_lock (&global_config_lock);

    /* first free the old value */
    g_free (global_config_str);

    /* now store the new one */
    global_config_str = g_strdup (new_config);

    g_mutex_unlock (&global_config_lock);
    return TRUE;
}

/**
 * bd_lvm_get_global_config:
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): a copy of a string representation of the currently
 *                           set LVM global configuration
 *
 * Tech category: %BD_LVM_TECH_GLOB_CONF no mode (it is ignored)
 */
gchar* bd_lvm_get_global_config (GError **error UNUSED) {
    gchar *ret = NULL;

    g_mutex_lock (&global_config_lock);
    ret = g_strdup (global_config_str ? global_config_str : "");
    g_mutex_unlock (&global_config_lock);

    return ret;
}

/**
 * bd_lvm_cache_get_default_md_size:
 * @cache_size: size of the cache to determine MD size for
 * @error: (out): place to store error (if any)
 *
 * Returns: recommended default size of the cache metadata LV or 0 in case of error
 *
 * Tech category: %BD_LVM_TECH_CACHE_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_cache_get_default_md_size (guint64 cache_size, GError **error UNUSED) {
    return MAX ((guint64) cache_size / 1000, BD_LVM_MIN_CACHE_MD_SIZE);
}

/**
 * get_lv_type_from_flags: (skip)
 * @meta: getting type for a (future) metadata LV
 *
 * Get LV type string from flags.
 */
static const gchar* get_lv_type_from_flags (BDLVMCachePoolFlags flags, gboolean meta, GError **error UNUSED) {
    if (!meta) {
        if (flags & BD_LVM_CACHE_POOL_STRIPED)
            return "striped";
        else if (flags & BD_LVM_CACHE_POOL_RAID1)
            return "raid1";
        else if (flags & BD_LVM_CACHE_POOL_RAID5)
            return "raid5";
        else if (flags & BD_LVM_CACHE_POOL_RAID6)
            return "raid6";
        else if (flags & BD_LVM_CACHE_POOL_RAID10)
            return "raid10";
        else
            return NULL;
    } else {
        if (flags & BD_LVM_CACHE_POOL_META_STRIPED)
            return "striped";
        else if (flags & BD_LVM_CACHE_POOL_META_RAID1)
            return "raid1";
        else if (flags & BD_LVM_CACHE_POOL_META_RAID5)
            return "raid5";
        else if (flags & BD_LVM_CACHE_POOL_META_RAID6)
            return "raid6";
        else if (flags & BD_LVM_CACHE_POOL_META_RAID10)
            return "raid10";
        else
            return NULL;
    }
}

/**
 * bd_lvm_cache_get_mode_str:
 * @mode: mode to get the string representation for
 * @error: (out): place to store error (if any)
 *
 * Returns: string representation of @mode or %NULL in case of error
 *
 * Tech category: always provided/supported
 */
const gchar* bd_lvm_cache_get_mode_str (BDLVMCacheMode mode, GError **error) {
    if (mode == BD_LVM_CACHE_MODE_WRITETHROUGH)
        return "writethrough";
    else if (mode == BD_LVM_CACHE_MODE_WRITEBACK)
        return "writeback";
    else if (mode == BD_LVM_CACHE_MODE_UNKNOWN)
        return "unknown";
    else {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                     "Invalid mode given: %d", mode);
        return NULL;
    }
}

/**
 * bd_lvm_cache_get_mode_from_str:
 * @mode_str: string representation of a cache mode
 * @error: (out): place to store error (if any)
 *
 * Returns: cache mode for the @mode_str or %BD_LVM_CACHE_MODE_UNKNOWN if
 *          failed to determine
 *
 * Tech category: always provided/supported
 */
BDLVMCacheMode bd_lvm_cache_get_mode_from_str (const gchar *mode_str, GError **error) {
    if (g_strcmp0 (mode_str, "writethrough") == 0)
        return BD_LVM_CACHE_MODE_WRITETHROUGH;
    else if (g_strcmp0 (mode_str, "writeback") == 0)
        return BD_LVM_CACHE_MODE_WRITEBACK;
    else if (g_strcmp0 (mode_str, "unknown") == 0)
        return BD_LVM_CACHE_MODE_UNKNOWN;
    else {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                     "Invalid mode given: %s", mode_str);
        return BD_LVM_CACHE_MODE_UNKNOWN;
    }
}

/**
 * bd_lvm_cache_create_pool:
 * @vg_name: name of the VG to create @pool_name in
 * @pool_name: name of the cache pool LV to create
 * @pool_size: desired size of the cache pool @pool_name
 * @md_size: desired size of the @pool_name cache pool's metadata LV or 0 to
 *           use the default
 * @mode: cache mode of the @pool_name cache pool
 * @flags: a combination of (ORed) #BDLVMCachePoolFlags
 * @fast_pvs: (array zero-terminated=1): list of (fast) PVs to create the @pool_name
 *                                       cache pool (and the metadata LV)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the cache pool @vg_name/@pool_name was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_CACHE-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_cache_create_pool (const gchar *vg_name, const gchar *pool_name, guint64 pool_size, guint64 md_size, BDLVMCacheMode mode, BDLVMCachePoolFlags flags, const gchar **fast_pvs, GError **error) {
    gboolean success = FALSE;
    const gchar *type = NULL;
    gchar *name = NULL;
    GVariantBuilder builder;
    GVariant *params = NULL;
    GVariant *extra = NULL;
    gchar *lv_id = NULL;
    gchar *lv_obj_path = NULL;
    const gchar *mode_str = NULL;
    gchar *msg = NULL;
    guint64 progress_id = 0;

    msg = g_strdup_printf ("Started 'create cache pool %s/%s'", vg_name, pool_name);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    /* create an LV for the pool */
    type = get_lv_type_from_flags (flags, FALSE, error);
    success = bd_lvm_lvcreate (vg_name, pool_name, pool_size, type, fast_pvs, NULL, error);
    if (!success) {
        g_prefix_error (error, "Failed to create the pool LV: ");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* 1/3 steps done */
    bd_utils_report_progress (progress_id, 33, "Created the data LV");

    /* determine the size of the metadata LV */
    type = get_lv_type_from_flags (flags, TRUE, error);
    if (md_size == 0)
        md_size = bd_lvm_cache_get_default_md_size (pool_size, error);
    if (*error) {
        g_prefix_error (error, "Failed to determine size for the pool metadata LV: ");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    name = g_strdup_printf ("%s_meta", pool_name);

    /* create the metadata LV */
    success = bd_lvm_lvcreate (vg_name, name, md_size, type, fast_pvs, NULL, error);
    if (!success) {
        g_free (name);
        g_prefix_error (error, "Failed to create the pool metadata LV: ");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* 2/3 steps done */
    bd_utils_report_progress (progress_id, 66, "Created the metadata LV");

    /* create the cache pool from the two LVs */
    /* build the params tuple */
    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    lv_id = g_strdup_printf ("%s/%s", vg_name, name);
    lv_obj_path = get_object_path (lv_id, error);
    g_free (lv_id);
    if (!lv_obj_path) {
        g_variant_builder_clear (&builder);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    g_variant_builder_add_value (&builder, g_variant_new ("o", lv_obj_path));
    lv_id = g_strdup_printf ("%s/%s", vg_name, pool_name);
    lv_obj_path = get_object_path (lv_id, error);
    g_free (lv_id);
    if (!lv_obj_path) {
        g_variant_builder_clear (&builder);
        return FALSE;
    }
    g_variant_builder_add_value (&builder, g_variant_new ("o", lv_obj_path));
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    /* build the dictionary with the extra params */
    g_variant_builder_init (&builder, G_VARIANT_TYPE_DICTIONARY);
    mode_str = bd_lvm_cache_get_mode_str (mode, error);
    if (!mode_str) {
        g_variant_builder_clear (&builder);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    g_variant_builder_add (&builder, "{sv}", "cachemode", g_variant_new ("s", mode_str));
    extra = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    call_lvm_obj_method_sync (vg_name, VG_INTF, "CreateCachePool", params, extra, NULL, TRUE, error);
    if (*error)
        bd_utils_report_finished (progress_id, (*error)->message);
    else
        bd_utils_report_finished (progress_id, "Completed");

    return ((*error) == NULL);
}

/**
 * bd_lvm_cache_attach:
 * @vg_name: name of the VG containing the @data_lv and the @cache_pool_lv LVs
 * @data_lv: data LV to attache the @cache_pool_lv to
 * @cache_pool_lv: cache pool LV to attach to the @data_lv
 * @extra: (allow-none) (array zero-terminated=1): extra options for the cache attachment
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @cache_pool_lv was successfully attached to the @data_lv or not
 *
 * Tech category: %BD_LVM_TECH_CACHE-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_cache_attach (const gchar *vg_name, const gchar *data_lv, const gchar *cache_pool_lv, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariant *params = NULL;
    gchar *lv_id = NULL;
    gchar *lv_obj_path = NULL;

    lv_id = g_strdup_printf ("%s/%s", vg_name, data_lv);
    lv_obj_path = get_object_path (lv_id, error);
    g_free (lv_id);
    if (!lv_obj_path)
        return FALSE;
    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("o", lv_obj_path));
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    lv_id = g_strdup_printf ("%s/%s", vg_name, cache_pool_lv);

    call_lvm_obj_method_sync (lv_id, CACHE_POOL_INTF, "CacheLv", params, NULL, extra, TRUE, error);
    return ((*error) == NULL);
}

/**
 * bd_lvm_cache_detach:
 * @vg_name: name of the VG containing the @cached_lv
 * @cached_lv: name of the cached LV to detach its cache from
 * @destroy: whether to destroy the cache after detach or not
 * @extra: (allow-none) (array zero-terminated=1): extra options for the cache detachment
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the cache was successfully detached from the @cached_lv or not
 *
 * Note: synces the cache first
 *
 * Tech category: %BD_LVM_TECH_CACHE-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_cache_detach (const gchar *vg_name, const gchar *cached_lv, gboolean destroy, const BDExtraArg **extra, GError **error) {
    gchar *lv_id = NULL;
    gchar *cache_pool_name = NULL;
    GVariantBuilder builder;
    GVariant *params = NULL;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("b", destroy));
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    cache_pool_name = bd_lvm_cache_pool_name (vg_name, cached_lv, error);
    if (!cache_pool_name)
        return FALSE;
    lv_id = g_strdup_printf ("%s/%s", vg_name, cached_lv);
    call_lvm_obj_method_sync (lv_id, CACHED_LV_INTF, "DetachCachePool", params, NULL, extra, TRUE, error);
    g_free (lv_id);
    g_free (cache_pool_name);
    return ((*error) == NULL);
}

/**
 * bd_lvm_cache_create_cached_lv:
 * @vg_name: name of the VG to create a cached LV in
 * @lv_name: name of the cached LV to create
 * @data_size: size of the data LV
 * @cache_size: size of the cache (or cached LV more precisely)
 * @md_size: size of the cache metadata LV or 0 to use the default
 * @mode: cache mode for the cached LV
 * @flags: a combination of (ORed) #BDLVMCachePoolFlags
 * @slow_pvs: (array zero-terminated=1): list of slow PVs (used for the data LV)
 * @fast_pvs: (array zero-terminated=1): list of fast PVs (used for the cache LV)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the cached LV @lv_name was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_CACHE-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_cache_create_cached_lv (const gchar *vg_name, const gchar *lv_name, guint64 data_size, guint64 cache_size, guint64 md_size, BDLVMCacheMode mode, BDLVMCachePoolFlags flags,
                                        const gchar **slow_pvs, const gchar **fast_pvs, GError **error) {
    gboolean success = FALSE;
    gchar *name = NULL;
    gchar *msg = NULL;
    guint64 progress_id = 0;

    msg = g_strdup_printf ("Started 'create cached LV %s/%s'", vg_name, lv_name);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    success = bd_lvm_lvcreate (vg_name, lv_name, data_size, NULL, slow_pvs, NULL, error);
    if (!success) {
        g_prefix_error (error, "Failed to create the data LV: ");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* 1/5 steps (cache pool creation has 3 steps) done */
    bd_utils_report_progress (progress_id, 20, "Data LV created");

    name = g_strdup_printf ("%s_cache", lv_name);
    success = bd_lvm_cache_create_pool (vg_name, name, cache_size, md_size, mode, flags, fast_pvs, error);
    if (!success) {
        g_prefix_error (error, "Failed to create the cache pool '%s': ", name);
        g_free (name);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* 4/5 steps (cache pool creation has 3 steps) done */
    bd_utils_report_progress (progress_id, 80, "Cache pool created");

    success = bd_lvm_cache_attach (vg_name, lv_name, name, NULL, error);
    if (!success) {
        g_prefix_error (error, "Failed to attach the cache pool '%s' to the data LV: ", name);
        g_free (name);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    bd_utils_report_finished (progress_id, "Completed");
    g_free (name);
    return TRUE;
}

/**
 * bd_lvm_cache_pool_name:
 * @vg_name: name of the VG containing the @cached_lv
 * @cached_lv: cached LV to get the name of the its pool LV for
 * @error: (out): place to store error (if any)
 *
 * Returns: name of the cache pool LV used by the @cached_lv or %NULL in case of error
 *
 * Tech category: %BD_LVM_TECH_CACHE-%BD_LVM_TECH_MODE_QUERY
 */
gchar* bd_lvm_cache_pool_name (const gchar *vg_name, const gchar *cached_lv, GError **error) {
    gchar *ret = NULL;
    gchar *name_start = NULL;
    gchar *name_end = NULL;
    gchar *pool_name = NULL;
    gchar *lv_spec = NULL;
    GVariant *prop = NULL;
    gchar *pool_obj_path = NULL;

    /* same as for a thin LV, but with square brackets */
    lv_spec = g_strdup_printf ("%s/%s", vg_name, cached_lv);
    prop = get_lvm_object_property (lv_spec, CACHED_LV_INTF, "CachePool", error);
    g_free (lv_spec);
    if (!prop)
        return NULL;
    g_variant_get (prop, "o", &pool_obj_path);
    prop = get_object_property (pool_obj_path, LV_CMN_INTF, "Name", error);
    g_free (pool_obj_path);
    if (!prop)
        return NULL;
    g_variant_get (prop, "s", &ret);
    g_variant_unref (prop);

    name_start = strchr (ret, '[');
    if (!name_start) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                     "Failed to determine cache pool name from: '%s'", ret);
        g_free (ret);
        return FALSE;
    }
    name_start++;

    name_end = strchr (ret, ']');
    if (!name_end) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                     "Failed to determine cache pool name from: '%s'", ret);
        g_free (ret);
        return FALSE;
    }

    pool_name = g_strndup (name_start, name_end - name_start);
    g_free (ret);

    return pool_name;
}

/**
 * bd_lvm_cache_stats:
 * @vg_name: name of the VG containing the @cached_lv
 * @cached_lv: cached LV to get stats for
 * @error: (out): place to store error (if any)
 *
 * Returns: stats for the @cached_lv or %NULL in case of error
 *
 * Tech category: %BD_LVM_TECH_CACHE-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMCacheStats* bd_lvm_cache_stats (const gchar *vg_name, const gchar *cached_lv, GError **error) {
    struct dm_pool *pool = NULL;
    struct dm_task *task = NULL;
    struct dm_info info;
    struct dm_status_cache *status = NULL;
    gchar *map_name = NULL;
    guint64 start = 0;
    guint64 length = 0;
    gchar *type = NULL;
    gchar *params = NULL;
    BDLVMCacheStats *ret = NULL;
    BDLVMLVdata *lvdata = NULL;
    gchar *data_lv_name = NULL;

    if (geteuid () != 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_NOT_ROOT,
                     "Not running as root, cannot query DM maps");
        return NULL;
    }

    lvdata = bd_lvm_lvinfo (vg_name, cached_lv, error);
    if (!lvdata)
        return NULL;

    pool = dm_pool_create ("bd-pool", 20);

    if (g_strcmp0 (lvdata->segtype, "thin-pool") == 0) {
        data_lv_name = bd_lvm_data_lv_name (vg_name, cached_lv, error);
        if (!data_lv_name) {
            dm_pool_destroy (pool);
            bd_lvm_lvdata_free (lvdata);
            return NULL;
        }

        map_name = dm_build_dm_name (pool, vg_name, data_lv_name, NULL);
        g_free (data_lv_name);
    } else
        /* translate the VG+LV name into the DM map name */
        map_name = dm_build_dm_name (pool, vg_name, cached_lv, NULL);

    bd_lvm_lvdata_free (lvdata);

    task = dm_task_create (DM_DEVICE_STATUS);
    if (!task) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to create DM task for the cache map '%s': ", map_name);
        dm_pool_destroy (pool);
        return NULL;
    }

    if (dm_task_set_name (task, map_name) == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to create DM task for the cache map '%s': ", map_name);
        dm_task_destroy (task);
        dm_pool_destroy (pool);
        return NULL;
    }

    if (dm_task_run (task) == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to run the DM task for the cache map '%s': ", map_name);
        dm_task_destroy (task);
        dm_pool_destroy (pool);
        return NULL;
    }

    if (dm_task_get_info (task, &info) == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to get task info for the cache map '%s': ", map_name);
        dm_task_destroy (task);
        dm_pool_destroy (pool);
        return NULL;
    }

    if (!info.exists) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_NOCACHE,
                     "The cache map '%s' doesn't exist: ", map_name);
        dm_task_destroy (task);
        dm_pool_destroy (pool);
        return NULL;
    }

    dm_get_next_target (task, NULL, &start, &length, &type, &params);

    if (dm_get_status_cache (pool, params, &status) == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                     "Failed to get status of the cache map '%s': ", map_name);
        dm_task_destroy (task);
        dm_pool_destroy (pool);
        return NULL;
    }

    ret = g_new0 (BDLVMCacheStats, 1);
    ret->block_size = status->block_size * SECTOR_SIZE;
    ret->cache_size = status->total_blocks * ret->block_size;
    ret->cache_used = status->used_blocks * ret->block_size;

    ret->md_block_size = status->metadata_block_size * SECTOR_SIZE;
    ret->md_size = status->metadata_total_blocks * ret->md_block_size;
    ret->md_used = status->metadata_used_blocks * ret->md_block_size;

    ret->read_hits = status->read_hits;
    ret->read_misses = status->read_misses;
    ret->write_hits = status->write_hits;
    ret->write_misses = status->write_misses;

    if (status->feature_flags & DM_CACHE_FEATURE_WRITETHROUGH)
        ret->mode = BD_LVM_CACHE_MODE_WRITETHROUGH;
    else if (status->feature_flags & DM_CACHE_FEATURE_WRITEBACK)
        ret->mode = BD_LVM_CACHE_MODE_WRITEBACK;
    else {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                      "Failed to determine status of the cache from '%"G_GUINT64_FORMAT"': ",
                      status->feature_flags);
        dm_task_destroy (task);
        dm_pool_destroy (pool);
        bd_lvm_cache_stats_free (ret);
        return NULL;
    }

    dm_task_destroy (task);
    dm_pool_destroy (pool);

    return ret;
}

/**
 * bd_lvm_data_lv_name:
 * @vg_name: name of the VG containing the queried LV
 * @lv_name: name of the queried LV
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): the name of the (internal) data LV of the
 * @vg_name/@lv_name LV
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
gchar* bd_lvm_data_lv_name (const gchar *vg_name, const gchar *lv_name, GError **error) {
    GVariant *prop = NULL;
    gchar *obj_id = NULL;
    gchar *obj_path = NULL;
    gchar *ret = NULL;
    gchar *segtype = NULL;

    obj_id = g_strdup_printf ("%s/%s", vg_name, lv_name);
    obj_path = get_object_path (obj_id, error);
    g_free (obj_id);
    if (!obj_path)
        return NULL;

    prop = get_lv_property (vg_name, lv_name, "SegType", error);
    if (!prop)
        return NULL;
    g_variant_get_child (prop, 0, "s", &segtype);
    g_variant_unref (prop);

    if (g_strcmp0 (segtype, "thin-pool") == 0)
        prop = get_object_property (obj_path, THPOOL_INTF, "DataLv", error);
    else if (g_strcmp0 (segtype, "cache-pool") == 0)
        prop = get_object_property (obj_path, CACHE_POOL_INTF, "DataLv", error);
    else if (g_strcmp0 (segtype, "vdo-pool") == 0)
        prop = get_object_property (obj_path, VDO_POOL_INTF, "DataLv", error);

    g_free (segtype);
    g_free (obj_path);
    if (!prop) {
        g_clear_error (error);
        return NULL;
    }
    g_variant_get (prop, "o", &obj_path);
    g_variant_unref (prop);

    if (g_strcmp0 (obj_path, "/") == 0) {
        /* no origin LV */
        g_free (obj_path);
        return NULL;
    }
    prop = get_object_property (obj_path, LV_CMN_INTF, "Name", error);
    if (!prop) {
        g_free (obj_path);
        return NULL;
    }

    g_variant_get (prop, "s", &ret);
    g_variant_unref (prop);

    return g_strstrip (g_strdelimit (ret, "[]", ' '));
}

/**
 * bd_lvm_metadata_lv_name:
 * @vg_name: name of the VG containing the queried LV
 * @lv_name: name of the queried LV
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): the name of the (internal) metadata LV of the
 * @vg_name/@lv_name LV
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
gchar* bd_lvm_metadata_lv_name (const gchar *vg_name, const gchar *lv_name, GError **error) {
    GVariant *prop = NULL;
    gchar *obj_id = NULL;
    gchar *obj_path = NULL;
    gchar *ret = NULL;

    obj_id = g_strdup_printf ("%s/%s", vg_name, lv_name);
    obj_path = get_object_path (obj_id, error);
    g_free (obj_id);
    if (!obj_path)
        return NULL;

    prop = get_object_property (obj_path, THPOOL_INTF, "MetaDataLv", error);
    if (!prop)
        prop = get_object_property (obj_path, CACHE_POOL_INTF, "MetaDataLv", error);
    g_free (obj_path);
    if (!prop) {
        g_clear_error (error);
        return NULL;
    }
    g_variant_get (prop, "o", &obj_path);
    g_variant_unref (prop);

    if (g_strcmp0 (obj_path, "/") == 0) {
        /* no origin LV */
        g_free (obj_path);
        return NULL;
    }
    prop = get_object_property (obj_path, LV_CMN_INTF, "Name", error);
    if (!prop) {
        g_free (obj_path);
        return NULL;
    }

    g_variant_get (prop, "s", &ret);
    g_variant_unref (prop);

    return g_strstrip (g_strdelimit (ret, "[]", ' '));
}

/**
 * bd_lvm_thpool_convert:
 * @vg_name: name of the VG to create the new thin pool in
 * @data_lv: name of the LV that should become the data part of the new pool
 * @metadata_lv: name of the LV that should become the metadata part of the new pool
 * @name: (allow-none): name for the thin pool (if %NULL, the name @data_lv is inherited)
 * @extra: (allow-none) (array zero-terminated=1): extra options for the thin pool creation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Converts the @data_lv and @metadata_lv into a new thin pool in the @vg_name
 * VG.
 *
 * Returns: whether the new thin pool was successfully created from @data_lv and
 *          @metadata_lv or not
 *
 * Tech category: %BD_LVM_TECH_THIN-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_thpool_convert (const gchar *vg_name, const gchar *data_lv, const gchar *metadata_lv, const gchar *name, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariant *params = NULL;
    gchar *obj_id = NULL;
    gchar *data_lv_path = NULL;
    gchar *metadata_lv_path = NULL;

    obj_id = g_strdup_printf ("%s/%s", vg_name, data_lv);
    data_lv_path = get_object_path (obj_id, error);
    g_free (obj_id);
    if (!data_lv_path)
        return FALSE;

    obj_id = g_strdup_printf ("%s/%s", vg_name, metadata_lv);
    metadata_lv_path = get_object_path (obj_id, error);
    g_free (obj_id);
    if (!metadata_lv_path)
        return FALSE;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("o", metadata_lv_path));
    g_variant_builder_add_value (&builder, g_variant_new ("o", data_lv_path));
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    call_lvm_obj_method_sync (vg_name, VG_INTF, "CreateThinPool", params, NULL, extra, TRUE, error);

    if (((*error) == NULL) && name)
        bd_lvm_lvrename (vg_name, data_lv, name, NULL, error);
    return ((*error) == NULL);
}

/**
 * bd_lvm_cache_pool_convert:
 * @vg_name: name of the VG to create the new thin pool in
 * @data_lv: name of the LV that should become the data part of the new pool
 * @metadata_lv: name of the LV that should become the metadata part of the new pool
 * @name: (allow-none): name for the thin pool (if %NULL, the name @data_lv is inherited)
 * @extra: (allow-none) (array zero-terminated=1): extra options for the thin pool creation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Converts the @data_lv and @metadata_lv into a new cache pool in the @vg_name
 * VG.
 *
 * Returns: whether the new cache pool was successfully created from @data_lv and
 *          @metadata_lv or not
 *
 * Tech category: %BD_LVM_TECH_CACHE-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_cache_pool_convert (const gchar *vg_name, const gchar *data_lv, const gchar *metadata_lv, const gchar *name, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariant *params = NULL;
    gchar *obj_id = NULL;
    gchar *data_lv_path = NULL;
    gchar *metadata_lv_path = NULL;

    obj_id = g_strdup_printf ("%s/%s", vg_name, data_lv);
    data_lv_path = get_object_path (obj_id, error);
    g_free (obj_id);
    if (!data_lv_path)
        return FALSE;

    obj_id = g_strdup_printf ("%s/%s", vg_name, metadata_lv);
    metadata_lv_path = get_object_path (obj_id, error);
    g_free (obj_id);
    if (!metadata_lv_path)
        return FALSE;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("o", metadata_lv_path));
    g_variant_builder_add_value (&builder, g_variant_new ("o", data_lv_path));
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    call_lvm_obj_method_sync (vg_name, VG_INTF, "CreateCachePool", params, NULL, extra, TRUE, error);

    if (((*error) == NULL) && name)
        bd_lvm_lvrename (vg_name, data_lv, name, NULL, error);

    return ((*error) == NULL);
}

/**
 * bd_lvm_vdo_pool_create:
 * @vg_name: name of the VG to create a new LV in
 * @lv_name: name of the to-be-created VDO LV
 * @pool_name: name of the to-be-created VDO pool LV
 * @data_size: requested size of the data VDO LV (physical size of the @pool_name VDO pool LV)
 * @virtual_size: requested virtual_size of the @lv_name VDO LV
 * @index_memory: amount of index memory (in bytes) or 0 for default
 * @compression: whether to enable compression or not
 * @deduplication: whether to enable deduplication or not
 * @write_policy: write policy for the volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO LV creation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given @vg_name/@lv_name VDO LV was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_vdo_pool_create (const gchar *vg_name, const gchar *lv_name, const gchar *pool_name, guint64 data_size, guint64 virtual_size, guint64 index_memory, gboolean compression, gboolean deduplication, BDLVMVDOWritePolicy write_policy, const BDExtraArg **extra, GError **error) {
    GVariantBuilder builder;
    GVariant *params = NULL;
    GVariant *extra_params = NULL;
    gchar *old_config = NULL;
    const gchar *write_policy_str = NULL;

    write_policy_str = bd_lvm_get_vdo_write_policy_str (write_policy, error);
    if (*error)
        return FALSE;

    /* build the params tuple */
    g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);
    g_variant_builder_add_value (&builder, g_variant_new ("s", pool_name));
    g_variant_builder_add_value (&builder, g_variant_new ("s", lv_name));
    g_variant_builder_add_value (&builder, g_variant_new ("t", data_size));
    g_variant_builder_add_value (&builder, g_variant_new ("t", virtual_size));
    params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    /* and now the extra_params params */
    g_variant_builder_init (&builder, G_VARIANT_TYPE_DICTIONARY);
    g_variant_builder_add_value (&builder, g_variant_new ("{sv}", "--compression", g_variant_new ("s", compression ? "y" : "n")));
    g_variant_builder_add_value (&builder, g_variant_new ("{sv}", "--deduplication", g_variant_new ("s", deduplication ? "y" : "n")));
    extra_params = g_variant_builder_end (&builder);
    g_variant_builder_clear (&builder);

    /* index_memory and write_policy can be specified only using the config */
    g_mutex_lock (&global_config_lock);
    old_config = global_config_str;
    if (index_memory != 0)
        global_config_str = g_strdup_printf ("%s allocation {vdo_index_memory_size_mb=%"G_GUINT64_FORMAT" vdo_write_policy=\"%s\"}", old_config ? old_config : "",
                                                                                                                                     index_memory / (1024 * 1024),
                                                                                                                                     write_policy_str);
    else
        global_config_str = g_strdup_printf ("%s allocation {vdo_write_policy=\"%s\"}", old_config ? old_config : "",
                                                                                        write_policy_str);

    call_lvm_obj_method_sync (vg_name, VG_VDO_INTF, "CreateVdoPoolandLv", params, extra_params, extra, FALSE, error);

    g_free (global_config_str);
    global_config_str = old_config;
    g_mutex_unlock (&global_config_lock);

    return ((*error) == NULL);
}

/**
 * bd_lvm_vdo_enable_compression:
 * @vg_name: name of the VG containing the to-be-changed VDO pool LV
 * @pool_name: name of the VDO pool LV to enable compression on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO change
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether compression was successfully enabled on @vg_name/@pool_name LV or not
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vdo_enable_compression (const gchar *vg_name UNUSED, const gchar *pool_name UNUSED, const BDExtraArg **extra UNUSED, GError **error) {
    return bd_lvm_is_tech_avail (BD_LVM_TECH_VDO, BD_LVM_TECH_MODE_MODIFY, error);
}

/**
 * bd_lvm_vdo_disable_compression:
 * @vg_name: name of the VG containing the to-be-changed VDO pool LV
 * @pool_name: name of the VDO pool LV to disable compression on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO change
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether compression was successfully disabled on @vg_name/@pool_name LV or not
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vdo_disable_compression (const gchar *vg_name UNUSED, const gchar *pool_name UNUSED, const BDExtraArg **extra UNUSED, GError **error) {
    return bd_lvm_is_tech_avail (BD_LVM_TECH_VDO, BD_LVM_TECH_MODE_MODIFY, error);
}

/**
 * bd_lvm_vdo_enable_deduplication:
 * @vg_name: name of the VG containing the to-be-changed VDO pool LV
 * @pool_name: name of the VDO pool LV to enable deduplication on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO change
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether deduplication was successfully enabled on @vg_name/@pool_name LV or not
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vdo_enable_deduplication (const gchar *vg_name UNUSED, const gchar *pool_name UNUSED, const BDExtraArg **extra UNUSED, GError **error) {
    return bd_lvm_is_tech_avail (BD_LVM_TECH_VDO, BD_LVM_TECH_MODE_MODIFY, error);
}

/**
 * bd_lvm_vdo_enable_deduplication:
 * @vg_name: name of the VG containing the to-be-changed VDO pool LV
 * @pool_name: name of the VDO pool LV to disable deduplication on
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO change
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether deduplication was successfully disabled on @vg_name/@pool_name LV or not
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vdo_disable_deduplication (const gchar *vg_name UNUSED, const gchar *pool_name UNUSED, const BDExtraArg **extra UNUSED, GError **error) {
    return bd_lvm_is_tech_avail (BD_LVM_TECH_VDO, BD_LVM_TECH_MODE_MODIFY, error);
}

/**
 * bd_lvm_vdo_info:
 * @vg_name: name of the VG that contains the LV to get information about
 * @pool_name: name of the VDO pool LV to get information about
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the @vg_name/@lv_name LV or %NULL in case
 * of error (the @error) gets populated in those cases)
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMVDOPooldata* bd_lvm_vdo_info (const gchar *vg_name, const gchar *pool_name, GError **error) {
    GVariant *props = NULL;

    props = get_vdo_properties (vg_name, pool_name, error);
    if (!props)
        /* the error is already populated */
        return NULL;

    return get_vdo_data_from_props (props, error);
}

/**
 * bd_lvm_vdo_resize:
 * @vg_name: name of the VG containing the to-be-resized VDO LV
 * @lv_name: name of the to-be-resized VDO LV
 * @size: the requested new size of the VDO LV
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO LV resize
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name VDO LV was successfully resized or not
 *
 * Note: Reduction needs to process TRIM for reduced disk area to unmap used data blocks
 *       from the VDO pool LV and it may take a long time.
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vdo_resize (const gchar *vg_name, const gchar *lv_name, guint64 size, const BDExtraArg **extra, GError **error) {
    return bd_lvm_lvresize (vg_name, lv_name, size, extra, error);
}

/**
 * bd_lvm_vdo_pool_resize:
 * @vg_name: name of the VG containing the to-be-resized VDO pool LV
 * @pool_name: name of the to-be-resized VDO pool LV
 * @size: the requested new size of the VDO pool LV
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO pool LV resize
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @vg_name/@pool_name VDO pool LV was successfully resized or not
 *
 * Note: Size of the VDO pool LV can be only extended, not reduced.
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vdo_pool_resize (const gchar *vg_name, const gchar *pool_name, guint64 size, const BDExtraArg **extra, GError **error) {
    BDLVMLVdata *info = NULL;

    info = bd_lvm_lvinfo (vg_name, pool_name, error);
    if (!info)
        return FALSE;

    if (info->size >= size) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_NOT_SUPPORTED,
                     "Reducing physical size of the VDO pool LV is not supported.");
        bd_lvm_lvdata_free (info);
        return FALSE;
    }

    bd_lvm_lvdata_free (info);

    return bd_lvm_lvresize (vg_name, pool_name, size, extra, error);
}

/**
 * bd_lvm_vdo_pool_convert:
 * @vg_name: name of the VG that contains @pool_lv
 * @pool_lv: name of the LV that should become the new VDO pool LV
 * @name: (allow-none): name for the VDO LV or %NULL for default name
 * @virtual_size: virtual size for the new VDO LV
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO pool creation
 *                                                 (just passed to LVM as is)
 * @error: (out): place to store error (if any)
 *
 * Converts the @pool_lv into a new VDO pool LV in the @vg_name VG and creates a new
 * @name VDO LV with size @virtual_size.
 *
 * Note: All data on @pool_lv will be irreversibly destroyed.
 *
 * Returns: whether the new VDO pool LV was successfully created from @pool_lv and or not
 *
 * Tech category: %BD_LVM_TECH_POOL-%BD_LVM_TECH_MODE_CREATE&%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vdo_pool_convert (const gchar *vg_name UNUSED, const gchar *pool_lv UNUSED, const gchar *name UNUSED, guint64 virtual_size UNUSED, guint64 index_memory UNUSED, gboolean compression UNUSED, gboolean deduplication UNUSED, BDLVMVDOWritePolicy write_policy UNUSED, const BDExtraArg **extra UNUSED, GError **error) {
    return bd_lvm_is_tech_avail (BD_LVM_TECH_VDO, BD_LVM_TECH_MODE_CREATE | BD_LVM_TECH_MODE_MODIFY, error);
}

/**
 * bd_lvm_vdolvpoolname:
 * @vg_name: name of the VG containing the queried VDO LV
 * @lv_name: name of the queried VDO LV
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): the name of the pool volume for the @vg_name/@lv_name
 * VDO LV or %NULL if failed to determine (@error) is set in those cases
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_QUERY
 */
gchar* bd_lvm_vdolvpoolname (const gchar *vg_name, const gchar *lv_name, GError **error) {
    GVariant *prop = NULL;
    const gchar *segtype = NULL;
    gchar *pool_obj_path = NULL;
    gchar *ret = NULL;

    prop = get_lv_property (vg_name, lv_name, "SegType", error);
    if (!prop)
        return NULL;

    g_variant_get_child (prop, 0, "&s", &segtype);
    if (g_strcmp0 (segtype, "vdo") != 0) {
        g_variant_unref (prop);
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_NOEXIST,
                     "The LV '%s' is not a VDO LV and thus have no VDO pool", lv_name);
        return NULL;
    }
    g_variant_unref (prop);

    prop = get_lv_property (vg_name, lv_name, "PoolLv", error);
    if (!prop)
        return NULL;
    g_variant_get (prop, "o", &pool_obj_path);
    g_variant_unref (prop);

    prop = get_object_property (pool_obj_path, LV_CMN_INTF, "Name", error);
    g_free (pool_obj_path);
    if (!prop)
        return NULL;
    g_variant_get (prop, "s", &ret);
    g_variant_unref (prop);

    return ret;
}

/**
 * bd_lvm_get_vdo_operating_mode_str:
 * @mode: mode to get the string representation for
 * @error: (out): place to store error (if any)
 *
 * Returns: string representation of @mode or %NULL in case of error
 *
 * Tech category: always provided/supported
 */
const gchar* bd_lvm_get_vdo_operating_mode_str (BDLVMVDOOperatingMode mode, GError **error) {
    switch (mode) {
    case BD_LVM_VDO_MODE_RECOVERING:
        return "recovering";
    case BD_LVM_VDO_MODE_READ_ONLY:
        return "read-only";
    case BD_LVM_VDO_MODE_NORMAL:
        return "normal";
    case BD_LVM_VDO_MODE_UNKNOWN:
        return "unknown";
    default:
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_FAIL,
                     "Invalid LVM VDO operating mode.");
        return NULL;
    }
}

/**
 * bd_lvm_get_vdo_compression_state_str:
 * @state: state to get the string representation for
 * @error: (out): place to store error (if any)
 *
 * Returns: string representation of @state or %NULL in case of error
 *
 * Tech category: always provided/supported
 */
const gchar* bd_lvm_get_vdo_compression_state_str (BDLVMVDOCompressionState state, GError **error) {
    switch (state) {
    case BD_LVM_VDO_COMPRESSION_ONLINE:
        return "online";
    case BD_LVM_VDO_COMPRESSION_OFFLINE:
        return "offline";
    case BD_LVM_VDO_COMPRESSION_UNKNOWN:
        return "unknown";
    default:
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_FAIL,
                     "Invalid LVM VDO compression state.");
        return NULL;
    }
}

/**
 * bd_lvm_get_vdo_index_state_str:
 * @state: state to get the string representation for
 * @error: (out): place to store error (if any)
 *
 * Returns: string representation of @state or %NULL in case of error
 *
 * Tech category: always provided/supported
 */
const gchar* bd_lvm_get_vdo_index_state_str (BDLVMVDOIndexState state, GError **error) {
    switch (state) {
    case BD_LVM_VDO_INDEX_ERROR:
        return "error";
    case BD_LVM_VDO_INDEX_CLOSED:
        return "closed";
    case BD_LVM_VDO_INDEX_OPENING:
        return "opening";
    case BD_LVM_VDO_INDEX_CLOSING:
        return "closing";
    case BD_LVM_VDO_INDEX_OFFLINE:
        return "offline";
    case BD_LVM_VDO_INDEX_ONLINE:
        return "online";
    case BD_LVM_VDO_INDEX_UNKNOWN:
        return "unknown";
    default:
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_FAIL,
                     "Invalid LVM VDO index state.");
        return NULL;
    }
}

/**
 * bd_lvm_get_vdo_write_policy_str:
 * @policy: policy to get the string representation for
 * @error: (out): place to store error (if any)
 *
 * Returns: string representation of @policy or %NULL in case of error
 *
 * Tech category: always provided/supported
 */
const gchar* bd_lvm_get_vdo_write_policy_str (BDLVMVDOWritePolicy policy, GError **error) {
    switch (policy) {
    case BD_LVM_VDO_WRITE_POLICY_AUTO:
        return "auto";
    case BD_LVM_VDO_WRITE_POLICY_SYNC:
        return "sync";
    case BD_LVM_VDO_WRITE_POLICY_ASYNC:
        return "async";
    case BD_LVM_VDO_WRITE_POLICY_UNKNOWN:
        return "unknown";
    default:
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_FAIL,
                     "Invalid LVM VDO write policy.");
        return NULL;
    }
}

/**
 * bd_lvm_get_vdo_write_policy_from_str:
 * @policy_str: string representation of a policy
 * @error: (out): place to store error (if any)
 *
 * Returns: write policy for the @policy_str or %BD_LVM_VDO_WRITE_POLICY_UNKNOWN if
 *          failed to determine
 *
 * Tech category: always provided/supported
 */
BDLVMVDOWritePolicy bd_lvm_get_vdo_write_policy_from_str (const gchar *policy_str, GError **error) {
    if (g_strcmp0 (policy_str, "auto") == 0)
        return BD_LVM_VDO_WRITE_POLICY_AUTO;
    else if (g_strcmp0 (policy_str, "sync") == 0)
        return BD_LVM_VDO_WRITE_POLICY_SYNC;
    else if (g_strcmp0 (policy_str, "async") == 0)
        return BD_LVM_VDO_WRITE_POLICY_ASYNC;
    else {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_VDO_POLICY_INVAL,
                     "Invalid policy given: %s", policy_str);
        return BD_LVM_VDO_WRITE_POLICY_UNKNOWN;
    }
}

/**
 * bd_lvm_vdo_get_stats_full:
 * @vg_name: name of the VG that contains @pool_name VDO pool
 * @pool_name: name of the VDO pool to get statistics for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full) (element-type utf8 utf8): hashtable of type string - string of available
 *                                                    statistics or %NULL in case of error
 *                                                    (@error gets populated in those cases)
 *
 * Statistics are collected from the values exposed by the kernel `kvdo` module
 * at the `/sys/kvdo/<VDO_NAME>/statistics/` path.
 * Some of the keys are computed to mimic the information produced by the vdo tools.
 * Please note the contents of the hashtable may vary depending on the actual kvdo module version.
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_QUERY
 */
GHashTable* bd_lvm_vdo_get_stats_full (const gchar *vg_name, const gchar *pool_name, GError **error) {
    g_autofree gchar *kvdo_name = g_strdup_printf ("%s-%s-%s", vg_name, pool_name, VDO_POOL_SUFFIX);
    return vdo_get_stats_full(kvdo_name, error);
}

/**
 * bd_lvm_vdo_get_stats:
 * @vg_name: name of the VG that contains @pool_name VDO pool
 * @pool_name: name of the VDO pool to get statistics for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): a structure containing selected statistics or %NULL in case of error
 *                           (@error gets populated in those cases)
 *
 * In contrast to @bd_lvm_vdo_get_stats_full this function will only return selected statistics
 * in a fixed structure. In case a value is not available, -1 would be returned.
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMVDOStats* bd_lvm_vdo_get_stats (const gchar *vg_name, const gchar *pool_name, GError **error) {
    GHashTable *full_stats = NULL;
    BDLVMVDOStats *stats = NULL;

    full_stats = bd_lvm_vdo_get_stats_full (vg_name, pool_name, error);
    if (!full_stats)
        return NULL;

    stats = g_new0 (BDLVMVDOStats, 1);
    get_stat_val64_default (full_stats, "block_size", &stats->block_size, -1);
    get_stat_val64_default (full_stats, "logical_block_size", &stats->logical_block_size, -1);
    get_stat_val64_default (full_stats, "physical_blocks", &stats->physical_blocks, -1);
    get_stat_val64_default (full_stats, "data_blocks_used", &stats->data_blocks_used, -1);
    get_stat_val64_default (full_stats, "overhead_blocks_used", &stats->overhead_blocks_used, -1);
    get_stat_val64_default (full_stats, "logical_blocks_used", &stats->logical_blocks_used, -1);
    get_stat_val64_default (full_stats, "usedPercent", &stats->used_percent, -1);
    get_stat_val64_default (full_stats, "savingPercent", &stats->saving_percent, -1);
    if (!get_stat_val_double (full_stats, "writeAmplificationRatio", &stats->write_amplification_ratio))
        stats->write_amplification_ratio = -1;

    g_hash_table_destroy (full_stats);

    return stats;
}
