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

#define _XOPEN_SOURCE  // needed for time.h

#include <glib.h>
#include <unistd.h>
#include <blockdev/utils.h>
#include <string.h>
#include <glob.h>
#include <time.h>
#include <bs_size.h>

#include "mdraid.h"
#include "check_deps.h"

/**
 * SECTION: mdraid
 * @short_description: plugin for basic operations with MD RAID
 * @title: MD RAID
 * @include: mdraid.h
 *
 * A plugin for basic operations with MD RAID. Also sizes are in
 * bytes unless specified otherwise.
 */

/**
 * bd_md_error_quark: (skip)
 */
GQuark bd_md_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-md-error-quark");
}

/**
 * bd_md_examine_data_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDMDExamineData* bd_md_examine_data_copy (BDMDExamineData *data) {
    if (data == NULL)
        return NULL;

    BDMDExamineData *new_data = g_new0 (BDMDExamineData, 1);

    new_data->device = g_strdup (data->device);
    new_data->level = g_strdup (data->level);
    new_data->num_devices = data->num_devices;
    new_data->name = g_strdup (data->name);
    new_data->size = data->size;
    new_data->uuid = g_strdup (data->uuid);
    new_data->update_time = data->update_time;
    new_data->dev_uuid = g_strdup (data->dev_uuid);
    new_data->events = data->events;
    new_data->metadata = g_strdup (data->metadata);
    new_data->chunk_size = data->chunk_size;
    return new_data;
}

/**
 * bd_md_examine_data_free: (skip)
 *
 * Frees @data.
 */
void bd_md_examine_data_free (BDMDExamineData *data) {
    if (data == NULL)
        return;

    g_free (data->device);
    g_free (data->level);
    g_free (data->name);
    g_free (data->uuid);
    g_free (data->dev_uuid);
    g_free (data->metadata);
    g_free (data);
}

/**
 * bd_md_detail_data_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDMDDetailData* bd_md_detail_data_copy (BDMDDetailData *data) {
    if (data == NULL)
        return NULL;

    BDMDDetailData *new_data = g_new0 (BDMDDetailData, 1);

    new_data->device = g_strdup (data->device);
    new_data->name = g_strdup (data->name);
    new_data->metadata = g_strdup (data->metadata);
    new_data->creation_time = g_strdup (data->creation_time);
    new_data->level = g_strdup (data->level);
    new_data->array_size = data->array_size;
    new_data->use_dev_size = data->use_dev_size;
    new_data->raid_devices = data->raid_devices;
    new_data->active_devices = data->active_devices;
    new_data->working_devices = data->working_devices;
    new_data->failed_devices = data->failed_devices;
    new_data->spare_devices = data->spare_devices;
    new_data->clean = data->clean;
    new_data->uuid = g_strdup (data->uuid);

    return new_data;
}

/**
 * bd_md_detail_data_free: (skip)
 *
 * Frees @data.
 */
void bd_md_detail_data_free (BDMDDetailData *data) {
    if (data == NULL)
        return;

    g_free (data->device);
    g_free (data->name);
    g_free (data->metadata);
    g_free (data->creation_time);
    g_free (data->level);
    g_free (data->uuid);

    g_free (data);
}


static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MDADM 0
#define DEPS_MDADM_MASK (1 << DEPS_MDADM)
#define DEPS_LAST 1

static const UtilDep deps[DEPS_LAST] = {
    {"mdadm", MDADM_MIN_VERSION, NULL, "mdadm - v([\\d\\.]+)"},
};


/**
 * bd_md_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_md_check_deps (void) {
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
        g_warning ("Cannot load the MDRAID plugin");

    return ret;
}


/**
 * bd_md_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_md_init (void) {
    /* nothing to do here */
    return TRUE;
};

/**
 * bd_md_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_md_close (void) {
    /* nothing to do here */
}

#define UNUSED __attribute__((unused))

