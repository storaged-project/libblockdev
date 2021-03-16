/*
 * Copyright (C) 2018  Red Hat, Inc.
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
#include <blockdev/utils.h>
#include <bs_size.h>
#include <yaml.h>

#include "vdo.h"
#include "check_deps.h"
#include "vdo_stats.h"

/**
 * SECTION: vdo
 * @short_description: DEPRECATED plugin for operations with VDO devices
 * @title: VDO
 * @include: vdo.h
 *
 * A plugin for operations with VDO devices.
 *
 * This plugin has been deprecated since version 2.24 and should not be used in newly-written code.
 * Use LVM-VDO integration instead.
 */

/**
 * bd_vdo_error_quark: (skip)
 */
GQuark bd_vdo_error_quark (void) {
    return g_quark_from_static_string ("g-bd-vdo-error-quark");
}

void bd_vdo_info_free (BDVDOInfo *info) {
    if (info == NULL)
        return;

    g_free (info->name);
    g_free (info->device);
    g_free (info);
}

BDVDOInfo* bd_vdo_info_copy (BDVDOInfo *info) {
    if (info == NULL)
        return NULL;

    BDVDOInfo *new_info = g_new0 (BDVDOInfo, 1);

    new_info->name = g_strdup (info->name);
    new_info->device = g_strdup (info->device);
    new_info->active = info->active;
    new_info->deduplication = info->deduplication;
    new_info->compression = info->compression;
    new_info->logical_size = info->logical_size;
    new_info->physical_size = info->physical_size;
    new_info->index_memory = info->index_memory;
    new_info->write_policy = info->write_policy;

    return new_info;
}

void bd_vdo_stats_free (BDVDOStats *stats) {
    if (stats == NULL)
        return;

    g_free (stats);
}

BDVDOStats* bd_vdo_stats_copy (BDVDOStats *stats) {
    if (stats == NULL)
        return NULL;

    BDVDOStats *new_stats = g_new0 (BDVDOStats, 1);

    new_stats->block_size = stats->block_size;
    new_stats->logical_block_size = stats->logical_block_size;
    new_stats->physical_blocks = stats->physical_blocks;
    new_stats->data_blocks_used = stats->data_blocks_used;
    new_stats->overhead_blocks_used = stats->overhead_blocks_used;
    new_stats->logical_blocks_used = stats->logical_blocks_used;
    new_stats->used_percent = stats->used_percent;
    new_stats->saving_percent = stats->saving_percent;
    new_stats->write_amplification_ratio = stats->write_amplification_ratio;

    return new_stats;
}


static volatile guint avail_deps = 0;
static volatile guint avail_module_deps = 0;
static GMutex deps_check_lock;

#define DEPS_VDO 0
#define DEPS_VDO_MASK (1 << DEPS_VDO)
#define DEPS_LAST 1

static const UtilDep deps[DEPS_LAST] = {
    {"vdo", NULL, NULL, NULL},
};

#define MODULE_DEPS_VDO 0
#define MODULE_DEPS_VDO_MASK (1 << MODULE_DEPS_VDO)
#define MODULE_DEPS_LAST 1

static const gchar*const module_deps[MODULE_DEPS_LAST] = { "kvdo" };

/**
 * bd_vdo_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_check_deps (void) {
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
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_init (void) {
    /* nothing to do here */
    return TRUE;
}

/**
 * bd_vdo_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
void bd_vdo_close (void) {
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
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_is_tech_avail (BDVDOTech tech UNUSED, guint64 mode UNUSED, GError **error) {
  /* all tech-mode combinations are supported by this implementation of the
     plugin, but it requires the 'vdo' utility */
  if (tech == BD_VDO_TECH_VDO)
    return check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) &&
           check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error);
  else {
    g_set_error (error, BD_VDO_ERROR, BD_VDO_ERROR_TECH_UNAVAIL, "Unknown technology");
    return FALSE;
  }

  return TRUE;
}

enum parse_flags {
  PARSE_NEXT_KEY,
  PARSE_NEXT_VAL,
  PARSE_NEXT_IGN,
};

