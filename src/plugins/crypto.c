/*
 * Copyright (C) 2014  Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <string.h>
#include <glib.h>
#include <libcryptsetup.h>
#include "crypto.h"

/**
 * SECTION: crypto
 * @short_description: libblockdev plugin for operations with encrypted devices
 * @title: Crypto
 * @include: crypto.h
 *
 * A libblockdev plugin for operations with encrypted devices. For now, only
 * LUKS devices are supported.
 *
 * Functions taking a parameter called "device" require the backing device to be
 * passed. On the other hand functions taking the "luks_device" parameter
 * require the LUKS device (/dev/mapper/SOMETHING").
 *
 * Sizes are given in bytes unless stated otherwise.
 */

/**
 * bd_crypto_generate_backup_passphrase:
 *
 * Returns: (transfer full): A newly generated %BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH-long passphrase.
 *
 * See %BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET for the definition of the charset used for the passphrase.
 */
gchar* bd_crypto_generate_backup_passphrase() {
    guint8 i = 0;
    guint8 offset = 0;
    guint8 charset_length = strlen (BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET);

    /* passphrase with groups of 5 characters separated with dashes */
    gchar *ret = g_new (gchar, BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH + (BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH / 5) - 1);

    for (i=0; i < BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH; i++) {
        if (i > 0 && (i % 5 == 0)) {
            /* put a dash between every 5 characters */
            ret[i+offset] = '-';
            offset++;
        }
        ret[i+offset] = BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET[g_random_int_range(0, charset_length)];
    }

    return ret;
}

/**
 * bd_crypto_device_is_luks:
 * @device: the queried device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: %TRUE if the given @device is a LUKS device or %FALSE if not or
 * failed to determine (the @error_message is populated with the error in such
 * cases)
 */
gboolean bd_crypto_device_is_luks (gchar *device, gchar **error_message) {
    struct crypt_device *cd = NULL;
    gint ret;

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to initialize device: %s", strerror(-ret));
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS1, NULL);
    crypt_free (cd);
    return (ret == 0);
}

/**
 * bd_crypto_luks_uuid:
 * @device: the queried device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: (transfer full): UUID of the @device or %NULL if failed to determine (@error_message
 * is populated with the error in such cases)
 */
gchar* bd_crypto_luks_uuid (gchar *device, gchar **error_message) {
    struct crypt_device *cd = NULL;
    gint ret_num;
    const gchar *ret;

    ret_num = crypt_init (&cd, device);
    if (ret_num != 0) {
        *error_message = g_strdup_printf ("Failed to initialize device: %s", strerror(-ret_num));
        return NULL;
    }

    ret_num = crypt_load (cd, CRYPT_LUKS1, NULL);
    if (ret_num != 0) {
        *error_message = g_strdup_printf ("Failed to load device: %s", strerror(-ret_num));
        return NULL;
    }

    ret = crypt_get_uuid (cd);
    if (ret)
        ret = g_strdup (ret);
    crypt_free (cd);

    return ret;
}

/**
 * bd_crypto_luks_status:
 * @luks_device: the queried LUKS device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: (transfer none): one of "invalid", "inactive", "active" or "busy" or
 * %NULL if failed to determine (@error_message is populated with the error in
 * such cases)
 */
gchar* bd_crypto_luks_status (gchar *luks_device, gchar **error_message) {
    struct crypt_device *cd = NULL;
    gint ret_num;
    gchar *ret = NULL;
    crypt_status_info status;

    ret_num = crypt_init (&cd, luks_device);
    if (ret_num != 0) {
        *error_message = g_strdup_printf ("Failed to initialize device: %s", strerror(-ret_num));
        return NULL;
    }

    status = crypt_status (cd, luks_device);
    switch (status) {
    case CRYPT_INVALID:
        ret = "invalid";
        break;
    case CRYPT_INACTIVE:
        ret = "inactive";
        break;
    case CRYPT_ACTIVE:
        ret = "active";
        break;
    case CRYPT_BUSY:
        ret = "busy";
        break;
    default:
        ret = NULL;
        *error_message = g_strdup ("Unknown device's state");
    }

    crypt_free (cd);
    return ret;
}

static int give_passphrase (const char *msg __attribute__((unused)), char *buf, size_t length __attribute__((unused)), void *usrptr) {
    if (usrptr) {
        strcpy (buf, (char*) usrptr);
        return strlen ((char*) usrptr);
    } else
        return 0;
}

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
gboolean bd_crypto_luks_format (gchar *device, gchar *cipher, guint64 key_size, gchar *passphrase, gchar *key_file, gchar **error_message) {
    struct crypt_device *cd = NULL;
    gint ret;
    gchar **cipher_specs = NULL;

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to initialize device: %s", strerror(-ret));
        return FALSE;
    }

    cipher = cipher ? cipher : DEFAULT_LUKS_CIPHER;
    cipher_specs = g_strsplit (cipher, "-", 2);
    if (g_strv_length (cipher_specs) != 2) {
        *error_message = g_strdup_printf ("Invalid cipher specification: '%s'", cipher);
        g_strfreev (cipher_specs);
        return FALSE;
    }

    /* resolve requested/default key_size (should be in bytes) */
    key_size = (key_size != 0) ? key_size : (DEFAULT_LUKS_KEYSIZE_BITS / 8);

    ret = crypt_format (cd, CRYPT_LUKS1, cipher_specs[0], cipher_specs[1],
                        NULL, NULL, key_size, NULL);
    g_strfreev (cipher_specs);

    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to format device: %s", strerror(-ret));
        return FALSE;
    }

    if (passphrase) {
        ret = crypt_keyslot_add_by_volume_key (cd, CRYPT_ANY_SLOT, NULL, 0, passphrase, strlen(passphrase));
        if (ret < 0) {
            *error_message = g_strdup_printf ("Failed to add passphrase: %s", strerror(-ret));
            return FALSE;
        }
    }

    if (key_file) {
        crypt_set_password_callback (cd, give_passphrase, (void*) passphrase);
        ret = crypt_keyslot_add_by_keyfile (cd, CRYPT_ANY_SLOT, NULL, 0, key_file, 0);
        if (ret < 0) {
            *error_message = g_strdup_printf ("Failed to add key file: %s", strerror(-ret));
            return FALSE;
        }
    }

    return TRUE;
}