/**
 * bd_md_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_md_is_tech_avail (BDMDTech tech UNUSED, guint64 mode UNUSED, GError **error) {
    /* all tech-mode combinations are supported by this implementation of the
       plugin, but it requires the 'mdadm' utility */
    return check_deps (&avail_deps, DEPS_MDADM_MASK, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * parse_mdadm_vars: (skip)
 * @str: string to parse
 * @item_sep: item separator(s) (key-value pairs separator)
 * @key_val_sep: key-value separator(s) (typically ":" or "=")
 * @num_items: (out): number of parsed items (key-value pairs)
 *
 * Returns: (transfer full): GHashTable containing the key-value pairs parsed
 * from the @str.
 */
static GHashTable* parse_mdadm_vars (const gchar *str, const gchar *item_sep, const gchar *key_val_sep, guint *num_items) {
    GHashTable *table = NULL;
    gchar **items = NULL;
    gchar **item_p = NULL;
    gchar **key_val = NULL;
    gchar **vals = NULL;

    table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    *num_items = 0;

    items = g_strsplit_set (str, item_sep, 0);
    for (item_p=items; *item_p; item_p++) {
        key_val = g_strsplit (*item_p, key_val_sep, 2);
        if (g_strv_length ((gchar **) key_val) == 2) {
            /* we only want to process valid lines (with the separator) */
            /* only use the first value for the given key */
            if (!g_hash_table_contains (table, g_strstrip (key_val[0]))) {
                if (strstr (key_val[1], "<--")) {
                    /* mdadm --examine output for a set being migrated */
                    vals = g_strsplit (key_val[1], "<--", 2);
                    g_hash_table_insert (table, g_strstrip (key_val[0]), g_strstrip (vals[0]));
                    g_free (key_val[1]);
                    g_free (vals[1]);
                    g_free (vals);
                } else {
                    g_hash_table_insert (table, g_strstrip (key_val[0]), g_strstrip (key_val[1]));
                }
                g_free (key_val);
            } else {
                g_strfreev (key_val);
            }
            (*num_items)++;
        } else
            /* invalid line, just free key_val */
            g_strfreev (key_val);
    }

    g_strfreev (items);
    return table;
}

static BDMDExamineData* get_examine_data_from_table (GHashTable *table, gboolean free_table, G_GNUC_UNUSED GError **error) {
    BDMDExamineData *data = g_new0 (BDMDExamineData, 1);
    gchar *value = NULL;
    gchar *first_space = NULL;
    BSSize size = NULL;
    BSError *bs_error = NULL;
    struct tm tm;
    char time_str[20];
    gchar *name_str = NULL;

    data->level = g_strdup ((gchar*) g_hash_table_lookup (table, "Raid Level"));
    if (!(data->level))
        /* BUG: mdadm outputs "RAID Level" for some metadata formats (rhbz#1380034) */
        data->level = g_strdup ((gchar*) g_hash_table_lookup (table, "RAID Level"));

    value = (gchar*) g_hash_table_lookup (table, "Raid Devices");
    if (!value)
        /* BUG: mdadm outputs "RAID Devices" for some metadata formats (rhbz#1380034) */
        value = (gchar*) g_hash_table_lookup (table, "RAID Devices");
    if (value)
        data->num_devices = g_ascii_strtoull (value, NULL, 0);
    else
        data->num_devices = 0;

    name_str = ((gchar*) g_hash_table_lookup (table, "Name"));
    if (name_str) {
        g_strstrip (name_str);
        first_space = strchr (name_str, ' ');
        if (first_space)
            *first_space = '\0';
        data->name = g_strdup (name_str);
    }

    value = (gchar*) g_hash_table_lookup (table, "Array Size");
    if (value) {
        first_space = strchr (value, ' ');
        if (first_space)
            *first_space = '\0';
        if (value && first_space)
            /* Array Size is in KiB */
            data->size = g_ascii_strtoull (value, NULL, 0) * 1024;
    } else
        data->size = 0;

    data->uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "Array UUID"));
    if (!data->uuid)
        /* also try just "UUID" which may be reported e.g for IMSM FW RAID */
        data->uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "UUID"));

    value = (gchar*) g_hash_table_lookup (table, "Update Time");
    if (value) {
        memset(&tm, 0, sizeof(struct tm));
        strptime(value, "%a %b %e %H:%M:%S %Y", &tm);
        strftime(time_str, sizeof(time_str), "%s" , &tm);

        data->update_time = g_ascii_strtoull (time_str, NULL, 0);
    } else
        data->update_time = 0;

    data->dev_uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "Device UUID"));

    value = (gchar*) g_hash_table_lookup (table, "Events");
    if (value)
        data->events = g_ascii_strtoull (value, NULL, 0);
    else
        data->events = 0;

    value = (gchar*) g_hash_table_lookup (table, "Version");
    if (value)
        data->metadata = g_strdup (value);
    else
        data->metadata = NULL;

    value = (gchar*) g_hash_table_lookup (table, "Chunk Size");
    if (value) {
        size = bs_size_new_from_str (value, &bs_error);
        if (size) {
            data->chunk_size = bs_size_get_bytes (size, NULL, &bs_error);
            bs_size_free (size);
        }

        if (bs_error) {
            g_warning ("get_examine_data_from_table(): Failed to parse chunk size from mdexamine data: %s", bs_error->msg);
            bs_clear_error (&bs_error);
        }
    } else
        data->chunk_size = 0;

    if (free_table)
        g_hash_table_destroy (table);

    return data;
}

static BDMDDetailData* get_detail_data_from_table (GHashTable *table, gboolean free_table) {
    BDMDDetailData *data = g_new0 (BDMDDetailData, 1);
    gchar *value = NULL;
    gchar *name_str = NULL;
    gchar *first_space = NULL;

    data->metadata = g_strdup ((gchar*) g_hash_table_lookup (table, "Version"));
    data->creation_time = g_strdup ((gchar*) g_hash_table_lookup (table, "Creation Time"));
    data->level = g_strdup ((gchar*) g_hash_table_lookup (table, "Raid Level"));
    data->uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "UUID"));

    name_str = ((gchar*) g_hash_table_lookup (table, "Name"));
    if (name_str) {
        g_strstrip (name_str);
        first_space = strchr (name_str, ' ');
        if (first_space)
            *first_space = '\0';
        data->name = g_strdup (name_str);
    }

    value = (gchar*) g_hash_table_lookup (table, "Array Size");
    if (value) {
        first_space = strchr (value, ' ');
        if (first_space)
            *first_space = '\0';
        if (value && first_space)
            data->array_size = g_ascii_strtoull (value, NULL, 0);
    }
    else
        data->array_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "Used Dev Size");
    if (value) {
        first_space = strchr (value, ' ');
        if (first_space)
            *first_space = '\0';
        if (value && first_space)
            data->use_dev_size = g_ascii_strtoull (value, NULL, 0);
    }
    else
        data->use_dev_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "Raid Devices");
    if (value)
        data->raid_devices = g_ascii_strtoull (value, NULL, 0);
    else
        data->raid_devices = 0;

    value = (gchar*) g_hash_table_lookup (table, "Total Devices");
    if (value)
        data->total_devices = g_ascii_strtoull (value, NULL, 0);
    else
        data->total_devices = 0;

    value = (gchar*) g_hash_table_lookup (table, "Active Devices");
    if (value)
        data->active_devices = g_ascii_strtoull (value, NULL, 0);
    else
        data->active_devices = 0;

    value = (gchar*) g_hash_table_lookup (table, "Working Devices");
    if (value)
        data->working_devices = g_ascii_strtoull (value, NULL, 0);
    else
        data->working_devices = 0;

    value = (gchar*) g_hash_table_lookup (table, "Failed Devices");
    if (value)
        data->failed_devices = g_ascii_strtoull (value, NULL, 0);
    else
        data->failed_devices = 0;

    value = (gchar*) g_hash_table_lookup (table, "Spare Devices");
    if (value)
        data->spare_devices = g_ascii_strtoull (value, NULL, 0);
    else
        data->spare_devices = 0;

    value = (gchar*) g_hash_table_lookup (table, "State");
    if (value)
        data->clean = (g_strcmp0 (value, "clean") == 0);
    else
        data->clean = FALSE;

    if (free_table)
        g_hash_table_destroy (table);

    return data;
}

