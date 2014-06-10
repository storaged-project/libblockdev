#include <glib/gprintf.h>

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

    return 0;
}
