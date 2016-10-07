#include <glib.h>
#include <utils.h>

#ifndef BD_CRYPTO
#define BD_CRYPTO

#define BD_CRYPTO_LUKS_METADATA_SIZE (2 MiB)

GQuark bd_crypto_error_quark (void);
#define BD_CRYPTO_ERROR bd_crypto_error_quark ()
typedef enum {
    BD_CRYPTO_ERROR_DEVICE,
    BD_CRYPTO_ERROR_STATE,
    BD_CRYPTO_ERROR_INVALID_SPEC,
    BD_CRYPTO_ERROR_FORMAT_FAILED,
    BD_CRYPTO_ERROR_RESIZE_FAILED,
    BD_CRYPTO_ERROR_ADD_KEY,
    BD_CRYPTO_ERROR_REMOVE_KEY,
    BD_CRYPTO_ERROR_NO_KEY,
    BD_CRYPTO_ERROR_KEY_SLOT,
    BD_CRYPTO_ERROR_NSS_INIT_FAILED,
    BD_CRYPTO_ERROR_CERT_DECODE,
    BD_CRYPTO_ERROR_ESCROW_FAILED,
} BDCryptoError;

#define BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz./"

/* KEEP THIS A MULTIPLE OF 5 SO THAT '-' CAN BE INSERTED BETWEEN EVERY 5 CHARACTERS! */
/* 20 chars * 6 bits per char (64-item charset) = 120 "bits of security" */
#define BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH 20

#define DEFAULT_LUKS_KEYSIZE_BITS 256
#define DEFAULT_LUKS_CIPHER "aes-xts-plain64"

/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * check_deps() - check plugin's dependencies, returning TRUE if satisfied
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_crypto_check_deps ();
gboolean bd_crypto_init ();
void bd_crypto_close ();

gchar* bd_crypto_generate_backup_passphrase(GError **error);
gboolean bd_crypto_device_is_luks (const gchar *device, GError **error);
gchar* bd_crypto_luks_uuid (const gchar *device, GError **error);
gchar* bd_crypto_luks_status (const gchar *luks_device, GError **error);
gboolean bd_crypto_luks_format (const gchar *device, const gchar *cipher, guint64 key_size, const gchar *passphrase, const gchar *key_file, guint64 min_entropy, GError **error);
gboolean bd_crypto_luks_open (const gchar *device, const gchar *name, const gchar *passphrase, const gchar *key_file, GError **error);
gboolean bd_crypto_luks_close (const gchar *luks_device, GError **error);
gboolean bd_crypto_luks_add_key (const gchar *device, const gchar *pass, const gchar *key_file, const gchar *npass, const gchar *nkey_file, GError **error);
gboolean bd_crypto_luks_remove_key (const gchar *device, const gchar *pass, const gchar *key_file, GError **error);
gboolean bd_crypto_luks_change_key (const gchar *device, const gchar *pass, const gchar *npass, GError **error);
gboolean bd_crypto_luks_resize (const gchar *device, guint64 size, GError **error);
gboolean bd_crypto_escrow_device (const gchar *device, const gchar *passphrase, const gchar *cert_data, const gchar *directory, const gchar *backup_passphrase, GError **error);

#endif  /* BD_CRYPTO */