/**
 * get_sysfs_name_from_input: (skip)
 * @input: either RAID name or node name
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): RAID node name
 */
static gchar* get_sysfs_name_from_input(const gchar *input, GError **error) {
  gchar* sysfs_name = NULL;
  gchar* path = NULL;

  /* get rid of the "/dev/" or "/dev/md/" prefix (if any) */
  if (g_str_has_prefix (input, "/dev/md/"))
      input = input + 8;
  else if (g_str_has_prefix (input, "/dev/"))
      input = input + 5;

  path = g_strdup_printf ("/sys/class/block/%s/md", input);
  if (access (path, F_OK) == 0)
      sysfs_name = g_strdup (input);
  else
      sysfs_name = bd_md_node_from_name (input, error);

  g_free (path);

  return sysfs_name;
}

/**
 * get_mdadm_spec_from_input: (skip)
 * @input: RAID specification from user
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): RAID specification for mdadm
 *
 * Takes some RAID specification (raid name, node name, path or name symlink)
 * and returns a new specification suitable for mdadm command.
 */
static gchar* get_mdadm_spec_from_input(const gchar *input, GError **error) {
  gchar* md_path_str = NULL;
  gchar* name_path_str = NULL;
  gchar* mdadm_spec = NULL;

  if (g_str_has_prefix (input, "/dev/")) {
      if (access (input, F_OK) == 0)
          mdadm_spec = g_strdup (input);
      else {
          g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_INVAL,
                       "Device %s doesn't exist.", input);
          mdadm_spec = NULL;
      }
  } else {
      md_path_str = g_strdup_printf ("/dev/%s", input);
      name_path_str = g_strdup_printf ("/dev/md/%s", input);
      if (access (name_path_str, F_OK) == 0)
          mdadm_spec = g_strdup (name_path_str);
      else if (access (md_path_str, F_OK) == 0)
          mdadm_spec = g_strdup (md_path_str);
      else
          mdadm_spec = g_strdup (input);
  }

  g_free (md_path_str);
  g_free (name_path_str);

  return mdadm_spec;
}

/**
 * bd_md_get_superblock_size:
 * @member_size: size of an array member
 * @version: (allow-none): metadata version or %NULL to use the current default version
 * @error: (out): place to store error (if any)
 *
 * Returns: Calculated superblock size for an array with a given @member_size
 * and metadata @version or default if unsupported @version is used.
 *
 * Tech category: always available
 */
guint64 bd_md_get_superblock_size (guint64 member_size, const gchar *version, GError **error __attribute__((unused))) {
    guint64 headroom = BD_MD_SUPERBLOCK_SIZE;
    guint64 min_headroom = (1 MiB);

    /* mdadm 3.2.4 made a major change in the amount of space used for 1.1 and
     * 1.2 in order to reserve space for reshaping. See commit 508a7f16 in the
     * upstream mdadm repository. */
    if (!version || (g_strcmp0 (version, "1.1") == 0) ||
        (g_strcmp0 (version, "1.2") == 0) || (g_strcmp0 (version, "default") == 0)) {
        /* MDADM: We try to leave 0.1% at the start for reshape
         * MDADM: operations, but limit this to 128Meg (0.1% of 10Gig)
         * MDADM: which is plenty for efficient reshapes
         * NOTE: In the mdadm code this is in 512b sectors. Converted to use MiB */
        headroom = (128 MiB);
        while (((headroom << 10) > member_size) && (headroom > min_headroom))
            headroom >>= 1;
    }

    return headroom;
}

/**
 * bd_md_create:
 * @device_name: name of the device to create
 * @level: RAID level (as understood by mdadm, see mdadm(8))
 * @disks: (array zero-terminated=1): disks to use for the new RAID (including spares)
 * @spares: number of spare devices
 * @version: (allow-none): metadata version
 * @bitmap: whether to create an internal bitmap on the device or not
 * @chunk_size: chunk size of the device to create
 * @extra: (allow-none) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mdadm' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the new MD RAID device @device_name was successfully created or not
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_CREATE
 */
