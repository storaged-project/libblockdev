int main (int argc, char* argv[]) {
    bd_init(NULL);
    g_printf ("max LV size: %"G_GUINT64_FORMAT"\n", bd_lvm_get_max_lv_size());

    return 0;
}
