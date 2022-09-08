#include <glib.h>
#include <glib-object.h>

#include "extra_arg.h"

/**
 * bd_extra_arg_copy:
 * @arg: (nullable): %BDExtraArg to copy
 *
 * Creates a new copy of @arg.
 */
BDExtraArg* bd_extra_arg_copy (BDExtraArg *arg) {
    if (arg == NULL)
        return NULL;

    BDExtraArg *ret = g_new0 (BDExtraArg, 1);
    ret->opt = g_strdup (arg->opt);
    ret->val = g_strdup (arg->val);

    return ret;
}

/**
 * bd_extra_arg_free:
 * @arg: (nullable): %BDExtraArg to free
 *
 * Frees @arg.
 */
void bd_extra_arg_free (BDExtraArg *arg) {
    if (arg == NULL)
        return;

    g_free (arg->opt);
    g_free (arg->val);
    g_free (arg);
}

/**
 * bd_extra_arg_list_free:
 * @args: (nullable) (array zero-terminated=1): A list of %BDExtraArg to free
 *
 * Frees @args and all its elements.
 */
void bd_extra_arg_list_free (BDExtraArg **args) {
    BDExtraArg **a;

    if (args == NULL)
        return;

    for (a = args; *a; a++)
        bd_extra_arg_free (*a);
    g_free (args);
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
 * Example of calling bd_fs_xfs_mkfs() with an extra argument.
 * This will result in calling `mkfs.xfs` with `-L label`.
 *
 * |[<!-- language="C" -->
 * BDExtraArg label_arg = {"-L", "label"};
 * const BDExtraArg *extra_args[2] = {&label_arg, NULL};
 *
 * ret = bd_fs_xfs_mkfs ("/dev/sda", extra_args, error);
 *
 * ]|
 *
 * Returns: (transfer full): a new extra argument
 */
BDExtraArg* bd_extra_arg_new (const gchar *opt, const gchar *val) {
    BDExtraArg *ret = g_new0 (BDExtraArg, 1);
    ret->opt = g_strdup (opt ? opt : "");
    ret->val = g_strdup (val ? val : "");

    return ret;
}
