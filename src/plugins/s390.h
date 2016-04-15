#include <glib.h>
#include <linux/fs.h>
#include <stdio.h>
#include <string.h>
#include <utils.h>
#include <asm/dasd.h>
#include <s390utils/vtoc.h>


GQuark bd_s390_error_quark (void);
#define BD_S390_ERROR bd_s390_error_quark ()
typedef enum {
    BD_S390_ERROR_DEVICE,
    BD_S390_ERROR_FORMAT_FAILED,
    BD_S390_ERROR_DASDFMT,
} BDS390Error;

gboolean bd_s390_dasd_format (const gchar *dasd, BDExtraArg **extra, GError **error);
gboolean bd_s390_dasd_needs_format (const gchar *dasd, GError **error);
gboolean bd_s390_dasd_online (gchar *dasd, GError **error);
gboolean bd_s390_dasd_is_ldl (const gchar *dasd, GError **error);
gchar* bd_s390_sanitize_dev_input (const gchar *dev, GError **error);
gchar* bd_s390_zfcp_sanitize_wwpn_input (const gchar *wwpn, GError **error);
gchar* bd_s390_zfcp_sanitize_lun_input (const gchar *lun, GError **error);
