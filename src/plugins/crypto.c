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
#include <nss.h>
#include <libvolume_key.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <linux/random.h>
#include <locale.h>
#include <unistd.h>

#include "crypto.h"

/**
 * SECTION: crypto
 * @short_description: plugin for operations with encrypted devices
 * @title: Crypto
 * @include: crypto.h
 *
 * A plugin for operations with encrypted devices. For now, only
 * LUKS devices are supported.
 *
 * Functions taking a parameter called "device" require the backing device to be
 * passed. On the other hand functions taking the "luks_device" parameter
 * require the LUKS device (/dev/mapper/SOMETHING").
 *
 * Sizes are given in bytes unless stated otherwise.
 */

/* "C" locale to get the locale-agnostic error messages */
static locale_t c_locale = (locale_t) 0;

/**
 * init: (skip)
 */
gboolean init () {
    c_locale = newlocale (LC_ALL_MASK, "C", c_locale);
    return TRUE;
}

/**
 * bd_crypto_error_quark: (skip)
 */
GQuark bd_crypto_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-crypto-error-quark");
}

/**
 * bd_crypto_generate_backup_passphrase:
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): A newly generated %BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH-long passphrase.
 *
 * See %BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET for the definition of the charset used for the passphrase.
 */
gchar* bd_crypto_generate_backup_passphrase(GError **error __attribute__((unused))) {
    guint8 i = 0;
    guint8 offset = 0;
    guint8 charset_length = strlen (BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET);

    /* passphrase with groups of 5 characters separated with dashes, plus a null terminator */
    gchar *ret = g_new0 (gchar, BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH + (BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH / 5));

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
 * @error: (out): place to store error (if any)
 *
 * Returns: %TRUE if the given @device is a LUKS device or %FALSE if not or
 * failed to determine (the @error) is populated with the error in such
 * cases)
 */
gboolean bd_crypto_device_is_luks (const gchar *device, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret;

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS1, NULL);
    crypt_free (cd);
    return (ret == 0);
}

/**
 * bd_crypto_luks_uuid:
 * @device: the queried device
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): UUID of the @device or %NULL if failed to determine (@error)
 * is populated with the error in such cases)
 */
gchar* bd_crypto_luks_uuid (const gchar *device, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret_num;
    gchar *ret;

    ret_num = crypt_init (&cd, device);
    if (ret_num != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret_num, c_locale));
        return NULL;
    }

    ret_num = crypt_load (cd, CRYPT_LUKS1, NULL);
    if (ret_num != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device: %s", strerror_l(-ret_num, c_locale));
        crypt_free (cd);
        return NULL;
    }

    ret = g_strdup (crypt_get_uuid (cd));
    crypt_free (cd);

    return ret;
}

/**
 * bd_crypto_luks_status:
 * @luks_device: the queried LUKS device
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer none): one of "invalid", "inactive", "active" or "busy" or
 * %NULL if failed to determine (@error is populated with the error in
 * such cases)
 */
gchar* bd_crypto_luks_status (const gchar *luks_device, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret_num;
    gchar *ret = NULL;
    crypt_status_info status;

    ret_num = crypt_init_by_name (&cd, luks_device);
    if (ret_num != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret_num, c_locale));
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
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_STATE,
                     "Unknown device's state");
    }

    crypt_free (cd);
    return ret;
}

static int give_passphrase (const char *msg __attribute__((unused)), char *buf, size_t length __attribute__((unused)), void *usrptr) {
    if (usrptr) {
        strcpy (buf, (const char*) usrptr);
        return strlen ((const char*) usrptr);
    } else
        return 0;
}