static GHashTable* parse_yaml_output (const gchar *output, GError **error) {
    GHashTable *table = NULL;
    yaml_parser_t parser;
    yaml_token_t token;
    gchar *key = NULL;

    if (!yaml_parser_initialize (&parser)) {
        g_set_error (error, BD_VDO_ERROR, BD_VDO_ERROR_PARSE,
                     "Failed to initialize YAML parser.");
        return NULL;
    }

    yaml_parser_set_input_string (&parser, (guchar *) output, strlen (output));

    int next_token = PARSE_NEXT_IGN;
    table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    do {
        yaml_parser_scan (&parser, &token);
        switch(token.type) {
            /* key */
            case YAML_KEY_TOKEN:
                next_token = PARSE_NEXT_KEY;
                break;
            /* value */
            case YAML_VALUE_TOKEN:
                next_token = PARSE_NEXT_VAL;
                break;
            /* block */
            case YAML_BLOCK_MAPPING_START_TOKEN:
                if (next_token == PARSE_NEXT_VAL)
                    /* we were expecting to read a key-value pair but this is actually
                       a block start, so we need to free the key we're not going to use */
                    g_free (key);
                break;
            /* actual data */
            case YAML_SCALAR_TOKEN:
                if (next_token == PARSE_NEXT_KEY)
                    key = g_strdup ((const gchar *) token.data.scalar.value);
                else if (next_token == PARSE_NEXT_VAL) {
                    gchar *val = g_strdup ((const gchar *) token.data.scalar.value);
                    g_hash_table_insert (table, key, val);
                }
                break;
            default:
              break;
          }

          if (token.type != YAML_STREAM_END_TOKEN)
              yaml_token_delete (&token);
    } while (token.type != YAML_STREAM_END_TOKEN);

    yaml_parser_delete (&parser);

    return table;
}

static BDVDOInfo* get_vdo_info_from_table (GHashTable *table, gboolean free_table) {
    GError *error = NULL;
    BSError *bs_error = NULL;
    BSSize size = NULL;
    BDVDOInfo *ret = g_new0 (BDVDOInfo, 1);
    gchar *value = NULL;
    gchar *size_str = NULL;

    ret->name = NULL;
    ret->device = g_hash_table_lookup (table, "Storage device");
    if (ret->device != NULL)
        /* get the real device path */
        ret->device = realpath (ret->device, NULL);

    value = (gchar*) g_hash_table_lookup (table, "Activate");
    if (value)
        ret->active = g_strcmp0 (value, "enabled") == 0;
    else
        ret->active = FALSE;

    value = (gchar*) g_hash_table_lookup (table, "Deduplication");
    if (value)
        ret->deduplication = g_strcmp0 (value, "enabled") == 0;
    else
        ret->deduplication = FALSE;

    value = (gchar*) g_hash_table_lookup (table, "Compression");
    if (value)
        ret->compression = g_strcmp0 (value, "enabled") == 0;
    else
        ret->compression = FALSE;

    value = (gchar*) g_hash_table_lookup (table, "Configured write policy");
    if (value) {
        ret->write_policy = bd_vdo_get_write_policy_from_str (value, &error);
        if (error) {
            g_warning ("%s", error->message);
            g_clear_error (&error);
        }
    } else
        ret->write_policy = BD_VDO_WRITE_POLICY_UNKNOWN;

    value = (gchar*) g_hash_table_lookup (table, "Index memory setting");
    if (value) {
        size_str = g_strdup_printf ("%s GB", value);
        size = bs_size_new_from_str (size_str, &bs_error);
        if (size) {
            ret->index_memory = bs_size_get_bytes (size, NULL, &bs_error);
            bs_size_free (size);
        }
        g_free (size_str);
        if (bs_error) {
            g_warning ("%s", bs_error->msg);
            bs_clear_error (&bs_error);
        }
    }
    else
        ret->index_memory = 0;

    value = (gchar*) g_hash_table_lookup (table, "Logical size");
    if (value) {
        size = bs_size_new_from_str (value, &bs_error);
        if (size) {
            ret->logical_size = bs_size_get_bytes (size, NULL, &bs_error);
            bs_size_free (size);
        }
        if (bs_error) {
            g_warning ("%s", bs_error->msg);
            bs_clear_error (&bs_error);
        }
    } else
        ret->logical_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "Physical size");
    if (value) {
        size = bs_size_new_from_str (value, &bs_error);
        if (size) {
            ret->physical_size = bs_size_get_bytes (size, NULL, &bs_error);
            bs_size_free (size);
        }
        if (error) {
            g_warning ("%s", bs_error->msg);
            bs_clear_error (&bs_error);
        }
    } else
        ret->physical_size = 0;

    if (free_table)
        g_hash_table_destroy (table);

    return ret;
}

