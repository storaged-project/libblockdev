#include <glib.h>
#include <glib-object.h>

#ifndef BD_UTILS_EXTRA_ARG
#define BD_UTILS_EXTRA_ARG

#define BD_UTIL_TYPE_EXTRA_ARG (bd_extra_arg_get_type ())
GType bd_extra_arg_get_type ();

typedef struct BDExtraArg {
    gchar *opt;
    gchar *val;
} BDExtraArg;

BDExtraArg* bd_extra_arg_copy (BDExtraArg *arg);
void bd_extra_arg_free (BDExtraArg *arg);
BDExtraArg* bd_extra_arg_new (gchar *opt, gchar *val);

#endif  /* BD_UTILS_EXTRA_ARG */