/**
 * bd_crypto_luks_format:
 * @device: a device to format as LUKS
 * @cipher: (allow-none): cipher specification (type-mode, e.g. "aes-xts-plain64") or %NULL to use the default
 * @key_size: size of the volume key in bits or 0 to use the default
 * @passphrase: (allow-none): a passphrase for the new LUKS device or %NULL if not requested
 * @key_file: (allow-none): a key file for the new LUKS device or %NULL if not requested
 * @min_entropy: minimum random data entropy (in bits) required to format @device as LUKS
 * @error: (out): place to store error (if any)
 *
 * Formats the given @device as LUKS according to the other parameters given. If
 * @min_entropy is specified (greater than 0), the function waits for enough
 * entropy to be available in the random data pool (WHICH MAY POTENTIALLY TAKE
 * FOREVER).
 *
 * Either @passhphrase or @key_file has to be != %NULL.
 *
 * Returns: whether the given @device was successfully formatted as LUKS or not
 * (the @error) contains the error in such cases)
 */
gboolean bd_crypto_luks_format (const gchar *device, const gchar *cipher, guint64 key_size, const gchar *passphrase, const gchar *key_file, guint64 min_entropy, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret;
    gchar **cipher_specs = NULL;
    guint32 current_entropy = 0;
    gint dev_random_fd = -1;

    if (!passphrase && !key_file) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NO_KEY,
                     "At least one of passphrase and key file have to be specified!");
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        return FALSE;
    }

    cipher = cipher ? cipher : DEFAULT_LUKS_CIPHER;
    cipher_specs = g_strsplit (cipher, "-", 2);
    if (g_strv_length (cipher_specs) != 2) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_SPEC,
                     "Invalid cipher specification: '%s'", cipher);
        crypt_free (cd);
        g_strfreev (cipher_specs);
        return FALSE;
    }

    /* resolve requested/default key_size (should be in bytes) */
    key_size = (key_size != 0) ? (key_size / 8) : (DEFAULT_LUKS_KEYSIZE_BITS / 8);

    /* wait for enough random data entropy (if requested) */
    if (min_entropy > 0) {
        dev_random_fd = open ("/dev/random", O_RDONLY);
        ioctl (dev_random_fd, RNDGETENTCNT, &current_entropy);
        while (current_entropy < min_entropy) {
            sleep (1);
            ioctl (dev_random_fd, RNDGETENTCNT, &current_entropy);
        }
        close (dev_random_fd);
    }

    ret = crypt_format (cd, CRYPT_LUKS1, cipher_specs[0], cipher_specs[1],
                        NULL, NULL, key_size, NULL);
    g_strfreev (cipher_specs);

    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_FORMAT_FAILED,
                     "Failed to format device: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    if (passphrase) {
        ret = crypt_keyslot_add_by_volume_key (cd, CRYPT_ANY_SLOT, NULL, 0, passphrase, strlen(passphrase));
        if (ret < 0) {
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ADD_KEY,
                         "Failed to add passphrase: %s", strerror_l(-ret, c_locale));
            crypt_free (cd);
            return FALSE;
        }
    }

    if (key_file) {
        crypt_set_password_callback (cd, give_passphrase, (void*) passphrase);
        ret = crypt_keyslot_add_by_keyfile (cd, CRYPT_ANY_SLOT, NULL, 0, key_file, 0);
        if (ret < 0) {
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ADD_KEY,
                         "Failed to add key file: %s", strerror_l(-ret, c_locale));
            crypt_free (cd);
            return FALSE;
        }
    }

    crypt_free (cd);
    return TRUE;
}

/**
 * bd_crypto_luks_open:
 * @device: the device to open
 * @name: name for the LUKS device
 * @passphrase: (allow-none): passphrase to open the @device or %NULL
 * @key_file: (allow-none): key file path to use for opening the @device or %NULL
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully opened or not
 *
 * One of @passphrase, @key_file has to be != %NULL.
 */
gboolean bd_crypto_luks_open (const gchar *device, const gchar *name, const gchar *passphrase, const gchar *key_file, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;

    if (!passphrase && !key_file) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NO_KEY,
                     "No passphrase nor key file specified, cannot open.");
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS1, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    if (passphrase)
        ret = crypt_activate_by_passphrase (cd, name, CRYPT_ANY_SLOT, passphrase, strlen(passphrase), 0);
    else
        ret = crypt_activate_by_keyfile (cd, name, CRYPT_ANY_SLOT, key_file, 0, 0);

    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to activate device: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    crypt_free (cd);
    return TRUE;
}

