#include <glib.h>

/**
 * bd_crypto_generate_backup_passphrase:
 *
 * Returns: A newly generated %BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH-long passphrase.
 *
 * See %BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET for the definition of the charset used for the passphrase.
 */
gchar* bd_crypto_generate_backup_passphrase();

/**
 * bd_crypto_device_is_luks:
 * @device: the queried device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: %TRUE if the given @device is a LUKS device or %FALSE if not or
 * failed to determine (the @error_message is populated with the error in such
 * cases)
 */
gboolean bd_crypto_device_is_luks (gchar *device, gchar **error_message);

/**
 * bd_crypto_luks_uuid:
 * @device: the queried device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: UUID of the @device or %NULL if failed to determine (@error_message
 * is populated with the error in such cases)
 */
gchar* bd_crypto_luks_uuid (gchar *device, gchar **error_message);

/**
 * bd_crypto_luks_status:
 * @luks_device: the queried LUKS device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: (transfer none): one of "invalid", "inactive", "active" or "busy" or
 * %NULL if failed to determine (@error_message is populated with the error in
 * such cases)
 */
gchar* bd_crypto_luks_status (gchar *luks_device, gchar **error_message);

/**
 * bd_crypto_luks_format:
 * @device: a device to format as LUKS
 * @cipher: (allow-none): cipher specification (type-mode, e.g. "aes-xts-plain64") or %NULL to use the default
 * @key_size: size of the volume key or 0 to use the default
 * @passphrase: (allow-none): a passphrase for the new LUKS device or %NULL if not requested
 * @key_file: (allow-none): a key file for the new LUKS device or %NULL if not requested
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the given @device was successfully formatted as LUKS or not
 * (the @error_message contains the error in such cases)
 */
gboolean bd_crypto_luks_format (gchar *device, gchar *cipher, guint64 key_size, gchar *passphrase, gchar *key_file, gchar **error_message);
