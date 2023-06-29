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
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <blockdev/utils.h>
#include <libdevmapper.h>

#include "lvm.h"
#include "check_deps.h"
#include "dm_logging.h"
#include "vdo_stats.h"

#define INT_FLOAT_EPS 1e-5
#define SECTOR_SIZE 512
#define VDO_POOL_SUFFIX "vpool"
#define DEFAULT_PE_SIZE (4 MiB)
#define USE_DEFAULT_PE_SIZE 0
#define RESOLVE_PE_SIZE(size) ((size) == USE_DEFAULT_PE_SIZE ? DEFAULT_PE_SIZE : (size))
#define THPOOL_MD_FACTOR_NEW (0.2)
#define THPOOL_MD_FACTOR_EXISTS (1 / 6.0)

#define MIN_PE_SIZE (1 KiB)
#define MAX_PE_SIZE (16 GiB)

#define MIN_THPOOL_MD_SIZE (4 MiB)
/* DM_THIN_MAX_METADATA_SIZE is in 512 sectors */
#define MAX_THPOOL_MD_SIZE (DM_THIN_MAX_METADATA_SIZE * 512)

#define MIN_THPOOL_CHUNK_SIZE (64 KiB)
#define MAX_THPOOL_CHUNK_SIZE (1 GiB)
#define DEFAULT_CHUNK_SIZE (64 KiB)

/* according to lvmcache (7) */
#define MIN_CACHE_MD_SIZE (8 MiB)

#ifdef __LP64__
/* 64bit system */
#define MAX_LV_SIZE (8 EiB)
#else
/* 32bit system */
#define MAX_LV_SIZE (16 TiB)
#endif

#define LVM_MIN_VERSION "2.02.116"
#define LVM_VERSION_FSRESIZE "2.03.19"

static GMutex global_config_lock;
static gchar *global_config_str = NULL;

static gchar *global_devices_str = NULL;

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
    new_data->pv_size = data->pv_size;
    new_data->pe_start = data->pe_start;
    new_data->vg_name = g_strdup (data->vg_name);
    new_data->vg_uuid = g_strdup (data->vg_uuid);
    new_data->vg_size = data->vg_size;
    new_data->vg_free = data->vg_free;
    new_data->vg_extent_size = data->vg_extent_size;
    new_data->vg_extent_count = data->vg_extent_count;
    new_data->vg_free_count = data->vg_free_count;
    new_data->vg_pv_count = data->vg_pv_count;
    new_data->pv_tags = g_strdupv (data->pv_tags);
    new_data->missing = data->missing;

    return new_data;
}

void bd_lvm_pvdata_free (BDLVMPVdata *data) {
    if (data == NULL)
        return;

    g_free (data->pv_name);
    g_free (data->pv_uuid);
    g_free (data->vg_name);
    g_free (data->vg_uuid);
    g_strfreev (data->pv_tags);
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
    new_data->vg_tags = g_strdupv (data->vg_tags);
    return new_data;
}

void bd_lvm_vgdata_free (BDLVMVGdata *data) {
    if (data == NULL)
        return;

    g_free (data->name);
    g_free (data->uuid);
    g_strfreev (data->vg_tags);
    g_free (data);
}

BDLVMSEGdata* bd_lvm_segdata_copy (BDLVMSEGdata *data) {
    if (data == NULL)
        return NULL;

    BDLVMSEGdata *new_data = g_new0 (BDLVMSEGdata, 1);

    new_data->size_pe = data->size_pe;
    new_data->pv_start_pe = data->pv_start_pe;
    new_data->pvdev = g_strdup (data->pvdev);
    return new_data;
}

void bd_lvm_segdata_free (BDLVMSEGdata *data) {
    if (data == NULL)
        return;

    g_free (data->pvdev);
    g_free (data);
}

static BDLVMSEGdata **copy_segs (BDLVMSEGdata **segs) {
    int len;
    BDLVMSEGdata **new_segs;

    if (segs == NULL)
       return NULL;

    for (len = 0; segs[len]; len++)
        ;

    new_segs = g_new0 (BDLVMSEGdata *, len+1);
    for (int i = 0; i < len; i++)
        new_segs[i] = bd_lvm_segdata_copy (segs[i]);

    return new_segs;
}

static void free_segs (BDLVMSEGdata **segs) {
    if (segs == NULL)
       return;

    for (int i = 0; segs[i]; i++)
        bd_lvm_segdata_free (segs[i]);
    (g_free) (segs);
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
    new_data->lv_tags = g_strdupv (data->lv_tags);
    new_data->data_lvs = g_strdupv (data->data_lvs);
    new_data->metadata_lvs = g_strdupv (data->metadata_lvs);
    new_data->segs = copy_segs (data->segs);
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
    g_strfreev (data->lv_tags);
    g_strfreev (data->data_lvs);
    g_strfreev (data->metadata_lvs);
    free_segs (data->segs);
    g_free (data);
}

BDLVMVDOPooldata* bd_lvm_vdopooldata_copy (BDLVMVDOPooldata *data) {
    if (data == NULL)
        return NULL;

    BDLVMVDOPooldata *new_data = g_new0 (BDLVMVDOPooldata, 1);

    new_data->operating_mode = data->operating_mode;
    new_data->compression_state = data->compression_state;
    new_data->index_state = data->index_state;
    new_data->write_policy = data->write_policy;
    new_data->used_size = data->used_size;
    new_data->saving_percent = data->saving_percent;
    new_data->index_memory_size = data->index_memory_size;
    new_data->deduplication = data->deduplication;
    new_data->compression = data->compression;
    return new_data;
}

void bd_lvm_vdopooldata_free (BDLVMVDOPooldata *data) {
    if (data == NULL)
        return;

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


static volatile guint avail_deps = 0;
static volatile guint avail_features = 0;
static volatile guint avail_module_deps = 0;
static GMutex deps_check_lock;

#define DEPS_LVM 0
#define DEPS_LVM_MASK (1 << DEPS_LVM)
#define DEPS_LVMDEVICES 1
#define DEPS_LVMDEVICES_MASK (1 << DEPS_LVMDEVICES)
#define DEPS_LAST 2

static const UtilDep deps[DEPS_LAST] = {
    {"lvm", LVM_MIN_VERSION, "version", "LVM version:\\s+([\\d\\.]+)"},
    {"lvmdevices", NULL, NULL, NULL},
};

#define FEATURES_VDO 0
#define FEATURES_VDO_MASK (1 << FEATURES_VDO)
#define FEATURES_WRITECACHE 0
#define FEATURES_WRITECACHE_MASK (1 << FEATURES_WRITECACHE)
#define FEATURES_LAST 2

static const UtilFeatureDep features[FEATURES_LAST] = {
    {"lvm", "vdo", "segtypes", NULL},
    {"lvm", "writecache", "segtypes", NULL},
};

#define MODULE_DEPS_VDO 0
#define MODULE_DEPS_VDO_MASK (1 << MODULE_DEPS_VDO)
#define MODULE_DEPS_LAST 1

static const gchar*const module_deps[MODULE_DEPS_LAST] = { "kvdo" };

#define UNUSED __attribute__((unused))

/**
 * bd_lvm_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_lvm_init (void) {
    dm_log_with_errno_init ((dm_log_with_errno_fn) redirect_dm_log);
#ifdef DEBUG
    dm_log_init_verbose (LOG_DEBUG);
#else
    dm_log_init_verbose (LOG_INFO);
#endif

    return TRUE;
};

/**
 * bd_lvm_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_lvm_close (void) {
    dm_log_with_errno_init (NULL);
    dm_log_init_verbose (0);
}

/**
 * bd_lvm_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDLVMTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
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
            return check_features (&avail_features, FEATURES_VDO_MASK, features, FEATURES_LAST, &deps_check_lock, error) &&
                   check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error) &&
                   check_deps (&avail_deps, DEPS_LVM_MASK, deps, DEPS_LAST, &deps_check_lock, error);
    case BD_LVM_TECH_WRITECACHE:
            return check_features (&avail_features, FEATURES_WRITECACHE_MASK, features, FEATURES_LAST, &deps_check_lock, error) &&
                   check_deps (&avail_deps, DEPS_LVM_MASK, deps, DEPS_LAST, &deps_check_lock, error);
    case BD_LVM_TECH_DEVICES:
            return check_deps (&avail_deps, DEPS_LVMDEVICES_MASK, deps, DEPS_LAST, &deps_check_lock, error);
    default:
        /* everything is supported by this implementation of the plugin */
        return check_deps (&avail_deps, DEPS_LVM_MASK, deps, DEPS_LAST, &deps_check_lock, error);
    }
}

static gboolean call_lvm_and_report_error (const gchar **args, const BDExtraArg **extra, gboolean lock_config, GError **error) {
    gboolean success = FALSE;
    guint i = 0;
    guint args_length = g_strv_length ((gchar **) args);
    g_autofree gchar *config_arg = NULL;
    g_autofree gchar *devices_arg = NULL;

    if (!check_deps (&avail_deps, DEPS_LVM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    /* don't allow global config string changes during the run */
    if (lock_config)
        g_mutex_lock (&global_config_lock);

    /* allocate enough space for the args plus "lvm", "--config", "--devices" and NULL */
    const gchar **argv = g_new0 (const gchar*, args_length + 4);

    /* construct argv from args with "lvm" prepended */
    argv[0] = "lvm";
    for (i=0; i < args_length; i++)
        argv[i+1] = args[i];
    if (global_config_str) {
        config_arg = g_strdup_printf ("--config=%s", global_config_str);
        argv[++args_length] = config_arg;
    }
    if (global_devices_str) {
        devices_arg = g_strdup_printf ("--devices=%s", global_devices_str);
        argv[++args_length] = devices_arg;
    }
    argv[++args_length] = NULL;

    success = bd_utils_exec_and_report_error (argv, extra, error);
    if (lock_config)
        g_mutex_unlock (&global_config_lock);
    g_free (argv);

    return success;
}

static gboolean call_lvm_and_capture_output (const gchar **args, const BDExtraArg **extra, gchar **output, GError **error) {
    gboolean success = FALSE;
    guint i = 0;
    guint args_length = g_strv_length ((gchar **) args);
    g_autofree gchar *config_arg = NULL;
    g_autofree gchar *devices_arg = NULL;

    if (!check_deps (&avail_deps, DEPS_LVM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    /* don't allow global config string changes during the run */
    g_mutex_lock (&global_config_lock);

    /* allocate enough space for the args plus "lvm", "--config", "--devices" and NULL */
    const gchar **argv = g_new0 (const gchar*, args_length + 4);

    /* construct argv from args with "lvm" prepended */
    argv[0] = "lvm";
    for (i=0; i < args_length; i++)
        argv[i+1] = args[i];
    if (global_config_str) {
        config_arg = g_strdup_printf ("--config=%s", global_config_str);
        argv[++args_length] = config_arg;
    }
    if (global_devices_str) {
        devices_arg = g_strdup_printf ("--devices=%s", global_devices_str);
        argv[++args_length] = devices_arg;
    }
    argv[++args_length] = NULL;

    success = bd_utils_exec_and_capture_output (argv, extra, output, error);
    g_mutex_unlock (&global_config_lock);
    g_free (argv);

    return success;
}

/**
 * parse_lvm_vars:
 * @str: string to parse
 * @num_items: (out): number of parsed items
 *
 * Returns: (transfer full): a GHashTable containing key-value items parsed from the @string
 */
static GHashTable* parse_lvm_vars (const gchar *str, guint *num_items) {
    GHashTable *table = NULL;
    gchar **items = NULL;
    gchar **item_p = NULL;
    gchar **key_val = NULL;

    table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    *num_items = 0;

    items = g_strsplit_set (str, " \t\n", 0);
    for (item_p=items; *item_p; item_p++) {
        key_val = g_strsplit (*item_p, "=", 2);
        if (g_strv_length (key_val) == 2) {
            /* we only want to process valid lines (with the '=' character) */
            g_hash_table_insert (table, key_val[0], key_val[1]);
            g_free (key_val);
            (*num_items)++;
        } else
            /* invalid line, just free key_val */
            g_strfreev (key_val);
    }

    g_strfreev (items);
    return table;
}

static BDLVMPVdata* get_pv_data_from_table (GHashTable *table, gboolean free_table) {
    BDLVMPVdata *data = g_new0 (BDLVMPVdata, 1);
    gchar *value = NULL;

    data->pv_name = g_strdup ((gchar*) g_hash_table_lookup (table, "LVM2_PV_NAME"));
    data->pv_uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "LVM2_PV_UUID"));

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PV_FREE");
    if (value)
        data->pv_free = g_ascii_strtoull (value, NULL, 0);
    else
        data->pv_free = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PV_SIZE");
    if (value)
        data->pv_size = g_ascii_strtoull (value, NULL, 0);
    else
        data->pv_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PE_START");
    if (value)
        data->pe_start = g_ascii_strtoull (value, NULL, 0);
    else
        data->pe_start = 0;

    data->vg_name = g_strdup ((gchar*) g_hash_table_lookup (table, "LVM2_VG_NAME"));
    data->vg_uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "LVM2_VG_UUID"));

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_SIZE");
    if (value)
        data->vg_size = g_ascii_strtoull (value, NULL, 0);
    else
        data->vg_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_FREE");
    if (value)
        data->vg_free = g_ascii_strtoull (value, NULL, 0);
    else
        data->vg_free = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_EXTENT_SIZE");
    if (value)
        data->vg_extent_size = g_ascii_strtoull (value, NULL, 0);
    else
        data->vg_extent_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_EXTENT_COUNT");
    if (value)
        data->vg_extent_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->vg_extent_count = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_FREE_COUNT");
    if (value)
        data->vg_free_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->vg_free_count = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PV_COUNT");
    if (value)
        data->vg_pv_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->vg_pv_count = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PV_TAGS");
    if (value)
        data->pv_tags = g_strsplit (value, ",", -1);
    else
        data->pv_tags = NULL;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PV_MISSING");
    data->missing = (g_strcmp0 (value, "missing") == 0);

    if (free_table)
        g_hash_table_destroy (table);

    return data;
}

