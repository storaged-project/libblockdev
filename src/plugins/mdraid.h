#include <glib.h>

#ifndef BD_MD
#define BD_MD

GQuark bd_md_error_quark (void);
#define BD_MD_ERROR bd_md_error_quark ()
typedef enum {
    BD_MD_ERROR_PARSE,
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
} BDMDExamineData;

/**
 * bd_md_examine_data_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDMDExamineData* bd_md_examine_data_copy (BDMDExamineData *data) {
    BDMDExamineData *new_data = g_new (BDMDExamineData, 1);

    new_data->device = g_strdup (data->device);
    new_data->level = g_strdup (data->level);
    new_data->num_devices = data->num_devices;
    new_data->name = g_strdup (data->name);
    new_data->size = data->size;
    new_data->uuid = g_strdup (data->uuid);
    new_data->update_time = data->update_time;
    new_data->dev_uuid = g_strdup (data->dev_uuid);
    new_data->events = data->events;
    new_data->metadata = g_strdup (data->metadata);
    return new_data;
}

/**
 * bd_md_examine_data_free: (skip)
 *
 * Frees @data.
 */
void bd_md_examine_data_free (BDMDExamineData *data) {
    g_free (data->device);
    g_free (data->level);
    g_free (data->name);
    g_free (data->uuid);
    g_free (data->dev_uuid);
    g_free (data->metadata);
    g_free (data);
}

/* taken from blivet */
// these defaults were determined empirically
#define MD_SUPERBLOCK_SIZE (2 MiB)
#define MD_CHUNK_SIZE (512 KiB)

guint64 bd_md_get_superblock_size (guint64 size, gchar *version);
gboolean bd_md_create (gchar *device_name, gchar *level, gchar **disks, guint64 spares, gchar *version, gboolean bitmap, GError **error);
gboolean bd_md_destroy (gchar *device, GError **error);
gboolean bd_md_deactivate (gchar *device_name, GError **error);
gboolean bd_md_activate (gchar *device_name, gchar **members, gchar *uuid, GError **error);
gboolean bd_md_nominate (gchar *device, GError **error);
gboolean bd_md_denominate (gchar *device, GError **error);
gboolean bd_md_add (gchar *raid_name, gchar *device, guint64 raid_devs, GError **error);
gboolean bd_md_remove (gchar *raid_name, gchar *device, gboolean fail, GError **error);
BDMDExamineData* bd_md_examine (gchar *device, GError **error);
gchar* bd_md_canonicalize_uuid (gchar *uuid, GError **error);

#endif  /* BD_MD */