/**
 * bd_vdo_info:
 * @device: a VDO volume to get information about
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the VDO volume or %NULL
 * in case of error (@error gets populated in those cases)
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_QUERY
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
BDVDOInfo* bd_vdo_info (const gchar *name, GError **error) {
    const gchar *args[6] = {"vdo", "status", "-n", name, NULL};
    GHashTable *table = NULL;
    gboolean success = FALSE;
    gchar *output = NULL;
    BDVDOInfo *ret = NULL;

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success)
        /* the error is already populated */
        return NULL;

    table = parse_yaml_output (output, error);
    g_free (output);
    if (!table) {
        /* the error is already populated */
        return NULL;
    }

    ret = get_vdo_info_from_table (table, TRUE);
    ret->name = g_strdup (name);

    return ret;
}

/**
 * bd_vdo_get_write_policy_str:
 * @policy: policy to get the string representation for
 * @error: (out): place to store error (if any)
 *
 * Returns: string representation of @policy or %NULL in case of error
 *
 * Tech category: always provided/supported
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
const gchar* bd_vdo_get_write_policy_str (BDVDOWritePolicy policy, GError **error) {
    if (policy == BD_VDO_WRITE_POLICY_SYNC)
        return "sync";
    else if (policy == BD_VDO_WRITE_POLICY_ASYNC)
        return "async";
    else if (policy == BD_VDO_WRITE_POLICY_AUTO)
        return "auto";
    else {
        g_set_error (error, BD_VDO_ERROR, BD_VDO_ERROR_POLICY_INVAL,
                     "Invalid policy given: %d", policy);
        return NULL;
    }
}

/**
 * bd_vdo_get_write_policy_from_str:
 * @policy_str: string representation of a write policy mode
 * @error: (out): place to store error (if any)
 *
 * Returns: write policy for the @mode_str or %BD_VDO_WRITE_POLICY_UNKNOWN if
 *          failed to determine
 *
 * Tech category: always provided/supported
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
BDVDOWritePolicy bd_vdo_get_write_policy_from_str (const gchar *policy_str, GError **error) {
    if (g_strcmp0 (policy_str, "sync") == 0)
        return BD_VDO_WRITE_POLICY_SYNC;
    else if (g_strcmp0 (policy_str, "async") == 0)
        return BD_VDO_WRITE_POLICY_ASYNC;
    else if (g_strcmp0 (policy_str, "auto") == 0)
        return BD_VDO_WRITE_POLICY_AUTO;
    else {
        g_set_error (error, BD_VDO_ERROR, BD_VDO_ERROR_POLICY_INVAL,
                     "Invalid policy given: %s", policy_str);
        return BD_VDO_WRITE_POLICY_UNKNOWN;
    }
}

static gchar* get_index_memory_str (guint64 index_memory, GError **error) {
    BSSize mem_size = NULL;
    BSError *bs_error = NULL;
    gchar *size_str = NULL;
    BSSize round_to = NULL;
    BSSize mod_size = NULL;
    BSSize rounded = NULL;
    int ret = 0;
    BSUnit un = { .dunit = BS_DUNIT_GB};

    mem_size = bs_size_new_from_bytes (index_memory, 0);

    ret = bs_size_cmp_bytes (mem_size, 1 GB, FALSE);
    if (ret == 0) {
        /* exactly 1 GB -- valid input, just return the string */
        size_str = bs_size_convert_to (mem_size, un, &bs_error);
        if (!size_str) {
            g_set_error (error, BD_VDO_ERROR, BD_VDO_ERROR_FAIL,
                         "Failed to convert index memory size to gigabytes: %s", bs_error->msg);
            bs_clear_error (&bs_error);
            bs_size_free (mem_size);
            return NULL;
        }

        bs_size_free (mem_size);
        return size_str;
    } else {
        if (ret == 1)
            /* bigger than (or equal to) 1 GB -> needs to be rounded to GB */
            round_to = bs_size_new_from_bytes (1 GB, 0);
        else if (ret == -1)
            /* smaller than 1 GB -> needs to be rounded to 0.25 GB */
            round_to = bs_size_new_from_bytes (250 MB, 0);

        mod_size = bs_size_mod (mem_size, round_to, &bs_error);
        if (!mod_size) {
            g_set_error (error, BD_VDO_ERROR, BD_VDO_ERROR_FAIL,
                         "Error when validation index memory size: %s", bs_error->msg);
            bs_size_free (mem_size);
            bs_size_free (round_to);
            bs_clear_error (&bs_error);
            return NULL;
        }
        if (bs_size_cmp_bytes (mod_size, 0, FALSE) != 0) {
            rounded = bs_size_round_to_nearest (mem_size, round_to, BS_ROUND_DIR_DOWN, &bs_error);
            if (bs_error) {
                g_set_error (error, BD_VDO_ERROR, BD_VDO_ERROR_FAIL,
                             "Error when rounding index memory size: %s", bs_error->msg);
                bs_size_free (mem_size);
                bs_size_free (round_to);
                bs_size_free (mod_size);
                bs_clear_error (&bs_error);
                return NULL;
            }
            g_warning ("%"G_GUINT64_FORMAT" is not valid size for index memory, rounding to %s",
                       index_memory, bs_size_get_bytes_str (rounded));
            bs_size_free (mem_size);
            mem_size = rounded;
        }
        bs_size_free (mod_size);

        size_str = bs_size_convert_to (mem_size, un, &bs_error);
        if (!size_str) {
            g_set_error (error, BD_VDO_ERROR, BD_VDO_ERROR_FAIL,
                         "Failed to convert index memory size to gigabytes: %s", bs_error->msg);
            bs_clear_error (&bs_error);
            bs_size_free (mem_size);
            return NULL;
        }

        bs_size_free (mem_size);
        return size_str;
    }
}