static BDLVMVGdata* get_vg_data_from_table (GHashTable *table, gboolean free_table) {
    BDLVMVGdata *data = g_new0 (BDLVMVGdata, 1);
    gchar *value = NULL;

    data->name = g_strdup (g_hash_table_lookup (table, "LVM2_VG_NAME"));
    data->uuid = g_strdup (g_hash_table_lookup (table, "LVM2_VG_UUID"));

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_SIZE");
    if (value)
        data->size = g_ascii_strtoull (value, NULL, 0);
    else
        data->size = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_FREE");
    if (value)
        data->free = g_ascii_strtoull (value, NULL, 0);
    else
        data->free= 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_EXTENT_SIZE");
    if (value)
        data->extent_size = g_ascii_strtoull (value, NULL, 0);
    else
        data->extent_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_EXTENT_COUNT");
    if (value)
        data->extent_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->extent_count = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_FREE_COUNT");
    if (value)
        data->free_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->free_count = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_PV_COUNT");
    if (value)
        data->pv_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->pv_count = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_EXPORTED");
    if (value && g_strcmp0 (value, "exported") == 0)
        data->exported = TRUE;
    else
        data->exported = FALSE;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VG_TAGS");
    if (value)
        data->vg_tags = g_strsplit (value, ",", -1);
    else
        data->vg_tags = NULL;

    if (free_table)
        g_hash_table_destroy (table);

    return data;
}

static gchar **prepare_sublvs (gchar **values, gchar *extra_value) {
  /* LVM2 guarantees: No "/dev/" prefixes or "[unknown]" in a list of sub-lvs. */
  gboolean found_extra = FALSE;
  for (int i = 0; values[i]; i++) {
    gchar *paren = strrchr (values[i], '(');
    if (paren) {
      /* LVM2 guarantees: start offsets of sub-lvs are always zero. */
      *paren = '\0';
    }
    if (g_strcmp0 (extra_value, values[i]) == 0)
      found_extra = TRUE;
  }
  if (extra_value && *extra_value && !found_extra) {
    int len = g_strv_length (values);
    gchar **new_values = g_new0 (gchar *, len+2);
    for (int j = 0; j < len; j++)
      new_values[j] = values[j];
    new_values[len] = g_strdup (extra_value);
    g_free (values);
    values = new_values;
  }
  return values;
}

static BDLVMLVdata* get_lv_data_from_table (GHashTable *table, gboolean free_table) {
    BDLVMLVdata *data = g_new0 (BDLVMLVdata, 1);
    gchar *value = NULL;

    data->lv_name = g_strdup (g_hash_table_lookup (table, "LVM2_LV_NAME"));
    data->vg_name = g_strdup (g_hash_table_lookup (table, "LVM2_VG_NAME"));
    data->uuid = g_strdup (g_hash_table_lookup (table, "LVM2_LV_UUID"));

    value = (gchar*) g_hash_table_lookup (table, "LVM2_LV_SIZE");
    if (value)
        data->size = g_ascii_strtoull (value, NULL, 0);
    else
        data->size = 0;

    data->attr = g_strdup (g_hash_table_lookup (table, "LVM2_LV_ATTR"));

    value = g_hash_table_lookup (table, "LVM2_SEGTYPE");
    if (g_strcmp0 (value, "error") == 0) {
      /* A segment type "error" appears when "vgreduce
       * --removemissing" replaces a missing PV with a device mapper
       * "error" target.  It very likely was a "linear" segment before that
       * and will again be "linear" after repair.  Let's not expose
       * this implementation detail.
       */
      value = "linear";
    }
    data->segtype = g_strdup (value);

    data->origin = g_strdup (g_hash_table_lookup (table, "LVM2_ORIGIN"));
    data->pool_lv = g_strdup (g_hash_table_lookup (table, "LVM2_POOL_LV"));
    data->data_lv = g_strdup (g_hash_table_lookup (table, "LVM2_DATA_LV"));
    data->metadata_lv = g_strdup (g_hash_table_lookup (table, "LVM2_METADATA_LV"));
    data->roles = g_strdup (g_hash_table_lookup (table, "LVM2_LV_ROLE"));

    data->move_pv = g_strdup (g_hash_table_lookup (table, "LVM2_MOVE_PV"));

    value = (gchar*) g_hash_table_lookup (table, "LVM2_DATA_PERCENT");
    if (value)
        data->data_percent = g_ascii_strtoull (value, NULL, 0);
    else
        data->data_percent = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_METADATA_PERCENT");
    if (value)
        data->metadata_percent = g_ascii_strtoull (value, NULL, 0);
    else
        data->metadata_percent = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_COPY_PERCENT");
    if (value)
        data->copy_percent = g_ascii_strtoull (value, NULL, 0);
    else
        data->copy_percent = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_LV_TAGS");
    if (value)
        data->lv_tags = g_strsplit (value, ",", -1);
    else
        data->lv_tags = NULL;

    /* replace '[' and ']' (marking LVs as internal) with spaces and then
       remove all the leading and trailing whitespace */
    g_strstrip (g_strdelimit (data->pool_lv, "[]", ' '));
    g_strstrip (g_strdelimit (data->data_lv, "[]", ' '));
    g_strstrip (g_strdelimit (data->metadata_lv, "[]", ' '));

    value = (gchar*) g_hash_table_lookup (table, "LVM2_DEVICES");
    if (value) {
      gchar **values = g_strsplit (value, ",", -1);

      /* If values starts with "/dev/", we have a single PV.

         If the list is empty, this is probably an "error" segment
         resulting from a "vgreduce --removemissing" operation.

         If the value starts with "[unknown]", it is a segment with a
         missing PV that hasn't been converted to an "error" segment
         yet.

         Otherwise it is a list of sub-lvs.

         LVM2 guarantees: only one entry if the first is a PV
         Additional segments are added in merge_lv_data below.
      */
      if (!values[0] || g_str_has_prefix (values[0], "[unknown]")) {
        data->segs = g_new0 (BDLVMSEGdata *, 1);
        data->segs[0] = NULL;
        g_strfreev (values);
      } else if (g_str_has_prefix (values[0], "/dev/")) {
        data->segs = g_new0 (BDLVMSEGdata *, 2);
        data->segs[0] = g_new0 (BDLVMSEGdata, 1);
        data->segs[1] = NULL;

        gchar *paren = strrchr (values[0], '(');
        if (paren) {
          data->segs[0]->pv_start_pe = atoi (paren+1);
          *paren = '\0';
        }
        data->segs[0]->pvdev = g_strdup (values[0]);
        value = (gchar*) g_hash_table_lookup (table, "LVM2_SEG_SIZE_PE");
        if (value)
          data->segs[0]->size_pe = g_ascii_strtoull (value, NULL, 0);
        g_strfreev (values);
      } else {
        data->data_lvs = prepare_sublvs (values, data->data_lv);
        value = (gchar*) g_hash_table_lookup (table, "LVM2_METADATA_DEVICES");
        data->metadata_lvs = prepare_sublvs (g_strsplit (value ?: "", ",", -1), data->metadata_lv);
      }
    }

    if (free_table)
        g_hash_table_destroy (table);

    return data;
}

static void merge_lv_data (BDLVMLVdata *data, BDLVMLVdata *more_data) {
  /* LVM2 guarantees:
     - more_data->data_lvs is NULL
     - more_data->metadata_lvs is NULL
     - more_data->segs has zero or one entry
     - more_data->seg_type is the same as data->seg_type (after mapping "error" to "linear")
  */

  if (more_data->segs && more_data->segs[0]) {
    int i;
    for (i = 0; data->segs && data->segs[i]; i++)
      ;

    BDLVMSEGdata **new_segs = g_new0 (BDLVMSEGdata *, i+2);
    for (i = 0; data->segs && data->segs[i]; i++)
      new_segs[i] = data->segs[i];
    new_segs[i] = more_data->segs[0];
    g_free (data->segs);
    data->segs = new_segs;
    more_data->segs[0] = NULL;
  }
}

static BDLVMVDOPooldata* get_vdo_data_from_table (GHashTable *table, gboolean free_table) {
    BDLVMVDOPooldata *data = g_new0 (BDLVMVDOPooldata, 1);
    gchar *value = NULL;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VDO_OPERATING_MODE");
    if (g_strcmp0 (value, "recovering") == 0)
        data->operating_mode = BD_LVM_VDO_MODE_RECOVERING;
    else if (g_strcmp0 (value, "read-only") == 0)
        data->operating_mode = BD_LVM_VDO_MODE_READ_ONLY;
    else if (g_strcmp0 (value, "normal") == 0)
        data->operating_mode = BD_LVM_VDO_MODE_NORMAL;
    else {
        bd_utils_log_format (BD_UTILS_LOG_DEBUG, "Unknown VDO operating mode: %s", value);
        data->operating_mode = BD_LVM_VDO_MODE_UNKNOWN;
    }

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VDO_COMPRESSION_STATE");
    if (g_strcmp0 (value, "online") == 0)
        data->compression_state = BD_LVM_VDO_COMPRESSION_ONLINE;
    else if (g_strcmp0 (value, "offline") == 0)
        data->compression_state = BD_LVM_VDO_COMPRESSION_OFFLINE;
    else {
        bd_utils_log_format (BD_UTILS_LOG_DEBUG, "Unknown VDO compression state: %s", value);
        data->compression_state = BD_LVM_VDO_COMPRESSION_UNKNOWN;
    }

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VDO_INDEX_STATE");
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
        bd_utils_log_format (BD_UTILS_LOG_DEBUG, "Unknown VDO index state: %s", value);
        data->index_state = BD_LVM_VDO_INDEX_UNKNOWN;
    }

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VDO_WRITE_POLICY");
    if (g_strcmp0 (value, "auto") == 0)
        data->write_policy = BD_LVM_VDO_WRITE_POLICY_AUTO;
    else if (g_strcmp0 (value, "sync") == 0)
        data->write_policy = BD_LVM_VDO_WRITE_POLICY_SYNC;
    else if (g_strcmp0 (value, "async") == 0)
        data->write_policy = BD_LVM_VDO_WRITE_POLICY_ASYNC;
    else {
        bd_utils_log_format (BD_UTILS_LOG_DEBUG, "Unknown VDO write policy: %s", value);
        data->write_policy = BD_LVM_VDO_WRITE_POLICY_UNKNOWN;
    }

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VDO_INDEX_MEMORY_SIZE");
    if (value)
        data->index_memory_size = g_ascii_strtoull (value, NULL, 0);
    else
        data->index_memory_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VDO_USED_SIZE");
    if (value)
        data->used_size = g_ascii_strtoull (value, NULL, 0);
    else
        data->used_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VDO_SAVING_PERCENT");
    if (value)
        data->saving_percent = g_ascii_strtoull (value, NULL, 0);
    else
        data->saving_percent = 0;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VDO_COMPRESSION");
    if (value && g_strcmp0 (value, "enabled") == 0)
        data->compression = TRUE;
    else
        data->compression = FALSE;

    value = (gchar*) g_hash_table_lookup (table, "LVM2_VDO_DEDUPLICATION");
    if (value && g_strcmp0 (value, "enabled") == 0)
        data->deduplication = TRUE;
    else
        data->deduplication = FALSE;

    if (free_table)
        g_hash_table_destroy (table);

    return data;
}

