#include <glib.h>
#include <glib-object.h>

/* BpG-skip */
#ifndef BD_MD_API
#define BD_MD_API

#define BD_MD_ERROR bd_md_error_quark ()
typedef enum {
    BD_MD_ERROR_PARSE,
    BD_MD_ERROR_BAD_FORMAT,
    BD_MD_ERROR_NO_MATCH,
} BDMDError;

#define BD_MD_TYPE_EXAMINEDATA (bd_md_examine_data_get_type ())
GType bd_md_examine_data_get_type();

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

GType bd_md_examine_data_get_type () {
    static GType type = 0;

    if (G_UNLIKELY(type == 0)) {
        type = g_boxed_type_register_static("BDMDExamineData",
                                            (GBoxedCopyFunc) bd_md_examine_data_copy,
                                            (GBoxedFreeFunc) bd_md_examine_data_free);
    }

    return type;
}

#define BD_MD_TYPE_DETAILDATA (bd_md_detail_data_get_type ())
GType bd_md_detail_data_get_type();

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

/**
 * bd_md_detail_data_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDMDDetailData* bd_md_detail_data_copy (BDMDDetailData *data) {
    BDMDDetailData *new_data = g_new (BDMDDetailData, 1);

    new_data->device = g_strdup (data->device);
    new_data->name = g_strdup (data->name);
    new_data->metadata = g_strdup (data->metadata);
    new_data->creation_time = g_strdup (data->creation_time);
    new_data->level = g_strdup (data->level);
    new_data->array_size = data->array_size;
    new_data->use_dev_size = data->use_dev_size;
    new_data->raid_devices = data->raid_devices;
    new_data->active_devices = data->active_devices;
    new_data->working_devices = data->working_devices;
    new_data->failed_devices = data->failed_devices;
    new_data->spare_devices = data->spare_devices;
    new_data->clean = data->clean;
    new_data->uuid = g_strdup (data->uuid);

    return new_data;
}

/**
 * bd_md_detail_data_free: (skip)
 *
 * Frees @data.
 */
void bd_md_detail_data_free (BDMDDetailData *data) {
    g_free (data->device);
    g_free (data->name);
    g_free (data->metadata);
    g_free (data->creation_time);
    g_free (data->level);
    g_free (data->uuid);

    g_free (data);
}

GType bd_md_detail_data_get_type () {
    static GType type = 0;

    if (G_UNLIKELY(type == 0)) {
        type = g_boxed_type_register_static("BDMDDetailData",
                                            (GBoxedCopyFunc) bd_md_detail_data_copy,
                                            (GBoxedFreeFunc) bd_md_detail_data_free);
    }

    return type;
}

/* BpG-skip-end */

/**
 * bd_md_get_superblock_size:
 * @size: size of the array
 * @version: (allow-none): metadata version or %NULL to use the current default version
 *
 * Returns: Calculated superblock size for given array @size and metadata @version
 * or default if unsupported @version is used.
 */
guint64 bd_md_get_superblock_size (guint64 size, gchar *version);

/**
 * bd_md_create:
 * @device_name: name of the device to create
 * @level: RAID level (as understood by mdadm, see mdadm(8))
 * @disks: (array zero-terminated=1): disks to use for the new RAID (including spares)
 * @spares: number of spare devices
 * @version: (allow-none): metadata version
 * @bitmap: whether to create an internal bitmap on the device or not
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the new MD RAID device @device_name was successfully created or not
 */
gboolean bd_md_create (gchar *device_name, gchar *level, gchar **disks, guint64 spares, gchar *version, gboolean bitmap, GError **error);

/**
 * bd_md_destroy:
 * @device: device to destroy MD RAID metadata on
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the MD RAID metadata was successfully destroyed on @device or not
 */
gboolean bd_md_destroy (gchar *device, GError **error);

/**
 * bd_md_deactivate:
 * @device_name: name of the RAID device to deactivate
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the RAID device @device_name was successfully deactivated or not
 */
gboolean bd_md_deactivate (gchar *device_name, GError **error);

