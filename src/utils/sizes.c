#include <sizes.h>
#include <glib.h>

#include <stdio.h>

#define INT_FLOAT_EPS 1e-5
#define INT_EQ_FLOAT(i, f) (ABS ((f) - (i)) < INT_FLOAT_EPS)
#define INT_LT_FLOAT(i, f) (((i) - (f)) < INT_FLOAT_EPS)
#define INT_GT_FLOAT(i, f) (((i) - (f)) > INT_FLOAT_EPS)

static gchar const * const size_prefixes[] = {"", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei", NULL};

/**
 * bd_utils_size_human_readable:
 * @size: size to get human readable representation of
 *
 * Returns: human readable representation of the given @size
 */
gchar* bd_utils_size_human_readable (guint64 size) {
    guint8 i = 0;
    gdouble value = (gdouble) size;
    gdouble prev_value = (gdouble) value;

    for (i=0; size_prefixes[i] && INT_LT_FLOAT(1024, value); i++) {
        prev_value = value;
        value = value / 1024.0;
    }

    if (INT_GT_FLOAT(10, value)) {
        value = prev_value;
        i = i - 1;
    }

    if (INT_EQ_FLOAT (value, (guint64) value))
        return g_strdup_printf ("%"G_GUINT64_FORMAT" %sB", (guint64) value, size_prefixes[i]);
    else
        return g_strdup_printf ("%.2f %sB", value, size_prefixes[i]);
}
