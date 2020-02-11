#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_CRYPTO
#define BD_CRYPTO

#define BD_CRYPTO_LUKS_METADATA_SIZE (2 MiB)

#define BD_CRYPTO_CHI_SQUARE_LOWER_LIMIT 136
#define BD_CRYPTO_CHI_SQUARE_UPPER_LIMIT 426
#define BD_CRYPTO_CHI_SQUARE_BYTES_TO_CHECK 512

GQuark bd_crypto_error_quark (void);
#define BD_CRYPTO_ERROR bd_crypto_error_quark ()
typedef enum {
    BD_CRYPTO_ERROR_DEVICE,
    BD_CRYPTO_ERROR_STATE,
    BD_CRYPTO_ERROR_INVALID_SPEC,
    BD_CRYPTO_ERROR_FORMAT_FAILED,
    BD_CRYPTO_ERROR_RESIZE_FAILED,
    BD_CRYPTO_ERROR_RESIZE_PERM,
    BD_CRYPTO_ERROR_ADD_KEY,
    BD_CRYPTO_ERROR_REMOVE_KEY,
    BD_CRYPTO_ERROR_NO_KEY,
    BD_CRYPTO_ERROR_KEY_SLOT,
    BD_CRYPTO_ERROR_NSS_INIT_FAILED,
    BD_CRYPTO_ERROR_CERT_DECODE,
    BD_CRYPTO_ERROR_ESCROW_FAILED,
    BD_CRYPTO_ERROR_INVALID_PARAMS,
    BD_CRYPTO_ERROR_TECH_UNAVAIL,
} BDCryptoError;

#define BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz./"

/* KEEP THIS A MULTIPLE OF 5 SO THAT '-' CAN BE INSERTED BETWEEN EVERY 5 CHARACTERS! */
/* 20 chars * 6 bits per char (64-item charset) = 120 "bits of security" */
#define BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH 20

#define DEFAULT_LUKS_KEYSIZE_BITS 512
#define DEFAULT_LUKS_CIPHER "aes-xts-plain64"
#define DEFAULT_LUKS2_SECTOR_SIZE 512

typedef enum {
    BD_CRYPTO_TECH_LUKS = 0,
    BD_CRYPTO_TECH_LUKS2,
    BD_CRYPTO_TECH_TRUECRYPT,
    BD_CRYPTO_TECH_ESCROW,
    BD_CRYPTO_TECH_INTEGRITY,
    BD_CRYPTO_TECH_BITLK,
} BDCryptoTech;

typedef enum {
    BD_CRYPTO_TECH_MODE_CREATE         = 1 << 0,
    BD_CRYPTO_TECH_MODE_OPEN_CLOSE     = 1 << 1,
    BD_CRYPTO_TECH_MODE_QUERY          = 1 << 2,
    BD_CRYPTO_TECH_MODE_ADD_KEY        = 1 << 3,
    BD_CRYPTO_TECH_MODE_REMOVE_KEY     = 1 << 4,
    BD_CRYPTO_TECH_MODE_RESIZE         = 1 << 5,
    BD_CRYPTO_TECH_MODE_SUSPEND_RESUME = 1 << 6,
    BD_CRYPTO_TECH_MODE_BACKUP_RESTORE = 1 << 7,
} BDCryptoTechMode;

typedef enum {
    BD_CRYPTO_LUKS_VERSION_LUKS1 = 0,
    BD_CRYPTO_LUKS_VERSION_LUKS2,
} BDCryptoLUKSVersion;

/**
 * BDCryptoLUKSPBKDF
 * @type: PBKDF algorithm
 * @hash: hash for LUKS header or NULL
 * @max_memory_kb: requested memory cost (in KiB) or 0 for default (benchmark)
 * @iterations: requested iterations or 0 for default (benchmark)
 * @time_ms: requested time cost or 0 for default (benchmark)
 * @parallel_threads: requested parallel cost (threads) or 0 for default (benchmark)
*/
typedef struct BDCryptoLUKSPBKDF {
    gchar *type;
    gchar *hash;
    guint32 max_memory_kb;
    guint32 iterations;
    guint32 time_ms;
    guint32 parallel_threads;
} BDCryptoLUKSPBKDF;

void bd_crypto_luks_pbkdf_free (BDCryptoLUKSPBKDF *pbkdf);
BDCryptoLUKSPBKDF* bd_crypto_luks_pbkdf_copy (BDCryptoLUKSPBKDF *pbkdf);
BDCryptoLUKSPBKDF* bd_crypto_luks_pbkdf_new (const gchar *type, const gchar *hash, guint32 max_memory_kb, guint32 iterations, guint32 time_ms, guint32 parallel_threads);

