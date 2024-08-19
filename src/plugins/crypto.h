#include <glib.h>
#include <blockdev/utils.h>

#ifndef BD_CRYPTO
#define BD_CRYPTO

GQuark bd_crypto_error_quark (void);
#define BD_CRYPTO_ERROR bd_crypto_error_quark ()
typedef enum {
    BD_CRYPTO_ERROR_TECH_UNAVAIL,
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
    BD_CRYPTO_ERROR_KEYRING,
    BD_CRYPTO_ERROR_KEYFILE_FAILED,
    BD_CRYPTO_ERROR_INVALID_CONTEXT,
    BD_CRYPTO_ERROR_CONVERT_FAILED,
} BDCryptoError;

#define BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz./"

/* KEEP THIS A MULTIPLE OF 5 SO THAT '-' CAN BE INSERTED BETWEEN EVERY 5 CHARACTERS! */
/* 20 chars * 6 bits per char (64-item charset) = 120 "bits of security" */
#define BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH 20

typedef enum {
    BD_CRYPTO_TECH_LUKS = 0,
    BD_CRYPTO_TECH_TRUECRYPT,
    BD_CRYPTO_TECH_ESCROW,
    BD_CRYPTO_TECH_INTEGRITY,
    BD_CRYPTO_TECH_BITLK,
    BD_CRYPTO_TECH_KEYRING,
    BD_CRYPTO_TECH_FVAULT2,
    BD_CRYPTO_TECH_SED_OPAL,
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
    BD_CRYPTO_TECH_MODE_MODIFY         = 1 << 8,
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
 * @subsystem: LUKS header subsystem or NULL
 *             Note: this field is valid only for LUKS 2
 * @pbkdf: key derivation function specification or NULL for default
 *         Note: this field is valid only for LUKS 2
 */
typedef struct BDCryptoLUKSExtra {
    guint64 data_alignment;
    gchar *data_device;
    gchar *integrity;
    guint32 sector_size;
    gchar *label;
    gchar *subsystem;
    BDCryptoLUKSPBKDF *pbkdf;
} BDCryptoLUKSExtra;

void bd_crypto_luks_extra_free (BDCryptoLUKSExtra *extra);
BDCryptoLUKSExtra* bd_crypto_luks_extra_copy (BDCryptoLUKSExtra *extra);
BDCryptoLUKSExtra* bd_crypto_luks_extra_new (guint64 data_alignment, const gchar *data_device, const gchar *integrity, guint32 sector_size, const gchar *label, const gchar *subsystem, BDCryptoLUKSPBKDF *pbkdf);

/**
 * BDCryptoIntegrityExtra:
 * @sector_size: integrity sector size
 * @journal_size: size of journal in bytes
 * @journal_watermark: journal flush watermark in percents; in bitmap mode sectors-per-bit
 * @journal_commit_time: journal commit time (or bitmap flush time) in ms
 * @interleave_sectors: number of interleave sectors (power of two)
 * @tag_size: tag size per-sector in bytes
 * @buffer_sectors: number of sectors in one buffer
 */
typedef struct BDCryptoIntegrityExtra {
    guint32 sector_size;
    guint64 journal_size;
    guint journal_watermark;
    guint journal_commit_time;
    guint32 interleave_sectors;
    guint32 tag_size;
    guint32 buffer_sectors;
} BDCryptoIntegrityExtra;

void bd_crypto_integrity_extra_free (BDCryptoIntegrityExtra *extra);
BDCryptoIntegrityExtra* bd_crypto_integrity_extra_copy (BDCryptoIntegrityExtra *extra);
BDCryptoIntegrityExtra* bd_crypto_integrity_extra_new (guint32 sector_size, guint64 journal_size, guint journal_watermark, guint journal_commit_time, guint64 interleave_sectors, guint64 tag_size, guint64 buffer_sectors);

typedef enum {
    BD_CRYPTO_INTEGRITY_OPEN_NO_JOURNAL         = 1 << 0,
    BD_CRYPTO_INTEGRITY_OPEN_RECOVERY           = 1 << 1,
    BD_CRYPTO_INTEGRITY_OPEN_NO_JOURNAL_BITMAP  = 1 << 2,
    BD_CRYPTO_INTEGRITY_OPEN_RECALCULATE        = 1 << 3,
    BD_CRYPTO_INTEGRITY_OPEN_RECALCULATE_RESET  = 1 << 4,
    BD_CRYPTO_INTEGRITY_OPEN_ALLOW_DISCARDS     = 1 << 5,
} BDCryptoIntegrityOpenFlags;

/**
 * BDCryptoLUKSHWEncryptionType:
 * @BD_CRYPTO_LUKS_HW_ENCRYPTION_UNKNOWN: used for unknown/unsupported hardware encryption or when
 *                                        error was detected when getting the information
 * @BD_CRYPTO_LUKS_HW_ENCRYPTION_SW_ONLY: hardware encryption is not configured on this device
 * @BD_CRYPTO_LUKS_HW_ENCRYPTION_OPAL_HW_ONLY: only OPAL hardware encryption is configured on this device
 * @BD_CRYPTO_LUKS_HW_ENCRYPTION_OPAL_HW_AND_SW: both OPAL hardware encryption and software encryption
 *                                               (using LUKS/dm-crypt) is configured on this device
 */
typedef enum {
    BD_CRYPTO_LUKS_HW_ENCRYPTION_UNKNOWN = 0,
    BD_CRYPTO_LUKS_HW_ENCRYPTION_SW_ONLY,
    BD_CRYPTO_LUKS_HW_ENCRYPTION_OPAL_HW_ONLY,
    BD_CRYPTO_LUKS_HW_ENCRYPTION_OPAL_HW_AND_SW,
} BDCryptoLUKSHWEncryptionType;

/**
 * BDCryptoLUKSInfo:
 * @version: LUKS version
 * @cipher: used cipher (e.g. "aes")
 * @mode: used cipher mode (e.g. "xts-plain")
 * @uuid: UUID of the LUKS device
 * @backing_device: name of the underlying block device
 * @sector_size: size (in bytes) of encryption sector
 *               Note: sector size is valid only for LUKS 2
 * @metadata_size: LUKS metadata size
 * @label: label of the LUKS device (valid only for LUKS 2)
 * @subsystem: subsystem of the LUKS device (valid only for LUKS 2)
 * @hw_encryption: hardware encryption type
 */
typedef struct BDCryptoLUKSInfo {
    BDCryptoLUKSVersion version;
    gchar *cipher;
    gchar *mode;
    gchar *uuid;
    gchar *backing_device;
    guint32 sector_size;
    guint64 metadata_size;
    gchar *label;
    gchar *subsystem;
    BDCryptoLUKSHWEncryptionType hw_encryption;
} BDCryptoLUKSInfo;

void bd_crypto_luks_info_free (BDCryptoLUKSInfo *info);
BDCryptoLUKSInfo* bd_crypto_luks_info_copy (BDCryptoLUKSInfo *info);

/**
 * BDCryptoBITLKInfo:
 * @cipher: used cipher (e.g. "aes")
 * @mode: used cipher mode (e.g. "xts-plain")
 * @uuid: UUID of the BITLK device
 * @backing_device: name of the underlying block device
 * @sector_size: size (in bytes) of encryption sector
 */
typedef struct BDCryptoBITLKInfo {
    gchar *cipher;
    gchar *mode;
    gchar *uuid;
    gchar *backing_device;
    guint32 sector_size;
} BDCryptoBITLKInfo;

void bd_crypto_bitlk_info_free (BDCryptoBITLKInfo *info);
BDCryptoBITLKInfo* bd_crypto_bitlk_info_copy (BDCryptoBITLKInfo *info);

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

/**
 * BDCryptoLUKSTokenInfo:
 * @id: ID of the token
 * @type: type of the token
 * @keyslot: keyslot this token is assigned to or -1 for inactive/unassigned tokens
 */
typedef struct BDCryptoLUKSTokenInfo {
    guint id;
    gchar *type;
    gint keyslot;
} BDCryptoLUKSTokenInfo;

void bd_crypto_luks_token_info_free (BDCryptoLUKSTokenInfo *info);
BDCryptoLUKSTokenInfo* bd_crypto_luks_token_info_copy (BDCryptoLUKSTokenInfo *info);

typedef struct _BDCryptoKeyslotContext BDCryptoKeyslotContext;

void bd_crypto_keyslot_context_free (BDCryptoKeyslotContext *context);
BDCryptoKeyslotContext* bd_crypto_keyslot_context_copy (BDCryptoKeyslotContext *context);

BDCryptoKeyslotContext* bd_crypto_keyslot_context_new_passphrase (const guint8 *pass_data, gsize data_len, GError **error);
BDCryptoKeyslotContext* bd_crypto_keyslot_context_new_keyfile (const gchar *keyfile, guint64 keyfile_offset, gsize key_size, GError **error);
BDCryptoKeyslotContext* bd_crypto_keyslot_context_new_keyring (const gchar *key_desc, GError **error);
BDCryptoKeyslotContext* bd_crypto_keyslot_context_new_volume_key (const guint8 *volume_key, gsize volume_key_size, GError **error);

/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_crypto_init (void);
void bd_crypto_close (void);

gboolean bd_crypto_is_tech_avail (BDCryptoTech tech, guint64 mode, GError **error);

gchar* bd_crypto_generate_backup_passphrase(GError **error);
gboolean bd_crypto_device_is_luks (const gchar *device, GError **error);
const gchar* bd_crypto_luks_status (const gchar *luks_device, GError **error);

gboolean bd_crypto_luks_format (const gchar *device, const gchar *cipher, guint64 key_size, BDCryptoKeyslotContext *context, guint64 min_entropy, BDCryptoLUKSVersion luks_version, BDCryptoLUKSExtra *extra,GError **error);
gboolean bd_crypto_luks_open (const gchar *device, const gchar *name, BDCryptoKeyslotContext *context, gboolean read_only, GError **error);
gboolean bd_crypto_luks_close (const gchar *luks_device, GError **error);
gboolean bd_crypto_luks_add_key (const gchar *device, BDCryptoKeyslotContext *context, BDCryptoKeyslotContext *ncontext, GError **error);
gboolean bd_crypto_luks_remove_key (const gchar *device, BDCryptoKeyslotContext *context, GError **error);
gboolean bd_crypto_luks_change_key (const gchar *device, BDCryptoKeyslotContext *context, BDCryptoKeyslotContext *ncontext, GError **error);
gboolean bd_crypto_luks_resize (const gchar *luks_device, guint64 size, BDCryptoKeyslotContext *context, GError **error);
gboolean bd_crypto_luks_suspend (const gchar *luks_device, GError **error);
gboolean bd_crypto_luks_resume (const gchar *luks_device, BDCryptoKeyslotContext *context, GError **error);
gboolean bd_crypto_luks_kill_slot (const gchar *device, gint slot, GError **error);
gboolean bd_crypto_luks_header_backup (const gchar *device, const gchar *backup_file, GError **error);
gboolean bd_crypto_luks_header_restore (const gchar *device, const gchar *backup_file, GError **error);
gboolean bd_crypto_luks_set_label (const gchar *device, const gchar *label, const gchar *subsystem, GError **error);
gboolean bd_crypto_luks_set_uuid (const gchar *device, const gchar *uuid, GError **error);
gboolean bd_crypto_luks_convert (const gchar *device, BDCryptoLUKSVersion target_version, GError **error);

BDCryptoLUKSInfo* bd_crypto_luks_info (const gchar *device, GError **error);
BDCryptoBITLKInfo* bd_crypto_bitlk_info (const gchar *device, GError **error);
BDCryptoIntegrityInfo* bd_crypto_integrity_info (const gchar *device, GError **error);
BDCryptoLUKSTokenInfo** bd_crypto_luks_token_info (const gchar *device, GError **error);

gboolean bd_crypto_integrity_format (const gchar *device, const gchar *algorithm, gboolean wipe, BDCryptoKeyslotContext *context, BDCryptoIntegrityExtra *extra, GError **error);
gboolean bd_crypto_integrity_open (const gchar *device, const gchar *name, const gchar *algorithm, BDCryptoKeyslotContext *context, BDCryptoIntegrityOpenFlags flags, BDCryptoIntegrityExtra *extra, GError **error);
gboolean bd_crypto_integrity_close (const gchar *integrity_device, GError **error);

gboolean bd_crypto_keyring_add_key (const gchar *key_desc, const guint8 *key_data, gsize data_len, GError **error);

gboolean bd_crypto_device_seems_encrypted (const gchar *device, GError **error);
gboolean bd_crypto_tc_open (const gchar *device, const gchar *name, BDCryptoKeyslotContext *context, const gchar **keyfiles, gboolean hidden, gboolean system, gboolean veracrypt, guint32 veracrypt_pim, gboolean read_only, GError **error);
gboolean bd_crypto_tc_close (const gchar *tc_device, GError **error);

gboolean bd_crypto_bitlk_open (const gchar *device, const gchar *name, BDCryptoKeyslotContext *context, gboolean read_only, GError **error);
gboolean bd_crypto_bitlk_close (const gchar *bitlk_device, GError **error);

gboolean bd_crypto_fvault2_open (const gchar *device, const gchar *name, BDCryptoKeyslotContext *context, gboolean read_only, GError **error);
gboolean bd_crypto_fvault2_close (const gchar *fvault2_device, GError **error);

gboolean bd_crypto_escrow_device (const gchar *device, const gchar *passphrase, const gchar *cert_data, const gchar *directory, const gchar *backup_passphrase, GError **error);

gboolean bd_crypto_opal_is_supported (const gchar *device, GError **error);
gboolean bd_crypto_opal_wipe_device (const gchar *device, BDCryptoKeyslotContext *context, GError **error);
gboolean bd_crypto_opal_reset_device (const gchar *device, BDCryptoKeyslotContext *context, GError **error);
gboolean bd_crypto_opal_format (const gchar *device, const gchar *cipher, guint64 key_size, BDCryptoKeyslotContext *context, guint64 min_entropy, BDCryptoLUKSHWEncryptionType hw_encryption,
                                BDCryptoKeyslotContext *opal_context, BDCryptoLUKSExtra *extra, GError **error);
#endif  /* BD_CRYPTO */