/**
 * bd_lvm_is_supported_pe_size:
 * @size: size (in bytes) to test
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given size is supported physical extent size or not
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
gboolean bd_lvm_is_supported_pe_size (guint64 size, GError **error UNUSED) {
    return (((size % 2) == 0) && (size >= (MIN_PE_SIZE)) && (size <= (MAX_PE_SIZE)));
}

/**
 * bd_lvm_get_supported_pe_sizes:
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full) (array fixed-size=25): list of supported PE sizes
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
guint64 *bd_lvm_get_supported_pe_sizes (GError **error UNUSED) {
    guint8 i;
    guint64 val = MIN_PE_SIZE;
    guint8 num_items = ((guint8) round (log2 ((double) MAX_PE_SIZE))) - ((guint8) round (log2 ((double) MIN_PE_SIZE))) + 2;
    guint64 *ret = g_new0 (guint64, num_items);

    for (i=0; (val <= MAX_PE_SIZE); i++, val = val * 2)
        ret[i] = val;

    ret[num_items-1] = 0;

    return ret;
}

/**
 * bd_lvm_get_max_lv_size:
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: maximum LV size in bytes
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_get_max_lv_size (GError **error UNUSED) {
    return MAX_LV_SIZE;
}

/**
 * bd_lvm_round_size_to_pe:
 * @size: size to be rounded
 * @pe_size: physical extent (PE) size or 0 to use the default
 * @roundup: whether to round up or down (ceil or floor)
 * @error: (out) (optional): place to store error (if any)
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
    pe_size = RESOLVE_PE_SIZE (pe_size);
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
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: space taken on disk(s) by the LV with given @size
 *
 * Gives number of bytes needed for an LV with the size @lv_size on an LVM stack
 * using given @pe_size.
 *
 * Tech category: %BD_LVM_TECH_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_get_lv_physical_size (guint64 lv_size, guint64 pe_size, GError **error) {
    pe_size = RESOLVE_PE_SIZE (pe_size);

    /* the LV just takes space rounded up to the multiple of extent size */
    return bd_lvm_round_size_to_pe (lv_size, pe_size, TRUE, error);
}

/**
 * bd_lvm_get_thpool_padding:
 * @size: size of the thin pool
 * @pe_size: PE size or 0 if the default value should be used
 * @included: if padding is already included in the size
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: size of the padding needed for a thin pool with the given @size
 *         according to the @pe_size and @included
 *
 * Tech category: %BD_LVM_TECH_THIN_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_get_thpool_padding (guint64 size, guint64 pe_size, gboolean included, GError **error) {
    guint64 raw_md_size;
    pe_size = RESOLVE_PE_SIZE (pe_size);

    if (included)
        raw_md_size = (guint64) ceil (size * THPOOL_MD_FACTOR_EXISTS);
    else
        raw_md_size = (guint64) ceil (size * THPOOL_MD_FACTOR_NEW);

    return MIN (bd_lvm_round_size_to_pe (raw_md_size, pe_size, TRUE, error),
                bd_lvm_round_size_to_pe (MAX_THPOOL_MD_SIZE, pe_size, TRUE, error));
}

/**
 * bd_lvm_get_thpool_meta_size:
 * @size: size of the thin pool
 * @chunk_size: chunk size of the thin pool or 0 to use the default
 * @n_snapshots: ignored
 * @error: (out) (optional): place to store error (if any)
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
    md_size = UINT64_C (64) * size / (chunk_size ? chunk_size : DEFAULT_CHUNK_SIZE);

    if (md_size > MAX_THPOOL_MD_SIZE)
        md_size = MAX_THPOOL_MD_SIZE;
    else if (md_size < MIN_THPOOL_MD_SIZE)
        md_size = MIN_THPOOL_MD_SIZE;

    return md_size;
}

/**
 * bd_lvm_is_valid_thpool_md_size:
 * @size: the size to be tested
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given size is a valid thin pool metadata size or not
 *
 * Tech category: %BD_LVM_TECH_THIN_CALCS no mode (it is ignored)
 */
gboolean bd_lvm_is_valid_thpool_md_size (guint64 size, GError **error UNUSED) {
    return ((MIN_THPOOL_MD_SIZE <= size) && (size <= MAX_THPOOL_MD_SIZE));
}

/**
 * bd_lvm_is_valid_thpool_chunk_size:
 * @size: the size to be tested
 * @discard: whether discard/TRIM is required to be supported or not
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given size is a valid thin pool chunk size or not
 *
 * Tech category: %BD_LVM_TECH_THIN_CALCS no mode (it is ignored)
 */
gboolean bd_lvm_is_valid_thpool_chunk_size (guint64 size, gboolean discard, GError **error UNUSED) {
    gdouble size_log2 = 0.0;

    if ((size < MIN_THPOOL_CHUNK_SIZE) || (size > MAX_THPOOL_CHUNK_SIZE))
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the PV creation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the PV was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_pvcreate (const gchar *device, guint64 data_alignment, guint64 metadata_size, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"pvcreate", device, NULL, NULL, NULL};
    guint next_arg = 2;
    gchar *dataalign_str = NULL;
    gchar *metadata_str = NULL;
    gboolean ret = FALSE;

    if (data_alignment != 0) {
        dataalign_str = g_strdup_printf ("--dataalignment=%"G_GUINT64_FORMAT"K", data_alignment / 1024);
        args[next_arg++] = dataalign_str;
    }

    if (metadata_size != 0) {
        metadata_str = g_strdup_printf ("--metadatasize=%"G_GUINT64_FORMAT"K", metadata_size / 1024);
        args[next_arg++] = metadata_str;
    }

    ret = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free (dataalign_str);
    g_free (metadata_str);

    return ret;
}

/**
 * bd_lvm_pvresize:
 * @device: the device to resize
 * @size: the new requested size of the PV or 0 if it should be adjusted to device's size
 * @extra: (nullable) (array zero-terminated=1): extra options for the PV resize
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the PV's size was successfully changed or not
 *
 * If given @size different from 0, sets the PV's size to the given value (see
 * pvresize(8)). If given @size 0, adjusts the PV's size to the underlying
 * block device's size.
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_pvresize (const gchar *device, guint64 size, const BDExtraArg **extra, GError **error) {
    gchar *size_str = NULL;
    const gchar *args[6] = {"pvresize", "-y", NULL, NULL, NULL, NULL};
    guint8 next_pos = 2;
    guint8 to_free_pos = 0;
    gboolean ret = FALSE;

    if (size != 0) {
        size_str = g_strdup_printf ("%"G_GUINT64_FORMAT"K", size / 1024);
        args[next_pos] = "--setphysicalvolumesize";
        next_pos++;
        args[next_pos] = size_str;
        to_free_pos = next_pos;
        next_pos++;
    }

    args[next_pos] = device;

    ret = call_lvm_and_report_error (args, extra, TRUE, error);
    if (to_free_pos > 0)
        g_free ((gchar *) args[to_free_pos]);

    return ret;
}

/**
 * bd_lvm_pvremove:
 * @device: the PV device to be removed/destroyed
 * @extra: (nullable) (array zero-terminated=1): extra options for the PV removal
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the PV was successfully removed/destroyed or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_REMOVE
 */
gboolean bd_lvm_pvremove (const gchar *device, const BDExtraArg **extra, GError **error) {
    /* one has to be really persuasive to remove a PV (the double --force is not
       bug, at least not in this code) */
    const gchar *args[6] = {"pvremove", "--force", "--force", "--yes", device, NULL};

    return call_lvm_and_report_error (args, extra, TRUE, error);
}

static gboolean extract_pvmove_progress (const gchar *line, guint8 *completion) {
    gchar *num_start = NULL;
    gint n_scanned = 0;
    guint8 try_completion = 0;

    num_start = strrchr (line, ' ');
    if (!num_start)
        return FALSE;
    num_start++;
    n_scanned = sscanf (num_start, "%hhu", &try_completion);
    if (n_scanned == 1) {
        *completion = try_completion;
        return TRUE;
    }
    return FALSE;
}

/**
 * bd_lvm_pvmove:
 * @src: the PV device to move extents off of
 * @dest: (nullable): the PV device to move extents onto or %NULL
 * @extra: (nullable) (array zero-terminated=1): extra options for the PV move
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the extents from the @src PV where successfully moved or not
 *
 * If @dest is %NULL, VG allocation rules are used for the extents from the @src
 * PV (see pvmove(8)).
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_pvmove (const gchar *src, const gchar *dest, const BDExtraArg **extra, GError **error) {
    const gchar *args[6] = {"pvmove", "-i", "1", src, NULL, NULL};
    gint status = 0;
    if (dest)
        args[4] = dest;

    return bd_utils_exec_and_report_progress (args, extra, extract_pvmove_progress, &status, error);
}

/**
 * bd_lvm_pvscan:
 * @device: (nullable): the device to scan for PVs or %NULL
 * @update_cache: whether to update the lvmetad cache or not
 * @extra: (nullable) (array zero-terminated=1): extra options for the PV scan
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the system or @device was successfully scanned for PVs or not
 *
 * The @device argument is used only if @update_cache is %TRUE. Otherwise the
 * whole system is scanned for PVs.
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
gboolean bd_lvm_pvscan (const gchar *device, gboolean update_cache, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"pvscan", NULL, NULL, NULL};
    if (update_cache) {
        args[1] = "--cache";
        args[2] = device;
    }
    else
        if (device)
            bd_utils_log_format (BD_UTILS_LOG_WARNING, "Ignoring the device argument in pvscan (cache update not requested)");

    return call_lvm_and_report_error (args, extra, TRUE, error);
}

static gboolean _manage_lvm_tags (const gchar *devspec, const gchar **tags, const gchar *action, const gchar *cmd, GError **error) {
    guint tags_len = g_strv_length ((gchar **) tags);
    const gchar **argv = g_new0 (const gchar*, 2 * tags_len + 3);
    guint next_arg = 0;
    gboolean success = FALSE;

    argv[next_arg++] = cmd;
    for (guint i = 0; i < tags_len; i++) {
        argv[next_arg++] = action;
        argv[next_arg++] = tags[i];
    }
    argv[next_arg++] = devspec;
    argv[next_arg] = NULL;

    success = call_lvm_and_report_error (argv, NULL, TRUE, error);
    g_free (argv);
    return success;
}

/**
 * bd_lvm_add_pv_tags:
 * @device: the device to set PV tags for
 * @tags: (array zero-terminated=1): list of tags to add
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the tags were successfully added to @device or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
gboolean bd_lvm_add_pv_tags (const gchar *device, const gchar **tags, GError **error) {
    return _manage_lvm_tags (device, tags, "--addtag", "pvchange", error);
}

/**
 * bd_lvm_delete_pv_tags:
 * @device: the device to set PV tags for
 * @tags: (array zero-terminated=1): list of tags to remove
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the tags were successfully removed from @device or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
gboolean bd_lvm_delete_pv_tags (const gchar *device, const gchar **tags, GError **error) {
    return _manage_lvm_tags (device, tags, "--deltag", "pvchange", error);
}

/**
 * bd_lvm_pvinfo:
 * @device: a PV to get information about or %NULL
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the PV on the given @device or
 * %NULL in case of error (the @error) gets populated in those cases)
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMPVdata* bd_lvm_pvinfo (const gchar *device, GError **error) {
    const gchar *args[10] = {"pvs", "--unit=b", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--noheadings",
                       "-o", "pv_name,pv_uuid,pv_free,pv_size,pe_start,vg_name,vg_uuid,vg_size," \
                       "vg_free,vg_extent_size,vg_extent_count,vg_free_count,pv_count,pv_tags,pv_missing",
                       device, NULL};
    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;

    success = call_lvm_and_capture_output (args, NULL, &output, error);
    if (!success)
        /* the error is already populated from the call */
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 15)) {
            g_clear_error (error);
            g_strfreev (lines);
            return get_pv_data_from_table (table, TRUE);
        } else
            if (table)
                g_hash_table_destroy (table);
    }
    g_strfreev (lines);

    /* getting here means no usable info was found */
    g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                 "Failed to parse information about the PV");
    return NULL;
}

/**
 * bd_lvm_pvs:
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about PVs found in the system
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMPVdata** bd_lvm_pvs (GError **error) {
    const gchar *args[9] = {"pvs", "--unit=b", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--noheadings",
                       "-o", "pv_name,pv_uuid,pv_free,pv_size,pe_start,vg_name,vg_uuid,vg_size," \
                       "vg_free,vg_extent_size,vg_extent_count,vg_free_count,pv_count,pv_tags,pv_missing",
                       NULL};
    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;
    GPtrArray *pvs;
    BDLVMPVdata *pvdata = NULL;

    pvs = g_ptr_array_new ();

    success = call_lvm_and_capture_output (args, NULL, &output, error);
    if (!success) {
        if (g_error_matches (*error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_NOOUT)) {
            /* no output => no VGs, not an error */
            g_clear_error (error);
            /* return an empty list */
            g_ptr_array_add (pvs, NULL);
            return (BDLVMPVdata **) g_ptr_array_free (pvs, FALSE);
        }
        else {
            /* the error is already populated from the call */
            g_ptr_array_free (pvs, TRUE);
            return NULL;
        }
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 15)) {
            /* valid line, try to parse and record it */
            pvdata = get_pv_data_from_table (table, TRUE);
            if (pvdata)
                g_ptr_array_add (pvs, pvdata);
        } else
            if (table)
                g_hash_table_destroy (table);
    }

    g_strfreev (lines);

    if (pvs->len == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                     "Failed to parse information about PVs");
        g_ptr_array_free (pvs, TRUE);
        return NULL;
    }

    /* returning NULL-terminated array of BDLVMPVdata */
    g_ptr_array_add (pvs, NULL);
    return (BDLVMPVdata **) g_ptr_array_free (pvs, FALSE);
}

