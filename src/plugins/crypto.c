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
gboolean bd_crypto_luks_open (gchar *device, gchar *name, gchar *passphrase, gchar *key_file, gchar **error_message) {
    struct crypt_device *cd = NULL;
    gint ret = 0;

    if (!passphrase && !key_file) {
        *error_message = g_strdup ("No passphrase nor key file specified, cannot open.");
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to initialize device: %s", strerror(-ret));
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS1, NULL);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to load device's parameters: %s", strerror(-ret));
        return FALSE;
    }

    if (passphrase)
        ret = crypt_activate_by_passphrase (cd, name, CRYPT_ANY_SLOT, passphrase, strlen(passphrase), 1);
    else
        ret = crypt_activate_by_keyfile (cd, name, CRYPT_ANY_SLOT, key_file, 0, 0);

    if (ret < 0) {
        *error_message = g_strdup_printf ("Failed to activate device: %s", strerror(-ret));
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_crypto_luks_close:
 * @luks_device: LUKS device to close
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the given @luks_device was successfully closed or not
 */
gboolean bd_crypto_luks_close (gchar *luks_device, gchar **error_message) {
    struct crypt_device *cd = NULL;
    gint ret = 0;

    ret = crypt_init (&cd, luks_device);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to initialize device: %s", strerror(-ret));
        return FALSE;
    }

    ret = crypt_deactivate (cd, luks_device);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to deactivate device: %s", strerror(-ret));
        return FALSE;
    }

    return TRUE;
}

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
 * @nkey_file. If adding @npass, @pass is required.
 */
gboolean bd_crypto_luks_add_key (gchar *device, gchar *pass, gchar *key_file, gchar *npass, gchar *nkey_file, gchar **error_message) {
    struct crypt_device *cd = NULL;
    gint ret = 0;

    if (!pass && !key_file) {
        *error_message = g_strdup ("No passphrase nor key file given, cannot add key.");
        return FALSE;
    }

    if (!npass && !nkey_file) {
        *error_message = g_strdup ("No new passphrase nor key file given, nothing to add.");
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to initialize device: %s", strerror(-ret));
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS1, NULL);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to load device's parameters: %s", strerror(-ret));
        return FALSE;
    }

    crypt_set_password_callback (cd, give_passphrase, (void*) pass);
    if (npass)
        ret = crypt_keyslot_add_by_passphrase (cd, CRYPT_ANY_SLOT, NULL, 0, npass, strlen(npass));
    else
        /* let it use the function giving the passphrase if key_file is NULL */
        ret = crypt_keyslot_add_by_keyfile (cd, CRYPT_ANY_SLOT, key_file, 0, nkey_file, 0);

    if (ret < 0) {
        *error_message = g_strdup_printf ("Failed to add key: %s", strerror(-ret));
        return FALSE;
    }

    return TRUE;
}

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
gboolean bd_crypto_luks_remove_key (gchar *device, gchar *pass, gchar *key_file, gchar **error_message) {
    struct crypt_device *cd = NULL;
    gint ret = 0;

    if (!pass && !key_file) {
        *error_message = g_strdup ("No passphrase nor key file given, cannot remove key.");
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to initialize device: %s", strerror(-ret));
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS1, NULL);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to load device's parameters: %s", strerror(-ret));
        return FALSE;
    }

    crypt_set_password_callback (cd, give_passphrase, (void*) pass);
    if (pass)
        ret = crypt_activate_by_passphrase (cd, NULL, CRYPT_ANY_SLOT, pass, strlen(pass), 0);
    else
        ret = crypt_activate_by_keyfile (cd, NULL, CRYPT_ANY_SLOT, key_file, 0, 0);

    if (ret < 0) {
        *error_message = g_strdup_printf ("Failed to determine key slot: %s", strerror(-ret));
        return FALSE;
    }

    ret = crypt_keyslot_destroy (cd, ret);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to remove key: %s", strerror(-ret));
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_crypto_luks_resize:
 * @device: device to resize
 * @size: requested size in sectors or 0 to adapt to the backing device
 * @error_message: (out): variable to store error message to (if any)
 *
 * Returns: whether the @device was successfully resized or not
 */
gboolean bd_crypto_luks_resize (gchar *device, guint64 size, gchar **error_message) {
    struct crypt_device *cd = NULL;
    gint ret = 0;

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to initialize device: %s", strerror(-ret));
        return FALSE;
    }

    ret = crypt_resize (cd, device, size);
    if (ret != 0) {
        *error_message = g_strdup_printf ("Failed to resize device: %s", strerror(-ret));
        return FALSE;
    }

    return TRUE;
}
