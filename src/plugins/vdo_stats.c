/*
 * Copyright (C) 2020  Red Hat, Inc.
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
#include <libdevmapper.h>
#include <yaml.h>

#include "vdo_stats.h"
#include "lvm.h"


G_GNUC_INTERNAL gboolean
get_stat_val64 (GHashTable *stats, const gchar *key, gint64 *val) {
    const gchar *s;
    gchar *endptr = NULL;

    s = g_hash_table_lookup (stats, key);
    if (s == NULL)
        return FALSE;

    *val = g_ascii_strtoll (s, &endptr, 0);
    if (endptr == NULL || *endptr != '\0')
        return FALSE;

    return TRUE;
}

G_GNUC_INTERNAL gboolean
get_stat_val64_default (GHashTable *stats, const gchar *key, gint64 *val, gint64 def) {
    if (!get_stat_val64 (stats, key, val))
        *val = def;
    return TRUE;
}

G_GNUC_INTERNAL gboolean
get_stat_val_double (GHashTable *stats, const gchar *key, gdouble *val) {
    const gchar *s;
    gchar *endptr = NULL;

    s = g_hash_table_lookup (stats, key);
    if (s == NULL)
        return FALSE;

    *val = g_ascii_strtod (s, &endptr);
    if (endptr == NULL || *endptr != '\0')
        return FALSE;

    return TRUE;
}

static void add_write_ampl_r_stats (GHashTable *stats) {
    gint64 bios_meta_write, bios_out_write, bios_in_write;

    if (! get_stat_val64 (stats, "biosMetaWrite", &bios_meta_write) ||
        ! get_stat_val64 (stats, "biosOutWrite", &bios_out_write) ||
        ! get_stat_val64 (stats, "biosInWrite", &bios_in_write))
        return;

    if (bios_in_write <= 0)
        g_hash_table_replace (stats, g_strdup ("writeAmplificationRatio"), g_strdup ("0.00"));
    else
        g_hash_table_replace (stats,
                              g_strdup ("writeAmplificationRatio"),
                              g_strdup_printf ("%.2f", (gfloat) (bios_meta_write + bios_out_write) / (gfloat) bios_in_write));
}

static void add_block_stats (GHashTable *stats) {
    gint64 physical_blocks, block_size, data_blocks_used, overhead_blocks_used, logical_blocks_used;
    gint64 savings;

    if (! get_stat_val64 (stats, "physicalBlocks", &physical_blocks) ||
        ! get_stat_val64 (stats, "blockSize", &block_size) ||
        ! get_stat_val64 (stats, "dataBlocksUsed", &data_blocks_used) ||
        ! get_stat_val64 (stats, "overheadBlocksUsed", &overhead_blocks_used) ||
        ! get_stat_val64 (stats, "logicalBlocksUsed", &logical_blocks_used))
        return;

    g_hash_table_replace (stats, g_strdup ("oneKBlocks"), g_strdup_printf ("%"G_GINT64_FORMAT, physical_blocks * block_size / 1024));
    g_hash_table_replace (stats, g_strdup ("oneKBlocksUsed"), g_strdup_printf ("%"G_GINT64_FORMAT, (data_blocks_used + overhead_blocks_used) * block_size / 1024));
    g_hash_table_replace (stats, g_strdup ("oneKBlocksAvailable"), g_strdup_printf ("%"G_GINT64_FORMAT, (physical_blocks - data_blocks_used - overhead_blocks_used) * block_size / 1024));
    g_hash_table_replace (stats, g_strdup ("usedPercent"), g_strdup_printf ("%.0f", 100.0 * (gfloat) (data_blocks_used + overhead_blocks_used) / (gfloat) physical_blocks + 0.5));
    savings = (logical_blocks_used > 0) ? (gint64) (100.0 * (gfloat) (logical_blocks_used - data_blocks_used) / (gfloat) logical_blocks_used) : 100;
    g_hash_table_replace (stats, g_strdup ("savings"), g_strdup_printf ("%"G_GINT64_FORMAT, savings));
    if (savings >= 0)
        g_hash_table_replace (stats, g_strdup ("savingPercent"), g_strdup_printf ("%"G_GINT64_FORMAT, savings));
}

static void add_journal_stats (GHashTable *stats) {
    gint64 journal_entries_committed, journal_entries_started, journal_entries_written;
    gint64 journal_blocks_committed, journal_blocks_started, journal_blocks_written;

    if (! get_stat_val64 (stats, "journalEntriesCommitted", &journal_entries_committed) ||
        ! get_stat_val64 (stats, "journalEntriesStarted", &journal_entries_started) ||
        ! get_stat_val64 (stats, "journalEntriesWritten", &journal_entries_written) ||
        ! get_stat_val64 (stats, "journalBlocksCommitted", &journal_blocks_committed) ||
        ! get_stat_val64 (stats, "journalBlocksStarted", &journal_blocks_started) ||
        ! get_stat_val64 (stats, "journalBlocksWritten", &journal_blocks_written))
        return;

    g_hash_table_replace (stats, g_strdup ("journalEntriesBatching"), g_strdup_printf ("%"G_GINT64_FORMAT, journal_entries_started - journal_entries_written));
    g_hash_table_replace (stats, g_strdup ("journalEntriesWriting"), g_strdup_printf ("%"G_GINT64_FORMAT, journal_entries_written - journal_entries_committed));
    g_hash_table_replace (stats, g_strdup ("journalBlocksBatching"), g_strdup_printf ("%"G_GINT64_FORMAT, journal_blocks_started - journal_blocks_written));
    g_hash_table_replace (stats, g_strdup ("journalBlocksWriting"), g_strdup_printf ("%"G_GINT64_FORMAT, journal_blocks_written - journal_blocks_committed));
}

static void add_computed_stats (GHashTable *stats) {
    const gchar *s;

    s = g_hash_table_lookup (stats, "logicalBlockSize");
    g_hash_table_replace (stats,
                          g_strdup ("fiveTwelveByteEmulation"),
                          g_strdup ((g_strcmp0 (s, "512") == 0) ? "true" : "false"));

    add_write_ampl_r_stats (stats);
    add_block_stats (stats);
    add_journal_stats (stats);
}

enum parse_flags {
  PARSE_NEXT_KEY,
  PARSE_NEXT_VAL,
  PARSE_NEXT_IGN,
};

G_GNUC_INTERNAL GHashTable *
vdo_get_stats_full (const gchar *name, GError **error) {
    struct dm_task *dmt = NULL;
    const gchar *response = NULL;
    yaml_parser_t parser;
    yaml_token_t token;
    GHashTable *stats = NULL;
    gchar *key = NULL;
    gsize len = 0;
    int next_token = PARSE_NEXT_IGN;
    gchar *prefix = NULL;

    dmt = dm_task_create (DM_DEVICE_TARGET_MSG);
    if (!dmt) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to create DM task");
        return NULL;
    }

    if (!dm_task_set_name (dmt, name)) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to set name for DM task");
        dm_task_destroy (dmt);
        return NULL;
    }

    if (!dm_task_set_message (dmt, "stats")) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to set message for DM task");
        dm_task_destroy (dmt);
        return NULL;
    }

    if (!dm_task_run (dmt)) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to run DM task");
        dm_task_destroy (dmt);
        return NULL;
    }

    response = dm_task_get_message_response (dmt);
    if (!response) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to get response from the DM task");
        dm_task_destroy (dmt);
        return NULL;
    }

    if (!yaml_parser_initialize (&parser)) {
        g_set_error (error, BD_LVM_ERROR, BD_LVM_ERROR_DM_ERROR,
                     "Failed to get initialize YAML parser");
        dm_task_destroy (dmt);
        return NULL;
    }

    stats = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    yaml_parser_set_input_string (&parser, (guchar *) response, strlen (response));

    do {
        yaml_parser_scan (&parser, &token);
        switch (token.type) {
            /* key */
            case YAML_KEY_TOKEN:
                next_token = PARSE_NEXT_KEY;
                break;
            /* value */
            case YAML_VALUE_TOKEN:
                next_token = PARSE_NEXT_VAL;
                break;
            /* block mapping */
            case YAML_BLOCK_MAPPING_START_TOKEN:
                if (next_token == PARSE_NEXT_VAL)
                    /* we were expecting to read a key-value pair but this is actually
                       a block start, so we need to free the key we're not going to use */
                    g_free (key);
                break;
            /* mapping */
            case YAML_FLOW_MAPPING_START_TOKEN:
                /* start of flow mapping -> previously read key will be used as prefix
                   for all keys in the mapping:
                        previous key: biosInProgress
                        keys in the mapping: Read, Write...
                        with prefix: biosInProgressRead, biosInProgressWrite...
                */
                prefix = key;
                break;
            case YAML_FLOW_MAPPING_END_TOKEN:
                /* end of flow mapping, discard the prefix used */
                g_free (prefix);
                prefix = NULL;
                break;
            /* actual data */
            case YAML_SCALAR_TOKEN:
                if (next_token == PARSE_NEXT_KEY) {
                    if (prefix) {
                        key = g_strdup_printf ("%s%s", prefix, (const gchar *) token.data.scalar.value);
                        len = strlen (prefix);
                        /* make sure the key with the prefix is still camelCase */
                        key[len] = g_ascii_toupper (key[len]);
                    } else
                        key = g_strdup ((const gchar *) token.data.scalar.value);
                } else if (next_token == PARSE_NEXT_VAL) {
                    gchar *val = g_strdup ((const gchar *) token.data.scalar.value);
                    g_hash_table_insert (stats, key, val);
                }
                break;
            default:
                break;
          }

          if (token.type != YAML_STREAM_END_TOKEN)
              yaml_token_delete (&token);
    } while (token.type != YAML_STREAM_END_TOKEN);

    yaml_parser_delete (&parser);
    dm_task_destroy (dmt);

    if (stats != NULL)
        add_computed_stats (stats);

    return stats;
}
