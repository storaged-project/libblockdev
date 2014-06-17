#include <glib/gprintf.h>

void print_hash_table (GHashTable *table) {
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, table);
    g_printf("HashTable contents: \n");
    g_printf("====================\n");
    while (g_hash_table_iter_next(&iter, &key, &value))
        g_printf("%s : %s\n", (gchar *) key, (gchar *) value);
}

int main (int argc, char **argv) {
    gint exit_status;
    const gchar **fname = NULL;
    gchar *msg = NULL;
    gchar *stdout = NULL;
    gchar *stderr = NULL;
    gboolean succ;
    guint64 result = 0;
    guint8 i;
    guint64 *sizes= NULL;
    GHashTable *table = NULL;
    guint num_items;
    BDLVMPVdata *data = NULL;
    gchar *ret_str = NULL;

    g_printf ("Supported functions:\n");
    for (fname=get_supported_functions(); (*fname); fname++) {
        puts ((*fname));
    }
    puts ("");

    gchar* args[] = {"lvs", "--all", NULL};

    /* test calling lvm */
    g_printf ("Calling\n");
    succ = call_lvm (args, &stdout, &stderr, &exit_status, &msg);
    g_printf ("Called\n");
    if (succ) {
        puts ("Everything ok\n");
        g_printf ("STDOUT: %s\n", stdout);
        g_printf ("STDERR: %s\n", stderr);
        g_printf ("Exit status: %d\n", exit_status);
    }
    else {
        puts ("Some error!");
        puts (msg);
    }
    g_free (stdout);
    g_free (stderr);
    g_free (msg);
    msg = NULL;

    succ = call_lvm_and_capture_output (args, &stdout, &msg);
    if (succ) {
        puts ("Called 'lvs' and captured output");
        g_printf ("OUTPUT: %s", stdout);
        g_free (stdout);
    } else {
        puts ("Failed to call 'lvs' and capture output");
        g_printf ("ERROR: %s", msg);
    }
    g_free (msg);

    if (bd_lvm_is_supported_pe_size(16 MEBIBYTE))
        puts ("16 MiB PE: Supported.");
    else
        puts ("16 MiB PE: Unsupported.");

    sizes = bd_lvm_get_supported_pe_sizes ();
    g_printf ("Supported PE sizes: ");
    for (i=0; sizes[i] != 0; i++)
        g_printf ("%s, ", bd_size_human_readable (sizes[i]));
    puts ("");
    g_free (sizes);

    g_printf ("max LV size: %s\n", bd_size_human_readable(bd_lvm_get_max_lv_size()));

    result = bd_lvm_round_size_to_pe ((13 MiB), USE_DEFAULT_PE_SIZE, TRUE);
    g_printf ("up-rounded size 13 MiB: %s\n", bd_size_human_readable(result));
    result = bd_lvm_round_size_to_pe ((13 MiB), USE_DEFAULT_PE_SIZE, FALSE);
    g_printf ("down-rounded size 13 MiB: %s\n", bd_size_human_readable(result));

    result = bd_lvm_get_lv_physical_size ((13 MiB), USE_DEFAULT_PE_SIZE);
    g_printf ("13 MiB physical size: %s\n", bd_size_human_readable(result));

    result = bd_lvm_get_thpool_padding ((1 GiB), USE_DEFAULT_PE_SIZE, TRUE);
    g_printf ("1 GiB ThPool padding size (included): %s\n", bd_size_human_readable(result));
    result = bd_lvm_get_thpool_padding ((1 GiB), USE_DEFAULT_PE_SIZE, FALSE);
    g_printf ("1 GiB ThPool padding size (not included): %s\n", bd_size_human_readable(result));

    if (bd_lvm_is_valid_thpool_md_size (512 MiB))
        puts ("512 MiB ThPool MD size: Valid.");
    else
        puts ("512 MiB ThPool MD size: Invalid.");

    if (bd_lvm_is_valid_thpool_chunk_size ((192 KiB), TRUE))
        puts ("192 KiB ThPool chunk size (discard): Valid.");
    else
        puts ("192 KiB ThPool chunk size (discard): Invalid.");

    if (bd_lvm_is_valid_thpool_chunk_size ((192 KiB), FALSE))
        puts ("192 KiB ThPool chunk size (no discard): Valid.");
    else
        puts ("192 KiB ThPool chunk size (no discard): Invalid.");

    succ = bd_lvm_pvcreate ("/dev/xd1", &msg);
    if (!succ)
        g_printf ("pvcreate failed: %s\n", msg);
    else
        puts ("pvcreate succeeded");
    g_free (msg);

    succ = bd_lvm_pvresize ("/dev/xd1", 12 GiB, &msg);
    if (!succ)
        g_printf ("pvresize failed: %s\n", msg);
    else
        puts ("pvresize succeeded");
    g_free (msg);

    succ = bd_lvm_pvremove ("/dev/xd1", &msg);
    if (!succ)
        g_printf ("pvremove failed: %s\n", msg);
    else
        puts ("pvremove succeeded");
    g_free (msg);

    succ = bd_lvm_pvmove ("/dev/xd1", NULL, &msg);
    if (!succ)
        g_printf ("pvmove failed: %s\n", msg);
    else
        puts ("pvmove succeeded");
    g_free (msg);

    succ = bd_lvm_pvscan ("/dev/xd1", TRUE, &msg);
    if (!succ)
        g_printf ("pvscan failed: %s\n", msg);
    else
        puts ("pvscan succeeded");
    g_free (msg);

    table = parse_lvm_vars ("key1=val1 key2val2 key3=val3", &num_items);
    g_printf ("Parsed %d items\n", num_items);
    print_hash_table (table);
    g_hash_table_destroy (table);

    data = bd_lvm_pvinfo ("/dev/xd1", &msg);
    if (!data)
        g_printf ("pvinfo failed: %s\n", msg);
    else
        puts ("pvinfo succeeded");
    g_free (msg);
    if (data)
        bd_lvm_pvdata_free (data);

    gchar *pv_list[] = {"/dev/xd1", "/dev/xd2", NULL};
    succ = bd_lvm_vgcreate ("newVG", pv_list, 0, &msg);
    if (!succ)
        g_printf ("vgcreate failed: %s\n", msg);
    else
        puts ("vgcreate succeeded");
    g_free (msg);

    succ = bd_lvm_vgremove ("newVG", &msg);
    if (!succ)
        g_printf ("vgremove failed: %s\n", msg);
    else
        puts ("vgremove succeeded");
    g_free (msg);

    succ = bd_lvm_vgactivate ("newVG", &msg);
    if (!succ)
        g_printf ("vgactivate failed: %s\n", msg);
    else
        puts ("vgactivate succeeded");
    g_free (msg);

    succ = bd_lvm_vgdeactivate ("newVG", &msg);
    if (!succ)
        g_printf ("vgdeactivate failed: %s\n", msg);
    else
        puts ("vgdeactivate succeeded");
    g_free (msg);

    succ = bd_lvm_vgextend ("newVG", "/dev/xd1", &msg);
    if (!succ)
        g_printf ("vgextend failed: %s\n", msg);
    else
        puts ("vgextend succeeded");
    g_free (msg);

    succ = bd_lvm_vgreduce ("newVG", "/dev/xd1", &msg);
    if (!succ)
        g_printf ("vgreduce with PV failed: %s\n", msg);
    else
        puts ("vgreduce with PV succeeded");
    g_free (msg);

    succ = bd_lvm_vgreduce ("newVG", NULL, &msg);
    if (!succ)
        g_printf ("vgreduce without PV failed: %s\n", msg);
    else
        puts ("vgextend without PV succeeded");
    g_free (msg);

    ret_str = bd_lvm_lvorigin ("newVG", "newLV", &msg);
    if (!ret_str)
        g_printf ("lvorigin failed: %s\n", msg);
    else
        g_printf ("lvorigin succeeded: %s\n", ret_str);
    g_free(msg);

    succ = bd_lvm_lvremove ("newVG", "newLV", TRUE, &msg);
    if (!succ)
        g_printf ("lvremove failed: %s\n", msg);
    else
        puts ("lvremove succeeded");
    g_free(msg);

    succ = bd_lvm_lvresize ("newVG", "newLV", 128 MiB, &msg);
    if (!succ)
        g_printf ("lvresize failed: %s\n", msg);
    else
        puts ("lvresize succeeded");
    g_free(msg);

    succ = bd_lvm_lvactivate ("newVG", "newLV", TRUE, &msg);
    if (!succ)
        g_printf ("lvactivate failed: %s\n", msg);
    else
        puts ("lvactivate succeeded");
    g_free(msg);

    succ = bd_lvm_lvdeactivate ("newVG", "newLV", &msg);
    if (!succ)
        g_printf ("lvdeactivate failed: %s\n", msg);
    else
        puts ("lvdeactivate succeeded");
    g_free(msg);

    return 0;
}
