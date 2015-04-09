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
#include <utils.h>
#include <string.h>
#include <glob.h>

#include "mdraid.h"

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
    BDMDExamineData *new_data = g_new (BDMDExamineData, 1);

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
    return new_data;
}

/**
 * bd_md_examine_data_free: (skip)
 *
 * Frees @data.
 */
void bd_md_examine_data_free (BDMDExamineData *data) {
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
    BDMDDetailData *new_data = g_new (BDMDDetailData, 1);

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
    g_free (data->device);
    g_free (data->name);
    g_free (data->metadata);
    g_free (data->creation_time);
    g_free (data->level);
    g_free (data->uuid);

    g_free (data);
}

/**
 * check: (skip)
 */
gboolean check() {
    GError *error = NULL;
    gboolean ret = bd_utils_check_util_version ("mdadm", MDADM_MIN_VERSION, NULL, "mdadm - v([\\d\\.]+)", &error);

    if (!ret && error) {
        g_warning("Cannot load the MDRAID plugin: %s" , error->message);
        g_clear_error (&error);
    }
    return ret;
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
static GHashTable* parse_mdadm_vars (gchar *str, gchar *item_sep, gchar *key_val_sep, guint *num_items) {
    GHashTable *table = NULL;
    gchar **items = NULL;
    gchar **item_p = NULL;
    gchar **key_val = NULL;

    table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    *num_items = 0;

    items = g_strsplit_set (str, item_sep, 0);
    for (item_p=items; *item_p; item_p++) {
        key_val = g_strsplit (*item_p, key_val_sep, 2);
        if (g_strv_length (key_val) == 2) {
            /* we only want to process valid lines (with the separator) */
            g_hash_table_insert (table, g_strstrip (key_val[0]), g_strstrip (key_val[1]));
            (*num_items)++;
        } else
            /* invalid line, just free key_val */
            g_strfreev (key_val);
    }

    g_strfreev (items);
    return table;
}

static BDMDExamineData* get_examine_data_from_table (GHashTable *table, gboolean free_table, GError **error) {
    BDMDExamineData *data = g_new (BDMDExamineData, 1);
    gchar *value = NULL;

    data->level = g_strdup ((gchar*) g_hash_table_lookup (table, "MD_LEVEL"));

    value = (gchar*) g_hash_table_lookup (table, "MD_DEVICES");
    if (value)
        data->num_devices = g_ascii_strtoull (value, NULL, 0);
    else
        data->num_devices = 0;

    data->name = g_strdup ((gchar*) g_hash_table_lookup (table, "MD_NAME"));

    value = (gchar*) g_hash_table_lookup (table, "MD_ARRAY_SIZE");
    if (value)
        data->size = bd_utils_size_from_spec (value, error);
    else
        data->size = 0;

    data->uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "MD_UUID"));

    value = (gchar*) g_hash_table_lookup (table, "MD_UPDATE_TIME");
    if (value)
        data->update_time = g_ascii_strtoull (value, NULL, 0);
    else
        data->update_time = 0;

    data->dev_uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "MD_DEV_UUID"));

    value = (gchar*) g_hash_table_lookup (table, "MD_EVENTS");
    if (value)
        data->events = g_ascii_strtoull (value, NULL, 0);
    else
        data->events = 0;

    value = (gchar*) g_hash_table_lookup (table, "MD_METADATA");
    if (value)
        data->metadata = g_strdup (value);
    else
        data->metadata = NULL;

    if (free_table)
        g_hash_table_destroy (table);

    return data;
}

