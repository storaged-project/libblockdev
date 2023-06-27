#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_S390
#define BD_S390

GQuark bd_s390_error_quark (void);
#define BD_S390_ERROR bd_s390_error_quark ()
typedef enum {
    BD_S390_ERROR_TECH_UNAVAIL,
    BD_S390_ERROR_DEVICE,
    BD_S390_ERROR_FORMAT_FAILED,
    BD_S390_ERROR_DASDFMT,
    BD_S390_ERROR_IO,
} BDS390Error;

typedef enum {
    BD_S390_TECH_DASD = 0,
    BD_S390_TECH_ZFCP,
} BDS390Tech;

typedef enum {
    BD_S390_TECH_MODE_MODIFY  = 1 << 0,
    BD_S390_TECH_MODE_QUERY   = 1 << 1,
} BDS390TechMode;

/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_s390_init (void);
void bd_s390_close (void);

gboolean bd_s390_is_tech_avail (BDS390Tech tech, guint64 mode, GError **error);

gboolean bd_s390_dasd_format (const gchar *dasd, const BDExtraArg **extra, GError **error);
gboolean bd_s390_dasd_needs_format (const gchar *dasd, GError **error);
gboolean bd_s390_dasd_online (const gchar *dasd, GError **error);
gboolean bd_s390_dasd_is_ldl (const gchar *dasd, GError **error);
gboolean bd_s390_dasd_is_fba (const gchar *dasd, GError **error);

gchar* bd_s390_sanitize_dev_input (const gchar *dev, GError **error);

gchar* bd_s390_zfcp_sanitize_wwpn_input (const gchar *wwpn, GError **error);
gchar* bd_s390_zfcp_sanitize_lun_input (const gchar *lun, GError **error);
gboolean bd_s390_zfcp_online (const gchar *devno, const gchar *wwpn, const gchar *lun, GError **error);
gboolean bd_s390_zfcp_scsi_offline(const gchar *devno, const gchar *wwpn, const gchar *lun, GError **error);
gboolean bd_s390_zfcp_offline(const gchar *devno, const gchar *wwpn, const gchar *lun, GError **error);

#endif  /* BD_S390 */