/**
 * bd_vdo_create:
 * @name: name for the VDO volume
 * @backing_device: device to use for VDO storage
 * @logical_size: logical VDO volume size or 0 for default (size of @backing_device)
 * @index_memory: amount of index memory or 0 for default; note that only some
 *                sizes are valid here (0.25, 0.5 and 0.75 GB and integer multiples of 1 GB)
 *                invalid sizes will be rounded DOWN to nearest GB (or one of the allowed
 *                decimal values)
 * @compression: whether to enable compression or not
 * @deduplication: whether to enable deduplication or not
 * @write_policy: write policy for the volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO creation
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VDO volume was successfully created or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_CREATE
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_create (const gchar *name, const gchar *backing_device, guint64 logical_size, guint64 index_memory, gboolean compression, gboolean deduplication, BDVDOWritePolicy write_policy, const BDExtraArg **extra, GError **error) {
    const gchar **args = g_new0 (const gchar*, 13);
    guint next_arg = 0;
    gboolean ret = FALSE;
    gchar *size_str = NULL;
    gchar *mem_str = NULL;
    gchar *mem_size = NULL;
    const gchar *policy = NULL;
    gchar *policy_str = NULL;

    args[next_arg++] = "vdo";
    args[next_arg++] = "create";
    args[next_arg++] = "--name";
    args[next_arg++] = name;
    args[next_arg++] = "--device";
    args[next_arg++] = backing_device;
    args[next_arg++] = "--force";


    if (logical_size != 0) {
        size_str = g_strdup_printf ("--vdoLogicalSize=%"G_GUINT64_FORMAT"B", logical_size);
        args[next_arg++] = size_str;
    }

    if (index_memory != 0) {
        mem_size = get_index_memory_str (index_memory, error);
        if (!mem_size) {
            g_prefix_error (error, "Failed to create VDO volume: ");
            g_free (args);
            return FALSE;
        }

        mem_str = g_strdup_printf ("--indexMem=%s", mem_size);
        args[next_arg++] = mem_str;

        g_free (mem_size);
    }

    if (compression)
        args[next_arg++] = "--compression=enabled";
    else
        args[next_arg++] = "--compression=disabled";

    if (deduplication)
        args[next_arg++] = "--deduplication=enabled";
    else
        args[next_arg++] = "--deduplication=disabled";

    policy = bd_vdo_get_write_policy_str (write_policy, error);
    if (!policy) {
        /* the error is already populated */
        g_free (size_str);
        g_free (mem_str);
        g_free (args);
        return FALSE;
    }

    policy_str = g_strdup_printf ("--writePolicy=%s", policy);
    args[next_arg++] = policy_str;

    args[next_arg] = NULL;

    ret = bd_utils_exec_and_report_error (args, extra, error);

    g_free (size_str);
    g_free (mem_str);
    g_free (policy_str);
    g_free (args);

    return ret;
}

