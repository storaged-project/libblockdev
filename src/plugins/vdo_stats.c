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
#include <parted/parted.h>
#include <blockdev/utils.h>

#include "vdo_stats.h"

#define VDO_SYS_PATH "/sys/kvdo"


gboolean __attribute__ ((visibility ("hidden")))
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

gboolean __attribute__ ((visibility ("hidden")))
get_stat_val64_default (GHashTable *stats, const gchar *key, gint64 *val, gint64 def) {
    if (!get_stat_val64 (stats, key, val))
        *val = def;
    return TRUE;
}

gboolean __attribute__ ((visibility ("hidden")))
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

    if (! get_stat_val64 (stats, "bios_meta_write", &bios_meta_write) ||
        ! get_stat_val64 (stats, "bios_out_write", &bios_out_write) ||
        ! get_stat_val64 (stats, "bios_in_write", &bios_in_write))
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

    if (! get_stat_val64 (stats, "physical_blocks", &physical_blocks) ||
        ! get_stat_val64 (stats, "block_size", &block_size) ||
        ! get_stat_val64 (stats, "data_blocks_used", &data_blocks_used) ||
        ! get_stat_val64 (stats, "overhead_blocks_used", &overhead_blocks_used) ||
        ! get_stat_val64 (stats, "logical_blocks_used", &logical_blocks_used))
        return;

    g_hash_table_replace (stats, g_strdup ("oneKBlocks"), g_strdup_printf ("%"G_GINT64_FORMAT, physical_blocks * block_size / 1024));
    g_hash_table_replace (stats, g_strdup ("oneKBlocksUsed"), g_strdup_printf ("%"G_GINT64_FORMAT, (data_blocks_used + overhead_blocks_used) * block_size / 1024));
    g_hash_table_replace (stats, g_strdup ("oneKBlocksAvailable"), g_strdup_printf ("%"G_GINT64_FORMAT, (physical_blocks - data_blocks_used - overhead_blocks_used) * block_size / 1024));
    g_hash_table_replace (stats, g_strdup ("usedPercent"), g_strdup_printf ("%.0f", 100.0 * (gfloat) (data_blocks_used + overhead_blocks_used) / (gfloat) physical_blocks + 0.5));
    savings = (logical_blocks_used > 0) ? (gint64) (100.0 * (gfloat) (logical_blocks_used - data_blocks_used) / (gfloat) logical_blocks_used) : -1;
    g_hash_table_replace (stats, g_strdup ("savings"), g_strdup_printf ("%"G_GINT64_FORMAT, savings));
    if (savings >= 0)
        g_hash_table_replace (stats, g_strdup ("savingPercent"), g_strdup_printf ("%"G_GINT64_FORMAT, savings));
}

static void add_journal_stats (GHashTable *stats) {
    gint64 journal_entries_committed, journal_entries_started, journal_entries_written;
    gint64 journal_blocks_committed, journal_blocks_started, journal_blocks_written;

    if (! get_stat_val64 (stats, "journal_entries_committed", &journal_entries_committed) ||
        ! get_stat_val64 (stats, "journal_entries_started", &journal_entries_started) ||
        ! get_stat_val64 (stats, "journal_entries_written", &journal_entries_written) ||
        ! get_stat_val64 (stats, "journal_blocks_committed", &journal_blocks_committed) ||
        ! get_stat_val64 (stats, "journal_blocks_started", &journal_blocks_started) ||
        ! get_stat_val64 (stats, "journal_blocks_written", &journal_blocks_written))
        return;

    g_hash_table_replace (stats, g_strdup ("journal_entries_batching"), g_strdup_printf ("%"G_GINT64_FORMAT, journal_entries_started - journal_entries_written));
    g_hash_table_replace (stats, g_strdup ("journal_entries_writing"), g_strdup_printf ("%"G_GINT64_FORMAT, journal_entries_written - journal_entries_committed));
    g_hash_table_replace (stats, g_strdup ("journal_blocks_batching"), g_strdup_printf ("%"G_GINT64_FORMAT, journal_blocks_started - journal_blocks_written));
    g_hash_table_replace (stats, g_strdup ("journal_blocks_writing"), g_strdup_printf ("%"G_GINT64_FORMAT, journal_blocks_written - journal_blocks_committed));
}

static void add_computed_stats (GHashTable *stats) {
    const gchar *s;

    s = g_hash_table_lookup (stats, "logical_block_size");
    g_hash_table_replace (stats,
                          g_strdup ("fiveTwelveByteEmulation"),
                          g_strdup ((g_strcmp0 (s, "512") == 0) ? "true" : "false"));

    add_write_ampl_r_stats (stats);
    add_block_stats (stats);
    add_journal_stats (stats);
}

GHashTable __attribute__ ((visibility ("hidden")))
*vdo_get_stats_full (const gchar *name, GError **error) {
    GHashTable *stats;
    GDir *dir;
    gchar *stats_dir;
    const gchar *direntry;
    gchar *s;
    gchar *val = NULL;

    /* TODO: does the `name` need to be escaped? */
    stats_dir = g_build_path (G_DIR_SEPARATOR_S, VDO_SYS_PATH, name, "statistics", NULL);
    dir = g_dir_open (stats_dir, 0, error);
    if (dir == NULL) {
        g_prefix_error (error, "Error reading statistics from %s: ", stats_dir);
        g_free (stats_dir);
        return NULL;
    }

    stats = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    while ((direntry = g_dir_read_name (dir))) {
        s = g_build_filename (stats_dir, direntry, NULL);
        if (! g_file_get_contents (s, &val, NULL, error)) {
            g_prefix_error (error, "Error reading statistics from %s: ", s);
            g_free (s);
            g_hash_table_destroy (stats);
            stats = NULL;
            break;
        }
        g_hash_table_replace (stats, g_strdup (direntry), g_strdup (g_strstrip (val)));
        g_free (val);
        g_free (s);
    }
    g_dir_close (dir);
    g_free (stats_dir);

    if (stats != NULL)
        add_computed_stats (stats);

    return stats;
}
