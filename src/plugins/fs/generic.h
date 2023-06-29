#include <glib.h>

#ifndef BD_FS_GENERIC
#define BD_FS_GENERIC

gboolean bd_fs_wipe (const gchar *device, gboolean all, gboolean force, GError **error) ;
gboolean bd_fs_clean (const gchar *device, gboolean force, GError **error);
gchar* bd_fs_get_fstype (const gchar *device,  GError **error);

gboolean bd_fs_freeze (const gchar *mountpoint, GError **error);
gboolean bd_fs_unfreeze (const gchar *mountpoint, GError **error);

typedef enum {
    BD_FS_MKFS_LABEL     = 1 << 0,
    BD_FS_MKFS_UUID      = 1 << 1,
    BD_FS_MKFS_DRY_RUN   = 1 << 2,
    BD_FS_MKFS_NODISCARD = 1 << 3,
    BD_FS_MKFS_FORCE     = 1 << 4,
    BD_FS_MKFS_NOPT      = 1 << 5,
} BDFSMkfsOptionsFlags;

typedef struct BDFSMkfsOptions {
    const gchar *label;
    const gchar *uuid;
    gboolean dry_run;
    gboolean no_discard;
    gboolean force;
    gboolean no_pt;
    guint8 reserve[32];
} BDFSMkfsOptions;

BDFSMkfsOptions* bd_fs_mkfs_options_copy (BDFSMkfsOptions *data);
void bd_fs_mkfs_options_free (BDFSMkfsOptions *data);

const gchar** bd_fs_supported_filesystems (GError **error);

gboolean bd_fs_mkfs (const gchar *device, const gchar *fstype, BDFSMkfsOptions *options, const BDExtraArg **extra, GError **error);

gboolean bd_fs_resize (const gchar *device, guint64 new_size, const gchar *fstype, GError **error);
gboolean bd_fs_repair (const gchar *device, const gchar *fstype, GError **error);
gboolean bd_fs_check (const gchar *device, const gchar *fstype, GError **error);
gboolean bd_fs_set_label (const gchar *device, const gchar *label, const gchar *fstype, GError **error);
gboolean bd_fs_check_label (const gchar *fstype, const gchar *label, GError **error);
gboolean bd_fs_set_uuid (const gchar *device, const gchar *uuid, const gchar *fstype, GError **error);
gboolean bd_fs_check_uuid (const gchar *fstype, const gchar *uuid, GError **error);
guint64 bd_fs_get_size (const gchar *device, const gchar *fstype, GError **error);
guint64 bd_fs_get_free_space (const gchar *device, const gchar *fstype, GError **error);

typedef enum {
    BD_FS_OFFLINE_SHRINK = 1 << 1,
    BD_FS_OFFLINE_GROW = 1 << 2,
    BD_FS_ONLINE_SHRINK = 1 << 3,
    BD_FS_ONLINE_GROW = 1 << 4
} BDFSResizeFlags;

typedef enum {
    BD_FS_SUPPORT_SET_LABEL = 1 << 1,
    BD_FS_SUPPORT_SET_UUID = 1 << 2
} BDFSConfigureFlags;

typedef enum {
    BD_FS_FSCK_CHECK = 1 << 1,
    BD_FS_FSCK_REPAIR = 1 << 2
} BDFSFsckFlags;

typedef enum {
    BD_FS_FEATURE_OWNERS  = 1 << 1,
    BD_FS_FEATURE_PARTITION_TABLE = 1 << 2,
} BDFSFeatureFlags;

typedef struct BDFSFeatures {
    BDFSResizeFlags resize;
    BDFSMkfsOptionsFlags mkfs;
    BDFSFsckFlags fsck;
    BDFSConfigureFlags configure;
    BDFSFeatureFlags features;
    const gchar *partition_id;
    const gchar *partition_type;
} BDFSFeatures;

BDFSFeatures* bd_fs_features_copy (BDFSFeatures *data);
void bd_fs_features_free (BDFSFeatures *data);

const BDFSFeatures* bd_fs_features (const gchar *fstype, GError **error);

gboolean bd_fs_can_mkfs (const gchar *type, BDFSMkfsOptionsFlags *options, gchar **required_utility, GError **error);
gboolean bd_fs_can_resize (const gchar *type, BDFSResizeFlags *mode, gchar **required_utility, GError **error);
gboolean bd_fs_can_check (const gchar *type, gchar **required_utility, GError **error);
gboolean bd_fs_can_repair (const gchar *type, gchar **required_utility, GError **error);
gboolean bd_fs_can_set_label (const gchar *type, gchar **required_utility, GError **error);
gboolean bd_fs_can_set_uuid (const gchar *type, gchar **required_utility, GError **error);
gboolean bd_fs_can_get_size (const gchar *type, gchar **required_utility, GError **error);
gboolean bd_fs_can_get_free_space (const gchar *type, gchar **required_utility, GError **error);

#endif  /* BD_FS_GENERIC */