/**
 * bd_vdo_remove:
 * @name: name of an existing VDO volume
 * @force: force remove the volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO creation
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VDO volume was successfully removed or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_MODIFY
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_remove (const gchar *name, gboolean force, const BDExtraArg **extra, GError **error) {
    const gchar *args[6] = {"vdo", "remove", "-n", name, NULL, NULL};

    if (!check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (force)
        args[5] = "--force";

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_vdo_change_write_policy:
 * @name: name of an existing VDO volume
 * @write_policy: new write policy for the volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO creation
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the policy was successfully changed or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_MODIFY
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_change_write_policy (const gchar *name, BDVDOWritePolicy write_policy, const BDExtraArg **extra, GError **error) {
    const gchar *args[6] = {"vdo", "changeWritePolicy", "-n", name, NULL, NULL};
    const gchar *policy = NULL;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    policy = bd_vdo_get_write_policy_str (write_policy, error);
    if (!policy)
        /* the error is already populated */
        return FALSE;

    args[4] = g_strdup_printf ("--writePolicy=%s", policy);

    ret = bd_utils_exec_and_report_error (args, extra, error);

    g_free ((gchar *) args[4]);
    return ret;
}

/**
 * bd_vdo_enable_compression:
 * @name (allow-none): name of an existing VDO volume; %NULL indicates this should be applied to all VDO devices
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO creation
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the compression was successfully enabled or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_MODIFY
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_enable_compression (const gchar *name, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"vdo", "enableCompression", "-n", name, NULL};

    if (!check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_vdo_disable_compression:
 * @name: name of an existing VDO volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO creation
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the compression was successfully disabled or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_MODIFY
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_disable_compression (const gchar *name, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"vdo", "disableCompression", "-n", name, NULL};

    if (!check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_vdo_enable_deduplication:
 * @name: name of an existing VDO volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO creation
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the deduplication was successfully enabled or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_MODIFY
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_enable_deduplication (const gchar *name, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"vdo", "enableDeduplication", "-n", name, NULL};

    if (!check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_vdo_disable_deduplication:
 * @name: name of an existing VDO volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO creation
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the deduplication was successfully disabled or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_MODIFY
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_disable_deduplication (const gchar *name, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"vdo", "disableDeduplication", "-n", name, NULL};

    if (!check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_vdo_activate:
 * @name: name of an existing VDO volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO creation
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VDO volume was successfully activated or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_ACTIVATE_DEACTIVATE
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_activate (const gchar *name, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"vdo", "activate", "-n", name, NULL};

    if (!check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_vdo_deactivate:
 * @name: name of an existing VDO volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO creation
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VDO volume was successfully deactivated or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_ACTIVATE_DEACTIVATE
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_deactivate (const gchar *name, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"vdo", "deactivate", "-n", name, NULL};

    if (!check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_vdo_start:
 * @name: name of an existing VDO volume
 * @rebuild: force rebuild the volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO creation
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VDO volume was successfully started or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_START_STOP
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_start (const gchar *name, gboolean rebuild, const BDExtraArg **extra, GError **error) {
    const gchar *args[6] = {"vdo", "start", "-n", name, NULL, NULL};

    if (!check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (rebuild)
        args[4] = "--forceRebuild";

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_vdo_stop:
 * @name: name of an existing VDO volume
 * @force: force stop the volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO creation
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VDO volume was successfully stopped or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_START_STOP
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_stop (const gchar *name, gboolean force, const BDExtraArg **extra, GError **error) {
    const gchar *args[6] = {"vdo", "stop", "-n", name, NULL, NULL};

    if (!check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (force)
        args[4] = "--force";

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_vdo_grow_logical:
 * @name: name of an existing VDO volume
 * @size: new logical size for the volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO creation
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VDO volume was successfully resized or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_GROW
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_grow_logical (const gchar *name, guint64 size, const BDExtraArg **extra, GError **error) {
    const gchar *args[6] = {"vdo", "growLogical", "-n", name, NULL, NULL};
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    args[4] = g_strdup_printf ("--vdoLogicalSize=%"G_GUINT64_FORMAT"B", size);

    ret =  bd_utils_exec_and_report_error (args, extra, error);

    g_free ((gchar *) args[4]);
    return ret;
}

/**
 * bd_vdo_grow_physical:
 * @name: name of an existing VDO volume
 * @extra: (allow-none) (array zero-terminated=1): extra options for the VDO tool
 *                                                 (just passed to VDO as is)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the VDO volume was successfully grown or not
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_GROW
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
gboolean bd_vdo_grow_physical (const gchar *name, const BDExtraArg **extra, GError **error) {
    const gchar *args[5] = {"vdo", "growPhysical", "-n", name, NULL};
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_VDO_MASK, deps, DEPS_LAST, &deps_check_lock, error) ||
        !check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_error (args, extra, error);

    return ret;
}


/**
 * bd_vdo_get_stats_full:
 * @name: name of an existing VDO volume
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full) (element-type utf8 utf8): hashtable of type string - string of available statistics or %NULL in case of error (@error gets populated in those cases)
 *
 * Statistics are collected from the values exposed by the kernel `kvdo` module at the `/sys/kvdo/<VDO_NAME>/statistics/` path. Some of the keys are computed to mimic the information produced by the vdo tools.
 * Please note the contents of the hashtable may vary depending on the actual kvdo module version.
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_QUERY
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
GHashTable* bd_vdo_get_stats_full (const gchar *name, GError **error) {
    if (!check_module_deps (&avail_module_deps, MODULE_DEPS_VDO_MASK, module_deps, MODULE_DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return vdo_get_stats_full(name, error);
}

/**
 * bd_vdo_get_stats:
 * @name: name of an existing VDO volume
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): a structure containing selected statistics or %NULL in case of error (@error gets populated in those cases)
 *
 * In contrast to @bd_vdo_get_stats_full this function will only return selected statistics in a fixed structure. In case a value is not available, -1 would be returned.
 *
 * The following statistics are presented:
 *   - `"block_size"`: The block size of a VDO volume, in bytes.
 *   - `"logical_block_size"`: The logical block size, in bytes.
 *   - `"physical_blocks"`: The total number of physical blocks allocated for a VDO volume.
 *   - `"data_blocks_used"`: The number of physical blocks currently in use by a VDO volume to store data.
 *   - `"overhead_blocks_used"`: The number of physical blocks currently in use by a VDO volume to store VDO metadata.
 *   - `"logical_blocks_used"`: The number of logical blocks currently mapped.
 *   - `"usedPercent"`: The percentage of physical blocks used on a VDO volume (= used blocks / allocated blocks * 100).
 *   - `"savingPercent"`: The percentage of physical blocks saved on a VDO volume (= [logical blocks used - physical blocks used] / logical blocks used).
 *   - `"writeAmplificationRatio"`: The average number of block writes to the underlying storage per block written to the VDO device.
 *
 * Tech category: %BD_VDO_TECH_VDO-%BD_VDO_TECH_MODE_QUERY
 *
 * Deprecated: 2.24: Use LVM-VDO integration instead.
 */