/**
 * bd_md_activate:
 * @device_name: name of the RAID device to activate
 * @members: (allow-none) (array zero-terminated=1): member devices to be considered for @device activation
 * @uuid: (allow-none): UUID (in the MD RAID format!) of the MD RAID to activate
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the MD RAID @device was successfully activated or not
 *
 * Note: either @members or @uuid (or both) have to be specified.
 */
gboolean bd_md_activate (gchar *device_name, gchar **members, gchar *uuid, GError **error);

/**
 * bd_md_nominate:
 * @device: device to nominate (add to its appropriate RAID) as a MD RAID device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully nominated (added to its
 * appropriate RAID) or not
 *
 * Note: may start the MD RAID if it becomes ready by adding @device.
 */
gboolean bd_md_nominate (gchar *device, GError **error);

/**
 * bd_md_denominate:
 * @device: device to denominate (remove from its appropriate RAID) as a MD RAID device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully denominated (added to its
 * appropriate RAID) or not
 *
 * Note: may start the MD RAID if it becomes ready by adding @device.
 */
gboolean bd_md_denominate (gchar *device, GError **error);

/**
 * bd_md_add:
 * @raid_name: name of the RAID device to add @device into
 * @device: name of the device to add to the @raid_name RAID device
 * @raid_devs: number of devices the @raid_name RAID should actively use (see
 *             below) or 0 to leave unspecified
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully added to the @raid_name RAID or
 * not
 *
 * The @raid_devs parameter is used when adding devices to a raid array that has
 * no actual redundancy. In this case it is necessary to explicitly grow the
 * array all at once rather than manage it in the sense of adding spares.
 *
 * Whether the new device will be added as a spare or an active member is
 * decided by mdadm.
 */
gboolean bd_md_add (gchar *raid_name, gchar *device, guint64 raid_devs, GError **error);

/**
 * bd_md_remove:
 * @raid_name: name of the RAID device to remove @device from
 * @device: device to remove from the @raid_name RAID
 * @fail: whether to mark the @device as failed before removing
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully removed from the @raid_name
 * RAID or not.
 */
gboolean bd_md_remove (gchar *raid_name, gchar *device, gboolean fail, GError **error);

/**
 * bd_md_examine:
 * @device: name of the device (a member of an MD RAID) to examine
 * @error: (out): place to store error (if any)
 *
 * Returns: information about the MD RAID extracted from the @device
 */
BDMDExamineData* bd_md_examine (gchar *device, GError **error);

/**
 * bd_md_canonicalize_uuid:
 * @uuid: UUID to canonicalize
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): cannonicalized form of @uuid
 *
 * This function expects a UUID in the form that mdadm returns. The change is as
 * follows: 3386ff85:f5012621:4a435f06:1eb47236 -> 3386ff85-f501-2621-4a43-5f061eb47236
 */
gchar* bd_md_canonicalize_uuid (gchar *uuid, GError **error);

/**
 * bd_md_get_md_uuid:
 * @uuid: UUID to transform into format used by MD RAID
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): transformed form of @uuid
 *
 * This function expects a UUID in the canonical (traditional format) and
 * returns a UUID in the format used by MD RAID and is thus reverse to
 * bd_md_canonicalize_uuid(). The change is as follows:
 * 3386ff85-f501-2621-4a43-5f061eb47236 -> 3386ff85:f5012621:4a435f06:1eb47236
 */
gchar* bd_md_get_md_uuid (gchar *uuid, GError **error);

/**
 * bd_md_detail:
 * @raid_name: name of the MD RAID to examine
 * @error: (out): place to store error (if any)
 *
 * Returns: information about the MD RAID @raid_name
 */
BDMDDetailData* bd_md_detail (gchar *raid_name, GError **error);

/**
 * bd_md_node_from_name:
 * @name: name of the MD RAID
 * @error: (out): place to store error (if any)
 *
 * Returns: path to the @name MD RAID's device node or %NULL in case of error
 */
gchar* bd_md_node_from_name (gchar *name, GError **error);

/**
 * bd_md_name_from_node:
 * @node: path of the MD RAID's device node
 * @error: (out): place to store error (if any)
 *
 * Returns: @name of the MD RAID the device node belongs to or %NULL in case of error
 */
gchar* bd_md_name_from_node (gchar *node, GError **error);

#endif  /* BD_MD_API */
