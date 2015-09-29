#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <math.h>

#include "sizes.h"

#define INT_FLOAT_EPS 1e-5
#define INT_EQ_FLOAT(i, f) (ABS ((f) - (i)) < INT_FLOAT_EPS)
#define INT_LT_FLOAT(i, f) ((i < f) && (ABS ((i) - (f)) > INT_FLOAT_EPS))
#define INT_GT_FLOAT(i, f) (((i) - (f)) > INT_FLOAT_EPS)

#define NUM_PREFIXES 7
static gchar const * const size_prefixes[NUM_PREFIXES] = {"", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei"};

GQuark bd_utils_size_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-utils-size-error-quark");
}

/**
 * get_unit_prefix_power: (skip)
 *
 * Returns: power of the @prefix or %NUM_PREFIXES if not found
 */
static guint8 get_unit_prefix_power (gchar *prefix) {
    guint i = 0;

    for (i = 0; size_prefixes[i]; i++)
        if (g_str_has_prefix (size_prefixes[i], prefix))
            return i;

    return NUM_PREFIXES;
}

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

    for (i=0; i < NUM_PREFIXES && INT_LT_FLOAT(1024, value); i++) {
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

/**
 * bd_utils_size_from_spec:
 * @spec: human readable size specification (e.g. "512 MiB")
 * @error: (out): place to store error (if any)
 *
 * Returns: number of bytes equal to the size specification rounded to bytes, if
 * 0, @error) may be set in case of error
 */
guint64 bd_utils_size_from_spec (gchar *spec, GError **error) {
    gchar const * const pattern = "^\\s*(\\d+\\.?\\d*)\\s*([kmgtpeKMGTPE]i?)[bB]";
    gchar const * const zero_pattern = "^\\s*0\\.?0*\\s*([kmgtpeKMGTPE]i?)?[bB]?$";
    GRegex *regex = NULL;
    GRegex *zero_regex = NULL;
    GMatchInfo *match_info = NULL;
    gboolean success = FALSE;
    gchar *num_str = NULL;
    gchar *prefix = NULL;
    guint64 inum = 0;
    gdouble fnum = 0.0;
    guint8 power = 0;

    regex = g_regex_new (pattern, 0, 0, error);
    if (!regex) {
        /* error is already populated */
        g_warning ("Failed to create new GRegex");
        return 0;
    }

    zero_regex = g_regex_new (zero_pattern, 0, 0, error);
    if (!zero_regex) {
        /* error is already populated */
        g_warning ("Failed to create new GRegex");
        return 0;
    }

    success = g_regex_match (regex, spec, 0, &match_info);
    if (!success) {
        success = g_regex_match (zero_regex, spec, 0, NULL);
        if (!success) {
            /* error */
            g_set_error (error, BD_UTILS_SIZE_ERROR, BD_UTILS_SIZE_ERROR_INVALID_SPEC,
                         "Failed to parse spec: %s", spec);
            g_regex_unref (regex);
            g_match_info_free (match_info);
        }
        /* just 0 */
        return 0;
    }
    g_regex_unref (regex);
    g_regex_unref (zero_regex);

    num_str = g_match_info_fetch (match_info, 1);
    prefix = g_match_info_fetch (match_info, 2);

    g_match_info_free (match_info);

    power = get_unit_prefix_power (prefix);
    if (power == NUM_PREFIXES) {
        g_set_error (error, BD_UTILS_SIZE_ERROR, BD_UTILS_SIZE_ERROR_INVALID_SPEC,
                     "Failed to recognize size prefix: %s", prefix);
        g_free (prefix);
        return 0;
    }

    if (strchr (num_str, '.') == NULL) {
        /* integer */
        inum = g_ascii_strtoull (num_str, NULL, 0);
        g_free (num_str);
        if (strchr (prefix, 'i')) {
            /* binary unit */
            g_free (prefix);
            return inum * ((guint64) pow (1024.0, (gdouble) power));
        } else {
            /* decimal unit */
            g_free (prefix);
            return inum * ((guint64) pow (1000.0, (gdouble) power));
        }
    } else {
        /* float number */
        fnum = g_ascii_strtod (num_str, NULL);
        g_free (num_str);
        if (strchr (prefix, 'i')) {
            /* binary unit */
            g_free (prefix);
            return (guint64) (fnum * (pow (1024.0, (gdouble) power)));
        } else {
            /* decimal unit */
            g_free (prefix);
            return (guint64) (fnum * (pow (1000.0, (gdouble) power)));
        }
    }
}