static BDMDDetailData* get_detail_data_from_table (GHashTable *table, gboolean free_table) {
    BDMDDetailData *data = g_new (BDMDDetailData, 1);
    gchar *value = NULL;
    gchar *first_space = NULL;

    data->metadata = g_strdup ((gchar*) g_hash_table_lookup (table, "Version"));
    data->creation_time = g_strdup ((gchar*) g_hash_table_lookup (table, "Creation Time"));
    data->level = g_strdup ((gchar*) g_hash_table_lookup (table, "Raid Level"));
    data->uuid = g_strdup ((gchar*) g_hash_table_lookup (table, "UUID"));
    data->name = g_strdup ((gchar*) g_hash_table_lookup (table, "Name"));

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
 * bd_md_get_superblock_size:
 * @size: size of the array
 * @version: (allow-none): metadata version or %NULL to use the current default version
 * @error: (out): place to store error (if any)
 *
 * Returns: Calculated superblock size for given array @size and metadata @version
 * or default if unsupported @version is used.
 */
guint64 bd_md_get_superblock_size (guint64 size, gchar *version, GError **error __attribute__((unused))) {
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
        while (((headroom << 10) > size) && (headroom > min_headroom))
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
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the new MD RAID device @device_name was successfully created or not
 */
gboolean bd_md_create (gchar *device_name, gchar *level, gchar **disks, guint64 spares, gchar *version, gboolean bitmap, GError **error) {
    gchar **argv = NULL;
    /* ["mdadm", "create", device, "--run", "level", "raid-devices",...] */
    guint argv_len = 6;
    guint argv_top = 0;
    guint i = 0;
    guint num_disks = 0;
    gchar *level_str = NULL;
    gchar *rdevices_str = NULL;
    gchar *spares_str = NULL;
    gchar *version_str = NULL;
    gboolean ret = FALSE;

    if (spares != 0)
        argv_len++;
    if (version)
        argv_len++;
    if (bitmap)
        argv_len++;
    num_disks = g_strv_length (disks);
    argv_len += num_disks;

    argv = g_new (gchar*, argv_len + 1);

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

    for (i=0; i < num_disks; i++)
        argv[argv_top++] = disks[i];
    argv[argv_top] = NULL;

    ret = bd_utils_exec_and_report_error (argv, error);

    g_free (level_str);
    g_free (rdevices_str);
    g_free (spares_str);
    g_free (version_str);
    g_free (argv);

    return ret;
}

/**
 * bd_md_destroy:
 * @device: device to destroy MD RAID metadata on
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the MD RAID metadata was successfully destroyed on @device or not
 */
gboolean bd_md_destroy (gchar *device, GError **error) {
    gchar *argv[] = {"mdadm", "--zero-superblock", device, NULL};

    return bd_utils_exec_and_report_error (argv, error);
}

/**
 * bd_md_deactivate:
 * @device_name: name of the RAID device to deactivate
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the RAID device @device_name was successfully deactivated or not
 */
gboolean bd_md_deactivate (gchar *device_name, GError **error) {
    gchar *argv[] = {"mdadm", "--stop", device_name, NULL};
    gchar *dev_md_path = NULL;
    gboolean ret = FALSE;

    /* XXX: mdadm doesn't recognize the user-defined name without the '/dev/md/'
       prefix, but its own device (e.g. md121) is okay */
    dev_md_path = g_strdup_printf ("/dev/md/%s", device_name);
    if (access (dev_md_path, F_OK) == 0)
        argv[2] = dev_md_path;

    ret = bd_utils_exec_and_report_error (argv, error);
    g_free (dev_md_path);

    return ret;
}

/**
 * bd_md_activate:
 * @device_name: name of the RAID device to activate
 * @members: (allow-none) (array zero-terminated=1): member devices to be considered for @device activation
 * @uuid: (allow-none): UUID (in the MD RAID format!) of the MD RAID to activate
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the MD RAID @device was successfully activated or not
 *
 * Note: either @members or @uuid (or both) have to be specified.
 */
gboolean bd_md_activate (gchar *device_name, gchar **members, gchar *uuid, GError **error) {
    guint64 num_members = members ? g_strv_length (members) : 0;
    gchar **argv = NULL;
    gchar *uuid_str = NULL;
    guint argv_top = 0;
    guint i = 0;
    gboolean ret = FALSE;

    /* mdadm, --assemble, device_name, --run, --uuid=uuid, member1, member2,..., NULL*/
    if (uuid) {
        argv = g_new (gchar*, num_members + 6);
        uuid_str = g_strdup_printf ("--uuid=%s", uuid);
    }
    else
        argv = g_new (gchar*, num_members + 5);

    argv[argv_top++] = "mdadm";
    argv[argv_top++] = "--assemble";
    argv[argv_top++] = device_name;
    argv[argv_top++] = "--run";
    if (uuid)
        argv[argv_top++] = uuid_str;
    for (i=0; i < num_members; i++)
        argv[argv_top++] = members[i];
    argv[argv_top] = NULL;

    ret = bd_utils_exec_and_report_error (argv, error);

    g_free (uuid_str);
    g_free (argv);

    return ret;
}

/**
 * bd_md_run:
 * @raid_name: name of the (possibly degraded) MD RAID to be started
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @raid_name was successfully started or not
 */
gboolean bd_md_run (gchar *raid_name, GError **error) {
    gchar *argv[] = {"mdadm", "--run", NULL, NULL};
    gchar *raid_name_str = NULL;
    gboolean ret = FALSE;

    raid_name_str = g_strdup_printf ("/dev/md/%s", raid_name);
    if (access (raid_name_str, F_OK) == 0)
        raid_name = raid_name_str;
    argv[2] = raid_name;

    ret = bd_utils_exec_and_report_error (argv, error);
    g_free (raid_name_str);

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
 */
gboolean bd_md_nominate (gchar *device, GError **error) {
    gchar *argv[] = {"mdadm", "--incremental", "--quiet", "--run", device, NULL};

    return bd_utils_exec_and_report_error (argv, error);
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
 */
gboolean bd_md_denominate (gchar *device, GError **error) {
    gchar *argv[] = {"mdadm", "--incremental", "--fail", device, NULL};

    /* XXX: stupid mdadm! --incremental --fail requires "sda1" instead of "/dev/sda1" */
    if (g_str_has_prefix (device, "/dev/"))
        argv[3] = (device + 5);

    return bd_utils_exec_and_report_error (argv, error);
}

/**
 * bd_md_add:
 * @raid_name: name of the RAID device to add @device into
 * @device: name of the device to add to the @raid_name RAID device
 * @raid_devs: number of devices the @raid_name RAID should actively use or 0
 *             to leave unspecified (see below)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully added to the @raid_name RAID or
 * not
 *
 * The @raid_devs parameter is used when adding devices to a raid array that has
 * no actual redundancy. In this case it is necessary to explicitly grow the
 * array all at once rather than manage it in the sense of adding spares.
 *
 * Whether the new device will be added as a spare or an active member is
 * decided by mdadm.
 */
gboolean bd_md_add (gchar *raid_name, gchar *device, guint64 raid_devs, GError **error) {
    gchar *argv[7] = {"mdadm", NULL, NULL, NULL, NULL, NULL, NULL};
    guint argv_top = 1;
    gchar *raid_name_str = NULL;
    gchar *raid_devs_str = NULL;
    gboolean ret = FALSE;

    raid_name_str = g_strdup_printf ("/dev/md/%s", raid_name);
    if (access (raid_name_str, F_OK) == 0)
        raid_name = raid_name_str;

    if (raid_devs != 0) {
        raid_devs_str = g_strdup_printf ("--raid-devices=%"G_GUINT64_FORMAT, raid_devs);
        argv[argv_top++] = "--grow";
        argv[argv_top++] = raid_name;
        argv[argv_top++] = raid_devs_str;
    } else
        argv[argv_top++] = raid_name;

    argv[argv_top++] = "--add";
    argv[argv_top] = device;

    ret = bd_utils_exec_and_report_error (argv, error);
    g_free (raid_name_str);
    g_free (raid_devs_str);

    return ret;
}

/**
 * bd_md_remove:
 * @raid_name: name of the RAID device to remove @device from
 * @device: device to remove from the @raid_name RAID
 * @fail: whether to mark the @device as failed before removing
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully removed from the @raid_name
 * RAID or not.
 */
gboolean bd_md_remove (gchar *raid_name, gchar *device, gboolean fail, GError **error) {
    gchar *argv[] = {"mdadm", raid_name, NULL, NULL, NULL, NULL};
    guint argv_top = 2;
    gchar *raid_name_str = NULL;
    gboolean ret = FALSE;

    raid_name_str = g_strdup_printf ("/dev/md/%s", raid_name);
    if (access (raid_name_str, F_OK) == 0)
        argv[1] = raid_name_str;

    if (fail)
        argv[argv_top++] = "--fail";

    argv[argv_top++] = "--remove";

    if (g_str_has_prefix (device, "/dev/"))
        argv[argv_top] = (device + 5);
    else
        argv[argv_top] = device;

    ret = bd_utils_exec_and_report_error (argv, error);
    g_free (raid_name_str);

    return ret;
}

/**
 * bd_md_examine:
 * @device: name of the device (a member of an MD RAID) to examine
 * @error: (out): place to store error (if any)
 *
 * Returns: information about the MD RAID extracted from the @device
 */
BDMDExamineData* bd_md_examine (gchar *device, GError **error) {
    gchar *argv[] = {"mdadm", "--examine", "--export", device, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    GHashTable *table = NULL;
    guint num_items = 0;
    BDMDExamineData *ret = NULL;
    gchar *value = NULL;
    gchar **output_fields = NULL;
    gchar *orig_data = NULL;
    guint i = 0;
    gboolean found_dev_name = FALSE;

    success = bd_utils_exec_and_capture_output (argv, &output, error);
    if (!success)
        /* error is already populated */
        return FALSE;

    table = parse_mdadm_vars (output, " \n", "=", &num_items);
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

    argv[2] = "--brief";
    success = bd_utils_exec_and_capture_output (argv, &output, error);
    if (!success)
        /* error is already populated */
        return FALSE;

    /* try to find the "ARRAY /dev/md/something" pair in the output */
    output_fields = g_strsplit_set (output, " \n", 0);
    for (i=0; !found_dev_name && (i < g_strv_length (output_fields) - 1); i++)
        if (g_strcmp0 (output_fields[i], "ARRAY") == 0)
            if (g_str_has_prefix (output_fields[i+1], "/dev/md/")) {
                ret->device = g_strdup (output_fields[i+1]);
                found_dev_name = TRUE;
            }
    if (!found_dev_name)
        ret->device = NULL;
    g_strfreev (output_fields);

    table = parse_mdadm_vars (output, " ", "=", &num_items);
    g_free (output);
    if (!table) {
        /* something bad happened or some expected items were missing  */
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_PARSE,
                     "Failed to parse mdexamine metadata");
        g_hash_table_destroy (table);
        return NULL;
    }

    value = (gchar*) g_hash_table_lookup (table, "metadata");
    if (value) {
        ret->metadata = g_strdup (value);
        g_hash_table_destroy (table);
        return ret;
    } else {
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_PARSE,
                     "Failed to parse mdexamine metadata");
        g_hash_table_destroy (table);
        return NULL;
    }
}

/**
 * bd_md_detail:
 * @raid_name: name of the MD RAID to examine
 * @error: (out): place to store error (if any)
 *
 * Returns: information about the MD RAID @raid_name
 */
BDMDDetailData* bd_md_detail (gchar *raid_name, GError **error) {
    gchar *argv[] = {"mdadm", "--detail", raid_name, NULL};
    gchar *output = NULL;
    gboolean success = FALSE;
    GHashTable *table = NULL;
    guint num_items = 0;
    gchar *orig_uuid = NULL;
    gchar *raid_name_str = NULL;
    BDMDDetailData *ret = NULL;

    raid_name_str = g_strdup_printf ("/dev/md/%s", raid_name);
    if (access (raid_name_str, F_OK) == 0)
        argv[2] = raid_name_str;

    success = bd_utils_exec_and_capture_output (argv, &output, error);
    if (!success) {
        g_free (raid_name_str);
        /* error is already populated */
        return FALSE;
    }

    table = parse_mdadm_vars (output, "\n", ":", &num_items);
    g_free (output);
    if (!table || (num_items == 0)) {
        g_free (raid_name_str);
        /* something bad happened or some expected items were missing  */
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_PARSE, "Failed to parse mddetail data");
        if (table)
            g_hash_table_destroy (table);
        return NULL;
    }

    ret = get_detail_data_from_table (table, TRUE);
    if (!ret) {
        g_free (raid_name_str);
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_PARSE, "Failed to get mddetail data");
        return NULL;
    }

    ret->device = g_strdup (argv[2]);

    orig_uuid = ret->uuid;
    if (orig_uuid) {
        ret->uuid = bd_md_canonicalize_uuid (orig_uuid, error);
        g_free (orig_uuid);
    }

    g_free (raid_name_str);

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
 */
gchar* bd_md_canonicalize_uuid (gchar *uuid, GError **error) {
    gchar *next_set = uuid;
    gchar *ret = g_new (gchar, 37);
    gchar *dest = ret;
    GRegex *regex = NULL;

    regex = g_regex_new ("[0-9a-f]{8}:[0-9a-f]{8}:[0-9a-f]{8}:[0-9a-f]{8}", 0, 0, error);
    if (!regex)
        /* error is already populated */
        return NULL;

    if (!g_regex_match (regex, uuid, 0, NULL)) {
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_BAD_FORMAT,
                     "malformed or invalid UUID: %s", uuid);
        g_regex_unref (regex);
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
 */
gchar* bd_md_get_md_uuid (gchar *uuid, GError **error) {
    gchar *next_set = uuid;
    gchar *ret = g_new (gchar, 37);
    gchar *dest = ret;
    GRegex *regex = NULL;

    regex = g_regex_new ("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", 0, 0, error);
    if (!regex)
        /* error is already populated */
        return NULL;

    if (!g_regex_match (regex, uuid, 0, NULL)) {
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_BAD_FORMAT,
                     "malformed or invalid UUID: %s", uuid);
        g_regex_unref (regex);
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
 * Returns: path to the @name MD RAID's device node or %NULL in case of error
 */
gchar* bd_md_node_from_name (gchar *name, GError **error) {
    gchar *symlink = NULL;
    gchar *ret = NULL;
    gchar *md_path = g_strdup_printf ("/dev/md/%s", name);

    symlink = g_file_read_link (md_path, error);
    if (!symlink) {
        /* error is already populated */
        g_free (md_path);
        return NULL;
    }

    g_strstrip (symlink);
    ret = g_path_get_basename (symlink);

    g_free (symlink);
    g_free (md_path);

    return ret;
}

/**
 * bd_md_name_from_node:
 * @node: path of the MD RAID's device node
 * @error: (out): place to store error (if any)
 *
 * Returns: @name of the MD RAID the device node belongs to or %NULL in case of error
 */
gchar* bd_md_name_from_node (gchar *node, GError **error) {
    gchar *node_path = NULL;
    glob_t glob_buf;
    gchar **path_p;
    gboolean found = FALSE;
    gchar *symlink = NULL;
    gchar *name = NULL;
    gchar *node_name = NULL;

    if (!g_str_has_prefix (node, "/dev/"))
        node_path = g_strdup_printf ("/dev/%s", node);
    else
        node_path = g_strdup_printf (node);

    if (glob ("/dev/md/*", GLOB_NOSORT, NULL, &glob_buf) != 0) {
        g_free (node_path);
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_NO_MATCH,
                     "No name found for the node '%s'", node);
        return NULL;
    }
    for (path_p = glob_buf.gl_pathv; *path_p && !found; path_p++) {
        symlink = g_file_read_link (*path_p, error);
        if (!symlink)
            continue;
        node_name = g_path_get_basename (symlink);
        if (g_strcmp0 (node_name, node) == 0) {
            found = TRUE;
            name = g_path_get_basename (*path_p);
        }
        g_free (node_name);
    }
    globfree (&glob_buf);
    g_free (node_path);

    if (!found)
        g_set_error (error, BD_MD_ERROR, BD_MD_ERROR_NO_MATCH,
                     "No name found for the node '%s'", node);
    return name;
}