/**
 * bd_crypto_luks_close:
 * @luks_device: LUKS device to close
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given @luks_device was successfully closed or not
 */
gboolean bd_crypto_luks_close (const gchar *luks_device, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;

    ret = crypt_init_by_name (&cd, luks_device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        return FALSE;
    }

    ret = crypt_deactivate (cd, luks_device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to deactivate device: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    crypt_free (cd);
    return TRUE;
}

/**
 * bd_crypto_luks_add_key:
 * @device: device to add new key to
 * @pass: (allow-none): passphrase for the @device or %NULL
 * @key_file: (allow-none): key file for the @device or %NULL
 * @npass: (allow-none): passphrase to add to @device or %NULL
 * @nkey_file: (allow-none): key file to add to @device or %NULL
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @npass or @nkey_file was successfully added to @device
 * or not
 *
 * One of @pass, @key_file has to be != %NULL and the same applies to @npass,
 * @nkey_file.
 */
gboolean bd_crypto_luks_add_key (const gchar *device, const gchar *pass, const gchar *key_file, const gchar *npass, const gchar *nkey_file, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;

    if (!pass && !key_file) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NO_KEY,
                     "No passphrase nor key file given, cannot add key.");
        return FALSE;
    }

    if (!npass && !nkey_file) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NO_KEY,
                     "No new passphrase nor key file given, nothing to add.");
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS1, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    if (pass && npass)
        ret = crypt_keyslot_add_by_passphrase (cd, CRYPT_ANY_SLOT, pass, strlen(pass), npass, strlen(npass));
    else {
        if (pass)
            /* give the old password (required if key_file is NULL) */
            crypt_set_password_callback (cd, give_passphrase, (void*) pass);
        else
            /* give the new password (required if nkey_file is NULL */
            crypt_set_password_callback (cd, give_passphrase, (void*) npass);
        ret = crypt_keyslot_add_by_keyfile (cd, CRYPT_ANY_SLOT, key_file, 0, nkey_file, 0);
    }

    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ADD_KEY,
                     "Failed to add key: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    crypt_free (cd);
    return TRUE;
}

/**
 * bd_crypto_luks_remove_key:
 * @device: device to add new key to
 * @pass: (allow-none): passphrase for the @device or %NULL
 * @key_file: (allow-none): key file for the @device or %NULL
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the key was successfully removed or not
 *
 * Either @pass or @key_file has to be != %NULL.
 */
gboolean bd_crypto_luks_remove_key (const gchar *device, const gchar *pass, const gchar *key_file, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;

    if (!pass && !key_file) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_REMOVE_KEY,
                     "No passphrase nor key file given, cannot remove key.");
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS1, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    crypt_set_password_callback (cd, give_passphrase, (void*) pass);
    if (pass)
        ret = crypt_activate_by_passphrase (cd, NULL, CRYPT_ANY_SLOT, pass, strlen(pass), 0);
    else
        ret = crypt_activate_by_keyfile (cd, NULL, CRYPT_ANY_SLOT, key_file, 0, 0);

    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEY_SLOT,
                     "Failed to determine key slot: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    ret = crypt_keyslot_destroy (cd, ret);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_REMOVE_KEY,
                     "Failed to remove key: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    crypt_free (cd);
    return TRUE;
}

/**
 * bd_crypto_luks_change_key:
 * @device: device to change key of
 * @pass: old passphrase
 * @npass: new passphrase
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the key was successfully changed or not
 *
 * No support for changing key files (yet).
 */
gboolean bd_crypto_luks_change_key (const gchar *device, const gchar *pass, const gchar *npass, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    gchar *volume_key = NULL;
    gsize vk_size = 0;

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS1, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    vk_size = crypt_get_volume_key_size(cd);
    volume_key = (gchar *) g_malloc (vk_size);

    ret = crypt_volume_key_get (cd, CRYPT_ANY_SLOT, volume_key, &vk_size, pass, strlen(pass));
    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's volume key: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    /* ret is the number of the slot with the given pass */
    ret = crypt_keyslot_destroy (cd, ret);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_REMOVE_KEY,
                     "Failed to remove the old passphrase: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    ret = crypt_keyslot_add_by_volume_key (cd, ret, volume_key, vk_size, npass, strlen(npass));
    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ADD_KEY,
                     "Failed to add the new passphrase: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    crypt_free (cd);
    return TRUE;
}

