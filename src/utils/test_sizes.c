int main (int argc, char **argv) {
    gchar *human = NULL;
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

    return 0;
}
