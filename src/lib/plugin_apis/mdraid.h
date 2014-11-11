#include <glib.h>

/* BpG-skip */
#define BD_MD_ERROR bd_md_error_quark ()
typedef enum {
    BD_MD_ERROR_BASE,
} BDMDError;
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
 * @disks: (array zero-terminated=1) disks to use for the new RAID (including spares)
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
