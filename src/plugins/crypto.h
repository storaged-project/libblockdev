#include <glib.h>

#ifndef BD_CRYPTO
#define BD_CRYPTO

#define BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz./"

/* KEEP THIS A MULTIPLE OF 5 SO THAT '-' CAN BE INSERTED BETWEEN EVERY 5 CHARACTERS! */
/* 20 chars * 6 bits per char (64-item charset) = 120 "bits of security" */
#define BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH 20

#define DEFAULT_LUKS_KEYSIZE_BITS 256
#define DEFAULT_LUKS_CIPHER "aes-xts-plain64"

gchar* bd_crypto_generate_backup_passphrase();
gboolean bd_crypto_device_is_luks (gchar *device, gchar **error_message);
gchar* bd_crypto_luks_uuid (gchar *device, gchar **error_message);
gchar* bd_crypto_luks_status (gchar *luks_device, gchar **error_message);
gboolean bd_crypto_luks_format (gchar *device, gchar *cipher, guint64 key_size, gchar *passphrase, gchar *key_file, gchar **error_message);

#endif  /* BD_CRYPTO */