/**
 * BDCryptoLUKSExtra:
 * @data_alignment: data alignment in sectors, 0 for default/auto detection
 * @data_device: detached encrypted data device or NULL
 * @integrity: integrity algorithm (e.g. "hmac-sha256") or NULL for no integrity support
 *             Note: this field is valid only for LUKS 2
 * @sector_size: encryption sector size, 0 for default (512)
 *               Note: this field is valid only for LUKS 2
 * @label: LUKS header label or NULL
 *         Note: this field is valid only for LUKS 2
 * @subsytem: LUKS header subsystem or NULL
 *            Note: this field is valid only for LUKS 2
 * @pbkdf: key derivation function specification or NULL for default
 *         Note: this field is valid only for LUKS 2
 */
typedef struct BDCryptoLUKSExtra {
    guint64 data_alignment;
    gchar *data_device;
    gchar *integrity;
    guint64 sector_size;
    gchar *label;
    gchar *subsystem;
    BDCryptoLUKSPBKDF *pbkdf;
} BDCryptoLUKSExtra;

void bd_crypto_luks_extra_free (BDCryptoLUKSExtra *extra);
BDCryptoLUKSExtra* bd_crypto_luks_extra_copy (BDCryptoLUKSExtra *extra);
BDCryptoLUKSExtra* bd_crypto_luks_extra_new (guint64 data_alignment, const gchar *data_device, const gchar *integrity, guint64 sector_size, const gchar *label, const gchar *subsystem, BDCryptoLUKSPBKDF *pbkdf);

/**
 * BDCryptoLUKSInfo:
 * @version: LUKS version
 * @cipher: used cipher (e.g. "aes")
 * @mode: used cipher mode (e.g. "xts-plain")
 * @uuid: UUID of the LUKS device
 * @backing_device: name of the underlying block device
 * @sector_size: size (in bytes) of encryption sector
 *               Note: sector size is valid only for LUKS 2
 */
typedef struct BDCryptoLUKSInfo {
    BDCryptoLUKSVersion version;
    gchar *cipher;
    gchar *mode;
    gchar *uuid;
    gchar *backing_device;
    gint64 sector_size;
} BDCryptoLUKSInfo;

void bd_crypto_luks_info_free (BDCryptoLUKSInfo *info);
BDCryptoLUKSInfo* bd_crypto_luks_info_copy (BDCryptoLUKSInfo *info);

/**
 * BDCryptoIntegrityInfo:
 * @algorithm: integrity algorithm
 * @key_size: integrity key size in bytes
 * @sector_size: sector size in bytes
 * @tag_size: tag size per-sector in bytes
 * @interleave_sectors: number of interleave sectors
 * @journal_size: size of journal in bytes
 * @journal_crypt: journal encryption algorithm
 * @journal_integrity: journal integrity algorithm
 */
typedef struct BDCryptoIntegrityInfo {
    gchar *algorithm;
    guint32 key_size;
    guint32 sector_size;
    guint32 tag_size;
    guint32 interleave_sectors;
    guint64 journal_size;
    gchar *journal_crypt;
    gchar *journal_integrity;
} BDCryptoIntegrityInfo;

void bd_crypto_integrity_info_free (BDCryptoIntegrityInfo *info);
BDCryptoIntegrityInfo* bd_crypto_integrity_info_copy (BDCryptoIntegrityInfo *info);

/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * check_deps() - check plugin's dependencies, returning TRUE if satisfied
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_crypto_check_deps (void);
gboolean bd_crypto_init (void);
void bd_crypto_close (void);

gboolean bd_crypto_is_tech_avail (BDCryptoTech tech, guint64 mode, GError **error);