gboolean bd_md_create (const gchar *device_name, const gchar *level, const gchar **disks, guint64 spares, const gchar *version, gboolean bitmap, guint64 chunk_size, const BDExtraArg **extra, GError **error) {
    const gchar **argv = NULL;
    /* {"mdadm", "create", device, "--run", "level", "raid-devices",...} */
    guint argv_len = 6;
    guint argv_top = 0;
    guint i = 0;
    guint num_disks = 0;
    gchar *level_str = NULL;
    gchar *rdevices_str = NULL;
    gchar *spares_str = NULL;
    gchar *version_str = NULL;
    gchar *chunk_str = NULL;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_MDADM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (spares != 0)
        argv_len++;
    if (version)
        argv_len++;
    if (bitmap)
        argv_len++;
    if (chunk_size != 0)
        argv_len++;

    num_disks = g_strv_length ((gchar **) disks);
    argv_len += num_disks;

    argv = g_new0 (const gchar*, argv_len + 1);

    level_str = g_strdup_printf ("--level=%s", level);
    rdevices_str = g_strdup_printf ("--raid-devices=%"G_GUINT64_FORMAT, (num_disks - spares));

    argv[argv_top++] = "mdadm";
    argv[argv_top++] = "--create";
    argv[argv_top++] = device_name;
    argv[argv_top++] = "--run";
    argv[argv_top++] = level_str;
    argv[argv_top++] = rdevices_str;

    if (spares != 0) {
        spares_str = g_strdup_printf ("--spare-devices=%"G_GUINT64_FORMAT, spares);
        argv[argv_top++] = spares_str;
    }
    if (version) {
        version_str = g_strdup_printf ("--metadata=%s", version);
        argv[argv_top++] = version_str;
    }
    if (bitmap)
        argv[argv_top++] = "--bitmap=internal";
    if (chunk_size != 0) {
        chunk_str = g_strdup_printf ("--chunk=%"G_GUINT64_FORMAT, chunk_size/1024);
        argv[argv_top++] = chunk_str;
    }

    for (i=0; i < num_disks; i++)
        argv[argv_top++] = disks[i];
    argv[argv_top] = NULL;

    ret = bd_utils_exec_and_report_error (argv, extra, error);

    g_free (level_str);
    g_free (rdevices_str);
    g_free (spares_str);
    g_free (version_str);
    g_free (chunk_str);
    g_free (argv);

    return ret;
}

/**
 * bd_md_destroy:
 * @device: device to destroy MD RAID metadata on
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the MD RAID metadata was successfully destroyed on @device or not
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_DELETE
 */
gboolean bd_md_destroy (const gchar *device, GError **error) {
    const gchar *argv[] = {"mdadm", "--zero-superblock", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MDADM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, NULL, error);
}

/**
 * bd_md_deactivate:
 * @raid_spec: specification of the RAID device (name, node or path)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the RAID device @raid_spec was successfully deactivated or not
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_MODIFY
 */
gboolean bd_md_deactivate (const gchar *raid_spec, GError **error) {
    const gchar *argv[] = {"mdadm", "--stop", NULL, NULL};
    gchar *mdadm_spec = NULL;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_MDADM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    mdadm_spec = get_mdadm_spec_from_input (raid_spec, error);
    if (!mdadm_spec)
        /* error is already populated */
        return FALSE;

    argv[2] = mdadm_spec;

    ret = bd_utils_exec_and_report_error (argv, NULL, error);
    g_free (mdadm_spec);

    return ret;
}

/**
 * bd_md_activate:
 * @raid_spec: (allow-none): specification of the RAID device (name, node or path) to activate (if not given "--scan" is implied and @members is ignored)
 * @members: (allow-none) (array zero-terminated=1): member devices to be considered for @device activation
 * @uuid: (allow-none): UUID (in the MD RAID format!) of the MD RAID to activate
 * @start_degraded: whether to start the array even if it's degraded
 * @extra: (allow-none) (array zero-terminated=1): extra options for the activation (right now
 *                                                 passed to the 'mdadm' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the MD RAID @device was successfully activated or not
 *
 * Note: either @members or @uuid (or both) have to be specified.
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_MODIFY
 */
gboolean bd_md_activate (const gchar *raid_spec, const gchar **members, const gchar *uuid, gboolean start_degraded, const BDExtraArg **extra, GError **error) {
    guint64 num_members = (raid_spec && members) ? g_strv_length ((gchar **) members) : 0;
    const gchar **argv = NULL;
    gchar *uuid_str = NULL;
    guint argv_top = 0;
    guint i = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_MDADM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    /* mdadm, --assemble, raid_spec/--scan, --run, --uuid=uuid, member1, member2,..., NULL*/
    argv = g_new0 (const gchar*, num_members + 6);

    argv[argv_top++] = "mdadm";
    argv[argv_top++] = "--assemble";
    if (raid_spec)
        argv[argv_top++] = raid_spec;
    else
        argv[argv_top++] = "--scan";
    if (start_degraded)
        argv[argv_top++] = "--run";
    if (uuid) {
        uuid_str = g_strdup_printf ("--uuid=%s", uuid);
        argv[argv_top++] = uuid_str;
    }
    /* only add member device if device_name given (a combination of --scan with
       a list of members doesn't work) */
    if (raid_spec && members)
        for (i=0; i < num_members; i++)
            argv[argv_top++] = members[i];
    argv[argv_top] = NULL;

    ret = bd_utils_exec_and_report_error (argv, extra, error);

    g_free (uuid_str);
    g_free (argv);

    return ret;
}

