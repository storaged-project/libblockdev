#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_NVDIMM
#define BD_NVDIMM

GQuark bd_nvdimm_error_quark (void);
#define BD_NVDIMM_ERROR bd_nvdimm_error_quark ()
typedef enum {
    BD_NVDIMM_ERROR_TECH_UNAVAIL,
    BD_NVDIMM_ERROR_NAMESPACE_FAIL,
    BD_NVDIMM_ERROR_NAMESPACE_PARSE,
    BD_NVDIMM_ERROR_NAMESPACE_NOEXIST,
    BD_NVDIMM_ERROR_NAMESPACE_MODE_INVAL,
} BDNVDIMMError;

typedef enum {
    BD_NVDIMM_NAMESPACE_MODE_RAW,
    BD_NVDIMM_NAMESPACE_MODE_SECTOR,
    BD_NVDIMM_NAMESPACE_MODE_MEMORY,
    BD_NVDIMM_NAMESPACE_MODE_DAX,
    BD_NVDIMM_NAMESPACE_MODE_FSDAX,
    BD_NVDIMM_NAMESPACE_MODE_DEVDAX,
    BD_NVDIMM_NAMESPACE_MODE_UNKNOWN,
} BDNVDIMMNamespaceMode;

typedef struct BDNVDIMMNamespaceInfo {
    gchar *dev;
    guint64 mode;
    guint64 size;
    gchar *uuid;
    guint64 sector_size;
    gchar *blockdev;
    gboolean enabled;
} BDNVDIMMNamespaceInfo;

void bd_nvdimm_namespace_info_free (BDNVDIMMNamespaceInfo *info);
BDNVDIMMNamespaceInfo* bd_nvdimm_namespace_info_copy (BDNVDIMMNamespaceInfo *info);

typedef enum {
    BD_NVDIMM_TECH_NAMESPACE = 0,
} BDNVDIMMTech;

typedef enum {
    BD_NVDIMM_TECH_MODE_CREATE              = 1 << 0,
    BD_NVDIMM_TECH_MODE_REMOVE              = 1 << 1,
    BD_NVDIMM_TECH_MODE_ACTIVATE_DEACTIVATE = 1 << 2,
    BD_NVDIMM_TECH_MODE_QUERY               = 1 << 3,
    BD_NVDIMM_TECH_MODE_RECONFIGURE         = 1 << 4,
} BDNVDIMMTechMode;

/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_nvdimm_init (void);
void bd_nvdimm_close (void);

gboolean bd_nvdimm_is_tech_avail (BDNVDIMMTech tech, guint64 mode, GError **error);

BDNVDIMMNamespaceMode bd_nvdimm_namespace_get_mode_from_str (const gchar *mode_str, GError **error);
const gchar* bd_nvdimm_namespace_get_mode_str (BDNVDIMMNamespaceMode mode, GError **error);

gchar* bd_nvdimm_namespace_get_devname (const gchar *device, GError **error);

gboolean bd_nvdimm_namespace_enable (const gchar *namespace, const BDExtraArg **extra, GError **error);
gboolean bd_nvdimm_namespace_disable (const gchar *namespace, const BDExtraArg **extra, GError **error);

BDNVDIMMNamespaceInfo* bd_nvdimm_namespace_info (const gchar *namespace, const BDExtraArg **extra, GError **error);
BDNVDIMMNamespaceInfo** bd_nvdimm_list_namespaces (const gchar *bus, const gchar *region, gboolean idle, const BDExtraArg **extra, GError **error);

gboolean bd_nvdimm_namespace_reconfigure (const gchar* namespace, BDNVDIMMNamespaceMode mode, gboolean force, const BDExtraArg **extra, GError** error);

const guint64 *bd_nvdimm_namespace_get_supported_sector_sizes (BDNVDIMMNamespaceMode mode, GError **error);

#endif  /* BD_NVDIMM */
