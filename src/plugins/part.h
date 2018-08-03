#include <glib.h>

#ifndef BD_PART
#define BD_PART

GQuark bd_part_error_quark (void);
#define BD_PART_ERROR bd_part_error_quark ()
typedef enum {
    BD_PART_ERROR_EXISTS,
    BD_PART_ERROR_INVAL,
    BD_PART_ERROR_FAIL,
    BD_PART_ERROR_TECH_UNAVAIL,
} BDPartError;

typedef enum {
    BD_PART_TABLE_MSDOS = 0,
    BD_PART_TABLE_GPT,
    BD_PART_TABLE_UNDEF
} BDPartTableType;

typedef enum {
    BD_PART_DISK_FLAG_GPT_PMBR_BOOT = 1
} BDPartDiskFlag;


/**
 * BDPartFlag:
 *
 * Partition flags supported by libblockdev. First part of the flags (up to
 * @BD_PART_FLAG_BASIC_LAST) corresponds to the flags supported by libparted
 * (see https://www.gnu.org/software/parted/manual/parted.html#set). Second
 * part corresponds to the flags supported by sgdisk (GPT, see `sgdisk -A=list`).
 *
 * The only exception from the above is @BD_PART_FLAG_LEGACY_BOOT which is
 * supported by libparted too but is GPT specific.
 *
 */
typedef enum {
    BD_PART_FLAG_BOOT = 1 << 1,
    BD_PART_FLAG_ROOT = 1 << 2,
    BD_PART_FLAG_SWAP = 1 << 3,
    BD_PART_FLAG_HIDDEN = 1 << 4,
    BD_PART_FLAG_RAID = 1 << 5,
    BD_PART_FLAG_LVM = 1 << 6,
    BD_PART_FLAG_LBA = 1 << 7,
    BD_PART_FLAG_HPSERVICE = 1 << 8,
    BD_PART_FLAG_CPALO = 1 << 9,
    BD_PART_FLAG_PREP = 1 << 10,
    BD_PART_FLAG_MSFT_RESERVED = 1 << 11,
    BD_PART_FLAG_BIOS_GRUB = 1 << 12,
    BD_PART_FLAG_APPLE_TV_RECOVERY = 1 << 13,
    BD_PART_FLAG_DIAG = 1 << 14,
    BD_PART_FLAG_LEGACY_BOOT = 1 << 15,
    BD_PART_FLAG_MSFT_DATA = 1 << 16,
    BD_PART_FLAG_IRST = 1 << 17,
    BD_PART_FLAG_ESP = 1 << 18,
    BD_PART_FLAG_BASIC_LAST = 1 << 19,
    BD_PART_FLAG_GPT_SYSTEM_PART = 1 << 25,
    BD_PART_FLAG_GPT_READ_ONLY = 1 << 26,
    BD_PART_FLAG_GPT_HIDDEN = 1 << 27,
    BD_PART_FLAG_GPT_NO_AUTOMOUNT = 1 << 28
} BDPartFlag;

typedef enum {
    BD_PART_TYPE_NORMAL    = 0x00,
    BD_PART_TYPE_LOGICAL   = 0x01,
    BD_PART_TYPE_EXTENDED  = 0x02,
    BD_PART_TYPE_FREESPACE = 0x04,
    BD_PART_TYPE_METADATA  = 0x08,
    BD_PART_TYPE_PROTECTED = 0x10
} BDPartType;

typedef enum {
    BD_PART_TYPE_REQ_NORMAL   = 0x00,
    BD_PART_TYPE_REQ_LOGICAL  = 0x01,
    BD_PART_TYPE_REQ_EXTENDED = 0x02,
    BD_PART_TYPE_REQ_NEXT     = 0x04
} BDPartTypeReq;

typedef enum {
    BD_PART_ALIGN_MINIMAL,
    BD_PART_ALIGN_OPTIMAL,
    BD_PART_ALIGN_NONE
} BDPartAlign;

typedef struct BDPartSpec {
    gchar *path;
    gchar *name;
    gchar *type_guid;
    guint64 type;
    guint64 start;
    guint64 size;
    guint64 flags;
} BDPartSpec;

BDPartSpec* bd_part_spec_copy (BDPartSpec *data);
void bd_part_spec_free (BDPartSpec *data);

typedef struct BDPartDiskSpec {
    gchar *path;
    BDPartTableType table_type;
    guint64 size;
    guint64 sector_size;
    guint64 flags;
} BDPartDiskSpec;

BDPartDiskSpec* bd_part_disk_spec_copy (BDPartDiskSpec *data);
void bd_part_disk_spec_free (BDPartDiskSpec *data);

typedef enum {
    BD_PART_TECH_MBR = 0,
    BD_PART_TECH_GPT,
} BDPartTech;

typedef enum {
    BD_PART_TECH_MODE_CREATE_TABLE = 1 << 0,
    BD_PART_TECH_MODE_MODIFY_TABLE = 1 << 1,
    BD_PART_TECH_MODE_QUERY_TABLE  = 1 << 2,
    BD_PART_TECH_MODE_MODIFY_PART  = 1 << 3,
    BD_PART_TECH_MODE_QUERY_PART   = 1 << 4,
} BDPartTechMode;

/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * check_deps() - check plugin's dependencies, returning TRUE if satisfied
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_part_check_deps (void);
gboolean bd_part_init (void);
void bd_part_close (void);

gboolean bd_part_is_tech_avail (BDPartTech tech, guint64 mode, GError **error);

gboolean bd_part_create_table (const gchar *disk, BDPartTableType type, gboolean ignore_existing, GError **error);

BDPartSpec* bd_part_get_part_spec (const gchar *disk, const gchar *part, GError **error);
BDPartSpec* bd_part_get_part_by_pos (const gchar *disk, guint64 position, GError **error);
BDPartDiskSpec* bd_part_get_disk_spec (const gchar *disk, GError **error);
BDPartSpec** bd_part_get_disk_parts (const gchar *disk, GError **error);
BDPartSpec** bd_part_get_disk_free_regions (const gchar *disk, GError **error);
BDPartSpec* bd_part_get_best_free_region (const gchar *disk, BDPartType type, guint64 size, GError **error);

BDPartSpec* bd_part_create_part (const gchar *disk, BDPartTypeReq type, guint64 start, guint64 size, BDPartAlign align, GError **error);
gboolean bd_part_delete_part (const gchar *disk, const gchar *part, GError **error);
gboolean bd_part_resize_part (const gchar *disk, const gchar *part, guint64 size, BDPartAlign align, GError **error);

gboolean bd_part_set_part_flag (const gchar *disk, const gchar *part, BDPartFlag flag, gboolean state, GError **error);
gboolean bd_part_set_disk_flag (const gchar *disk, BDPartDiskFlag flag, gboolean state, GError **error);
gboolean bd_part_set_part_flags (const gchar *disk, const gchar *part, guint64 flags, GError **error);
gboolean bd_part_set_part_name (const gchar *disk, const gchar *part, const gchar *name, GError **error);
gboolean bd_part_set_part_type (const gchar *disk, const gchar *part, const gchar *type_guid, GError **error);
gboolean bd_part_set_part_id (const gchar *disk, const gchar *part, const gchar *part_id, GError **error);
gchar* bd_part_get_part_id (const gchar *disk, const gchar *part, GError **error);

const gchar* bd_part_get_part_table_type_str (BDPartTableType type, GError **error);
const gchar* bd_part_get_flag_str (BDPartFlag flag, GError **error);
const gchar* bd_part_get_type_str (BDPartType type, GError **error);

#endif  /* BD_PART */