/**
 * bd_lvm_vgcreate:
 * @name: name of the newly created VG
 * @pv_list: (array zero-terminated=1): list of PVs the newly created VG should use
 * @pe_size: PE size or 0 if the default value should be used
 * @extra: (nullable) (array zero-terminated=1): extra options for the VG creation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the VG @name was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_vgcreate (const gchar *name, const gchar **pv_list, guint64 pe_size, const BDExtraArg **extra, GError **error) {
    guint i = 0;
    guint pv_list_len = pv_list ? g_strv_length ((gchar **) pv_list) : 0;
    const gchar **argv = g_new0 (const gchar*, pv_list_len + 5);
    pe_size = RESOLVE_PE_SIZE (pe_size);
    gboolean success = FALSE;

    argv[0] = "vgcreate";
    argv[1] = "-s";
    argv[2] = g_strdup_printf ("%"G_GUINT64_FORMAT"K", pe_size / 1024);
    argv[3] = name;
    for (i=4; i < (pv_list_len + 4); i++) {
        argv[i] = pv_list[i-4];
    }
    argv[i] = NULL;

    success = call_lvm_and_report_error (argv, extra, TRUE, error);
    g_free ((gchar *) argv[2]);
    g_free (argv);

    return success;
}

/**
 * bd_lvm_vgremove:
 * @vg_name: name of the to be removed VG
 * @extra: (nullable) (array zero-terminated=1): extra options for the VG removal
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the VG was successfully removed or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_REMOVE
 */
gboolean bd_lvm_vgremove (const gchar *vg_name, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"vgremove", "--force", vg_name, NULL};

    return call_lvm_and_report_error (args, extra, TRUE, error);
}

/**
 * bd_lvm_vgrename:
 * @old_vg_name: old name of the VG to rename
 * @new_vg_name: new name for the @old_vg_name VG
 * @extra: (nullable) (array zero-terminated=1): extra options for the VG rename
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the VG was successfully renamed or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vgrename (const gchar *old_vg_name, const gchar *new_vg_name, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"vgrename", old_vg_name, new_vg_name, NULL};

    return call_lvm_and_report_error (args, extra, TRUE, error);
}

/**
 * bd_lvm_vgactivate:
 * @vg_name: name of the to be activated VG
 * @extra: (nullable) (array zero-terminated=1): extra options for the VG activation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the VG was successfully activated or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vgactivate (const gchar *vg_name, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"vgchange", "-ay", vg_name, NULL};

    return call_lvm_and_report_error (args, extra, TRUE, error);
}

/**
 * bd_lvm_vgdeactivate:
 * @vg_name: name of the to be deactivated VG
 * @extra: (nullable) (array zero-terminated=1): extra options for the VG deactivation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the VG was successfully deactivated or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vgdeactivate (const gchar *vg_name, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"vgchange", "-an", vg_name, NULL};

    return call_lvm_and_report_error (args, extra, TRUE, error);
}

/**
 * bd_lvm_vgextend:
 * @vg_name: name of the to be extended VG
 * @device: PV device to extend the @vg_name VG with
 * @extra: (nullable) (array zero-terminated=1): extra options for the VG extension
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the VG @vg_name was successfully extended with the given @device or not.
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vgextend (const gchar *vg_name, const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"vgextend", vg_name, device, NULL};

    return call_lvm_and_report_error (args, extra, TRUE, error);
}

/**
 * bd_lvm_vgreduce:
 * @vg_name: name of the to be reduced VG
 * @device: (nullable): PV device the @vg_name VG should be reduced of or %NULL
 *                        if the VG should be reduced of the missing PVs
 * @extra: (nullable) (array zero-terminated=1): extra options for the VG reduction
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the VG @vg_name was successfully reduced of the given @device or not
 *
 * Note: This function does not move extents off of the PV before removing
 *       it from the VG. You must do that first by calling #bd_lvm_pvmove.
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vgreduce (const gchar *vg_name, const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"vgreduce", NULL, NULL, NULL, NULL};

    if (!device) {
        args[1] = "--removemissing";
        args[2] = "--force";
        args[3] = vg_name;
    } else {
        args[1] = vg_name;
        args[2] = device;
    }

    return call_lvm_and_report_error (args, extra, TRUE, error);
}

/**
 * bd_lvm_add_vg_tags:
 * @vg_name: the VG to set tags on
 * @tags: (array zero-terminated=1): list of tags to add
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the tags were successfully added to @vg_name or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
gboolean bd_lvm_add_vg_tags (const gchar *vg_name, const gchar **tags, GError **error) {
    return _manage_lvm_tags (vg_name, tags, "--addtag", "vgchange", error);
}

/**
 * bd_lvm_delete_vg_tags:
 * @vg_name: the VG to set tags on
 * @tags: (array zero-terminated=1): list of tags to remove
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the tags were successfully removed from @vg_name or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
gboolean bd_lvm_delete_vg_tags (const gchar *vg_name, const gchar **tags, GError **error) {
    return _manage_lvm_tags (vg_name, tags, "--deltag", "vgchange", error);
}

static gboolean _vglock_start_stop (const gchar *vg_name, gboolean start, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"vgchange", NULL, vg_name, NULL};

    if (start)
        args[1] = "--lockstart";
    else
        args[1] = "--lockstop";

    return call_lvm_and_report_error (args, extra, TRUE, error);
}

/**
 * bd_lvm_vglock_start:
 * @vg_name: a shared VG to start the lockspace in lvmlockd
 * @extra: (nullable) (array zero-terminated=1): extra options for the vgchange command
 *                                               (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the lock was successfully started for @vg_name or not
 *
 * Tech category: %BD_LVM_TECH_SHARED-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vglock_start (const gchar *vg_name, const BDExtraArg **extra, GError **error) {
    return _vglock_start_stop (vg_name, TRUE, extra, error);
}

/**
 * bd_lvm_vglock_stop:
 * @vg_name: a shared VG to stop the lockspace in lvmlockd
 * @extra: (nullable) (array zero-terminated=1): extra options for the vgchange command
 *                                               (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the lock was successfully stopped for @vg_name or not
 *
 * Tech category: %BD_LVM_TECH_SHARED-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vglock_stop (const gchar *vg_name, const BDExtraArg **extra, GError **error) {
    return _vglock_start_stop (vg_name, FALSE, extra, error);
}

/**
 * bd_lvm_vginfo:
 * @vg_name: a VG to get information about
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the @vg_name VG or %NULL in case
 * of error (the @error) gets populated in those cases)
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMVGdata* bd_lvm_vginfo (const gchar *vg_name, GError **error) {
    const gchar *args[10] = {"vgs", "--noheadings", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--units=b",
                       "-o", "name,uuid,size,free,extent_size,extent_count,free_count,pv_count,vg_exported,vg_tags",
                       vg_name, NULL};

    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;

    success = call_lvm_and_capture_output (args, NULL, &output, error);
    if (!success)
        /* the error is already populated from the call */
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 10)) {
            g_strfreev (lines);
            return get_vg_data_from_table (table, TRUE);
        } else
            if (table)
                g_hash_table_destroy (table);
    }
    g_strfreev (lines);

    /* getting here means no usable info was found */
    g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                 "Failed to parse information about the VG");
    return NULL;
}

/**
 * bd_lvm_vgs:
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about VGs found in the system
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMVGdata** bd_lvm_vgs (GError **error) {
    const gchar *args[9] = {"vgs", "--noheadings", "--nosuffix", "--nameprefixes",
                      "--unquoted", "--units=b",
                      "-o", "name,uuid,size,free,extent_size,extent_count,free_count,pv_count,vg_tags",
                      NULL};
    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;
    GPtrArray *vgs;
    BDLVMVGdata *vgdata = NULL;
    GError *l_error = NULL;

    vgs = g_ptr_array_new ();

    success = call_lvm_and_capture_output (args, NULL, &output, &l_error);
    if (!success) {
        if (g_error_matches (l_error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_NOOUT)) {
            /* no output => no VGs, not an error */
            g_clear_error (&l_error);
            /* return an empty list */
            g_ptr_array_add (vgs, NULL);
            return (BDLVMVGdata **) g_ptr_array_free (vgs, FALSE);
        } else {
            /* the error is already populated from the call */
            g_ptr_array_free (vgs, TRUE);
            g_propagate_error (error, l_error);
            return NULL;
       }
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 9)) {
            /* valid line, try to parse and record it */
            vgdata = get_vg_data_from_table (table, TRUE);
            if (vgdata)
                g_ptr_array_add (vgs, vgdata);
        } else
            if (table)
                g_hash_table_destroy (table);
    }

    g_strfreev (lines);

    if (vgs->len == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                     "Failed to parse information about VGs");
        g_ptr_array_free (vgs, TRUE);
        return NULL;
    }

    /* returning NULL-terminated array of BDLVMVGdata */
    g_ptr_array_add (vgs, NULL);
    return (BDLVMVGdata **) g_ptr_array_free (vgs, FALSE);
}

/**
 * bd_lvm_lvorigin:
 * @vg_name: name of the VG containing the queried LV
 * @lv_name: name of the queried LV
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): the origin volume for the @vg_name/@lv_name LV or
 * %NULL if failed to determine (@error) is set in those cases)
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
gchar* bd_lvm_lvorigin (const gchar *vg_name, const gchar *lv_name, GError **error) {
    gboolean success = FALSE;
    gchar *output = NULL;
    const gchar *args[6] = {"lvs", "--noheadings", "-o", "origin", NULL, NULL};
    args[4] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_capture_output (args, NULL, &output, error);
    g_free ((gchar *) args[4]);

    if (!success)
        /* the error is already populated from the call */
        return NULL;

    return g_strstrip (output);
}

/**
 * bd_lvm_lvcreate:
 * @vg_name: name of the VG to create a new LV in
 * @lv_name: name of the to-be-created LV
 * @size: requested size of the new LV
 * @type: (nullable): type of the new LV ("striped", "raid1",..., see lvcreate (8))
 * @pv_list: (nullable) (array zero-terminated=1): list of PVs the newly created LV should use or %NULL
 * if not specified
 * @extra: (nullable) (array zero-terminated=1): extra options for the LV creation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given @vg_name/@lv_name LV was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_lvcreate (const gchar *vg_name, const gchar *lv_name, guint64 size, const gchar *type, const gchar **pv_list, const BDExtraArg **extra, GError **error) {
    guint8 pv_list_len = pv_list ? g_strv_length ((gchar **) pv_list) : 0;
    const gchar **args = g_new0 (const gchar*, pv_list_len + 10);
    gboolean success = FALSE;
    guint64 i = 0;
    guint64 j = 0;
    gchar *size_str = NULL;
    gchar *type_str = NULL;

    args[i++] = "lvcreate";
    args[i++] = "-n";
    args[i++] = lv_name;
    args[i++] = "-L";
    size_str = g_strdup_printf ("%"G_GUINT64_FORMAT"K", size/1024);
    args[i++] = size_str;
    args[i++] = "-y";
    if (type) {
        if (g_strcmp0 (type, "striped") == 0) {
            args[i++] = "--stripes";
            type_str = g_strdup_printf ("%d", pv_list_len);
            args[i++] = type_str;
        } else {
            args[i++] = "--type";
            args[i++] = type;
        }
    }
    args[i++] = vg_name;

    for (j=0; j < pv_list_len; j++)
        args[i++] = pv_list[j];

    args[i] = NULL;

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free (size_str);
    g_free (type_str);
    g_free (args);

    return success;
}

/**
 * bd_lvm_lvremove:
 * @vg_name: name of the VG containing the to-be-removed LV
 * @lv_name: name of the to-be-removed LV
 * @force: whether to force removal or not
 * @extra: (nullable) (array zero-terminated=1): extra options for the LV removal
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully removed or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_REMOVE
 */
gboolean bd_lvm_lvremove (const gchar *vg_name, const gchar *lv_name, gboolean force, const BDExtraArg **extra, GError **error) {
    /* '--yes' is needed if DISCARD is enabled */
    const gchar *args[5] = {"lvremove", "--yes", NULL, NULL, NULL};
    guint8 next_arg = 2;
    gboolean success = FALSE;

    if (force) {
        args[next_arg] = "--force";
        next_arg++;
    }

    args[next_arg] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free ((gchar *) args[next_arg]);

    return success;
}

