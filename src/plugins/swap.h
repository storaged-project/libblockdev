#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_SWAP
#define BD_SWAP

#define MKSWAP_MIN_VERSION "2.23.2"

GQuark bd_swap_error_quark (void);
#define BD_SWAP_ERROR bd_swap_error_quark ()
typedef enum {
    BD_SWAP_ERROR_UNKNOWN_STATE,
    BD_SWAP_ERROR_ACTIVATE,
    BD_SWAP_ERROR_TECH_UNAVAIL,
} BDSwapError;

typedef enum {
    BD_SWAP_TECH_SWAP = 0,
} BDSwapTech;

typedef enum {
    BD_SWAP_TECH_MODE_CREATE              = 1 << 0,
    BD_SWAP_TECH_MODE_ACTIVATE_DEACTIVATE = 1 << 1,
    BD_SWAP_TECH_MODE_QUERY               = 1 << 2,
    BD_SWAP_TECH_MODE_SET_LABEL           = 1 << 3,
} BDSwapTechMode;

/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * check_deps() - check plugin's dependencies, returning TRUE if satisfied
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_swap_check_deps ();
gboolean bd_swap_init ();
void bd_swap_close ();

gboolean bd_swap_is_tech_avail (BDSwapTech tech, guint64 mode, GError **error);

gboolean bd_swap_mkswap (const gchar *device, const gchar *label, const BDExtraArg **extra, GError **error);
gboolean bd_swap_swapon (const gchar *device, gint priority, GError **error);
gboolean bd_swap_swapoff (const gchar *device, GError **error);
gboolean bd_swap_swapstatus (const gchar *device, GError **error);
gboolean bd_swap_set_label (const gchar *device, const gchar *label, GError **error);

#endif  /* BD_SWAP */
