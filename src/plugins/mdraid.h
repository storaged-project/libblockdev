#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_MD
#define BD_MD

#define MDADM_MIN_VERSION "3.3.2"

/* taken from blivet */
// these defaults were determined empirically
#define BD_MD_SUPERBLOCK_SIZE (2 MiB)
#define BD_MD_CHUNK_SIZE (512 KiB)

GQuark bd_md_error_quark (void);
#define BD_MD_ERROR bd_md_error_quark ()
typedef enum {
    BD_MD_ERROR_PARSE,
    BD_MD_ERROR_BAD_FORMAT,
    BD_MD_ERROR_NO_MATCH,
    BD_MD_ERROR_INVAL,
    BD_MD_ERROR_FAIL,
    BD_MD_ERROR_TECH_UNAVAIL,
} BDMDError;

typedef struct BDMDExamineData {
    gchar *device;
    gchar *level;
    guint64 num_devices;
    gchar *name;
    guint64 size;
    gchar *uuid;
    guint64 update_time;
    gchar *dev_uuid;
    guint64 events;
    gchar *metadata;
    guint64 chunk_size;
} BDMDExamineData;

BDMDExamineData* bd_md_examine_data_copy (BDMDExamineData *data);
void bd_md_examine_data_free (BDMDExamineData *data);

typedef struct BDMDDetailData {
    gchar *device;
    gchar *metadata;
    gchar *creation_time;
    gchar *level;
    gchar *name;
    guint64 array_size;
    guint64 use_dev_size;
    guint64 raid_devices;
    guint64 total_devices;
    guint64 active_devices;
    guint64 working_devices;
    guint64 failed_devices;
    guint64 spare_devices;
    gboolean clean;
    gchar *uuid;
} BDMDDetailData;

void bd_md_detail_data_free (BDMDDetailData *data);
BDMDDetailData* bd_md_detail_data_copy (BDMDDetailData *data);

typedef enum {
    BD_MD_TECH_MDRAID = 0,
} BDMDTech;

typedef enum {
    BD_MD_TECH_MODE_CREATE = 1 << 0,
    BD_MD_TECH_MODE_DELETE = 1 << 1,
    BD_MD_TECH_MODE_MODIFY = 1 << 2,
    BD_MD_TECH_MODE_QUERY  = 1 << 3,
} BDMDTechMode;


/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * check_deps() - check plugin's dependencies, returning TRUE if satisfied
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_md_check_deps (void);
gboolean bd_md_init (void);
void bd_md_close (void);

gboolean bd_md_is_tech_avail (BDMDTech tech, guint64 mode, GError **error);

guint64 bd_md_get_superblock_size (guint64 member_size, const gchar *version, GError **error);
gboolean bd_md_create (const gchar *raid_spec, const gchar *level, const gchar **disks, guint64 spares, const gchar *version, gboolean bitmap, guint64 chunk_size, const BDExtraArg **extra, GError **error);
gboolean bd_md_destroy (const gchar *device, GError **error);
gboolean bd_md_deactivate (const gchar *raid_spec, GError **error);
gboolean bd_md_activate (const gchar *raid_spec, const gchar **members, const gchar *uuid, gboolean start_degraded, const BDExtraArg **extra, GError **error);
gboolean bd_md_run (const gchar *raid_spec, GError **error);
gboolean bd_md_nominate (const gchar *device, GError **error);
gboolean bd_md_denominate (const gchar *device, GError **error);
gboolean bd_md_add (const gchar *raid_spec, const gchar *device, guint64 raid_devs, const BDExtraArg **extra, GError **error);
gboolean bd_md_remove (const gchar *raid_spec, const gchar *device, gboolean fail, const BDExtraArg **extra, GError **error);
BDMDExamineData* bd_md_examine (const gchar *device, GError **error);
BDMDDetailData* bd_md_detail (const gchar *raid_spec, GError **error);
gchar* bd_md_canonicalize_uuid (const gchar *uuid, GError **error);
gchar* bd_md_get_md_uuid (const gchar *uuid, GError **error);
gchar* bd_md_node_from_name (const gchar *name, GError **error);
gchar* bd_md_name_from_node (const gchar *node, GError **error);
gchar* bd_md_get_status (const gchar *raid_spec, GError **error);
gboolean bd_md_set_bitmap_location (const gchar *raid_spec, const gchar *location, GError **error);
gchar* bd_md_get_bitmap_location (const gchar *raid_spec, GError **error);
gboolean bd_md_request_sync_action (const gchar *raid_spec, const gchar *action, GError **error);

#endif  /* BD_MD */
