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

gboolean bd_lvm_is_supported_pe_size (guint64 size);
guint64 *bd_lvm_get_supported_pe_sizes ();
guint64 bd_lvm_get_max_lv_size ();
guint64 bd_lvm_round_size_to_pe (guint64 size, guint64 pesize, gboolean roundup);
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

#endif /* BD_LVM */