/**
 * bd_lvm_lvrename:
 * @vg_name: name of the VG containing the to-be-renamed LV
 * @lv_name: name of the to-be-renamed LV
 * @new_name: new name for the @vg_name/@lv_name LV
 * @extra: (nullable) (array zero-terminated=1): extra options for the LV rename
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully renamed to
 * @vg_name/@new_name or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_lvrename (const gchar *vg_name, const gchar *lv_name, const gchar *new_name, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"lvrename", vg_name, lv_name, new_name, NULL};
    return call_lvm_and_report_error (args, extra, TRUE, error);
}


/**
 * bd_lvm_lvresize:
 * @vg_name: name of the VG containing the to-be-resized LV
 * @lv_name: name of the to-be-resized LV
 * @size: the requested new size of the LV
 * @extra: (nullable) (array zero-terminated=1): extra options for the LV resize
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully resized or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_lvresize (const gchar *vg_name, const gchar *lv_name, guint64 size, const BDExtraArg **extra, GError **error) {
    const gchar *args[8] = {"lvresize", "--force", "-L", NULL, NULL, NULL, NULL, NULL};
    gboolean success = FALSE;
    guint8 next_arg = 4;
    g_autofree gchar *lvspec = NULL;
    BDLVMLVdata *lvinfo = NULL;

    lvinfo = bd_lvm_lvinfo (vg_name, lv_name, error);
    if (!lvinfo)
        /* error is already populated */
        return FALSE;

    args[3] = g_strdup_printf ("%"G_GUINT64_FORMAT"K", size/1024);

    if (lvinfo->attr[4] != 'a') {
        /* starting with 2.03.19 we need to add extra option to allow resizing of inactive LVs */
        success = bd_utils_check_util_version (deps[DEPS_LVM].name, LVM_VERSION_FSRESIZE,
                                               deps[DEPS_LVM].ver_arg, deps[DEPS_LVM].ver_regexp, NULL);
        if (success) {
            args[next_arg++] = "--fs";
            args[next_arg++] = "ignore";
        }
    }

    bd_lvm_lvdata_free (lvinfo);

    lvspec = g_strdup_printf ("%s/%s", vg_name, lv_name);
    args[next_arg++] = lvspec;

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free ((gchar *) args[3]);

    return success;
}

/**
 * bd_lvm_lvrepair:
 * @vg_name: name of the VG containing the to-be-repaired LV
 * @lv_name: name of the to-be-repaired LV
 * @pv_list: (array zero-terminated=1): list of PVs to be used for the repair
 * @extra: (nullable) (array zero-terminated=1): extra options for the LV repair
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully repaired or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_lvrepair (const gchar *vg_name, const gchar *lv_name, const gchar **pv_list, const BDExtraArg **extra, GError **error) {
    guint i = 0;
    guint pv_list_len = pv_list ? g_strv_length ((gchar **) pv_list) : 0;
    const gchar **argv = g_new0 (const gchar*, pv_list_len + 5);
    gboolean success = FALSE;

    argv[0] = "lvconvert";
    argv[1] = "--repair";
    argv[2] = "--yes";
    argv[3] = g_strdup_printf ("%s/%s", vg_name, lv_name);
    for (i=4; i < (pv_list_len + 4); i++) {
        argv[i] = pv_list[i-4];
    }
    argv[i] = NULL;

    success = call_lvm_and_report_error (argv, extra, TRUE, error);
    g_free ((gchar *) argv[3]);
    g_free (argv);

    return success;
}

/**
 * bd_lvm_lvactivate:
 * @vg_name: name of the VG containing the to-be-activated LV
 * @lv_name: name of the to-be-activated LV
 * @ignore_skip: whether to ignore the skip flag or not
 * @shared: whether to activate the LV in shared mode (used for shared LVM setups with lvmlockd,
 *          use %FALSE if not sure)
 * @extra: (nullable) (array zero-terminated=1): extra options for the LV activation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully activated or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_lvactivate (const gchar *vg_name, const gchar *lv_name, gboolean ignore_skip, gboolean shared, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"lvchange", NULL, NULL, NULL, NULL};
    guint8 next_arg = 2;
    gboolean success = FALSE;

    if (shared)
        args[1] = "-asy";
    else
        args[1] = "-ay";

    if (ignore_skip) {
        args[next_arg] = "-K";
        next_arg++;
    }
    args[next_arg] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free ((gchar *) args[next_arg]);

    return success;
}

/**
 * bd_lvm_lvdeactivate:
 * @vg_name: name of the VG containing the to-be-deactivated LV
 * @lv_name: name of the to-be-deactivated LV
 * @extra: (nullable) (array zero-terminated=1): extra options for the LV deactivation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name LV was successfully deactivated or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_lvdeactivate (const gchar *vg_name, const gchar *lv_name, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"lvchange", "-an", NULL, NULL};
    gboolean success = FALSE;

    args[2] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free ((gchar *) args[2]);

    return success;
}

/**
 * bd_lvm_lvsnapshotcreate:
 * @vg_name: name of the VG containing the LV a new snapshot should be created of
 * @origin_name: name of the LV a new snapshot should be created of
 * @snapshot_name: name of the to-be-created snapshot
 * @size: requested size for the snapshot
 * @extra: (nullable) (array zero-terminated=1): extra options for the LV snapshot creation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @snapshot_name snapshot of the @vg_name/@origin_name LV
 * was successfully created or not.
 *
 * Tech category: %BD_LVM_TECH_BASIC_SNAP-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_lvsnapshotcreate (const gchar *vg_name, const gchar *origin_name, const gchar *snapshot_name, guint64 size, const BDExtraArg **extra, GError **error) {
    const gchar *args[8] = {"lvcreate", "-s", "-L", NULL, "-n", snapshot_name, NULL, NULL};
    gboolean success = FALSE;

    args[3] = g_strdup_printf ("%"G_GUINT64_FORMAT"K", size / 1024);
    args[6] = g_strdup_printf ("%s/%s", vg_name, origin_name);

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free ((gchar *) args[3]);
    g_free ((gchar *) args[6]);

    return success;
}

/**
 * bd_lvm_lvsnapshotmerge:
 * @vg_name: name of the VG containing the to-be-merged LV snapshot
 * @snapshot_name: name of the to-be-merged LV snapshot
 * @extra: (nullable) (array zero-terminated=1): extra options for the LV snapshot merge
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @vg_name/@snapshot_name LV snapshot was successfully merged or not
 *
 * Tech category: %BD_LVM_TECH_BASIC_SNAP-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_lvsnapshotmerge (const gchar *vg_name, const gchar *snapshot_name, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"lvconvert", "--merge", NULL, NULL};
    gboolean success = FALSE;

    args[2] = g_strdup_printf ("%s/%s", vg_name, snapshot_name);

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free ((gchar *) args[2]);

    return success;
}

/**
 * bd_lvm_add_lv_tags:
 * @vg_name: name of the VG that contains the LV to set tags on
 * @lv_name: name of the LV to set tags on
 * @tags: (array zero-terminated=1): list of tags to add
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the tags were successfully added to @device or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
gboolean bd_lvm_add_lv_tags (const gchar *vg_name, const gchar *lv_name, const gchar **tags, GError **error) {
    g_autofree gchar *lvspec = g_strdup_printf ("%s/%s", vg_name, lv_name);
    return _manage_lvm_tags (lvspec, tags, "--addtag", "lvchange", error);
}

/**
 * bd_lvm_delete_lv_tags:
 * @vg_name: name of the VG that contains the LV to set tags on
 * @lv_name: name of the LV to set tags on
 * @tags: (array zero-terminated=1): list of tags to remove
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the tags were successfully removed from @device or not
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
gboolean bd_lvm_delete_lv_tags (const gchar *vg_name, const gchar *lv_name, const gchar **tags, GError **error) {
    g_autofree gchar *lvspec = g_strdup_printf ("%s/%s", vg_name, lv_name);
    return _manage_lvm_tags (lvspec, tags, "--deltag", "lvchange", error);
}

/**
 * bd_lvm_lvinfo:
 * @vg_name: name of the VG that contains the LV to get information about
 * @lv_name: name of the LV to get information about
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the @vg_name/@lv_name LV or %NULL in case
 * of error (the @error) gets populated in those cases)
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMLVdata* bd_lvm_lvinfo (const gchar *vg_name, const gchar *lv_name, GError **error) {
    const gchar *args[11] = {"lvs", "--noheadings", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--units=b", "-a",
                       "-o", "vg_name,lv_name,lv_uuid,lv_size,lv_attr,segtype,origin,pool_lv,data_lv,metadata_lv,role,move_pv,data_percent,metadata_percent,copy_percent,lv_tags",
                       NULL, NULL};

    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;

    args[9] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_capture_output (args, NULL, &output, error);
    g_free ((gchar *) args[9]);

    if (!success)
        /* the error is already populated from the call */
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 16)) {
            g_strfreev (lines);
            return get_lv_data_from_table (table, TRUE);
        } else
            if (table)
                g_hash_table_destroy (table);
    }
    g_strfreev (lines);

    /* getting here means no usable info was found */
    g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                 "Failed to parse information about the LV");
    return NULL;
}

BDLVMLVdata* bd_lvm_lvinfo_tree (const gchar *vg_name, const gchar *lv_name, GError **error) {
    const gchar *args[11] = {"lvs", "--noheadings", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--units=b", "-a",
                       "-o", "vg_name,lv_name,lv_uuid,lv_size,lv_attr,segtype,origin,pool_lv,data_lv,metadata_lv,role,move_pv,data_percent,metadata_percent,copy_percent,lv_tags,devices,metadata_devices,seg_size_pe",
                       NULL, NULL};

    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;
    BDLVMLVdata *result = NULL;

    args[9] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_capture_output (args, NULL, &output, error);
    g_free ((gchar *) args[9]);

    if (!success)
        /* the error is already populated from the call */
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 19)) {
            BDLVMLVdata *lvdata = get_lv_data_from_table (table, TRUE);
            if (result) {
                merge_lv_data (result, lvdata);
                bd_lvm_lvdata_free (lvdata);
            } else
                result = lvdata;
        } else {
            if (table)
                g_hash_table_destroy (table);
        }
    }
    g_strfreev (lines);

    if (result == NULL)
      g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                   "Failed to parse information about the LV");
    return result;
}

/**
 * bd_lvm_lvs:
 * @vg_name: (nullable): name of the VG to get information about LVs from
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (array zero-terminated=1): information about LVs found in the given
 * @vg_name VG or in system if @vg_name is %NULL
 *
 * Tech category: %BD_LVM_TECH_BASIC-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMLVdata** bd_lvm_lvs (const gchar *vg_name, GError **error) {
    const gchar *args[11] = {"lvs", "--noheadings", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--units=b", "-a",
                       "-o", "vg_name,lv_name,lv_uuid,lv_size,lv_attr,segtype,origin,pool_lv,data_lv,metadata_lv,role,move_pv,data_percent,metadata_percent,copy_percent,lv_tags",
                       NULL, NULL};

    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;
    GPtrArray *lvs;
    BDLVMLVdata *lvdata = NULL;
    GError *l_error = NULL;

    lvs = g_ptr_array_new ();

    if (vg_name)
        args[9] = vg_name;

    success = call_lvm_and_capture_output (args, NULL, &output, &l_error);
    if (!success) {
        if (g_error_matches (l_error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_NOOUT)) {
            /* no output => no LVs, not an error */
            g_clear_error (&l_error);
            /* return an empty list */
            g_ptr_array_add (lvs, NULL);
            return (BDLVMLVdata **) g_ptr_array_free (lvs, FALSE);
        }
        else {
            /* the error is already populated from the call */
            g_ptr_array_free (lvs, TRUE);
            g_propagate_error (error, l_error);
            return NULL;
        }
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 16)) {
            /* valid line, try to parse and record it */
            lvdata = get_lv_data_from_table (table, TRUE);
            if (lvdata) {
                /* ignore duplicate entries in lvs output, these are caused by multi segments LVs */
                for (gsize i = 0; i < lvs->len; i++) {
                    if (g_strcmp0 (((BDLVMLVdata *) g_ptr_array_index (lvs, i))->lv_name, lvdata->lv_name) == 0) {
                        bd_utils_log_format (BD_UTILS_LOG_DEBUG,
                                             "Duplicate LV entry for '%s' found in lvs output",
                                             lvdata->lv_name);
                        bd_lvm_lvdata_free (lvdata);
                        lvdata = NULL;
                        break;
                    }
                }

                if (lvdata)
                    g_ptr_array_add (lvs, lvdata);
            }
        } else
            if (table)
                g_hash_table_destroy (table);
    }

    g_strfreev (lines);

    if (lvs->len == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                     "Failed to parse information about LVs");
        g_ptr_array_free (lvs, TRUE);
        return NULL;
    }

    /* returning NULL-terminated array of BDLVMLVdata */
    g_ptr_array_add (lvs, NULL);
    return (BDLVMLVdata **) g_ptr_array_free (lvs, FALSE);
}

