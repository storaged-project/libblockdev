#include <sizes.h>
#include <glib.h>

#ifndef BD_LVM
#define BD_LVM

#ifdef __LP64__
// 64bit system
#define MAX_LV_SIZE (8 EiB)
#else
// 32bit system
#define MAX_LV_SIZE (16 TiB)
#endif

#define DEFAULT_PE_SIZE (4 MiB)
#define MIN_PE_SIZE (1 KiB)
#define MAX_PE_SIZE (16 GiB)
#define USE_DEFAULT_PE_SIZE 0
#define RESOLVE_PE_SIZE(size) ((size) == USE_DEFAULT_PE_SIZE ? DEFAULT_PE_SIZE : (size))

#define MIN_THPOOL_MD_SIZE (2 MiB)
#define MAX_THPOOL_MD_SIZE (16 GiB)
#define MIN_THPOOL_CHUNK_SIZE (64 KiB)
#define MAX_THPOOL_CHUNK_SIZE (1 GiB)
#define THPOOL_MD_FACTOR_NEW (0.2)
#define THPOOL_MD_FACTOR_EXISTS (1 / 6.0)

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

BDLVMPVdata* bd_lvm_pvdata_copy (BDLVMPVdata *data) {
    BDLVMPVdata *new_data = g_new (BDLVMPVdata, 1);

    new_data->pv_name = g_strdup (data->pv_name);
    new_data->pv_uuid = g_strdup (data->pv_uuid);
    new_data->pe_start = data->pe_start;
    new_data->vg_name = g_strdup (data->vg_name);
    new_data->vg_size = data->vg_size;
    new_data->vg_free = data->vg_free;
    new_data->vg_extent_size = data->vg_extent_size;
    new_data->vg_extent_count = data->vg_extent_count;
    new_data->vg_free_count = data->vg_free_count;
    new_data->vg_pv_count = data->vg_pv_count;

    return new_data;
}

void bd_lvm_pvdata_free (BDLVMPVdata *data) {
    g_free (data->pv_name);
    g_free (data->pv_uuid);
    g_free (data->vg_name);
    g_free (data);
}

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

BDLVMVGdata* bd_lvm_vgdata_copy (BDLVMVGdata *data) {
    BDLVMVGdata *new_data = g_new (BDLVMVGdata, 1);

    new_data->name = g_strdup (data->name);
    new_data->uuid = g_strdup (data->uuid);
    new_data->size = data->size;
    new_data->free = data->free;
    new_data->extent_size = data->extent_size;
    new_data->extent_count = data->extent_count;
    new_data->free_count = data->free_count;
    new_data->pv_count = data->pv_count;
    return new_data;
}

void bd_lvm_vgdata_free (BDLVMVGdata *data) {
    g_free (data->name);
    g_free (data->uuid);
    g_free (data);
}

typedef struct BDLVMLVdata {
    gchar *lv_name;
    gchar *vg_name;
    gchar *uuid;
    guint64 size;
    gchar *attr;
    gchar *segtype;
} BDLVMLVdata;

BDLVMLVdata* bd_lvm_lvdata_copy (BDLVMLVdata *data) {
    BDLVMLVdata *new_data = g_new (BDLVMLVdata, 1);

    new_data->lv_name = g_strdup (data->lv_name);
    new_data->vg_name = g_strdup (data->vg_name);
    new_data->uuid = g_strdup (data->uuid);
    new_data->size = data->size;
    new_data->attr = g_strdup (data->attr);
    new_data->segtype = g_strdup (data->segtype);
    return new_data;
}

void bd_lvm_lvdata_free (BDLVMLVdata *data) {
    g_free (data->lv_name);
    g_free (data->vg_name);
    g_free (data->uuid);
    g_free (data->attr);
    g_free (data->segtype);
    g_free (data);
}

gboolean bd_lvm_is_supported_pe_size (guint64 size);
guint64 *bd_lvm_get_supported_pe_sizes ();
guint64 bd_lvm_get_max_lv_size ();
guint64 bd_lvm_round_size_to_pe (guint64 size, guint64 pe_size, gboolean roundup);
guint64 bd_lvm_get_lv_physical_size (guint64 lv_size, guint64 pe_size);
guint64 bd_lvm_get_thpool_padding (guint64 size, guint64 pe_size, gboolean included);
gboolean bd_lvm_is_valid_thpool_md_size (guint64 size);
gboolean bd_lvm_is_valid_thpool_chunk_size (guint64 size, gboolean discard);

gboolean bd_lvm_pvcreate (gchar *device, gchar **error_message);
gboolean bd_lvm_pvresize (gchar *device, guint64 size, gchar **error_message);
gboolean bd_lvm_pvremove (gchar *device, gchar **error_message);
gboolean bd_lvm_pvmove (gchar *src, gchar *dest, gchar **error_message);
gboolean bd_lvm_pvscan (gchar *device, gboolean update_cache, gchar **error_message);
BDLVMPVdata* bd_lvm_pvinfo (gchar *device, gchar **error_message);
BDLVMPVdata** bd_lvm_pvs (gchar **error_message);

gboolean bd_lvm_vgcreate (gchar *name, gchar **pv_list, guint64 pe_size, gchar **error_message);
gboolean bd_lvm_vgremove (gchar *vg_name, gchar **error_message);
gboolean bd_lvm_vgactivate (gchar *vg_name, gchar **error_message);
gboolean bd_lvm_vgdeactivate (gchar *vg_name, gchar **error_message);
gboolean bd_lvm_vgextend (gchar *vg_name, gchar *device, gchar **error_message);
gboolean bd_lvm_vgreduce (gchar *vg_name, gchar *device, gchar **error_message);
BDLVMVGdata* bd_lvm_vginfo (gchar *device, gchar **error_message);
BDLVMVGdata** bd_lvm_vgs (gchar **error_message);

gchar* bd_lvm_lvorigin (gchar *vg_name, gchar *lv_name, gchar **error_message);
gboolean bd_lvm_lvcreate (gchar *vg_name, gchar *lv_name, guint64 size, gchar **pv_list, gchar **error_message);
gboolean bd_lvm_lvremove (gchar *vg_name, gchar *lv_name, gboolean force, gchar **error_message);
gboolean bd_lvm_lvresize (gchar *vg_name, gchar *lv_name, guint64 size, gchar **error_message);
gboolean bd_lvm_lvactivate (gchar *vg_name, gchar *lv_name, gboolean ignore_skip, gchar **error_message);
gboolean bd_lvm_lvdeactivate (gchar *vg_name, gchar *lv_name, gchar **error_message);
gboolean bd_lvm_lvsnapshotcreate (gchar *vg_name, gchar *origin_name, gchar *snapshot_name, guint64 size, gchar **error_message);
gboolean bd_lvm_lvsnapshotmerge (gchar *vg_name, gchar *snapshot_name, gchar **error_message);
BDLVMLVdata* bd_lvm_lvinfo (gchar *vg_name, gchar *lv_name, gchar **error_message);
BDLVMLVdata** bd_lvm_lvs (gchar *vg_name, gchar **error_message);

#endif /* BD_LVM */
