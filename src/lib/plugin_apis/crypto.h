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