BDLVMLVdata** bd_lvm_lvs_tree (const gchar *vg_name, GError **error) {
    const gchar *args[11] = {"lvs", "--noheadings", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--units=b", "-a",
                       "-o", "vg_name,lv_name,lv_uuid,lv_size,lv_attr,segtype,origin,pool_lv,data_lv,metadata_lv,role,move_pv,data_percent,metadata_percent,copy_percent,lv_tags,devices,metadata_devices,seg_size_pe",
                       NULL, NULL};

    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;
    GPtrArray *lvs;
    BDLVMLVdata *lvdata = NULL;
    GError *l_error = NULL;

    lvs = g_ptr_array_new ();

    if (vg_name)
        args[9] = vg_name;

    success = call_lvm_and_capture_output (args, NULL, &output, &l_error);
    if (!success) {
        if (g_error_matches (l_error, BD_UTILS_EXEC_ERROR, BD_UTILS_EXEC_ERROR_NOOUT)) {
            /* no output => no LVs, not an error */
            g_clear_error (&l_error);
            /* return an empty list */
            g_ptr_array_add (lvs, NULL);
            return (BDLVMLVdata **) g_ptr_array_free (lvs, FALSE);
        }
        else {
            /* the error is already populated from the call */
            g_ptr_array_free (lvs, TRUE);
            g_propagate_error (error, l_error);
            return NULL;
        }
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 19)) {
            /* valid line, try to parse and record it */
            lvdata = get_lv_data_from_table (table, TRUE);
            if (lvdata) {
                for (gsize i = 0; i < lvs->len; i++) {
                    BDLVMLVdata *other = (BDLVMLVdata *) g_ptr_array_index (lvs, i);
                    if (g_strcmp0 (other->lv_name, lvdata->lv_name) == 0) {
                        merge_lv_data (other, lvdata);
                        bd_lvm_lvdata_free (lvdata);
                        lvdata = NULL;
                        break;
                    }
                }

                if (lvdata)
                    g_ptr_array_add (lvs, lvdata);
            }
        } else
            if (table)
                g_hash_table_destroy (table);
    }

    g_strfreev (lines);

    if (lvs->len == 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                     "Failed to parse information about LVs");
        g_ptr_array_free (lvs, TRUE);
        return NULL;
    }

    /* returning NULL-terminated array of BDLVMLVdata */
    g_ptr_array_add (lvs, NULL);
    return (BDLVMLVdata **) g_ptr_array_free (lvs, FALSE);
}

/**
 * bd_lvm_thpoolcreate:
 * @vg_name: name of the VG to create a thin pool in
 * @lv_name: name of the to-be-created pool LV
 * @size: requested size of the to-be-created pool
 * @md_size: requested metadata size or 0 to use the default
 * @chunk_size: requested chunk size or 0 to use the default
 * @profile: (nullable): profile to use (see lvm(8) for more information) or %NULL to use
 *                         the default
 * @extra: (nullable) (array zero-terminated=1): extra options for the thin pool creation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name thin pool was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_THIN-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_thpoolcreate (const gchar *vg_name, const gchar *lv_name, guint64 size, guint64 md_size, guint64 chunk_size, const gchar *profile, const BDExtraArg **extra, GError **error) {
    const gchar *args[9] = {"lvcreate", "-T", "-L", NULL, NULL, NULL, NULL, NULL, NULL};
    guint8 next_arg = 4;
    gboolean success = FALSE;

    args[3] = g_strdup_printf ("%"G_GUINT64_FORMAT"K", size/1024);

    if (md_size != 0) {
        args[next_arg] = g_strdup_printf ("--poolmetadatasize=%"G_GUINT64_FORMAT"K", md_size / 1024);
        next_arg++;
    }

    if (chunk_size != 0) {
        args[next_arg] = g_strdup_printf ("--chunksize=%"G_GUINT64_FORMAT"K", chunk_size / 1024);
        next_arg++;
    }

    if (profile) {
        args[next_arg] = g_strdup_printf ("--profile=%s", profile);
        next_arg++;
    }

    args[next_arg] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free ((gchar *) args[3]);
    g_free ((gchar *) args[4]);
    g_free ((gchar *) args[5]);
    g_free ((gchar *) args[6]);
    g_free ((gchar *) args[7]);

    return success;
}

/**
 * bd_lvm_thlvcreate:
 * @vg_name: name of the VG containing the thin pool providing extents for the to-be-created thin LV
 * @pool_name: name of the pool LV providing extents for the to-be-created thin LV
 * @lv_name: name of the to-be-created thin LV
 * @size: requested virtual size of the to-be-created thin LV
 * @extra: (nullable) (array zero-terminated=1): extra options for the thin LV creation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @vg_name/@lv_name thin LV was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_THIN-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_thlvcreate (const gchar *vg_name, const gchar *pool_name, const gchar *lv_name, guint64 size, const BDExtraArg **extra, GError **error) {
    const gchar *args[8] = {"lvcreate", "-T", NULL, "-V", NULL, "-n", lv_name, NULL};
    gboolean success;

    args[2] = g_strdup_printf ("%s/%s", vg_name, pool_name);
    args[4] = g_strdup_printf ("%"G_GUINT64_FORMAT"K", size / 1024);

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free ((gchar *) args[2]);
    g_free ((gchar *) args[4]);

    return success;
}

/**
 * bd_lvm_thlvpoolname:
 * @vg_name: name of the VG containing the queried thin LV
 * @lv_name: name of the queried thin LV
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): the name of the pool volume for the @vg_name/@lv_name
 * thin LV or %NULL if failed to determine (@error) is set in those cases)
 *
 * Tech category: %BD_LVM_TECH_THIN-%BD_LVM_TECH_MODE_QUERY
 */
gchar* bd_lvm_thlvpoolname (const gchar *vg_name, const gchar *lv_name, GError **error) {
    gboolean success = FALSE;
    gchar *output = NULL;
    const gchar *args[6] = {"lvs", "--noheadings", "-o", "pool_lv", NULL, NULL};
    args[4] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_capture_output (args, NULL, &output, error);
    g_free ((gchar *) args[4]);

    if (!success)
        /* the error is already populated from the call */
        return NULL;

    return g_strstrip (output);
}

/**
 * bd_lvm_thsnapshotcreate:
 * @vg_name: name of the VG containing the thin LV a new snapshot should be created of
 * @origin_name: name of the thin LV a new snapshot should be created of
 * @snapshot_name: name of the to-be-created snapshot
 * @pool_name: (nullable): name of the thin pool to create the snapshot in or %NULL if not specified
 * @extra: (nullable) (array zero-terminated=1): extra options for the thin LV snapshot creation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @snapshot_name snapshot of the @vg_name/@origin_name
 * thin LV was successfully created or not.
 *
 * Tech category: %BD_LVM_TECH_THIN-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_thsnapshotcreate (const gchar *vg_name, const gchar *origin_name, const gchar *snapshot_name, const gchar *pool_name, const BDExtraArg **extra, GError **error) {
    const gchar *args[8] = {"lvcreate", "-s", "-n", snapshot_name, NULL, NULL, NULL, NULL};
    guint next_arg = 4;
    gboolean success = FALSE;

    if (pool_name) {
        args[next_arg] = "--thinpool";
        next_arg++;
        args[next_arg] = pool_name;
        next_arg++;
    }

    args[next_arg] = g_strdup_printf ("%s/%s", vg_name, origin_name);

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free ((gchar *) args[next_arg]);

    return success;
}

/**
 * bd_lvm_set_global_config:
 * @new_config: (nullable): string representation of the new global LVM
 *                            configuration to set or %NULL to reset to default
 * @error: (out) (optional): place to store error (if any)
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
    if (!new_config || g_strcmp0 (new_config, "") == 0)
         global_config_str = NULL;
    else
        global_config_str = g_strdup (new_config);

    g_mutex_unlock (&global_config_lock);
    return TRUE;
}

/**
 * bd_lvm_get_global_config:
 * @error: (out) (optional): place to store error (if any)
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
 * bd_lvm_set_devices_filter:
 * @devices: (nullable) (array zero-terminated=1): list of devices for lvm commands to work on
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the devices filter was successfully set or not
 *
 * Tech category: %BD_LVM_TECH_DEVICES no mode (it is ignored)
 */
gboolean bd_lvm_set_devices_filter (const gchar **devices, GError **error) {
    if (!bd_lvm_is_tech_avail (BD_LVM_TECH_DEVICES, 0, error))
        return FALSE;

    g_mutex_lock (&global_config_lock);

    /* first free the old value */
    g_free (global_devices_str);

    /* now store the new one */
    if (!devices || !(*devices))
        global_devices_str = NULL;
    else
        global_devices_str = g_strjoinv (",", (gchar **) devices);

    g_mutex_unlock (&global_config_lock);
    return TRUE;
}

/**
 * bd_lvm_get_devices_filter:
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full) (array zero-terminated=1): a copy of a string representation of
 *                                                     the currently set LVM devices filter
 *
 * Tech category: %BD_LVM_TECH_DEVICES no mode (it is ignored)
 */
gchar** bd_lvm_get_devices_filter (GError **error UNUSED) {
    gchar **ret = NULL;

    g_mutex_lock (&global_config_lock);

    if (global_devices_str)
        ret = g_strsplit (global_devices_str, ",", -1);
    else
        ret = NULL;

    g_mutex_unlock (&global_config_lock);

    return ret;
}

/**
 * bd_lvm_cache_get_default_md_size:
 * @cache_size: size of the cache to determine MD size for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: recommended default size of the cache metadata LV or 0 in case of error
 *
 * Tech category: %BD_LVM_TECH_CACHE_CALCS no mode (it is ignored)
 */
