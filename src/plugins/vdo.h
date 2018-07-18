#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_VDO
#define BD_VDO

GQuark bd_vdo_error_quark (void);
#define BD_VDO_ERROR bd_vdo_error_quark ()
typedef enum {
    BD_VDO_ERROR_FAIL,
    BD_VDO_ERROR_PARSE,
    BD_VDO_ERROR_TECH_UNAVAIL,
    BD_VDO_ERROR_POLICY_INVAL,
} BDVDOError;

typedef enum {
    BD_VDO_TECH_VDO = 0,
} BDVDOTech;

typedef enum {
    BD_VDO_TECH_MODE_CREATE              = 1 << 0,
    BD_VDO_TECH_MODE_REMOVE              = 1 << 1,
    BD_VDO_TECH_MODE_MODIFY              = 1 << 2,
    BD_VDO_TECH_MODE_ACTIVATE_DEACTIVATE = 1 << 3,
    BD_VDO_TECH_MODE_START_STOP          = 1 << 4,
    BD_VDO_TECH_MODE_QUERY               = 1 << 5,
    BD_VDO_TECH_MODE_GROW                = 1 << 6,
} BDVDOTechMode;

typedef enum {
    BD_VDO_WRITE_POLICY_SYNC,
    BD_VDO_WRITE_POLICY_ASYNC,
    BD_VDO_WRITE_POLICY_AUTO,
    BD_VDO_WRITE_POLICY_UNKNOWN,
} BDVDOWritePolicy;

typedef struct BDVDOInfo {
    gchar *name;
    gchar *device;
    gboolean active;
    gboolean deduplication;
    gboolean compression;
    guint64 logical_size;
    guint64 physical_size;
    guint64 index_memory;
    BDVDOWritePolicy write_policy;
} BDVDOInfo;

typedef struct BDVDOStats {
    gint64 block_size;
    gint64 logical_block_size;
    gint64 physical_blocks;
    gint64 data_blocks_used;
    gint64 overhead_blocks_used;
    gint64 logical_blocks_used;
    gint64 used_percent;
    gint64 saving_percent;
    gdouble write_amplification_ratio;
} BDVDOStats;

void bd_vdo_info_free (BDVDOInfo *info);
BDVDOInfo* bd_vdo_info_copy (BDVDOInfo *info);

void bd_vdo_stats_free (BDVDOStats *stats);
BDVDOStats* bd_vdo_stats_copy (BDVDOStats *stats);

/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * check_deps() - check plugin's dependencies, returning TRUE if satisfied
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_vdo_check_deps (void);
gboolean bd_vdo_init (void);
void bd_vdo_close (void);

gboolean bd_vdo_is_tech_avail (BDVDOTech tech, guint64 mode, GError **error);

const gchar* bd_vdo_get_write_policy_str (BDVDOWritePolicy policy, GError **error);
BDVDOWritePolicy bd_vdo_get_write_policy_from_str (const gchar *policy_str, GError **error);

BDVDOInfo* bd_vdo_info (const gchar *name, GError **error);

gboolean bd_vdo_create (const gchar *name, const gchar *backing_device, guint64 logical_size, guint64 index_memory, gboolean compression, gboolean deduplication, BDVDOWritePolicy write_policy, const BDExtraArg **extra, GError **error);
gboolean bd_vdo_remove (const gchar *name, gboolean force, const BDExtraArg **extra, GError **error);

gboolean bd_vdo_change_write_policy (const gchar *name, BDVDOWritePolicy write_policy, const BDExtraArg **extra, GError **error);

gboolean bd_vdo_enable_compression (const gchar *name, const BDExtraArg **extra, GError **error);
gboolean bd_vdo_disable_compression (const gchar *name, const BDExtraArg **extra, GError **error);
gboolean bd_vdo_enable_deduplication (const gchar *name, const BDExtraArg **extra, GError **error);
gboolean bd_vdo_disable_deduplication (const gchar *name, const BDExtraArg **extra, GError **error);

gboolean bd_vdo_activate (const gchar *name, const BDExtraArg **extra, GError **error);
gboolean bd_vdo_deactivate (const gchar *name, const BDExtraArg **extra, GError **error);

gboolean bd_vdo_start (const gchar *name, gboolean rebuild, const BDExtraArg **extra, GError **error);
gboolean bd_vdo_stop (const gchar *name, gboolean force, const BDExtraArg **extra, GError **error);

gboolean bd_vdo_grow_logical (const gchar *name, guint64 size, const BDExtraArg **extra, GError **error);
gboolean bd_vdo_grow_physical (const gchar *name, const BDExtraArg **extra, GError **error);

BDVDOStats* bd_vdo_get_stats (const gchar *name, GError **error);
GHashTable* bd_vdo_get_stats_full (const gchar *name, GError **error);

#endif  /* BD_VDO */
