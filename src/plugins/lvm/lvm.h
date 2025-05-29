#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_LVM
#define BD_LVM

GQuark bd_lvm_error_quark (void);
#define BD_LVM_ERROR bd_lvm_error_quark ()
typedef enum {
    BD_LVM_ERROR_TECH_UNAVAIL,
    BD_LVM_ERROR_FAIL,
    BD_LVM_ERROR_PARSE,
    BD_LVM_ERROR_NOEXIST,
    BD_LVM_ERROR_DM_ERROR,
    BD_LVM_ERROR_NOT_ROOT,
    BD_LVM_ERROR_CACHE_INVAL,
    BD_LVM_ERROR_CACHE_NOCACHE,
    BD_LVM_ERROR_NOT_SUPPORTED,
    BD_LVM_ERROR_VDO_POLICY_INVAL,
    BD_LVM_ERROR_DEVICES_DISABLED,
} BDLVMError;

typedef enum {
    BD_LVM_CACHE_POOL_STRIPED =  1 << 0,
    BD_LVM_CACHE_POOL_RAID1 =    1 << 1,
    BD_LVM_CACHE_POOL_RAID5 =    1 << 2,
    BD_LVM_CACHE_POOL_RAID6 =    1 << 3,
    BD_LVM_CACHE_POOL_RAID10 =   1 << 4,

    BD_LVM_CACHE_POOL_META_STRIPED =  1 << 10,
    BD_LVM_CACHE_POOL_META_RAID1 =    1 << 11,
    BD_LVM_CACHE_POOL_META_RAID5 =    1 << 12,
    BD_LVM_CACHE_POOL_META_RAID6 =    1 << 13,
    BD_LVM_CACHE_POOL_META_RAID10 =   1 << 14,
} BDLVMCachePoolFlags;

typedef enum {
    BD_LVM_CACHE_MODE_UNKNOWN,
    BD_LVM_CACHE_MODE_WRITETHROUGH,
    BD_LVM_CACHE_MODE_WRITEBACK,
} BDLVMCacheMode;

typedef enum {
    BD_LVM_VDO_MODE_UNKNOWN,
    BD_LVM_VDO_MODE_RECOVERING,
    BD_LVM_VDO_MODE_READ_ONLY,
    BD_LVM_VDO_MODE_NORMAL,
} BDLVMVDOOperatingMode;

typedef enum {
    BD_LVM_VDO_COMPRESSION_UNKNOWN,
    BD_LVM_VDO_COMPRESSION_ONLINE,
    BD_LVM_VDO_COMPRESSION_OFFLINE,
} BDLVMVDOCompressionState;

typedef enum {
    BD_LVM_VDO_INDEX_UNKNOWN,
    BD_LVM_VDO_INDEX_ERROR,
    BD_LVM_VDO_INDEX_CLOSED,
    BD_LVM_VDO_INDEX_OPENING,
    BD_LVM_VDO_INDEX_CLOSING,
    BD_LVM_VDO_INDEX_OFFLINE,
    BD_LVM_VDO_INDEX_ONLINE,
} BDLVMVDOIndexState;

typedef enum {
    BD_LVM_VDO_WRITE_POLICY_UNKNOWN,
    BD_LVM_VDO_WRITE_POLICY_AUTO,
    BD_LVM_VDO_WRITE_POLICY_SYNC,
    BD_LVM_VDO_WRITE_POLICY_ASYNC,
} BDLVMVDOWritePolicy;

typedef struct BDLVMPVdata {
    gchar *pv_name;
    gchar *pv_uuid;
    guint64 pv_free;
    guint64 pv_size;
    guint64 pe_start;
    gchar *vg_name;
    gchar *vg_uuid;
    guint64 vg_size;
    guint64 vg_free;
    guint64 vg_extent_size;
    guint64 vg_extent_count;
    guint64 vg_free_count;
    guint64 vg_pv_count;
    gchar **pv_tags;
    gboolean missing;
} BDLVMPVdata;

void bd_lvm_pvdata_free (BDLVMPVdata *data);
BDLVMPVdata* bd_lvm_pvdata_copy (BDLVMPVdata *data);

typedef struct BDLVMVGdata {
    gchar *name;
    gchar *uuid;
    guint64 size;
    guint64 free;
    guint64 extent_size;
    guint64 extent_count;
    guint64 free_count;
    guint64 pv_count;
    gboolean exported;
    gchar **vg_tags;
} BDLVMVGdata;

void bd_lvm_vgdata_free (BDLVMVGdata *data);
BDLVMVGdata* bd_lvm_vgdata_copy (BDLVMVGdata *data);

