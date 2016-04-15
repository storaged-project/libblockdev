#include <glib.h>
#include <glib-object.h>

#include "extra_arg.h"

/**
 * bd_extra_arg_copy:
 *
 * Creates a new copy of @arg.
 */
BDExtraArg* bd_extra_arg_copy (BDExtraArg *arg) {
    BDExtraArg *ret = g_new0 (BDExtraArg, 1);
    ret->opt = g_strdup (arg->opt);
    ret->val = g_strdup (arg->val);

    return ret;
}

/**
 * bd_extra_arg_free:
 *
 * Frees @arg.
 */
void bd_extra_arg_free (BDExtraArg *arg) {
    g_free (arg->opt);
    g_free (arg->val);
    g_free (arg);
}

GType bd_extra_arg_get_type (void) {
    static GType type = 0;

    if (G_UNLIKELY (!type))
        type = g_boxed_type_register_static ("BDExtraArg",
                                             (GBoxedCopyFunc) bd_extra_arg_copy,
                                             (GBoxedFreeFunc) bd_extra_arg_free);

    return type;
}

/**
 * bd_extra_arg_new: (constructor)
 * @opt: extra option
 * @val: value for the extra option @opt
 *
 * Returns: (transfer full): a new extra argument
 */
BDExtraArg* bd_extra_arg_new (gchar *opt, gchar *val) {
    BDExtraArg *ret = g_new0 (BDExtraArg, 1);
    ret->opt = g_strdup (opt);
    ret->val = g_strdup (val);

    return ret;
}
