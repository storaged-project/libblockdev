#include <glib.h>
#include <glib-object.h>

#ifndef BD_UTILS_EXTRA_ARG
#define BD_UTILS_EXTRA_ARG

#define BD_UTIL_TYPE_EXTRA_ARG (bd_extra_arg_get_type ())
GType bd_extra_arg_get_type (void);

/**
 * BDExtraArg:
 * @opt: extra option (command line option for most functions that allow extra options
 *       to be passed, e.g. "-L" to call `mkfs.xfs -L`)
 * @val: value for @opt, can be an empty string or %NULL for options without parameter
 *
 * See bd_extra_arg_new() for an example on how to construct the extra args.
 */
typedef struct BDExtraArg {
    gchar *opt;
    gchar *val;
} BDExtraArg;

BDExtraArg* bd_extra_arg_copy (BDExtraArg *arg);
void bd_extra_arg_free (BDExtraArg *arg);
void bd_extra_arg_list_free (BDExtraArg **args);
BDExtraArg* bd_extra_arg_new (const gchar *opt, const gchar *val);

#endif  /* BD_UTILS_EXTRA_ARG */