BDVDOStats* bd_vdo_get_stats (const gchar *name, GError **error) {
    GHashTable *full_stats;
    BDVDOStats *stats;

    full_stats = bd_vdo_get_stats_full (name, error);
    if (! full_stats)
        return NULL;

    stats = g_new0 (BDVDOStats, 1);
    get_stat_val64_default (full_stats, "block_size", &stats->block_size, -1);
    get_stat_val64_default (full_stats, "logical_block_size", &stats->logical_block_size, -1);
    get_stat_val64_default (full_stats, "physical_blocks", &stats->physical_blocks, -1);
    get_stat_val64_default (full_stats, "data_blocks_used", &stats->data_blocks_used, -1);
    get_stat_val64_default (full_stats, "overhead_blocks_used", &stats->overhead_blocks_used, -1);
    get_stat_val64_default (full_stats, "logical_blocks_used", &stats->logical_blocks_used, -1);
    get_stat_val64_default (full_stats, "usedPercent", &stats->used_percent, -1);
    get_stat_val64_default (full_stats, "savingPercent", &stats->saving_percent, -1);
    if (! get_stat_val_double (full_stats, "writeAmplificationRatio", &stats->write_amplification_ratio))
        stats->write_amplification_ratio = -1;

    g_hash_table_destroy (full_stats);

    return stats;
}
