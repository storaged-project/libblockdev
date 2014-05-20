int main (int argc, char **argv) {
    gint exit_status;
    const gchar **fname = NULL;
    gchar *msg = NULL;
    gchar *stdout = NULL;
    gchar *stderr = NULL;
    gboolean succ;
    guint64 result = 0;

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

    if (stdout)
        g_free (stdout);
    if (stderr)
        g_free (stderr);
    if (msg)
        g_free (msg);

    if (bd_lvm_is_supported_pe_size(16 MEBIBYTE))
        puts ("16 MiB PE: Supported.");
    else
        puts ("16 MiB PE: Unsupported.");

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

    return 0;
}
