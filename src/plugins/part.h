#include <glib.h>

#ifndef BD_PART
#define BD_PART

GQuark bd_part_error_quark (void);
#define BD_PART_ERROR bd_part_error_quark ()
typedef enum {
    BD_PART_ERROR_TECH_UNAVAIL,
    BD_PART_ERROR_FAIL,
    BD_PART_ERROR_INVAL,
    BD_PART_ERROR_EXISTS,
} BDPartError;

typedef enum {
    BD_PART_TABLE_MSDOS,
    BD_PART_TABLE_GPT,
    BD_PART_TABLE_UNDEF,
} BDPartTableType;

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
    BD_PART_ALIGN_NONE,
    BD_PART_ALIGN_MINIMAL,
    BD_PART_ALIGN_OPTIMAL,
} BDPartAlign;

typedef struct BDPartSpec {
    gchar *path;
    gchar *name;
    gchar *uuid;
    gchar *id;
    gchar *type_guid;
    guint64 type;
    guint64 start;
    guint64 size;
    gboolean bootable;
    guint64 attrs;
} BDPartSpec;

BDPartSpec* bd_part_spec_copy (BDPartSpec *data);
void bd_part_spec_free (BDPartSpec *data);

typedef struct BDPartDiskSpec {
    gchar *path;
    BDPartTableType table_type;
    guint64 size;
    guint64 sector_size;
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
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
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

gboolean bd_part_set_part_name (const gchar *disk, const gchar *part, const gchar *name, GError **error);
gboolean bd_part_set_part_type (const gchar *disk, const gchar *part, const gchar *type_guid, GError **error);
gboolean bd_part_set_part_id (const gchar *disk, const gchar *part, const gchar *part_id, GError **error);
gboolean bd_part_set_part_bootable (const gchar *disk, const gchar *part, gboolean bootable, GError **error);
gboolean bd_part_set_part_attributes (const gchar *disk, const gchar *part, guint64 attrs, GError **error);
gboolean bd_part_set_part_uuid (const gchar *disk, const gchar *part, const gchar *uuid, GError **error);

const gchar* bd_part_get_part_table_type_str (BDPartTableType type, GError **error);
const gchar* bd_part_get_type_str (BDPartType type, GError **error);

#endif  /* BD_PART */
