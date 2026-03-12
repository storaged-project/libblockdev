#include <glib.h>
#include <blkid.h>

#ifndef BD_FS_COMMON
#define BD_FS_COMMON

/* "C" locale to get the locale-agnostic error messages */
#define _C_LOCALE (locale_t) 0

gint synced_close (gint fd);
gboolean get_uuid_label (const gchar *device, gchar **uuid, gchar **label, GError **error);
gboolean check_uuid (const gchar *uuid, GError **error);

void _fs_ext_reset_avail_deps (void);
void _fs_xfs_reset_avail_deps (void);
void _fs_vfat_reset_avail_deps (void);
void _fs_ntfs_reset_avail_deps (void);
void _fs_exfat_reset_avail_deps (void);
void _fs_btrfs_reset_avail_deps (void);
void _fs_udf_reset_avail_deps (void);
void _fs_f2fs_reset_avail_deps (void);
void _fs_nilfs_reset_avail_deps (void);

#endif  /* BD_FS_COMMON */