typedef struct BDLVMSEGdata {
    guint64 size_pe;
    guint64 pv_start_pe;
    gchar *pvdev;
} BDLVMSEGdata;

BDLVMSEGdata* bd_lvm_segdata_copy (BDLVMSEGdata *data);
void bd_lvm_segdata_free (BDLVMSEGdata *data);

typedef struct BDLVMLVdata {
    gchar *lv_name;
    gchar *vg_name;
    gchar *uuid;
    guint64 size;
    gchar *attr;
    gchar *segtype;
    gchar *origin;
    gchar *pool_lv;
    gchar *data_lv;
    gchar *metadata_lv;
    gchar *roles;
    gchar *move_pv;
    guint64 data_percent;
    guint64 metadata_percent;
    guint64 copy_percent;
    gchar **lv_tags;
    gchar **data_lvs;
    gchar **metadata_lvs;
    BDLVMSEGdata **segs;
} BDLVMLVdata;

void bd_lvm_lvdata_free (BDLVMLVdata *data);
BDLVMLVdata* bd_lvm_lvdata_copy (BDLVMLVdata *data);

typedef struct BDLVMVDOPooldata {
    BDLVMVDOOperatingMode operating_mode;
    BDLVMVDOCompressionState compression_state;
    BDLVMVDOIndexState index_state;
    BDLVMVDOWritePolicy write_policy;
    guint64 used_size;
    gint32 saving_percent;
    guint64 index_memory_size;
    gboolean deduplication;
    gboolean compression;
} BDLVMVDOPooldata;

void bd_lvm_vdopooldata_free (BDLVMVDOPooldata *data);
BDLVMVDOPooldata* bd_lvm_vdopooldata_copy (BDLVMVDOPooldata *data);

typedef struct BDLVMVDOStats {
    gint64 block_size;
    gint64 logical_block_size;
    gint64 physical_blocks;
    gint64 data_blocks_used;
    gint64 overhead_blocks_used;
    gint64 logical_blocks_used;
    gint64 used_percent;
    gint64 saving_percent;
    gdouble write_amplification_ratio;
} BDLVMVDOStats;

void bd_lvm_vdo_stats_free (BDLVMVDOStats *stats);
BDLVMVDOStats* bd_lvm_vdo_stats_copy (BDLVMVDOStats *stats);

typedef struct BDLVMCacheStats {
    guint64 block_size;
    guint64 cache_size;
    guint64 cache_used;
    guint64 md_block_size;
    guint64 md_size;
    guint64 md_used;
    guint64 read_hits;
    guint64 read_misses;
    guint64 write_hits;
    guint64 write_misses;
    BDLVMCacheMode mode;
} BDLVMCacheStats;

void bd_lvm_cache_stats_free (BDLVMCacheStats *data);
BDLVMCacheStats* bd_lvm_cache_stats_copy (BDLVMCacheStats *data);

typedef enum {
    BD_LVM_TECH_BASIC = 0,
    BD_LVM_TECH_BASIC_SNAP,
    BD_LVM_TECH_THIN,
    BD_LVM_TECH_CACHE,
    BD_LVM_TECH_CALCS,
    BD_LVM_TECH_THIN_CALCS,
    BD_LVM_TECH_CACHE_CALCS,
    BD_LVM_TECH_GLOB_CONF,
    BD_LVM_TECH_VDO,
    BD_LVM_TECH_WRITECACHE,
    BD_LVM_TECH_DEVICES,
    BD_LVM_TECH_SHARED,
    BD_LVM_TECH_CONFIG,
} BDLVMTech;

typedef enum {
    BD_LVM_TECH_MODE_CREATE = 1 << 0,
    BD_LVM_TECH_MODE_REMOVE = 1 << 2,
    BD_LVM_TECH_MODE_MODIFY = 1 << 3,
    BD_LVM_TECH_MODE_QUERY  = 1 << 4,
} BDLVMTechMode;


/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_lvm_init (void);
void bd_lvm_close (void);

gboolean bd_lvm_is_tech_avail (BDLVMTech tech, guint64 mode, GError **error);

gboolean bd_lvm_is_supported_pe_size (guint64 size, GError **error);
guint64 *bd_lvm_get_supported_pe_sizes (GError **error);
guint64 bd_lvm_get_max_lv_size (GError **error);
guint64 bd_lvm_round_size_to_pe (guint64 size, guint64 pe_size, gboolean roundup, GError **error);
guint64 bd_lvm_get_lv_physical_size (guint64 lv_size, guint64 pe_size, GError **error);
guint64 bd_lvm_get_thpool_padding (guint64 size, guint64 pe_size, gboolean included, GError **error);
guint64 bd_lvm_get_thpool_meta_size (guint64 size, guint64 chunk_size, guint64 n_snapshots, GError **error);
gboolean bd_lvm_is_valid_thpool_md_size (guint64 size, GError **error);
gboolean bd_lvm_is_valid_thpool_chunk_size (guint64 size, gboolean discard, GError **error);

