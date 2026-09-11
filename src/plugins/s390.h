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
    BD_S390_ERROR_ZKEY,
} BDS390Error;

typedef enum {
    BD_S390_TECH_DASD = 0,
    BD_S390_TECH_ZFCP,
    BD_S390_TECH_PAES,
} BDS390Tech;

typedef enum {
    BD_S390_TECH_MODE_MODIFY  = 1 << 0,
    BD_S390_TECH_MODE_QUERY   = 1 << 1,
    BD_S390_TECH_MODE_CREATE  = 1 << 2,
} BDS390TechMode;

/**
 * BDS390ZkeyInfo:
 * @name: name of the secure key in the secure key repository;
 * @description: user provided description of the key (empty if none was set);
 * @secure_key_size: size of the secure key in bytes;
 * @clear_key_size: size of the effective (clear) key in bits;
 * @xts: whether the key is an XTS type key or not;
 * @key_type: type of the secure key (e.g. "CCA-AESCIPHER");
 * @volumes: (array zero-terminated=1): volumes associated with the key, each in the "volume:dmname" format;
 * @apqns: (array zero-terminated=1): cryptographic adapters (APQNs) associated with the key, each in the "card.domain" format;
 * @key_file_name: full path to the file holding the secure key;
 * @sector_size: sector size in bytes to use with dm-crypt or 0 for the system default;
 * @volume_type: volume type the key is to be used with (e.g. "LUKS2");
 * @dummy_passphrase: (nullable): path to the dummy passphrase file associated with the key or %NULL if none is set;
 */
typedef struct BDS390ZkeyInfo {
    gchar *name;
    gchar *description;
    guint64 secure_key_size;
    guint64 clear_key_size;
    gboolean xts;
    gchar *key_type;
    gchar **volumes;
    gchar **apqns;
    gchar *key_file_name;
    guint64 sector_size;
    gchar *volume_type;
    gchar *dummy_passphrase;
} BDS390ZkeyInfo;

void bd_s390_zkey_info_free (BDS390ZkeyInfo *info);
BDS390ZkeyInfo* bd_s390_zkey_info_copy (BDS390ZkeyInfo *info);

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

gboolean bd_s390_zkey_generate (const gchar *name, const gchar *key_type, guint64 keybits, const gchar **volumes, const gchar **apqns, guint64 sector_size, gboolean dummy_passphrase, const BDExtraArg **extra, GError **error);
BDS390ZkeyInfo** bd_s390_zkey_list (const gchar *name, GError **error);
gboolean bd_s390_zkey_remove (const gchar *name, GError **error);
gboolean bd_s390_zkey_cryptsetup_setvp (const gchar *device, const gchar *key_file, const BDExtraArg **extra, GError **error);
gboolean bd_s390_zkey_cryptsetup_validate (const gchar *device, const gchar *key_file, const BDExtraArg **extra, GError **error);

#endif  /* BD_S390 */