gchar* bd_crypto_generate_backup_passphrase(GError **error);
gboolean bd_crypto_device_is_luks (const gchar *device, GError **error);
gchar* bd_crypto_luks_uuid (const gchar *device, GError **error);
guint64 bd_crypto_luks_get_metadata_size (const gchar *device, GError **error);
gchar* bd_crypto_luks_status (const gchar *luks_device, GError **error);
gboolean bd_crypto_luks_format (const gchar *device, const gchar *cipher, guint64 key_size, const gchar *passphrase, const gchar *key_file, guint64 min_entropy, GError **error);
gboolean bd_crypto_luks_format_blob (const gchar *device, const gchar *cipher, guint64 key_size, const guint8 *pass_data, gsize data_len, guint64 min_entropy, GError **error);
gboolean bd_crypto_luks_format_luks2 (const gchar *device, const gchar *cipher, guint64 key_size, const gchar *passphrase, const gchar *key_file, guint64 min_entropy, BDCryptoLUKSVersion luks_version, BDCryptoLUKSExtra *extra,GError **error);
gboolean bd_crypto_luks_format_luks2_blob (const gchar *device, const gchar *cipher, guint64 key_size, const guint8 *pass_data, gsize data_len, guint64 min_entropy, BDCryptoLUKSVersion luks_version, BDCryptoLUKSExtra *extra, GError **error);
gboolean bd_crypto_luks_open (const gchar *device, const gchar *name, const gchar *passphrase, const gchar *key_file, gboolean read_only, GError **error);
gboolean bd_crypto_luks_open_blob (const gchar *device, const gchar *name, const guint8* pass_data, gsize data_len, gboolean read_only, GError **error);
gboolean bd_crypto_luks_close (const gchar *luks_device, GError **error);
gboolean bd_crypto_luks_add_key (const gchar *device, const gchar *pass, const gchar *key_file, const gchar *npass, const gchar *nkey_file, GError **error);
gboolean bd_crypto_luks_add_key_blob (const gchar *device, const guint8 *pass_data, gsize data_len, const guint8 *npass_data, gsize ndata_len, GError **error);
gboolean bd_crypto_luks_remove_key (const gchar *device, const gchar *pass, const gchar *key_file, GError **error);
gboolean bd_crypto_luks_remove_key_blob (const gchar *device, const guint8 *pass_data, gsize data_len, GError **error);
gboolean bd_crypto_luks_change_key (const gchar *device, const gchar *pass, const gchar *npass, GError **error);
gboolean bd_crypto_luks_change_key_blob (const gchar *device, const guint8 *pass_data, gsize data_len, const guint8 *npass_data, gsize ndata_len, GError **error);
gboolean bd_crypto_luks_resize (const gchar *device, guint64 size, GError **error);
gboolean bd_crypto_luks_resize_luks2 (const gchar *luks_device, guint64 size, const gchar *passphrase, const gchar *key_file, GError **error);
gboolean bd_crypto_luks_resize_luks2_blob (const gchar *luks_device, guint64 size, const guint8* pass_data, gsize data_len, GError **error);
gboolean bd_crypto_luks_suspend (const gchar *luks_device, GError **error);
gboolean bd_crypto_luks_resume_blob (const gchar *luks_device, const guint8 *pass_data, gsize data_len, GError **error);
gboolean bd_crypto_luks_resume (const gchar *luks_device, const gchar *passphrase, const gchar *key_file, GError **error);
gboolean bd_crypto_luks_kill_slot (const gchar *device, gint slot, GError **error);
gboolean bd_crypto_luks_header_backup (const gchar *device, const gchar *backup_file, GError **error);
gboolean bd_crypto_luks_header_restore (const gchar *device, const gchar *backup_file, GError **error);

BDCryptoLUKSInfo* bd_crypto_luks_info (const gchar *luks_device, GError **error);
BDCryptoIntegrityInfo* bd_crypto_integrity_info (const gchar *device, GError **error);

gboolean bd_crypto_device_seems_encrypted (const gchar *device, GError **error);
gboolean bd_crypto_tc_open (const gchar *device, const gchar *name, const guint8* pass_data, gsize data_len, gboolean read_only, GError **error);
gboolean bd_crypto_tc_open_full (const gchar *device, const gchar *name, const guint8* pass_data, gsize data_len, const gchar **keyfiles, gboolean hidden, gboolean system, gboolean veracrypt, guint32 veracrypt_pim, gboolean read_only, GError **error);
gboolean bd_crypto_tc_close (const gchar *tc_device, GError **error);

gboolean bd_crypto_bitlk_open (const gchar *device, const gchar *name, const guint8* pass_data, gsize data_len, gboolean read_only, GError **error);
gboolean bd_crypto_bitlk_close (const gchar *bitlk_device, GError **error);

gboolean bd_crypto_escrow_device (const gchar *device, const gchar *passphrase, const gchar *cert_data, const gchar *directory, const gchar *backup_passphrase, GError **error);

#endif  /* BD_CRYPTO */