guint64 bd_lvm_cache_get_default_md_size (guint64 cache_size, GError **error UNUSED) {
    return MAX ((guint64) cache_size / 1000, MIN_CACHE_MD_SIZE);
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
 * @error: (out) (optional): place to store error (if any)
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
 * @error: (out) (optional): place to store error (if any)
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
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the cache pool @vg_name/@pool_name was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_CACHE-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_cache_create_pool (const gchar *vg_name, const gchar *pool_name, guint64 pool_size, guint64 md_size, BDLVMCacheMode mode, BDLVMCachePoolFlags flags, const gchar **fast_pvs, GError **error) {
    gboolean success = FALSE;
    const gchar *type = NULL;
    gchar *name = NULL;
    gchar *msg = NULL;
    guint64 progress_id = 0;
    const gchar *args[10] = {"lvconvert", "-y", "--type", "cache-pool", "--poolmetadata", NULL, "--cachemode", NULL, NULL, NULL};
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started 'create cache pool %s/%s'", vg_name, pool_name);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    /* create an LV for the pool */
    type = get_lv_type_from_flags (flags, FALSE, NULL);
    success = bd_lvm_lvcreate (vg_name, pool_name, pool_size, type, fast_pvs, NULL, &l_error);
    if (!success) {
        g_prefix_error (&l_error, "Failed to create the pool LV: ");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* 1/3 steps done */
    bd_utils_report_progress (progress_id, 33, "Created the data LV");

    /* determine the size of the metadata LV */
    type = get_lv_type_from_flags (flags, TRUE, NULL);
    if (md_size == 0)
        md_size = bd_lvm_cache_get_default_md_size (pool_size, &l_error);
    if (l_error) {
        g_prefix_error (&l_error, "Failed to determine size for the pool metadata LV: ");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    name = g_strdup_printf ("%s_meta", pool_name);

    /* create the metadata LV */
    success = bd_lvm_lvcreate (vg_name, name, md_size, type, fast_pvs, NULL, &l_error);
    if (!success) {
        g_free (name);
        g_prefix_error (&l_error, "Failed to create the pool metadata LV: ");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* 2/3 steps done */
    bd_utils_report_progress (progress_id, 66, "Created the metadata LV");

    /* create the cache pool from the two LVs */
    args[5] = name;
    args[7] = (const gchar *) bd_lvm_cache_get_mode_str (mode, &l_error);
    if (!args[7]) {
        g_free ((gchar *) args[5]);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }
    name = g_strdup_printf ("%s/%s", vg_name, pool_name);
    args[8] = name;
    success = call_lvm_and_report_error (args, NULL, TRUE, &l_error);
    g_free ((gchar *) args[5]);
    g_free ((gchar *) args[8]);

    if (!success) {
        if (l_error)
            bd_utils_report_finished (progress_id, l_error->message);
        else
            bd_utils_report_finished (progress_id, "Completed");
        g_propagate_error (error, l_error);
    } else
        bd_utils_report_finished (progress_id, "Completed");

    /* just return the result of the last step (it sets error on fail) */
    return success;
}

/**
 * bd_lvm_cache_attach:
 * @vg_name: name of the VG containing the @data_lv and the @cache_pool_lv LVs
 * @data_lv: data LV to attach the @cache_pool_lv to
 * @cache_pool_lv: cache pool LV to attach to the @data_lv
 * @extra: (nullable) (array zero-terminated=1): extra options for the cache attachment
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @cache_pool_lv was successfully attached to the @data_lv or not
 *
 * Note: Both @data_lv and @cache_lv will be deactivated before the operation.
 *
 * Tech category: %BD_LVM_TECH_CACHE-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_cache_attach (const gchar *vg_name, const gchar *data_lv, const gchar *cache_pool_lv, const BDExtraArg **extra, GError **error) {
    const gchar *args[8] = {"lvconvert", "-y", "--type", "cache", "--cachepool", NULL, NULL, NULL};
    gboolean success = FALSE;

    args[5] = g_strdup_printf ("%s/%s", vg_name, cache_pool_lv);
    args[6] = g_strdup_printf ("%s/%s", vg_name, data_lv);
    success = call_lvm_and_report_error (args, extra, TRUE, error);

    g_free ((gchar *) args[5]);
    g_free ((gchar *) args[6]);
    return success;
}

/**
 * bd_lvm_cache_detach:
 * @vg_name: name of the VG containing the @cached_lv
 * @cached_lv: name of the cached LV to detach its cache from
 * @destroy: whether to destroy the cache after detach or not
 * @extra: (nullable) (array zero-terminated=1): extra options for the cache detachment
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the cache was successfully detached from the @cached_lv or not
 *
 * Note: synces the cache first
 *
 * Tech category: %BD_LVM_TECH_CACHE-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_cache_detach (const gchar *vg_name, const gchar *cached_lv, gboolean destroy, const BDExtraArg **extra, GError **error) {
    /* need to both "assume yes" and "force" to get rid of the interactive
       questions in case of "--uncache" */
    const gchar *args[6] = {"lvconvert", "-y", "-f", NULL, NULL, NULL};
    gboolean success = FALSE;

    args[3] = destroy ? "--uncache" : "--splitcache";
    args[4] = g_strdup_printf ("%s/%s", vg_name, cached_lv);
    success = call_lvm_and_report_error (args, extra, TRUE, error);

    g_free ((gchar *) args[4]);
    return success;
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
 * @error: (out) (optional): place to store error (if any)
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
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started 'create cached LV %s/%s'", vg_name, lv_name);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    name = g_strdup_printf ("%s_cache", lv_name);
    success = bd_lvm_cache_create_pool (vg_name, name, cache_size, md_size, mode, flags, fast_pvs, &l_error);
    if (!success) {
        g_prefix_error (&l_error, "Failed to create the cache pool '%s': ", name);
        g_free (name);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* 3/5 steps (cache pool creation has 3 steps) done */
    bd_utils_report_progress (progress_id, 60, "Cache pool created");

    success = bd_lvm_lvcreate (vg_name, lv_name, data_size, NULL, slow_pvs, NULL, &l_error);
    if (!success) {
        g_free (name);
        g_prefix_error (&l_error, "Failed to create the data LV: ");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* 4/5 steps (cache pool creation has 3 steps) done */
    bd_utils_report_progress (progress_id, 80, "Data LV created");

    success = bd_lvm_cache_attach (vg_name, lv_name, name, NULL, &l_error);
    if (!success) {
        g_prefix_error (error, "Failed to attach the cache pool '%s' to the data LV: ", name);
        g_free (name);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    bd_utils_report_finished (progress_id, "Completed");
    g_free (name);
    return TRUE;
}

/**
 * bd_lvm_writecache_attach:
 * @vg_name: name of the VG containing the @data_lv and the @cache_pool_lv LVs
 * @data_lv: data LV to attach the @cache_lv to
 * @cache_lv: cache (fast) LV to attach to the @data_lv
 * @extra: (nullable) (array zero-terminated=1): extra options for the cache attachment
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @cache_lv was successfully attached to the @data_lv or not
 *
 * Tech category: %BD_LVM_TECH_WRITECACHE-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_writecache_attach (const gchar *vg_name, const gchar *data_lv, const gchar *cache_lv, const BDExtraArg **extra, GError **error) {
    const gchar *args[8] = {"lvconvert", "-y", "--type", "writecache", "--cachevol", NULL, NULL, NULL};
    gboolean success = FALSE;

    /* both LVs need to be inactive for the writecache convert to work */
    success = bd_lvm_lvdeactivate (vg_name, data_lv, NULL, error);
    if (!success)
        return FALSE;

    success = bd_lvm_lvdeactivate (vg_name, cache_lv, NULL, error);
    if (!success)
        return FALSE;

    args[5] = g_strdup_printf ("%s/%s", vg_name, cache_lv);
    args[6] = g_strdup_printf ("%s/%s", vg_name, data_lv);
    success = call_lvm_and_report_error (args, extra, TRUE, error);

    g_free ((gchar *) args[5]);
    g_free ((gchar *) args[6]);
    return success;
}

/**
 * bd_lvm_writecache_detach:
 * @vg_name: name of the VG containing the @cached_lv
 * @cached_lv: name of the cached LV to detach its cache from
 * @destroy: whether to destroy the cache after detach or not
 * @extra: (nullable) (array zero-terminated=1): extra options for the cache detachment
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the cache was successfully detached from the @cached_lv or not
 *
 * Note: synces the cache first
 *
 * Tech category: %BD_LVM_TECH_WRITECACHE-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_writecache_detach (const gchar *vg_name, const gchar *cached_lv, gboolean destroy, const BDExtraArg **extra, GError **error) {
    return bd_lvm_cache_detach (vg_name, cached_lv, destroy, extra, error);
}

/**
 * bd_lvm_writecache_create_cached_lv:
 * @vg_name: name of the VG to create a cached LV in
 * @lv_name: name of the cached LV to create
 * @data_size: size of the data LV
 * @cache_size: size of the cache (or cached LV more precisely)
 * @slow_pvs: (array zero-terminated=1): list of slow PVs (used for the data LV)
 * @fast_pvs: (array zero-terminated=1): list of fast PVs (used for the cache LV)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the cached LV @lv_name was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_WRITECACHE-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_writecache_create_cached_lv (const gchar *vg_name, const gchar *lv_name, guint64 data_size, guint64 cache_size,
                                             const gchar **slow_pvs, const gchar **fast_pvs, GError **error) {
    gboolean success = FALSE;
    gchar *name = NULL;
    gchar *msg = NULL;
    guint64 progress_id = 0;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started 'create cached LV %s/%s'", vg_name, lv_name);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    name = g_strdup_printf ("%s_writecache", lv_name);
    success = bd_lvm_lvcreate (vg_name, name, cache_size, NULL, fast_pvs, NULL, &l_error);
    if (!success) {
        g_prefix_error (&l_error, "Failed to create the cache LV '%s': ", name);
        g_free (name);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* 1/3 steps done */
    bd_utils_report_progress (progress_id, 33, "Cache LV created");

    success = bd_lvm_lvcreate (vg_name, lv_name, data_size, NULL, slow_pvs, NULL, &l_error);
    if (!success) {
        g_free (name);
        g_prefix_error (&l_error, "Failed to create the data LV: ");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* 2/3 steps done */
    bd_utils_report_progress (progress_id, 66, "Data LV created");

    success = bd_lvm_writecache_attach (vg_name, lv_name, name, NULL, &l_error);
    if (!success) {
        g_prefix_error (&l_error, "Failed to attach the cache LV '%s' to the data LV: ", name);
        g_free (name);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
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
 * @error: (out) (optional): place to store error (if any)
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

    /* same as for a thin LV, but with square brackets */
    ret = bd_lvm_thlvpoolname (vg_name, cached_lv, error);
    if (!ret)
        return NULL;

    name_start = strchr (ret, '[');
    if (!name_start) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                     "Failed to determine cache pool name from: '%s'", ret);
        g_free (ret);
        return NULL;
    }
    name_start++;

    name_end = strchr (ret, ']');
    if (!name_end) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_CACHE_INVAL,
                     "Failed to determine cache pool name from: '%s'", ret);
        g_free (ret);
        return NULL;
    }

    pool_name = g_strndup (name_start, name_end - name_start);
    g_free (ret);

    return pool_name;
}

/**
 * bd_lvm_cache_stats:
 * @vg_name: name of the VG containing the @cached_lv
 * @cached_lv: cached LV to get stats for
 * @error: (out) (optional): place to store error (if any)
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

    if (geteuid () != 0) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_NOT_ROOT,
                     "Not running as root, cannot query DM maps");
        return NULL;
    }

    lvdata = bd_lvm_lvinfo (vg_name, cached_lv, error);
    if (!lvdata)
        return NULL;

    pool = dm_pool_create ("bd-pool", 20);

    if (g_strcmp0 (lvdata->segtype, "thin-pool") == 0)
        map_name = dm_build_dm_name (pool, vg_name, lvdata->data_lv, NULL);
    else
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
 * bd_lvm_thpool_convert:
 * @vg_name: name of the VG to create the new thin pool in
 * @data_lv: name of the LV that should become the data part of the new pool
 * @metadata_lv: name of the LV that should become the metadata part of the new pool
 * @name: (nullable): name for the thin pool (if %NULL, the name @data_lv is inherited)
 * @extra: (nullable) (array zero-terminated=1): extra options for the thin pool creation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
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
    const gchar *args[8] = {"lvconvert", "--yes", "--type", "thin-pool", "--poolmetadata", metadata_lv, NULL, NULL};
    gboolean success = FALSE;

    args[6] = g_strdup_printf ("%s/%s", vg_name, data_lv);

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free ((gchar *) args[6]);

    if (success && name)
        success = bd_lvm_lvrename (vg_name, data_lv, name, NULL, error);

    return success;
}

/**
 * bd_lvm_cache_pool_convert:
 * @vg_name: name of the VG to create the new thin pool in
 * @data_lv: name of the LV that should become the data part of the new pool
 * @metadata_lv: name of the LV that should become the metadata part of the new pool
 * @name: (nullable): name for the thin pool (if %NULL, the name @data_lv is inherited)
 * @extra: (nullable) (array zero-terminated=1): extra options for the thin pool creation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
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
    const gchar *args[8] = {"lvconvert", "--yes", "--type", "cache-pool", "--poolmetadata", metadata_lv, NULL, NULL};
    gboolean success = FALSE;

    args[6] = g_strdup_printf ("%s/%s", vg_name, data_lv);

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free ((gchar *) args[6]);

    if (success && name)
        success = bd_lvm_lvrename (vg_name, data_lv, name, NULL, error);

    return success;
}

/**
 * bd_lvm_vdo_pool_create:
 * @vg_name: name of the VG to create a new LV in
 * @lv_name: name of the to-be-created VDO LV
 * @pool_name: (nullable): name of the to-be-created VDO pool LV or %NULL for default name
 * @data_size: requested size of the data VDO LV (physical size of the @pool_name VDO pool LV)
 * @virtual_size: requested virtual_size of the @lv_name VDO LV
 * @index_memory: amount of index memory (in bytes) or 0 for default
 * @compression: whether to enable compression or not
 * @deduplication: whether to enable deduplication or not
 * @write_policy: write policy for the volume
 * @extra: (nullable) (array zero-terminated=1): extra options for the VDO LV creation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given @vg_name/@lv_name VDO LV was successfully created or not
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_CREATE
 */
gboolean bd_lvm_vdo_pool_create (const gchar *vg_name, const gchar *lv_name, const gchar *pool_name, guint64 data_size, guint64 virtual_size, guint64 index_memory, gboolean compression, gboolean deduplication, BDLVMVDOWritePolicy write_policy, const BDExtraArg **extra, GError **error) {
    const gchar *args[16] = {"lvcreate", "--type", "vdo", "-n", lv_name, "-L", NULL, "-V", NULL,
                             "--compression", compression ? "y" : "n",
                             "--deduplication", deduplication ? "y" : "n",
                             "-y", NULL, NULL};
    gboolean success = FALSE;
    gchar *old_config = NULL;
    const gchar *write_policy_str = NULL;

    write_policy_str = bd_lvm_get_vdo_write_policy_str (write_policy, error);
    if (!write_policy_str)
        return FALSE;

    args[6] = g_strdup_printf ("%"G_GUINT64_FORMAT"K", data_size / 1024);
    args[8] = g_strdup_printf ("%"G_GUINT64_FORMAT"K", virtual_size / 1024);

    if (pool_name) {
        args[14] = g_strdup_printf ("%s/%s", vg_name, pool_name);
    } else
        args[14] = vg_name;

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

    success = call_lvm_and_report_error (args, extra, FALSE, error);

    g_free (global_config_str);
    global_config_str = old_config;
    g_mutex_unlock (&global_config_lock);

    g_free ((gchar *) args[6]);
    g_free ((gchar *) args[8]);

    if (pool_name)
        g_free ((gchar *) args[14]);

    return success;
}

static gboolean _vdo_set_compression_deduplication (const gchar *vg_name, const gchar *pool_name, const gchar *op, gboolean enable, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"lvchange", op, enable ? "y" : "n", NULL, NULL};
    gboolean success = FALSE;

    args[3] = g_strdup_printf ("%s/%s", vg_name, pool_name);

    success = call_lvm_and_report_error (args, extra, TRUE, error);
    g_free ((gchar *) args[3]);

    return success;
}

/**
 * bd_lvm_vdo_enable_compression:
 * @vg_name: name of the VG containing the to-be-changed VDO pool LV
 * @pool_name: name of the VDO pool LV to enable compression on
 * @extra: (nullable) (array zero-terminated=1): extra options for the VDO change
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether compression was successfully enabled on @vg_name/@pool_name LV or not
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vdo_enable_compression (const gchar *vg_name, const gchar *pool_name, const BDExtraArg **extra, GError **error) {
    return _vdo_set_compression_deduplication (vg_name, pool_name, "--compression", TRUE, extra, error);
}

/**
 * bd_lvm_vdo_disable_compression:
 * @vg_name: name of the VG containing the to-be-changed VDO pool LV
 * @pool_name: name of the VDO pool LV to disable compression on
 * @extra: (nullable) (array zero-terminated=1): extra options for the VDO change
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether compression was successfully disabled on @vg_name/@pool_name LV or not
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vdo_disable_compression (const gchar *vg_name, const gchar *pool_name, const BDExtraArg **extra, GError **error) {
    return _vdo_set_compression_deduplication (vg_name, pool_name, "--compression", FALSE, extra, error);
}

/**
 * bd_lvm_vdo_enable_deduplication:
 * @vg_name: name of the VG containing the to-be-changed VDO pool LV
 * @pool_name: name of the VDO pool LV to enable deduplication on
 * @extra: (nullable) (array zero-terminated=1): extra options for the VDO change
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether deduplication was successfully enabled on @vg_name/@pool_name LV or not
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vdo_enable_deduplication (const gchar *vg_name, const gchar *pool_name, const BDExtraArg **extra, GError **error) {
    return _vdo_set_compression_deduplication (vg_name, pool_name, "--deduplication", TRUE, extra, error);
}

/**
 * bd_lvm_vdo_enable_deduplication:
 * @vg_name: name of the VG containing the to-be-changed VDO pool LV
 * @pool_name: name of the VDO pool LV to disable deduplication on
 * @extra: (nullable) (array zero-terminated=1): extra options for the VDO change
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether deduplication was successfully disabled on @vg_name/@pool_name LV or not
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vdo_disable_deduplication (const gchar *vg_name, const gchar *pool_name, const BDExtraArg **extra, GError **error) {
    return _vdo_set_compression_deduplication (vg_name, pool_name, "--deduplication", FALSE, extra, error);
}

/**
 * bd_lvm_vdo_info:
 * @vg_name: name of the VG that contains the LV to get information about
 * @lv_name: name of the LV to get information about
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the @vg_name/@lv_name LV or %NULL in case
 * of error (the @error) gets populated in those cases)
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_QUERY
 */
BDLVMVDOPooldata* bd_lvm_vdo_info (const gchar *vg_name, const gchar *lv_name, GError **error) {
    const gchar *args[11] = {"lvs", "--noheadings", "--nosuffix", "--nameprefixes",
                       "--unquoted", "--units=b", "-a",
                       "-o", "vdo_operating_mode,vdo_compression_state,vdo_index_state,vdo_write_policy,vdo_index_memory_size,vdo_used_size,vdo_saving_percent,vdo_compression,vdo_deduplication",
                       NULL, NULL};

    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **lines_p = NULL;
    guint num_items;

    args[9] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_capture_output (args, NULL, &output, error);
    g_free ((gchar *) args[9]);

    if (!success)
        /* the error is already populated from the call */
        return NULL;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);

    for (lines_p = lines; *lines_p; lines_p++) {
        table = parse_lvm_vars ((*lines_p), &num_items);
        if (table && (num_items == 9)) {
            g_strfreev (lines);
            return get_vdo_data_from_table (table, TRUE);
        } else if (table)
            g_hash_table_destroy (table);
    }
    g_strfreev (lines);

    /* getting here means no usable info was found */
    g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_PARSE,
                 "Failed to parse information about the VDO LV");
    return NULL;
}