/**
 * bd_crypto_luks_resize:
 * @luks_device: opened LUKS device to resize
 * @size: requested size in sectors or 0 to adapt to the backing device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @luks_device was successfully resized or not
 */
gboolean bd_crypto_luks_resize (const gchar *luks_device, guint64 size, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;

    ret = crypt_init_by_name (&cd, luks_device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        return FALSE;
    }

    ret = crypt_resize (cd, luks_device, size);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_RESIZE_FAILED,
                     "Failed to resize device: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    crypt_free (cd);
    return TRUE;
}

static gchar *always_fail_cb (gpointer data __attribute__((unused)), const gchar *prompt __attribute__((unused)), int echo __attribute__((unused))) {
    return NULL;
}

static gchar *give_passphrase_cb (gpointer data, const gchar *prompt __attribute__((unused)), unsigned failed_attempts) {
    if (failed_attempts == 0)
        /* Return a copy of the passphrase that will be freed by volume_key */
        return g_strdup (data);
    return NULL;
}

static void free_passphrase_cb (gpointer data) {
    g_free (data);
}

/**
 * replace_char:
 *
 * Replaces all appereances of @orig in @str with @new (in place).
 */
static gchar *replace_char (gchar *str, gchar orig, gchar new) {
    gchar *pos = str;
    if (!str)
        return str;

    for (pos=str; pos; pos++)
        *pos = *pos == orig ? new : *pos;

    return str;
}

static gboolean write_escrow_data_file (struct libvk_volume *volume, struct libvk_ui *ui, enum libvk_packet_format format, const gchar *out_path,
                                        CERTCertificate *cert, GError **error) {
    gpointer packet_data = NULL;
    gsize packet_data_size = 0;
    GIOChannel *out_file = NULL;
    GIOStatus status = G_IO_STATUS_ERROR;
    gsize bytes_written = 0;
    GError *tmp_error = NULL;

    packet_data = libvk_volume_create_packet_asymmetric_with_format (volume, &packet_data_size, format, cert,
                                                                     ui, LIBVK_PACKET_FORMAT_ASYMMETRIC_WRAP_SECRET_ONLY, error);

    if (!packet_data) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ESCROW_FAILED,
                     "Failed to get escrow data");
        libvk_volume_free (volume);
        return FALSE;
    }

    out_file = g_io_channel_new_file (out_path, "w", error);
    if (!out_file) {
        /* error is already populated */
        g_free (packet_data);
        return FALSE;
    }

    status = g_io_channel_set_encoding (out_file, NULL, error);
    if (status != G_IO_STATUS_NORMAL) {
        g_free(packet_data);

        /* try to shutdown the channel, but if it fails, we cannot do anything about it here */
        g_io_channel_shutdown (out_file, TRUE, &tmp_error);

        /* error is already populated */
        g_io_channel_unref (out_file);
        return FALSE;
    }

    status = g_io_channel_write_chars (out_file, (gchar *) packet_data, (gssize) packet_data_size,
                                       &bytes_written, error);
    g_free (packet_data);
    if (status != G_IO_STATUS_NORMAL) {
        /* try to shutdown the channel, but if it fails, we cannot do anything about it here */
        g_io_channel_shutdown (out_file, TRUE, &tmp_error);

        /* error is already populated */
        g_io_channel_unref (out_file);
        return FALSE;
    }

    if (g_io_channel_shutdown (out_file, TRUE, error) != G_IO_STATUS_NORMAL) {
        /* error is already populated */
        g_io_channel_unref (out_file);
        return FALSE;
    }
    g_io_channel_unref (out_file);

    return TRUE;
}

