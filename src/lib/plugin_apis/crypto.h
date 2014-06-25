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

/**
 * bd_crypto_luks_open:
 * @device: the device to open
 * @name: name for the LUKS device
 * @passphrase: (allow-none): passphrase to open the @device or %NULL
 * @key_file: (allow-none): key file path to use for opening the @device or %NULL
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @device was successfully opened or not
 *
 * One of @passphrase, @key_file has to be != %NULL.
 */
gboolean bd_crypto_luks_open (gchar *device, gchar *name, gchar *passphrase, gchar *key_file, gchar **error_message);

/**
 * bd_crypto_luks_close:
 * @luks_device: LUKS device to close
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the given @luks_device was successfully closed or not
 */
gboolean bd_crypto_luks_close (gchar *luks_device, gchar **error_message);

/**
 * bd_crypto_luks_add_key:
 * @device: device to add new key to
 * @pass: (allow-none): passphrase for the @device or %NULL
 * @key_file: (allow-none): key file for the @device or %NULL
 * @npass: (allow-none): passphrase to add to @device or %NULL
 * @nkey_file: (allow-none): key file to add to @device or %NULL
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @npass or @nkey_file was successfully added to @device
 * or not
 *
 * One of @pass, @key_file has to be != %NULL and the same applies to @npass,
 * @nkey_file.
 */
gboolean bd_crypto_luks_add_key (gchar *device, gchar *pass, gchar *key_file, gchar *npass, gchar *nkey_file, gchar **error_message);

/**
 * bd_crypto_luks_remove_key:
 * @device: device to add new key to
 * @pass: (allow-none): passphrase for the @device or %NULL
 * @key_file: (allow-none): key file for the @device or %NULL
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the key was successfully removed or not
 *
 * Either @pass or @key_file has to be != %NULL.
 */
gboolean bd_crypto_luks_remove_key (gchar *device, gchar *pass, gchar *key_file, gchar **error_message);

/**
 * bd_crypto_luks_resize:
 * @device: device to resize
 * @size: requested size in sectors or 0 to adapt to the backing device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @device was successfully resized or not
 */
gboolean bd_crypto_luks_resize (gchar *device, guint64 size, gchar **error_message);