gboolean bd_lvm_pvcreate (const gchar *device, guint64 data_alignment, guint64 metadata_size, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_pvresize (const gchar *device, guint64 size, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_pvremove (const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_pvmove (const gchar *src, const gchar *dest, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_pvscan (const gchar *device, gboolean update_cache, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_add_pv_tags (const gchar *device, const gchar **tags, GError **error);
gboolean bd_lvm_delete_pv_tags (const gchar *device, const gchar **tags, GError **error);
BDLVMPVdata* bd_lvm_pvinfo (const gchar *device, GError **error);
BDLVMPVdata** bd_lvm_pvs (GError **error);

gboolean bd_lvm_vgcreate (const gchar *name, const gchar **pv_list, guint64 pe_size, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_vgremove (const gchar *vg_name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_vgrename (const gchar *old_vg_name, const gchar *new_vg_name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_vgactivate (const gchar *vg_name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_vgdeactivate (const gchar *vg_name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_vgextend (const gchar *vg_name, const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_vgreduce (const gchar *vg_name, const gchar *device, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_add_vg_tags (const gchar *vg_name, const gchar **tags, GError **error);
gboolean bd_lvm_delete_vg_tags (const gchar *vg_name, const gchar **tags, GError **error);
gboolean bd_lvm_vglock_start (const gchar *vg_name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_vglock_stop (const gchar *vg_name, const BDExtraArg **extra, GError **error);
BDLVMVGdata* bd_lvm_vginfo (const gchar *vg_name, GError **error);
BDLVMVGdata** bd_lvm_vgs (GError **error);

gchar* bd_lvm_lvorigin (const gchar *vg_name, const gchar *lv_name, GError **error);
gboolean bd_lvm_lvcreate (const gchar *vg_name, const gchar *lv_name, guint64 size, const gchar *type, const gchar **pv_list, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_lvremove (const gchar *vg_name, const gchar *lv_name, gboolean force, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_lvrename (const gchar *vg_name, const gchar *lv_name, const gchar *new_name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_lvresize (const gchar *vg_name, const gchar *lv_name, guint64 size, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_lvrepair (const gchar *vg_name, const gchar *lv_name, const gchar **pv_list, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_lvactivate (const gchar *vg_name, const gchar *lv_name, gboolean ignore_skip, gboolean shared, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_lvdeactivate (const gchar *vg_name, const gchar *lv_name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_lvsnapshotcreate (const gchar *vg_name, const gchar *origin_name, const gchar *snapshot_name, guint64 size, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_lvsnapshotmerge (const gchar *vg_name, const gchar *snapshot_name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_add_lv_tags (const gchar *vg_name, const gchar *lv_name, const gchar **tags, GError **error);
gboolean bd_lvm_delete_lv_tags (const gchar *vg_name, const gchar *lv_name, const gchar **tags, GError **error);
BDLVMLVdata* bd_lvm_lvinfo (const gchar *vg_name, const gchar *lv_name, GError **error);
BDLVMLVdata* bd_lvm_lvinfo_tree (const gchar *vg_name, const gchar *lv_name, GError **error);
BDLVMLVdata** bd_lvm_lvs (const gchar *vg_name, GError **error);
BDLVMLVdata** bd_lvm_lvs_tree (const gchar *vg_name, GError **error);

gboolean bd_lvm_thpoolcreate (const gchar *vg_name, const gchar *lv_name, guint64 size, guint64 md_size, guint64 chunk_size, const gchar *profile, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_thlvcreate (const gchar *vg_name, const gchar *pool_name, const gchar *lv_name, guint64 size, const BDExtraArg **extra, GError **error);
gchar* bd_lvm_thlvpoolname (const gchar *vg_name, const gchar *lv_name, GError **error);
gboolean bd_lvm_thsnapshotcreate (const gchar *vg_name, const gchar *origin_name, const gchar *snapshot_name, const gchar *pool_name, const BDExtraArg **extra, GError **error);

gboolean bd_lvm_set_global_config (const gchar *new_config, GError **error);
gchar* bd_lvm_get_global_config (GError **error);

gboolean bd_lvm_set_devices_filter (const gchar **devices, GError **error);
gchar** bd_lvm_get_devices_filter (GError **error);

guint64 bd_lvm_cache_get_default_md_size (guint64 cache_size, GError **error);
const gchar* bd_lvm_cache_get_mode_str (BDLVMCacheMode mode, GError **error);
BDLVMCacheMode bd_lvm_cache_get_mode_from_str (const gchar *mode_str, GError **error);
gboolean bd_lvm_cache_create_pool (const gchar *vg_name, const gchar *pool_name, guint64 pool_size, guint64 md_size, BDLVMCacheMode mode, BDLVMCachePoolFlags flags, const gchar **fast_pvs, GError **error);
gboolean bd_lvm_cache_attach (const gchar *vg_name, const gchar *data_lv, const gchar *cache_pool_lv, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_cache_detach (const gchar *vg_name, const gchar *cached_lv, gboolean destroy, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_cache_create_cached_lv (const gchar *vg_name, const gchar *lv_name, guint64 data_size, guint64 cache_size, guint64 md_size, BDLVMCacheMode mode, BDLVMCachePoolFlags flags,
                                        const gchar **slow_pvs, const gchar **fast_pvs, GError **error);
gchar* bd_lvm_cache_pool_name (const gchar *vg_name, const gchar *cached_lv, GError **error);
BDLVMCacheStats* bd_lvm_cache_stats (const gchar *vg_name, const gchar *cached_lv, GError **error);

gboolean bd_lvm_writecache_attach (const gchar *vg_name, const gchar *data_lv, const gchar *cache_lv, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_writecache_detach (const gchar *vg_name, const gchar *cached_lv, gboolean destroy, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_writecache_create_cached_lv (const gchar *vg_name, const gchar *lv_name, guint64 data_size, guint64 cache_size, const gchar **slow_pvs, const gchar **fast_pvs, GError **error);

gboolean bd_lvm_vdo_pool_create (const gchar *vg_name, const gchar *lv_name, const gchar *pool_name, guint64 data_size, guint64 virtual_size, guint64 index_memory, gboolean compression, gboolean deduplication, BDLVMVDOWritePolicy write_policy, const BDExtraArg **extra, GError **error);
BDLVMVDOPooldata *bd_lvm_vdo_info (const gchar *vg_name, const gchar *lv_name, GError **error);

gboolean bd_lvm_vdo_resize (const gchar *vg_name, const gchar *lv_name, guint64 size, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_vdo_pool_resize (const gchar *vg_name, const gchar *pool_name, guint64 size, const BDExtraArg **extra, GError **error);

gboolean bd_lvm_vdo_enable_compression (const gchar *vg_name, const gchar *pool_name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_vdo_disable_compression (const gchar *vg_name, const gchar *pool_name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_vdo_enable_deduplication (const gchar *vg_name, const gchar *pool_name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_vdo_disable_deduplication (const gchar *vg_name, const gchar *pool_name, const BDExtraArg **extra, GError **error);

gboolean bd_lvm_thpool_convert (const gchar *vg_name, const gchar *data_lv, const gchar *metadata_lv, const gchar *name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_cache_pool_convert (const gchar *vg_name, const gchar *data_lv, const gchar *metadata_lv, const gchar *name, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_vdo_pool_convert (const gchar *vg_name, const gchar *pool_lv, const gchar *name, guint64 virtual_size, guint64 index_memory, gboolean compression, gboolean deduplication, BDLVMVDOWritePolicy write_policy, const BDExtraArg **extra, GError **error);
gchar* bd_lvm_vdolvpoolname (const gchar *vg_name, const gchar *lv_name, GError **error);

const gchar* bd_lvm_get_vdo_operating_mode_str (BDLVMVDOOperatingMode mode, GError **error);
const gchar* bd_lvm_get_vdo_compression_state_str (BDLVMVDOCompressionState state, GError **error);
const gchar* bd_lvm_get_vdo_index_state_str (BDLVMVDOIndexState state, GError **error);
const gchar* bd_lvm_get_vdo_write_policy_str (BDLVMVDOWritePolicy policy, GError **error);

BDLVMVDOWritePolicy bd_lvm_get_vdo_write_policy_from_str (const gchar *policy_str, GError **error);

BDLVMVDOStats* bd_lvm_vdo_get_stats (const gchar *vg_name, const gchar *pool_name, GError **error);
GHashTable* bd_lvm_vdo_get_stats_full (const gchar *vg_name, const gchar *pool_name, GError **error);

gboolean bd_lvm_devices_add (const gchar *device, const gchar *devices_file, const BDExtraArg **extra, GError **error);
gboolean bd_lvm_devices_delete (const gchar *device, const gchar *devices_file, const BDExtraArg **extra, GError **error);

gchar* bd_lvm_config_get (const gchar *section, const gchar *setting, const gchar *type, gboolean values_only, gboolean global_config, const BDExtraArg **extra, GError **error);

#endif /* BD_LVM */