/**
 * bd_crypto_escrow_device:
 * @device: path of the device to create escrow data for
 * @passphrase: passphrase used for the device
 * @cert_data: (array zero-terminated=1) (element-type gchar): certificate data to use for escrow
 * @directory: directory to put escrow data into
 * @backup_passphrase: (allow-none): backup passphrase for the device or %NULL
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the escrow data was successfully created for @device or not
 */
gboolean bd_crypto_escrow_device (const gchar *device, const gchar *passphrase, const gchar *cert_data, const gchar *directory, const gchar *backup_passphrase, GError **error) {
    struct libvk_volume *volume = NULL;
    struct libvk_ui *ui = NULL;
    gchar *label = NULL;
    gchar *uuid = NULL;
    CERTCertificate *cert = NULL;
    gchar *volume_ident = NULL;
    gchar *out_path = NULL;
    gboolean ret = FALSE;
    gchar *passphrase_copy = NULL;
    gchar *cert_data_copy = NULL;

    if (!NSS_IsInitialized())
        if (NSS_NoDB_Init(NULL) != SECSuccess) {
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NSS_INIT_FAILED,
                         "Failed to initialize NSS");
            return FALSE;
        }

    volume = libvk_volume_open (device, error);
    if (!volume)
        /* error is already populated */
        return FALSE;


    ui = libvk_ui_new ();
    /* not supposed to be called -> always fail */
    libvk_ui_set_generic_cb (ui, always_fail_cb, NULL, NULL);

    /* Create a copy of the passphrase to be used by the passphrase callback.
     * The passphrase will be freed by volume_key via the free callback.
     */
    passphrase_copy = g_strdup (passphrase);
    libvk_ui_set_passphrase_cb (ui, give_passphrase_cb, passphrase_copy, free_passphrase_cb);

    if (libvk_volume_get_secret (volume, LIBVK_SECRET_DEFAULT, ui, error) != 0) {
        /* error is already populated */
        libvk_volume_free (volume);
        libvk_ui_free (ui);
        return FALSE;
    }

    cert_data_copy = g_strdup(cert_data);
    cert = CERT_DecodeCertFromPackage (cert_data_copy, strlen(cert_data_copy));
    if (!cert) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_CERT_DECODE,
                     "Failed to decode the certificate data");
        libvk_volume_free (volume);
        libvk_ui_free (ui);
        g_free(cert_data_copy);
        return FALSE;
    }

    label = libvk_volume_get_label (volume);
    replace_char (label, '/', '_');
    uuid = libvk_volume_get_uuid (volume);
    replace_char (uuid, '/', '_');

    if (label && uuid) {
        volume_ident = g_strdup_printf ("%s-%s", label, uuid);
        g_free (label);
        g_free (uuid);
    } else if (uuid)
        volume_ident = uuid;
    else
        volume_ident = g_strdup ("_unknown");

    out_path = g_strdup_printf ("%s/%s-escrow", directory, volume_ident);
    ret = write_escrow_data_file (volume, ui, LIBVK_SECRET_DEFAULT, out_path, cert, error);
    g_free (out_path);

    if (!ret) {
        CERT_DestroyCertificate (cert);
        libvk_volume_free (volume);
        libvk_ui_free (ui);
        g_free (volume_ident);
        g_free(cert_data_copy);
        return FALSE;
    }

    if (backup_passphrase) {
        if (libvk_volume_add_secret (volume, LIBVK_SECRET_PASSPHRASE, backup_passphrase, strlen (backup_passphrase), error) != 0) {
            /* error is already populated */
            CERT_DestroyCertificate (cert);
            libvk_volume_free (volume);
            libvk_ui_free (ui);
            g_free (volume_ident);
            g_free(cert_data_copy);
            return FALSE;
        }

        out_path = g_strdup_printf ("%s/%s-escrow-backup-passphrase", directory, volume_ident);
        ret = write_escrow_data_file (volume, ui, LIBVK_SECRET_PASSPHRASE, out_path, cert, error);
        g_free (out_path);
    }

    CERT_DestroyCertificate (cert);
    libvk_volume_free (volume);
    libvk_ui_free (ui);
    g_free (volume_ident);
    g_free(cert_data_copy);
    return ret;
}
