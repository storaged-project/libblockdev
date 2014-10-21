#include <glib.h>

#ifndef BD_CRYPTO
#define BD_CRYPTO

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
} BDCryptoError;

#define BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz./"

/* KEEP THIS A MULTIPLE OF 5 SO THAT '-' CAN BE INSERTED BETWEEN EVERY 5 CHARACTERS! */
/* 20 chars * 6 bits per char (64-item charset) = 120 "bits of security" */
#define BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH 20

#define DEFAULT_LUKS_KEYSIZE_BITS 256
#define DEFAULT_LUKS_CIPHER "aes-xts-plain64"

gchar* bd_crypto_generate_backup_passphrase();
gboolean bd_crypto_device_is_luks (gchar *device, GError **error);
gchar* bd_crypto_luks_uuid (gchar *device, GError **error);
gchar* bd_crypto_luks_status (gchar *luks_device, GError **error);
gboolean bd_crypto_luks_format (gchar *device, gchar *cipher, guint64 key_size, gchar *passphrase, gchar *key_file, GError **error);
gboolean bd_crypto_luks_open (gchar *device, gchar *name, gchar *passphrase, gchar *key_file, GError **error);
gboolean bd_crypto_luks_close (gchar *device, GError **error);
gboolean bd_crypto_luks_add_key (gchar *device, gchar *pass, gchar *key_file, gchar *npass, gchar *nkey_file, GError **error);
gboolean bd_crypto_luks_remove_key (gchar *device, gchar *pass, gchar *key_file, GError **error);
gboolean bd_crypto_luks_change_key (gchar *device, gchar *pass, gchar *npass, GError **error);
gboolean bd_crypto_luks_resize (gchar *device, guint64 size, GError **error);

#endif  /* BD_CRYPTO */