/**
 * bd_md_run:
 * @raid_spec: specification of the (possibly degraded) RAID device (name, node or path) to be started
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @raid_spec was successfully started or not
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_MODIFY
 */
gboolean bd_md_run (const gchar *raid_spec, GError **error) {
    const gchar *argv[] = {"mdadm", "--run", NULL, NULL};
    gchar *mdadm_spec = NULL;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_MDADM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    mdadm_spec = get_mdadm_spec_from_input (raid_spec, error);
    if (!mdadm_spec)
        /* error is already populated */
        return FALSE;

    argv[2] = mdadm_spec;

    ret = bd_utils_exec_and_report_error (argv, NULL, error);
    g_free (mdadm_spec);

    return ret;
}

/**
 * bd_md_nominate:
 * @device: device to nominate (add to its appropriate RAID) as a MD RAID device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully nominated (added to its
 * appropriate RAID) or not
 *
 * Note: may start the MD RAID if it becomes ready by adding @device.
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_MODIFY
 */
gboolean bd_md_nominate (const gchar *device, GError **error) {
    const gchar *argv[] = {"mdadm", "--incremental", "--quiet", "--run", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MDADM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (argv, NULL, error);
}

/**
 * bd_md_denominate:
 * @device: device to denominate (remove from its appropriate RAID) as a MD RAID device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully denominated (added to its
 * appropriate RAID) or not
 *
 * Note: may start the MD RAID if it becomes ready by adding @device.
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_MODIFY
 */
gboolean bd_md_denominate (const gchar *device, GError **error) {
    const gchar *argv[] = {"mdadm", "--incremental", "--fail", device, NULL};

    /* XXX: stupid mdadm! --incremental --fail requires "sda1" instead of "/dev/sda1" */
    if (g_str_has_prefix (device, "/dev/"))
        argv[3] = (device + 5);

    return bd_utils_exec_and_report_error (argv, NULL, error);
}

/**
 * bd_md_add:
 * @raid_spec: specification of the RAID device (name, node or path) to add @device into
 * @device: name of the device to add to the @raid_spec RAID device
 * @raid_devs: number of devices the @raid_spec RAID should actively use or 0
 *             to leave unspecified (see below)
 * @extra: (allow-none) (array zero-terminated=1): extra options for the addition (right now
 *                                                 passed to the 'mdadm' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully added to the @raid_spec RAID or
 * not
 *
 * The @raid_devs parameter is used when adding devices to a raid array that has
 * no actual redundancy. In this case it is necessary to explicitly grow the
 * array all at once rather than manage it in the sense of adding spares.
 *
 * Whether the new device will be added as a spare or an active member is
 * decided by mdadm.
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_MODIFY
 */
gboolean bd_md_add (const gchar *raid_spec, const gchar *device, guint64 raid_devs, const BDExtraArg **extra, GError **error) {
    const gchar *argv[7] = {"mdadm", NULL, NULL, NULL, NULL, NULL, NULL};
    guint argv_top = 1;
    gchar *mdadm_spec = NULL;
    gchar *raid_devs_str = NULL;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_MDADM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    mdadm_spec = get_mdadm_spec_from_input (raid_spec, error);
    if (!mdadm_spec)
        /* error is already populated */
        return FALSE;

    if (raid_devs != 0) {
        raid_devs_str = g_strdup_printf ("--raid-devices=%"G_GUINT64_FORMAT, raid_devs);
        argv[argv_top++] = "--grow";
        argv[argv_top++] = mdadm_spec;
        argv[argv_top++] = raid_devs_str;
    } else
        argv[argv_top++] = mdadm_spec;

    argv[argv_top++] = "--add";
    argv[argv_top] = device;

    ret = bd_utils_exec_and_report_error (argv, extra, error);
    g_free (mdadm_spec);
    g_free (raid_devs_str);

    return ret;
}

/**
 * bd_md_remove:
 * @raid_spec: specification of the RAID device (name, node or path) to remove @device from
 * @device: device to remove from the @raid_spec RAID
 * @fail: whether to mark the @device as failed before removing
 * @extra: (allow-none) (array zero-terminated=1): extra options for the removal (right now
 *                                                 passed to the 'mdadm' utility)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully removed from the @raid_spec
 * RAID or not.
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_MODIFY
 */
gboolean bd_md_remove (const gchar *raid_spec, const gchar *device, gboolean fail, const BDExtraArg **extra, GError **error) {
    const gchar *argv[] = {"mdadm", NULL, NULL, NULL, NULL, NULL, NULL};
    guint argv_top = 2;
    gchar *mdadm_spec = NULL;
    gboolean ret = FALSE;
    gchar *dev_path = NULL;

    if (!check_deps (&avail_deps, DEPS_MDADM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    mdadm_spec = get_mdadm_spec_from_input (raid_spec, error);
    if (!mdadm_spec)
        /* error is already populated */
        return FALSE;

    argv[1] = mdadm_spec;

    dev_path = bd_utils_resolve_device (device, error);
    if (!dev_path) {
        /* error is populated */
        g_free (mdadm_spec);
        return FALSE;
    }

    if (fail) {
        argv[argv_top++] = "--fail";
        argv[argv_top++] = dev_path;
    }

    argv[argv_top++] = "--remove";
    argv[argv_top++] = dev_path;

    ret = bd_utils_exec_and_report_error (argv, extra, error);
    g_free (dev_path);
    g_free (mdadm_spec);

    return ret;
}

/**
 * bd_md_examine:
 * @device: name of the device (a member of an MD RAID) to examine
 * @error: (out): place to store error (if any)
 *
 * Returns: information about the MD RAID extracted from the @device
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_QUERY
 */
BDMDExamineData* bd_md_examine (const gchar *device, GError **error) {
    const gchar *argv[] = {"mdadm", "--examine", "-E", device, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    GHashTable *table = NULL;
    guint num_items = 0;
    BDMDExamineData *ret = NULL;
    gchar *value = NULL;
    gchar **output_fields = NULL;
    gchar *orig_data = NULL;
    guint i = 0;
    gboolean found_array_line = FALSE;

    if (!check_deps (&avail_deps, DEPS_MDADM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    success = bd_utils_exec_and_capture_output (argv, NULL, &output, error);
    if (!success)
        /* error is already populated */
        return FALSE;

    table = parse_mdadm_vars (output, "\n", ":", &num_items);
    g_free (output);
    if (!table || (num_items == 0)) {
        /* something bad happened */
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_PARSE, "Failed to parse mdexamine data");
        if (table)
            g_hash_table_destroy (table);
        return NULL;
    }

    ret = get_examine_data_from_table (table, TRUE, error);
    if (!ret)
        /* error is already populated */
        return NULL;

    /* canonicalize UUIDs (as long as we got them) */
    orig_data = ret->uuid;
    if (orig_data) {
        ret->uuid = bd_md_canonicalize_uuid (orig_data, error);
        g_free (orig_data);
    }

    orig_data = ret->dev_uuid;
    if (orig_data) {
        ret->dev_uuid = bd_md_canonicalize_uuid (orig_data, error);
        g_free (orig_data);
    }

    argv[2] = "--export";
    success = bd_utils_exec_and_capture_output (argv, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        bd_md_examine_data_free (ret);
        return FALSE;
    }

    /* try to get a better information about RAID level because it may be
       misleading in the output without --export */
    output_fields = g_strsplit (output, "\n", 0);
    g_free (output);
    output = NULL;
    for (i=0; (i < g_strv_length (output_fields) - 1); i++)
        if (g_str_has_prefix (output_fields[i], "MD_LEVEL=")) {
            value = strchr (output_fields[i], '=');
            value++;
            g_free (ret->level);
            ret->level = g_strdup (value);
        }
    g_strfreev (output_fields);

    argv[2] = "--brief";
    success = bd_utils_exec_and_capture_output (argv, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        bd_md_examine_data_free (ret);
        return FALSE;
    }

    /* try to find the "ARRAY /dev/md/something" pair in the output */
    output_fields = g_strsplit_set (output, " \n", 0);
    for (i=0; !found_array_line && (i < g_strv_length (output_fields) - 1); i++)
        if (g_strcmp0 (output_fields[i], "ARRAY") == 0) {
            found_array_line = TRUE;
            if (g_str_has_prefix (output_fields[i+1], "/dev/md/")) {
                ret->device = g_strdup (output_fields[i+1]);
            } else {
                ret->device = NULL;
            }
        }
    if (!found_array_line)
        ret->device = NULL;
    g_strfreev (output_fields);

    table = parse_mdadm_vars (output, " ", "=", &num_items);
    g_free (output);
    if (!table) {
        /* something bad happened or some expected items were missing  */
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_PARSE,
                     "Failed to parse mdexamine metadata");
        g_hash_table_destroy (table);
        bd_md_examine_data_free (ret);
        return NULL;
    }

    /* try to get metadata version from the output (may be missing) */
    g_free (ret->metadata);
    value = (gchar*) g_hash_table_lookup (table, "metadata");
    if (value)
        ret->metadata = g_strdup (value);
    else
        ret->metadata = NULL;
    g_hash_table_destroy (table);

    return ret;
}

/**
 * bd_md_detail:
 * @raid_spec: specification of the RAID device (name, node or path) to examine
 * @error: (out): place to store error (if any)
 *
 * Returns: information about the MD RAID @raid_spec
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_QUERY
 */
BDMDDetailData* bd_md_detail (const gchar *raid_spec, GError **error) {
    const gchar *argv[] = {"mdadm", "--detail", NULL, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    GHashTable *table = NULL;
    guint num_items = 0;
    gchar *orig_uuid = NULL;
    gchar *mdadm_spec = NULL;
    BDMDDetailData *ret = NULL;

    if (!check_deps (&avail_deps, DEPS_MDADM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    mdadm_spec = get_mdadm_spec_from_input (raid_spec, error);
    if (!mdadm_spec)
        /* error is already populated */
        return NULL;

    argv[2] = mdadm_spec;

    success = bd_utils_exec_and_capture_output (argv, NULL, &output, error);
    if (!success) {
        g_free (mdadm_spec);
        /* error is already populated */
        return NULL;
    }

    table = parse_mdadm_vars (output, "\n", ":", &num_items);
    g_free (output);
    if (!table || (num_items == 0)) {
        g_free (mdadm_spec);
        /* something bad happened or some expected items were missing  */
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_PARSE, "Failed to parse mddetail data");
        if (table)
            g_hash_table_destroy (table);
        return NULL;
    }

    ret = get_detail_data_from_table (table, TRUE);
    if (!ret) {
        g_free (mdadm_spec);
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_PARSE, "Failed to get mddetail data");
        return NULL;
    }

    ret->device = g_strdup (argv[2]);

    orig_uuid = ret->uuid;
    if (orig_uuid) {
        ret->uuid = bd_md_canonicalize_uuid (orig_uuid, error);
        g_free (orig_uuid);
    }

    g_free (mdadm_spec);

    return ret;
}

/**
 * bd_md_canonicalize_uuid:
 * @uuid: UUID to canonicalize
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): cannonicalized form of @uuid or %NULL in case of error
 *
 * This function expects a UUID in the form that mdadm returns. The change is as
 * follows: 3386ff85:f5012621:4a435f06:1eb47236 -> 3386ff85-f501-2621-4a43-5f061eb47236
 *
 * Tech category: always available
 */
gchar* bd_md_canonicalize_uuid (const gchar *uuid, GError **error) {
    const gchar *next_set = uuid;
    gchar *ret = g_new0 (gchar, 37);
    gchar *dest = ret;
    GRegex *regex = NULL;

    regex = g_regex_new ("[0-9a-f]{8}:[0-9a-f]{8}:[0-9a-f]{8}:[0-9a-f]{8}", 0, 0, error);
    if (!regex) {
        /* error is already populated */
        g_free (ret);
        return NULL;
    }

    if (!g_regex_match (regex, uuid, 0, NULL)) {
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_BAD_FORMAT,
                     "malformed or invalid UUID: %s", uuid);
        g_regex_unref (regex);
        g_free (ret);
        return NULL;
    }
    g_regex_unref (regex);

    /* first 8 symbols */
    memcpy (dest, next_set, 8);
    next_set += 9;
    dest += 8;
    *dest = '-';
    dest++;

    /* 4 symbols from the second 8 */
    memcpy (dest, next_set, 4);
    next_set += 4;
    dest += 4;
    *dest = '-';
    dest++;

    /* 4 symbols from the second 8 */
    memcpy (dest, next_set, 4);
    next_set += 5;
    dest += 4;
    *dest = '-';
    dest++;

    /* 4 symbols from the third 8 */
    memcpy (dest, next_set, 4);
    next_set += 4;
    dest += 4;
    *dest = '-';
    dest++;

    /* 4 symbols from the third 8 with no trailing - */
    memcpy (dest, next_set, 4);
    next_set += 5;
    dest += 4;

    /* 9 symbols (8 + \0) from the fourth 8 */
    memcpy (dest, next_set, 9);

    return ret;
}

/**
 * bd_md_get_md_uuid:
 * @uuid: UUID to transform into format used by MD RAID
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): transformed form of @uuid or %NULL in case of error
 *
 * This function expects a UUID in the canonical (traditional format) and
 * returns a UUID in the format used by MD RAID and is thus reverse to
 * bd_md_canonicalize_uuid(). The change is as follows:
 * 3386ff85-f501-2621-4a43-5f061eb47236 -> 3386ff85:f5012621:4a435f06:1eb47236
 *
 * Tech category: always available
 */
gchar* bd_md_get_md_uuid (const gchar *uuid, GError **error) {
    const gchar *next_set = uuid;
    gchar *ret = g_new0 (gchar, 37);
    gchar *dest = ret;
    GRegex *regex = NULL;

    regex = g_regex_new ("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", 0, 0, error);
    if (!regex) {
        /* error is already populated */
        g_free (ret);
        return NULL;
    }

    if (!g_regex_match (regex, uuid, 0, NULL)) {
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_BAD_FORMAT,
                     "malformed or invalid UUID: %s", uuid);
        g_regex_unref (regex);
        g_free (ret);
        return NULL;
    }
    g_regex_unref (regex);

    /* first 8 symbols */
    memcpy (dest, next_set, 8);
    next_set += 9;
    dest += 8;
    *dest = ':';
    dest++;

    /* 4 symbols from the 4 */
    memcpy (dest, next_set, 4);
    next_set += 5;
    dest += 4;

    /* 4 symbols from the second 4 plus :*/
    memcpy (dest, next_set, 4);
    next_set += 5;
    dest += 4;
    *dest = ':';
    dest++;

    /* 4 symbols from the third 4 */
    memcpy (dest, next_set, 4);
    next_set += 5;
    dest += 4;

    /* 4 symbols from the 12 */
    memcpy (dest, next_set, 4);
    next_set += 4;
    dest += 4;
    *dest = ':';
    dest++;

    /* 9 symbols (8 + \0) from the 12 */
    memcpy (dest, next_set, 9);

    return ret;
}

/**
 * bd_md_node_from_name:
 * @name: name of the MD RAID
 * @error: (out): place to store error (if any)
 *
 * Returns: device node of the @name MD RAID or %NULL in case of error
 *
 * Tech category: always available
 */
gchar* bd_md_node_from_name (const gchar *name, GError **error) {
    gchar *dev_path = NULL;
    gchar *ret = NULL;
    gchar *md_path = g_strdup_printf ("/dev/md/%s", name);

    dev_path = bd_utils_resolve_device (md_path, error);
    g_free (md_path);
    if (!dev_path)
        /* error is already populated */
        return NULL;

    ret = g_path_get_basename (dev_path);
    g_free (dev_path);

    return ret;
}

/**
 * bd_md_name_from_node:
 * @node: path of the MD RAID's device node
 * @error: (out): place to store error (if any)
 *
 * Returns: @name of the MD RAID the device node belongs to or %NULL in case of error
 *
 * Tech category: always available
 */
gchar* bd_md_name_from_node (const gchar *node, GError **error) {
    glob_t glob_buf;
    gchar **path_p;
    gboolean found = FALSE;
    gchar *dev_path = NULL;
    gchar *name = NULL;
    gchar *node_name = NULL;

    /* get rid of the "/dev/" prefix (if any) */
    if (g_str_has_prefix (node, "/dev/"))
        node = node + 5;

    if (glob ("/dev/md/*", GLOB_NOSORT, NULL, &glob_buf) != 0) {
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_NO_MATCH,
                     "No name found for the node '%s'", node);
        return NULL;
    }
    for (path_p = glob_buf.gl_pathv; *path_p && !found; path_p++) {
        dev_path = bd_utils_resolve_device (*path_p, error);
        if (!dev_path) {
            g_clear_error (error);
            continue;
        }
        node_name = g_path_get_basename (dev_path);
        g_free (dev_path);
        if (g_strcmp0 (node_name, node) == 0) {
            found = TRUE;
            name = g_path_get_basename (*path_p);
        }
        g_free (node_name);
    }
    globfree (&glob_buf);

    if (!found)
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_NO_MATCH,
                     "No name found for the node '%s'", node);
    return name;
}

/**
 * bd_md_get_status
 * @raid_spec: specification of the RAID device (name, node or path) to get status
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): status of the @raid_spec RAID.
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_QUERY
 */
gchar* bd_md_get_status (const gchar *raid_spec, GError **error) {
    gboolean success = FALSE;
    gchar *ret = NULL;
    gchar *raid_node = NULL;
    gchar *sys_path = NULL;

    raid_node = get_sysfs_name_from_input (raid_spec, error);
    if (!raid_node)
        /* error is already populated */
        return NULL;

    sys_path = g_strdup_printf ("/sys/class/block/%s/md/array_state", raid_node);
    g_free (raid_node);

    success = g_file_get_contents (sys_path, &ret, NULL, error);
    if (!success) {
        /* error is alraedy populated */
        g_free (sys_path);
        return NULL;
    }

    g_free (sys_path);

    return g_strstrip (ret);
}

/**
 * bd_md_set_bitmap_location:
 * @raid_spec: specification of the RAID device (name, node or path) to set the bitmap location
 * @location: bitmap location (none, internal or path)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether @location was successfully set for @raid_spec
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_MODIFY
 */
gboolean bd_md_set_bitmap_location (const gchar *raid_spec, const gchar *location, GError **error) {
    const gchar *argv[] = {"mdadm", "--grow", NULL, "--bitmap", location, NULL};
    gchar* mdadm_spec = NULL;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_MDADM_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    mdadm_spec = get_mdadm_spec_from_input (raid_spec, error);
    if (!mdadm_spec)
        /* error is already populated */
        return FALSE;

    argv[2] = mdadm_spec;

    if ((g_strcmp0 (location, "none") != 0) && (g_strcmp0 (location, "internal") != 0) &&
        !g_str_has_prefix (location , "/")) {

        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_INVAL,
                     "Bitmap location must start with '/' or be 'internal' or 'none'.");
        g_free (mdadm_spec);
        return FALSE;
    }

    ret = bd_utils_exec_and_report_error (argv, NULL, error);

    g_free (mdadm_spec);

    return ret;
}

