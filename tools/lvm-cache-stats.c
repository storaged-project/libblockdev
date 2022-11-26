#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <blockdev/blockdev.h>
#include <blockdev/lvm.h>
#include <bytesize/bs_size.h>

void print_usage (const char *cmd) {
    fprintf (stderr,
             "Usage: %s CACHED_LV [CACHED_LV2...]\n"
             "-h    --help   Print this usage info\n"
             "-j    --json   Print stats as JSON\n"
             "Options need to be specified before LVs.\n",
             cmd);
}

void print_size (guint64 bytes, gboolean newline) {
    BSSize size = bs_size_new_from_bytes (bytes, 1);
    char *size_str = bs_size_human_readable (size, BS_BUNIT_MiB, 2, true);
    printf ("%10s%s", size_str, newline ? "\n" : "");
    free (size_str);
    bs_size_free (size);
}

void print_ratio (guint64 part, guint64 total, gboolean space, gboolean newline) {
    float percent = ((float) part / total) * 100;
    printf ("%s[%6.2f%%]%s", space ? " " : "", percent, newline ? "\n" : "");
}

gboolean print_lv_stats (const char *vg_name, const char *lv_name, GError **error) {
    BDLVMLVdata *lv_data = bd_lvm_lvinfo (vg_name, lv_name, error);
    if (!lv_data)
        return FALSE;
    BDLVMCacheStats *stats = bd_lvm_cache_stats (vg_name, lv_name, error);
    if (!stats)
        return FALSE;

    printf ("%s/%s:\n", vg_name, lv_name);
    printf ("  mode:      %13s\n", bd_lvm_cache_get_mode_str (stats->mode, error)); /* ignoring 'error', must be a valid mode */
    printf ("  LV size:      "); print_size (lv_data->size, TRUE);
    printf ("  cache size:   "); print_size (stats->cache_size, TRUE);
    printf ("  cache used:   "); print_size (stats->cache_used, FALSE); print_ratio (stats->cache_used, stats->cache_size, TRUE, TRUE);
    printf ("  read misses:  %10"G_GUINT64_FORMAT"\n", stats->read_misses);
    printf ("  read hits:    %10"G_GUINT64_FORMAT, stats->read_hits); print_ratio (stats->read_hits, stats->read_hits + stats->read_misses, TRUE, TRUE);
    printf ("  write misses: %10"G_GUINT64_FORMAT"\n", stats->write_misses);
    printf ("  write hits:   %10"G_GUINT64_FORMAT, stats->write_hits); print_ratio (stats->write_hits, stats->write_hits + stats->write_misses, TRUE, TRUE);

    bd_lvm_lvdata_free (lv_data);
    bd_lvm_cache_stats_free (stats);

    return TRUE;
}

gboolean print_lv_stats_json(const char *vg_name, const char *lv_name, GError **error) {
    BDLVMLVdata *lv_data = bd_lvm_lvinfo (vg_name, lv_name, error);
    if (!lv_data)
        return FALSE;
    BDLVMCacheStats *stats = bd_lvm_cache_stats (vg_name, lv_name, error);
    if (!stats)
        return FALSE;

    printf ("{\n");
    printf ("  \"lv\": \"%s/%s\",\n", vg_name, lv_name);
    printf ("  \"mode\": \"%s\",\n", bd_lvm_cache_get_mode_str (stats->mode, error)); /* ignoring 'error', must be a valid mode */
    printf ("  \"lv-size\": %"G_GUINT64_FORMAT",\n", lv_data->size);
    printf ("  \"cache-size\": %"G_GUINT64_FORMAT",\n", stats->cache_size);
    printf ("  \"cache-used\": %"G_GUINT64_FORMAT",\n", stats->cache_size);
    printf ("  \"cache-used-pct\": %0.2f,\n", (double) stats->cache_used / stats->cache_size);
    printf ("  \"read-misses\": %"G_GUINT64_FORMAT",\n", stats->read_misses);
    printf ("  \"read-hits\": %"G_GUINT64_FORMAT",\n", stats->read_hits);
    printf ("  \"read-hit-ratio\": %0.2f,\n", (double)stats->read_hits / (stats->read_hits + stats->read_misses));
    printf ("  \"write-misses\": %"G_GUINT64_FORMAT",\n", stats->write_misses);
    printf ("  \"write-hits\": %"G_GUINT64_FORMAT",\n", stats->write_hits);
    printf ("  \"write-hit-ratio\": %0.2f\n", (double)stats->write_hits / (stats->write_hits + stats->write_misses));
    printf ("}\n");

    bd_lvm_lvdata_free (lv_data);
    bd_lvm_cache_stats_free (stats);

    return TRUE;
}

int main (int argc, char *argv[]) {
    gboolean ret = FALSE;
    GError *error = NULL;

    if ((g_strcmp0 (argv[1], "-h") == 0) || g_strcmp0 (argv[1], "--help") == 0) {
        print_usage (argv[0]);
        return 1;
    }

    gboolean json = FALSE;
    int first_lv_arg = 1;
    for (int i=0; i < argc; i++) {
        if ((g_strcmp0 (argv[i], "-j") == 0) || g_strcmp0 (argv[i], "--json") == 0) {
            json = TRUE;
            first_lv_arg++;
        }
    }

    if (first_lv_arg >= argc) {
        fprintf (stderr, "No cached LV to get the stats for specified!\n");
        print_usage (argv[0]);
        return 1;
    }

    /* check that we are running as root */
    if ((getuid() != 0) || (geteuid() != 0)) {
        fprintf (stderr, "This utility must be run as root.\n");
        return 1;
    }

    /* initialize the library -- we only need the LVM plugin */
    BDPluginSpec lvm_plugin = {BD_PLUGIN_LVM, NULL};
    BDPluginSpec *plugins[] = {&lvm_plugin, NULL};
    ret = bd_init (plugins, NULL, &error);

    if (!ret) {
        fprintf (stderr, "Failed to initialize the libblockdev library: %s\n",
                 error->message);
        return 2;
    }

    gboolean ok = TRUE;
    for (int i = first_lv_arg; i < argc; i++) {
        /* Add one blank line between stats for the individual LVs */
        if (i > first_lv_arg)
            printf("\n");

        char *slash = strchr (argv[i], '/');
        if (!slash) {
            fprintf (stderr, "Invalid LV specified: '%s'. Has to be in the VG/LV format.\n", argv[i]);
            ok = FALSE;
            continue;
        }
        *slash = '\0';
        const char *vg_name = argv[i];
        const char *lv_name = slash + 1;

        if (json)
            ret = print_lv_stats_json (vg_name, lv_name, &error);
        else
            ret = print_lv_stats (vg_name, lv_name, &error);

        if (!ret) {
            fprintf (stderr, "Failed to get stats for '%s/%s': %s\n",
                     vg_name, lv_name, error->message);
            ok = FALSE;
        }
    }

    return ok ? 0 : 3;
}
