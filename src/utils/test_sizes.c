#include "sizes.c"
#include <glib/gprintf.h>

int main () {
    gchar *human = NULL;
    GError *error = NULL;

    human = bd_utils_size_human_readable ((16 MiB));
    puts (human);
    g_free(human);
    human = bd_utils_size_human_readable ((9 KiB));
    puts (human);
    g_free(human);
    human = bd_utils_size_human_readable ((8 EiB));
    puts (human);
    g_free(human);
    human = bd_utils_size_human_readable ((12 EiB));
    puts (human);
    g_free(human);
    human = bd_utils_size_human_readable ((16.4356 GiB));
    puts (human);
    g_free(human);

    g_printf ("10 KiB == %"G_GUINT64_FORMAT"\n", bd_utils_size_from_spec ("10 KiB", &error));
    if (error)
        puts (error->message);
    g_clear_error (&error);

    g_printf ("10 KB == %"G_GUINT64_FORMAT"\n", bd_utils_size_from_spec ("10 KB", &error));
    if (error)
        puts (error->message);
    g_clear_error (&error);

    g_printf ("5 MiB == %"G_GUINT64_FORMAT"\n", bd_utils_size_from_spec ("5 MiB", &error));
    if (error)
        puts (error->message);
    g_clear_error (&error);

    g_printf ("5 MB == %"G_GUINT64_FORMAT"\n", bd_utils_size_from_spec ("5 MB", &error));
    if (error)
        puts (error->message);
    g_clear_error (&error);

    g_printf ("3.2 MiB == %"G_GUINT64_FORMAT"\n", bd_utils_size_from_spec ("3.2 MiB", &error));
    if (error)
        puts (error->message);
    g_clear_error (&error);

    g_printf ("3.2 MB == %"G_GUINT64_FORMAT"\n", bd_utils_size_from_spec ("3.2 MB", &error));
    if (error)
        puts (error->message);
    g_clear_error (&error);

    g_printf ("0 MiB == %"G_GUINT64_FORMAT"\n", bd_utils_size_from_spec ("0 MiB", &error));
    if (error)
        puts (error->message);
    g_clear_error (&error);

    g_printf ("0 == %"G_GUINT64_FORMAT"\n", bd_utils_size_from_spec ("0", &error));
    if (error)
        puts (error->message);
    g_clear_error (&error);

    g_printf ("0.00 == %"G_GUINT64_FORMAT"\n", bd_utils_size_from_spec ("0.00", &error));
    if (error)
        puts (error->message);
    g_clear_error (&error);

    g_printf ("3 XiB == %"G_GUINT64_FORMAT"\n", bd_utils_size_from_spec ("3 XiB", &error));
    if (error)
        puts (error->message);
    g_clear_error (&error);

    return 0;
}