/**
 * bd_md_get_bitmap_location:
 * @raid_spec: specification of the RAID device (name, node or path) to get the bitmap location
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): bitmap location for @raid_spec
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_QUERY
 */
gchar* bd_md_get_bitmap_location (const gchar *raid_spec, GError **error) {
    gchar *raid_node = NULL;
    gchar *sys_path = NULL;
    gchar *ret = NULL;
    gboolean success = FALSE;

    raid_node = get_sysfs_name_from_input (raid_spec, error);
    if (!raid_node)
        /* error is already populated */
        return NULL;

    sys_path = g_strdup_printf ("/sys/class/block/%s/md/bitmap/location", raid_node);
    g_free (raid_node);

    success = g_file_get_contents (sys_path, &ret, NULL, error);
    if (!success) {
        /* error is alraedy populated */
        g_free (sys_path);
        return NULL;
    }

    g_free (sys_path);

    return g_strstrip (ret);
}

/**
 * bd_md_request_sync_action:
 * @raid_spec: specification of the RAID device (name, node or path) to request sync action on
 * @action: requested sync action (resync, recovery, check, repair or idle)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @action was successfully requested for the @raid_spec
 * RAID or not.
 *
 * Tech category: %BD_MD_TECH_MDRAID-%BD_MD_TECH_MODE_MODIFY
 */
gboolean bd_md_request_sync_action (const gchar *raid_spec, const gchar *action, GError **error) {
    gchar *sys_path = NULL;
    gchar *raid_node = NULL;
    gboolean success = FALSE;

    if ((g_strcmp0 (action, "resync") != 0) && (g_strcmp0 (action, "recovery") != 0) &&
        (g_strcmp0 (action, "check") != 0) && (g_strcmp0 (action, "repair") != 0) &&
        (g_strcmp0 (action, "idle") != 0)) {

        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_INVAL,
                     "Action must be one of resync, recovery, check, repair or idle.");
        return FALSE;
    }

    raid_node = get_sysfs_name_from_input (raid_spec, error);
    if (!raid_node)
        /* error is already populated */
        return FALSE;

    sys_path = g_strdup_printf ("/sys/class/block/%s/md/sync_action", raid_node);
    g_free (raid_node);

    success = bd_utils_echo_str_to_file (action, sys_path, error);
    g_free (sys_path);
    if (!success) {
        g_prefix_error (error,  "Failed to set requested sync action.");
        return FALSE;
    }

    return TRUE;
}