/**
 * bd_lvm_vdo_resize:
 * @vg_name: name of the VG containing the to-be-resized VDO LV
 * @lv_name: name of the to-be-resized VDO LV
 * @size: the requested new size of the VDO LV
 * @extra: (nullable) (array zero-terminated=1): extra options for the VDO LV resize
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
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
 * @extra: (nullable) (array zero-terminated=1): extra options for the VDO pool LV resize
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
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
 * @name: (nullable): name for the VDO LV or %NULL for default name
 * @virtual_size: virtual size for the new VDO LV
 * @index_memory: amount of index memory (in bytes) or 0 for default
 * @compression: whether to enable compression or not
 * @deduplication: whether to enable deduplication or not
 * @write_policy: write policy for the volume
 * @extra: (nullable) (array zero-terminated=1): extra options for the VDO pool creation
 *                                                 (just passed to LVM as is)
 * @error: (out) (optional): place to store error (if any)
 *
 * Converts the @pool_lv into a new VDO pool LV in the @vg_name VG and creates a new
 * @name VDO LV with size @virtual_size.
 *
 * Note: All data on @pool_lv will be irreversibly destroyed.
 *
 * Returns: whether the new VDO pool LV was successfully created from @pool_lv and or not
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_CREATE&%BD_LVM_TECH_MODE_MODIFY
 */
gboolean bd_lvm_vdo_pool_convert (const gchar *vg_name, const gchar *pool_lv, const gchar *name, guint64 virtual_size, guint64 index_memory, gboolean compression, gboolean deduplication, BDLVMVDOWritePolicy write_policy, const BDExtraArg **extra, GError **error) {
    const gchar *args[14] = {"lvconvert", "--yes", "--type", "vdo-pool",
                             "--compression", compression ? "y" : "n",
                             "--deduplication", deduplication ? "y" : "n",
                             NULL, NULL, NULL, NULL, NULL, NULL};
    gboolean success = FALSE;
    guint next_arg = 4;
    gchar *size_str = NULL;
    gchar *lv_spec = NULL;
    gchar *old_config = NULL;
    const gchar *write_policy_str = NULL;

    write_policy_str = bd_lvm_get_vdo_write_policy_str (write_policy, error);
    if (!write_policy_str)
        return FALSE;

    if (name) {
        args[next_arg++] = "-n";
        args[next_arg++] = name;
    }

    args[next_arg++] = "-V";
    size_str = g_strdup_printf ("%"G_GUINT64_FORMAT"K", virtual_size / 1024);
    args[next_arg++] = size_str;
    lv_spec = g_strdup_printf ("%s/%s", vg_name, pool_lv);
    args[next_arg++] = lv_spec;

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

    success = call_lvm_and_report_error (args, extra, FALSE, error);

    g_free (global_config_str);
    global_config_str = old_config;
    g_mutex_unlock (&global_config_lock);

    g_free (size_str);
    g_free (lv_spec);

    return success;
}

/**
 * bd_lvm_vdolvpoolname:
 * @vg_name: name of the VG containing the queried VDO LV
 * @lv_name: name of the queried VDO LV
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): the name of the pool volume for the @vg_name/@lv_name
 * VDO LV or %NULL if failed to determine (@error) is set in those cases)
 *
 * Tech category: %BD_LVM_TECH_VDO-%BD_LVM_TECH_MODE_QUERY
 */
gchar* bd_lvm_vdolvpoolname (const gchar *vg_name, const gchar *lv_name, GError **error) {
    gboolean success = FALSE;
    gchar *output = NULL;
    const gchar *args[6] = {"lvs", "--noheadings", "-o", "pool_lv", NULL, NULL};
    args[4] = g_strdup_printf ("%s/%s", vg_name, lv_name);

    success = call_lvm_and_capture_output (args, NULL, &output, error);
    g_free ((gchar *) args[4]);

    if (!success)
        /* the error is already populated from the call */
        return NULL;

    return g_strstrip (output);
}

/**
 * bd_lvm_get_vdo_operating_mode_str:
 * @mode: mode to get the string representation for
 * @error: (out) (optional): place to store error (if any)
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
 * @error: (out) (optional): place to store error (if any)
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
 * @error: (out) (optional): place to store error (if any)
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
 * @error: (out) (optional): place to store error (if any)
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
 * @error: (out) (optional): place to store error (if any)
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
 * @error: (out) (optional): place to store error (if any)
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
    return vdo_get_stats_full (kvdo_name, error);
}

/**
 * bd_lvm_vdo_get_stats:
 * @vg_name: name of the VG that contains @pool_name VDO pool
 * @pool_name: name of the VDO pool to get statistics for
 * @error: (out) (optional): place to store error (if any)
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

/* check whether the LVM devices file is enabled by LVM
 * we use the existence of the "lvmdevices" command to check whether the feature is available
 * or not, but this can still be disabled either in LVM or in lvm.conf
 */
static gboolean _lvm_devices_enabled () {
    const gchar *args[5] = {"config", "--typeconfig", NULL, "devices/use_devicesfile", NULL};
    gboolean ret = FALSE;
    GError *loc_error = NULL;
    gchar *output = NULL;
    gboolean enabled = FALSE;
    gint scanned = 0;

    /* try full config first -- if we get something from this it means the feature is
       explicitly enabled or disabled by system lvm.conf or using the --config option */
    args[2] = "full";
    ret = call_lvm_and_capture_output (args, NULL, &output, &loc_error);
    if (ret) {
        scanned = sscanf (output, "use_devicesfile=%u", &enabled);
        g_free (output);
        if (scanned != 1)
            return FALSE;

        return enabled;
    } else {
        g_clear_error (&loc_error);
        g_free (output);
    }

    output = NULL;

    /* now try default */
    args[2] = "default";
    ret = call_lvm_and_capture_output (args, NULL, &output, &loc_error);
    if (ret) {
        scanned = sscanf (output, "# use_devicesfile=%u", &enabled);
        g_free (output);
        if (scanned != 1)
            return FALSE;

        return enabled;
    } else {
        g_clear_error (&loc_error);
        g_free (output);
    }

    return FALSE;
}

/**
 * bd_lvm_devices_add:
 * @device: device (PV) to add to the devices file
 * @devices_file: (nullable): LVM devices file or %NULL for default
 * @extra: (nullable) (array zero-terminated=1): extra options for the lvmdevices command
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @device was successfully added to @devices_file or not
 *
 * Tech category: %BD_LVM_TECH_DEVICES no mode (it is ignored)
 */
gboolean bd_lvm_devices_add (const gchar *device, const gchar *devices_file, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"lvmdevices", "--adddev", device, NULL, NULL};
    g_autofree gchar *devfile = NULL;

    if (!bd_lvm_is_tech_avail (BD_LVM_TECH_DEVICES, 0, error))
        return FALSE;

    if (!_lvm_devices_enabled ()) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DEVICES_DISABLED,
                     "LVM devices file not enabled.");
        return FALSE;
    }

    if (devices_file) {
        devfile = g_strdup_printf ("--devicesfile=%s", devices_file);
        args[3] = devfile;
    }

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_lvm_devices_delete:
 * @device: device (PV) to delete from the devices file
 * @devices_file: (nullable): LVM devices file or %NULL for default
 * @extra: (nullable) (array zero-terminated=1): extra options for the lvmdevices command
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @device was successfully removed from @devices_file or not
 *
 * Tech category: %BD_LVM_TECH_DEVICES no mode (it is ignored)
 */
gboolean bd_lvm_devices_delete (const gchar *device, const gchar *devices_file, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"lvmdevices", "--deldev", device, NULL, NULL};
    g_autofree gchar *devfile = NULL;

    if (!bd_lvm_is_tech_avail (BD_LVM_TECH_DEVICES, 0, error))
        return FALSE;

    if (!_lvm_devices_enabled ()) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DEVICES_DISABLED,
                     "LVM devices file not enabled.");
        return FALSE;
    }

    if (devices_file) {
        devfile = g_strdup_printf ("--devicesfile=%s", devices_file);
        args[3] = devfile;
    }

    return bd_utils_exec_and_report_error (args, extra, error);
}
