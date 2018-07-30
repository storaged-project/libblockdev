#include <glib.h>

#ifndef BD_FS
#define BD_FS

GQuark bd_fs_error_quark (void);
#define BD_FS_ERROR bd_fs_error_quark ()
typedef enum {
    BD_FS_ERROR_INVAL,
    BD_FS_ERROR_PARSE,
    BD_FS_ERROR_FAIL,
    BD_FS_ERROR_NOFS,
    BD_FS_ERROR_PIPE,
    BD_FS_ERROR_UNMOUNT_FAIL,
    BD_FS_ERROR_NOT_SUPPORTED,
    BD_FS_ERROR_NOT_MOUNTED,
    BD_FS_ERROR_AUTH, // keep this entry last (XXX?)
    BD_FS_ERROR_TECH_UNAVAIL,
} BDFsError;

/* XXX: where the file systems start at the enum of technologies */
#define BD_FS_OFFSET 2
#define BD_FS_LAST_FS 7
typedef enum {
    BD_FS_TECH_GENERIC = 0,
    BD_FS_TECH_MOUNT   = 1,
    BD_FS_TECH_EXT2    = 2,
    BD_FS_TECH_EXT3    = 3,
    BD_FS_TECH_EXT4    = 4,
    BD_FS_TECH_XFS     = 5,
    BD_FS_TECH_VFAT    = 6,
    BD_FS_TECH_NTFS    = 7,
} BDFSTech;

/* XXX: number of the highest bit of all modes */
#define BD_FS_MODE_LAST 6
typedef enum {
    BD_FS_TECH_MODE_MKFS      = 1 << 0,
    BD_FS_TECH_MODE_WIPE      = 1 << 1,
    BD_FS_TECH_MODE_CHECK     = 1 << 2,
    BD_FS_TECH_MODE_REPAIR    = 1 << 3,
    BD_FS_TECH_MODE_SET_LABEL = 1 << 4,
    BD_FS_TECH_MODE_QUERY     = 1 << 5,
    BD_FS_TECH_MODE_RESIZE    = 1 << 6,
} BDFSTechMode;


/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * check_deps() - check plugin's dependencies, returning TRUE if satisfied
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_fs_check_deps (void);
gboolean bd_fs_init (void);
void bd_fs_close (void);

gboolean bd_fs_is_tech_avail (BDFSTech tech, guint64 mode, GError **error);

#endif  /* BD_FS */

#include "fs/ext.h"
#include "fs/generic.h"
#include "fs/mount.h"
#include "fs/ntfs.h"
#include "fs/vfat.h"
#include "fs/xfs.h"
