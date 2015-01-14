#include <glib.h>
#include <utils.h>

#ifndef BD_LVM
#define BD_LVM

#ifdef __LP64__
// 64bit system
#define BD_LVM_MAX_LV_SIZE (8 EiB)
#else
// 32bit system
#define BD_LVM_MAX_LV_SIZE (16 TiB)
#endif

#define BD_LVM_DEFAULT_PE_SIZE (4 MiB)
#define BD_LVM_MIN_PE_SIZE (1 KiB)
#define BD_LVM_MAX_PE_SIZE (16 GiB)
#define USE_DEFAULT_PE_SIZE 0
#define RESOLVE_PE_SIZE(size) ((size) == USE_DEFAULT_PE_SIZE ? BD_LVM_DEFAULT_PE_SIZE : (size))

#define BD_LVM_MIN_THPOOL_MD_SIZE (2 MiB)
#define BD_LVM_MAX_THPOOL_MD_SIZE (16 GiB)
#define BD_LVM_MIN_THPOOL_CHUNK_SIZE (64 KiB)
#define BD_LVM_MAX_THPOOL_CHUNK_SIZE (1 GiB)
#define THPOOL_MD_FACTOR_NEW (0.2)
#define THPOOL_MD_FACTOR_EXISTS (1 / 6.0)

GQuark bd_lvm_error_quark (void);
#define BD_LVM_ERROR bd_lvm_error_quark ()
typedef enum {
    BD_LVM_ERROR_PARSE,
} BDLVMError;

typedef struct BDLVMPVdata {
    gchar *pv_name;
    gchar *pv_uuid;
    guint64 pe_start;
    gchar *vg_name;
    gchar *vg_uuid;
    guint64 vg_size;
    guint64 vg_free;
    guint64 vg_extent_size;
    guint64 vg_extent_count;
    guint64 vg_free_count;
    guint64 vg_pv_count;
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
} BDLVMVGdata;

void bd_lvm_vgdata_free (BDLVMVGdata *data);
BDLVMVGdata* bd_lvm_vgdata_copy (BDLVMVGdata *data);

typedef struct BDLVMLVdata {
    gchar *lv_name;
    gchar *vg_name;
    gchar *uuid;
    guint64 size;
    gchar *attr;
    gchar *segtype;
} BDLVMLVdata;

void bd_lvm_lvdata_free (BDLVMLVdata *data);
BDLVMLVdata* bd_lvm_lvdata_copy (BDLVMLVdata *data);

gboolean bd_lvm_is_supported_pe_size (guint64 size);
guint64 *bd_lvm_get_supported_pe_sizes ();
guint64 bd_lvm_get_max_lv_size ();
guint64 bd_lvm_round_size_to_pe (guint64 size, guint64 pe_size, gboolean roundup);
guint64 bd_lvm_get_lv_physical_size (guint64 lv_size, guint64 pe_size);
guint64 bd_lvm_get_thpool_padding (guint64 size, guint64 pe_size, gboolean included);
gboolean bd_lvm_is_valid_thpool_md_size (guint64 size);
gboolean bd_lvm_is_valid_thpool_chunk_size (guint64 size, gboolean discard);

gboolean bd_lvm_pvcreate (gchar *device, GError **error);
gboolean bd_lvm_pvresize (gchar *device, guint64 size, GError **error);
gboolean bd_lvm_pvremove (gchar *device, GError **error);
gboolean bd_lvm_pvmove (gchar *src, gchar *dest, GError **error);
gboolean bd_lvm_pvscan (gchar *device, gboolean update_cache, GError **error);
BDLVMPVdata* bd_lvm_pvinfo (gchar *device, GError **error);
BDLVMPVdata** bd_lvm_pvs (GError **error);

gboolean bd_lvm_vgcreate (gchar *name, gchar **pv_list, guint64 pe_size, GError **error);
gboolean bd_lvm_vgremove (gchar *vg_name, GError **error);
gboolean bd_lvm_vgactivate (gchar *vg_name, GError **error);
gboolean bd_lvm_vgdeactivate (gchar *vg_name, GError **error);
gboolean bd_lvm_vgextend (gchar *vg_name, gchar *device, GError **error);
gboolean bd_lvm_vgreduce (gchar *vg_name, gchar *device, GError **error);
BDLVMVGdata* bd_lvm_vginfo (gchar *device, GError **error);
BDLVMVGdata** bd_lvm_vgs (GError **error);

gchar* bd_lvm_lvorigin (gchar *vg_name, gchar *lv_name, GError **error);
gboolean bd_lvm_lvcreate (gchar *vg_name, gchar *lv_name, guint64 size, gchar **pv_list, GError **error);
gboolean bd_lvm_lvremove (gchar *vg_name, gchar *lv_name, gboolean force, GError **error);
gboolean bd_lvm_lvresize (gchar *vg_name, gchar *lv_name, guint64 size, GError **error);
gboolean bd_lvm_lvactivate (gchar *vg_name, gchar *lv_name, gboolean ignore_skip, GError **error);
gboolean bd_lvm_lvdeactivate (gchar *vg_name, gchar *lv_name, GError **error);
gboolean bd_lvm_lvsnapshotcreate (gchar *vg_name, gchar *origin_name, gchar *snapshot_name, guint64 size, GError **error);
gboolean bd_lvm_lvsnapshotmerge (gchar *vg_name, gchar *snapshot_name, GError **error);
BDLVMLVdata* bd_lvm_lvinfo (gchar *vg_name, gchar *lv_name, GError **error);
BDLVMLVdata** bd_lvm_lvs (gchar *vg_name, GError **error);

gboolean bd_lvm_thpoolcreate (gchar *vg_name, gchar *lv_name, guint64 size, guint64 md_size, guint64 chunk_size, gchar *profile, GError **error);
gboolean bd_lvm_thlvcreate (gchar *vg_name, gchar *pool_name, gchar *lv_name, guint64 size, GError **error);
gchar* bd_lvm_thpoolname (gchar *vg_name, gchar *lv_name, GError **error);
gboolean bd_lvm_thsnapshotcreate (gchar *vg_name, gchar *origin_name, gchar *snapshot_name, gchar *pool_name, GError **error);
gboolean bd_lvm_set_global_config (gchar *new_config, GError **error);
gchar* bd_lvm_get_global_config ();

#endif /* BD_LVM */
