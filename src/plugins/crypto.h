#include <glib.h>

#ifndef BD_CRYPTO
#define BD_CRYPTO

#define BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz./"

/* KEEP THIS A MULTIPLE OF 5 SO THAT '-' CAN BE INSERTED BETWEEN EVERY 5 CHARACTERS! */
/* 20 chars * 6 bits per char (64-item charset) = 120 "bits of security" */
#define BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH 20

gchar* bd_crypto_generate_backup_passphrase();
gboolean bd_crypto_device_is_luks (gchar *device, gchar **error_message);

#endif  /* BD_CRYPTO */
