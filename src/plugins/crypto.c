/*
 * Copyright (C) 2014  Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 */

#include <string.h>
#include <glib.h>
#include <libcryptsetup.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/random.h>
#include <locale.h>
#include <unistd.h>
#include <errno.h>
#include <blkid.h>
#include <sys/types.h>
#include <keyutils.h>
#include <blockdev/utils.h>

#ifdef WITH_BD_ESCROW
#include <nss.h>
#include <volume_key/libvolume_key.h>
#endif

#include "crypto.h"

#ifdef __clang__
#define ZERO_INIT {}
#else
#define ZERO_INIT {0}
#endif

#define SECTOR_SIZE 512

#define DEFAULT_LUKS_KEYSIZE_BITS 256
#define DEFAULT_LUKS_CIPHER "aes-xts-plain64"

#define SQUARE_LOWER_LIMIT 136
#define SQUARE_UPPER_LIMIT 426
#define SQUARE_BYTES_TO_CHECK 512

#ifdef LIBCRYPTSETUP_24
/* 0 for autodetect since 2.4.0 */
#define DEFAULT_LUKS2_SECTOR_SIZE 0
#else
#define DEFAULT_LUKS2_SECTOR_SIZE 512
#endif


/**
 * SECTION: crypto
 * @short_description: plugin for operations with encrypted devices
 * @title: Crypto
 * @include: crypto.h
 *
 * A plugin for operations with encrypted devices. Currently LUKS, Integrity,
 * TrueCrypt/VeraCrypt, BitLocker and FileVault2 devices are supported.
 * See %BDCryptoTech for full list of supported technologies.
 *
 * Functions taking a parameter called "device" require the backing device to be
 * passed. On the other hand functions taking the "luks_device" parameter
 * require the LUKS device (/dev/mapper/SOMETHING").
 *
 * Sizes are given in bytes unless stated otherwise.
 */

BDCryptoLUKSPBKDF* bd_crypto_luks_pbkdf_copy (BDCryptoLUKSPBKDF *pbkdf) {
    if (pbkdf == NULL)
        return NULL;

    BDCryptoLUKSPBKDF *new_pbkdf = g_new0 (BDCryptoLUKSPBKDF, 1);
    new_pbkdf->type = g_strdup (pbkdf->type);
    new_pbkdf->hash = g_strdup (pbkdf->hash);
    new_pbkdf->max_memory_kb = pbkdf->max_memory_kb;
    new_pbkdf->iterations = pbkdf->iterations;
    new_pbkdf->time_ms = pbkdf->time_ms;
    new_pbkdf->parallel_threads = pbkdf->parallel_threads;

    return new_pbkdf;
}

void bd_crypto_luks_pbkdf_free (BDCryptoLUKSPBKDF *pbkdf) {
    if (pbkdf == NULL)
        return;

    g_free (pbkdf->type);
    g_free (pbkdf->hash);
    g_free (pbkdf);
}

BDCryptoLUKSPBKDF* bd_crypto_luks_pbkdf_new (const gchar *type, const gchar *hash, guint32 max_memory_kb, guint32 iterations, guint32 time_ms, guint32 parallel_threads) {
    BDCryptoLUKSPBKDF *ret = g_new0 (BDCryptoLUKSPBKDF, 1);
    ret->type = g_strdup (type);
    ret->hash = g_strdup (hash);
    ret->max_memory_kb = max_memory_kb;
    ret->iterations = iterations;
    ret->time_ms = time_ms;
    ret->parallel_threads = parallel_threads;

    return ret;
}

BDCryptoLUKSExtra* bd_crypto_luks_extra_copy (BDCryptoLUKSExtra *extra) {
    if (extra == NULL)
        return NULL;

    BDCryptoLUKSExtra *new_extra = g_new0 (BDCryptoLUKSExtra, 1);

    new_extra->integrity = g_strdup (extra->integrity);
    new_extra->data_alignment = extra->data_alignment;
    new_extra->data_device = g_strdup (extra->data_device);
    new_extra->sector_size = extra->sector_size;
    new_extra->label = g_strdup (extra->label);
    new_extra->subsystem = g_strdup (extra->subsystem);
    new_extra->pbkdf = bd_crypto_luks_pbkdf_copy (extra->pbkdf);

    return new_extra;
}

void bd_crypto_luks_extra_free (BDCryptoLUKSExtra *extra) {
    if (extra == NULL)
        return;

    g_free (extra->integrity);
    g_free (extra->data_device);
    g_free (extra->label);
    g_free (extra->subsystem);
    bd_crypto_luks_pbkdf_free (extra->pbkdf);
    g_free (extra);
}

BDCryptoLUKSExtra* bd_crypto_luks_extra_new (guint64 data_alignment, const gchar *data_device, const gchar *integrity, guint32 sector_size, const gchar *label, const gchar *subsystem, BDCryptoLUKSPBKDF *pbkdf) {
    BDCryptoLUKSExtra *ret = g_new0 (BDCryptoLUKSExtra, 1);
    ret->integrity = g_strdup (integrity);
    ret->data_alignment = data_alignment;
    ret->data_device = g_strdup (data_device);
    ret->sector_size = sector_size;
    ret->label = g_strdup (label);
    ret->subsystem = g_strdup (subsystem);
    ret->pbkdf = bd_crypto_luks_pbkdf_copy (pbkdf);

    return ret;
}

BDCryptoIntegrityExtra* bd_crypto_integrity_extra_new (guint32 sector_size, guint64 journal_size, guint journal_watermark, guint journal_commit_time, guint64 interleave_sectors, guint64 tag_size, guint64 buffer_sectors) {
    BDCryptoIntegrityExtra *ret = g_new0 (BDCryptoIntegrityExtra, 1);
    ret->sector_size = sector_size;
    ret->journal_size = journal_size;
    ret->journal_watermark = journal_watermark;
    ret->journal_commit_time = journal_commit_time;
    ret->interleave_sectors = interleave_sectors;
    ret->tag_size = tag_size;
    ret->buffer_sectors = buffer_sectors;

    return ret;
}

BDCryptoIntegrityExtra* bd_crypto_integrity_extra_copy (BDCryptoIntegrityExtra *extra) {
    if (extra == NULL)
        return NULL;

    BDCryptoIntegrityExtra *new_extra = g_new0 (BDCryptoIntegrityExtra, 1);

    new_extra->sector_size = extra->sector_size;
    new_extra->journal_size = extra->journal_size;
    new_extra->journal_watermark = extra->journal_watermark;
    new_extra->journal_commit_time = extra->journal_commit_time;
    new_extra->interleave_sectors = extra->interleave_sectors;
    new_extra->tag_size = extra->tag_size;
    new_extra->buffer_sectors = extra->buffer_sectors;

    return new_extra;
}

void bd_crypto_integrity_extra_free (BDCryptoIntegrityExtra *extra) {
    if (extra == NULL)
        return;

    g_free (extra);
}

void bd_crypto_luks_info_free (BDCryptoLUKSInfo *info) {
    if (info == NULL)
        return;

    g_free (info->cipher);
    g_free (info->mode);
    g_free (info->uuid);
    g_free (info->backing_device);
    g_free (info->label);
    g_free (info->subsystem);
    g_free (info);
}

BDCryptoLUKSInfo* bd_crypto_luks_info_copy (BDCryptoLUKSInfo *info) {
    if (info == NULL)
        return NULL;

    BDCryptoLUKSInfo *new_info = g_new0 (BDCryptoLUKSInfo, 1);

    new_info->version = info->version;
    new_info->cipher = g_strdup (info->cipher);
    new_info->mode = g_strdup (info->mode);
    new_info->uuid = g_strdup (info->uuid);
    new_info->backing_device = g_strdup (info->backing_device);
    new_info->sector_size = info->sector_size;
    new_info->metadata_size = info->metadata_size;
    new_info->label = g_strdup (info->label);
    new_info->subsystem = g_strdup (info->subsystem);

    return new_info;
}

void bd_crypto_bitlk_info_free (BDCryptoBITLKInfo *info) {
    if (info == NULL)
        return;

    g_free (info->cipher);
    g_free (info->mode);
    g_free (info->uuid);
    g_free (info->backing_device);
    g_free (info);
}

BDCryptoBITLKInfo* bd_crypto_bitlk_info_copy (BDCryptoBITLKInfo *info) {
    if (info == NULL)
        return NULL;

    BDCryptoBITLKInfo *new_info = g_new0 (BDCryptoBITLKInfo, 1);

    new_info->cipher = g_strdup (info->cipher);
    new_info->mode = g_strdup (info->mode);
    new_info->uuid = g_strdup (info->uuid);
    new_info->backing_device = g_strdup (info->backing_device);
    new_info->sector_size = info->sector_size;

    return new_info;
}

void bd_crypto_integrity_info_free (BDCryptoIntegrityInfo *info) {
    if (info == NULL)
        return;

    g_free (info->algorithm);
    g_free (info->journal_crypt);
    g_free (info->journal_integrity);
    g_free (info);
}

BDCryptoIntegrityInfo* bd_crypto_integrity_info_copy (BDCryptoIntegrityInfo *info) {
    if (info == NULL)
        return NULL;

    BDCryptoIntegrityInfo *new_info = g_new0 (BDCryptoIntegrityInfo, 1);

    new_info->algorithm = g_strdup (info->algorithm);
    new_info->key_size = info->key_size;
    new_info->sector_size = info->sector_size;
    new_info->tag_size = info->tag_size;
    new_info->interleave_sectors = info->interleave_sectors;
    new_info->journal_size = info->journal_size;
    new_info->journal_crypt = g_strdup (info->journal_crypt);
    new_info->journal_integrity = g_strdup (info->journal_integrity);

    return new_info;
}

void bd_crypto_luks_token_info_free (BDCryptoLUKSTokenInfo *info) {
    if (info == NULL)
        return;

    g_free (info->type);
    g_free (info);
}

BDCryptoLUKSTokenInfo* bd_crypto_luks_token_info_copy (BDCryptoLUKSTokenInfo *info) {
    if (info == NULL)
        return NULL;

    BDCryptoLUKSTokenInfo *new_info = g_new0 (BDCryptoLUKSTokenInfo, 1);

    new_info->id = info->id;
    new_info->type = g_strdup (info->type);
    new_info->keyslot = info->keyslot;

    return new_info;
}

/* "C" locale to get the locale-agnostic error messages */
static locale_t c_locale = (locale_t) 0;

static void crypto_log_redirect (gint level, const gchar *msg, void *usrptr G_GNUC_UNUSED) {
    gchar *message = NULL;

    switch (level) {
        case CRYPT_LOG_DEBUG:
        case CRYPT_LOG_VERBOSE:
            message = g_strdup_printf ("[cryptsetup] %s", msg);
            bd_utils_log (BD_UTILS_LOG_DEBUG, message);
            g_free (message);
            break;
        case CRYPT_LOG_NORMAL:
        case CRYPT_LOG_ERROR:
            message = g_strdup_printf ("[cryptsetup] %s", msg);
            bd_utils_log (BD_UTILS_LOG_INFO, message);
            g_free (message);
            break;
        default:
            bd_utils_log_format (BD_UTILS_LOG_WARNING, "Unknown cryptsetup log level %d.", level);
            message = g_strdup_printf ("[cryptsetup] %s", msg);
            bd_utils_log (BD_UTILS_LOG_INFO, message);
            g_free (message);
            break;

    }
}

/**
 * bd_crypto_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_crypto_init (void) {
#ifdef DEBUG
    crypt_set_debug_level (CRYPT_DEBUG_ALL);
#endif
    c_locale = newlocale (LC_ALL_MASK, "C", c_locale);
    crypt_set_log_callback (NULL, &crypto_log_redirect, NULL);
    return TRUE;
}

/**
 * bd_crypto_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_crypto_close (void) {
    c_locale = (locale_t) 0;
    crypt_set_log_callback (NULL, NULL, NULL);
    crypt_set_debug_level (CRYPT_DEBUG_NONE);
}

/**
 * bd_crypto_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDCryptoTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_crypto_is_tech_avail (BDCryptoTech tech, guint64 mode, GError **error) {
    guint64 ret = 0;
    switch (tech) {
        case BD_CRYPTO_TECH_LUKS:
            ret = mode & (BD_CRYPTO_TECH_MODE_CREATE|BD_CRYPTO_TECH_MODE_OPEN_CLOSE|BD_CRYPTO_TECH_MODE_QUERY|
                          BD_CRYPTO_TECH_MODE_ADD_KEY|BD_CRYPTO_TECH_MODE_REMOVE_KEY|BD_CRYPTO_TECH_MODE_RESIZE|
                          BD_CRYPTO_TECH_MODE_SUSPEND_RESUME|BD_CRYPTO_TECH_MODE_BACKUP_RESTORE);
            if (ret != mode) {
                g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                             "Only 'create', 'open', 'query', 'add-key', 'remove-key', 'resize', 'suspend-resume', 'backup-restore' supported for LUKS");
                return FALSE;
            } else
                return TRUE;
        case BD_CRYPTO_TECH_TRUECRYPT:
            ret = mode & BD_CRYPTO_TECH_MODE_OPEN_CLOSE;
            if (ret != mode) {
                g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                             "Only 'open' supported for TrueCrypt");
                return FALSE;
            } else
                return TRUE;
        case BD_CRYPTO_TECH_ESCROW:
#ifndef WITH_BD_ESCROW
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                         "Escrow technology is not available, libblockdev has been compiled without escrow support.");
            return FALSE;
#endif
            ret = mode & BD_CRYPTO_TECH_MODE_CREATE;
            if (ret != mode) {
                g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                             "Only 'create' supported for device escrow");
                return FALSE;
            } else
                return TRUE;
        case BD_CRYPTO_TECH_INTEGRITY:
            ret = mode & (BD_CRYPTO_TECH_MODE_CREATE|BD_CRYPTO_TECH_MODE_OPEN_CLOSE|BD_CRYPTO_TECH_MODE_QUERY);
            if (ret != mode) {
                g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                             "Only 'create', 'open' and 'query' supported for Integrity");
                return FALSE;
            } else
                return TRUE;
        case BD_CRYPTO_TECH_BITLK:
            ret = mode & (BD_CRYPTO_TECH_MODE_OPEN_CLOSE|BD_CRYPTO_TECH_MODE_QUERY);
            if (ret != mode) {
                g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                             "Only 'open' and 'query' supported for BITLK");
                return FALSE;
            } else
                return TRUE;
        case BD_CRYPTO_TECH_KEYRING:
            ret = mode & BD_CRYPTO_TECH_MODE_ADD_KEY;
            if (ret != mode) {
                g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                             "Only 'add key' supported for kernel keyring");
                return FALSE;
            } else
                return TRUE;
        case BD_CRYPTO_TECH_FVAULT2:
#ifndef LIBCRYPTSETUP_26
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                         "FVAULT2 technology requires libcryptsetup >= 2.6.0");
            return FALSE;
#endif
            ret = mode & BD_CRYPTO_TECH_MODE_OPEN_CLOSE;
            if (ret != mode) {
                g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                             "Only 'open' supported for FVAULT2");
                return FALSE;
            } else
                return TRUE;
        default:
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL, "Unknown technology");
            return FALSE;
    }

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
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): A newly generated %BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH-long passphrase.
 *
 * See %BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET for the definition of the charset used for the passphrase.
 *
 * Tech category: always available
 */
gchar* bd_crypto_generate_backup_passphrase (GError **error G_GNUC_UNUSED) {
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
        ret[i+offset] = BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET[g_random_int_range (0, charset_length)];
    }

    return ret;
}

/**
 * bd_crypto_device_is_luks:
 * @device: the queried device
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: %TRUE if the given @device is a LUKS device or %FALSE if not or
 * failed to determine (the @error) is populated with the error in such
 * cases)
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_QUERY
 */
gboolean bd_crypto_device_is_luks (const gchar *device, GError **error) {
    blkid_probe probe = NULL;
    gint fd = 0;
    gint status = 0;
    const gchar *value = NULL;
    guint n_try = 0;

    probe = blkid_new_probe ();
    if (!probe) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to create a new probe");
        return FALSE;
    }

    fd = open (device, O_RDONLY|O_CLOEXEC);
    if (fd == -1) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to open the device '%s'", device);
        blkid_free_probe (probe);
        return FALSE;
    }

    /* we may need to try multiple times with some delays in case the device is
       busy at the very moment */
    for (n_try=5, status=-1; (status != 0) && (n_try > 0); n_try--) {
        status = blkid_probe_set_device (probe, fd, 0, 0);
        if (status != 0)
            g_usleep (100 * 1000); /* microseconds */
    }
    if (status != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to create a probe for the device '%s'", device);
        blkid_free_probe (probe);
        close (fd);
        return FALSE;
    }

    blkid_probe_enable_partitions (probe, 1);
    blkid_probe_set_partitions_flags (probe, BLKID_PARTS_MAGIC);
    blkid_probe_enable_superblocks (probe, 1);
    blkid_probe_set_superblocks_flags (probe, BLKID_SUBLKS_USAGE | BLKID_SUBLKS_TYPE |
                                              BLKID_SUBLKS_MAGIC | BLKID_SUBLKS_BADCSUM);

    /* we may need to try multiple times with some delays in case the device is
       busy at the very moment */
    for (n_try=5, status=-1; !(status == 0 || status == 1) && (n_try > 0); n_try--) {
        status = blkid_do_safeprobe (probe);
        if (status < 0)
            g_usleep (100 * 1000); /* microseconds */
    }
    if (status < 0) {
        /* -1 or -2 = error during probing*/
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to probe the device '%s'", device);
        blkid_free_probe (probe);
        close (fd);
        return FALSE;
    } else if (status == 1) {
        /* 1 = nothing detected */
        blkid_free_probe (probe);
        close (fd);
        return FALSE;
    }

    status = blkid_probe_lookup_value (probe, "USAGE", &value, NULL);
    if (status != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to get usage for the device '%s'", device);
        blkid_free_probe (probe);
        close (fd);
        return FALSE;
    }

    if (g_strcmp0 (value, "crypto") != 0) {
        blkid_free_probe (probe);
        close (fd);
        return FALSE;
    }

    status = blkid_probe_lookup_value (probe, "TYPE", &value, NULL);
    if (status != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to get filesystem type for the device '%s'", device);
        blkid_free_probe (probe);
        close (fd);
        return FALSE;
    }

    if (g_strcmp0 (value, "crypto_LUKS") != 0) {
        blkid_free_probe (probe);
        close (fd);
        return FALSE;
    }

    blkid_free_probe (probe);
    close (fd);

    return TRUE;
}

/**
 * bd_crypto_luks_status:
 * @luks_device: the queried LUKS device
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer none): one of "invalid", "inactive", "active" or "busy" or
 * %NULL if failed to determine (@error is populated with the error in
 * such cases)
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_QUERY
 */
const gchar* bd_crypto_luks_status (const gchar *luks_device, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret_num;
    const gchar *ret = NULL;
    crypt_status_info status;

    ret_num = crypt_init_by_name (&cd, luks_device);
    if (ret_num != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret_num, c_locale));
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

static struct crypt_pbkdf_type *get_pbkdf_params (BDCryptoLUKSPBKDF *user_pbkdf, GError **error) {
    const struct crypt_pbkdf_type *default_pbkdf = NULL;
    struct crypt_pbkdf_type *new_pbkdf = NULL;

    if (user_pbkdf == NULL)
        return NULL;

    /* crypt_get_pbkdf_default returns default pbkdf parameters only based
       on the luks version -- so for LUKS2 it returns default values for
       argon2 but we also need to be able to provide default values if user
       wants pbkdf2 and only specifies type -- we will use the defaults for
       argon2 and ignore parameters specific to it
       better API for this should be part of cryptsetup 2.0.4
     */
    default_pbkdf = crypt_get_pbkdf_default (CRYPT_LUKS2);
    if (!default_pbkdf) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_FORMAT_FAILED,
                     "Failed to get default values for pbkdf.");
        return NULL;
    }

    new_pbkdf = g_new0 (struct crypt_pbkdf_type, 1);

    new_pbkdf->flags = default_pbkdf->flags;

    if (user_pbkdf->type)
        new_pbkdf->type = user_pbkdf->type;
    else
        new_pbkdf->type = default_pbkdf->type;

    if (user_pbkdf->hash)
        new_pbkdf->hash = user_pbkdf->hash;
    else
        new_pbkdf->hash = default_pbkdf->hash;

    if (user_pbkdf->time_ms)
        new_pbkdf->time_ms = user_pbkdf->time_ms;
    else
        new_pbkdf->time_ms = default_pbkdf->time_ms;

    if (user_pbkdf->iterations) {
        new_pbkdf->iterations = user_pbkdf->iterations;
        /* iterations set manually -> do not run benchmark */
        new_pbkdf->flags = CRYPT_PBKDF_NO_BENCHMARK;
    } else
        new_pbkdf->iterations = default_pbkdf->iterations;

    /* 'max_memory_kb' and 'parallel_threads' are not used in pbkdf2 */
    if (g_strcmp0 (user_pbkdf->type, CRYPT_KDF_PBKDF2) == 0) {
        if (user_pbkdf->max_memory_kb)
            bd_utils_log_format (LOG_WARNING, "'max_memory_kb' is not valid option for 'pbkdf2', ignoring.");

        new_pbkdf->max_memory_kb = 0;
        new_pbkdf->parallel_threads = 0;
    } else {
        if (user_pbkdf->max_memory_kb)
            new_pbkdf->max_memory_kb = user_pbkdf->max_memory_kb;
        else
            new_pbkdf->max_memory_kb = default_pbkdf->max_memory_kb;

        if (user_pbkdf->parallel_threads)
            new_pbkdf->parallel_threads = user_pbkdf->parallel_threads;
        else
            new_pbkdf->parallel_threads = default_pbkdf->parallel_threads;
    }

    return new_pbkdf;
}

typedef enum {
    BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_NONE = 0,
    BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE,
    BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE,
    BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYRING,
    BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_VOLUME_KEY,
} BDCryptoKeyslotContextType;

struct _BDCryptoKeyslotContext {
    BDCryptoKeyslotContextType type;

    union {
        struct {
            guint8 *pass_data;
            gsize data_len;
        } passphrase;

        struct {
            gchar *keyfile;
            guint64 keyfile_offset;
            gsize key_size;
        } keyfile;

        struct {
            gchar *key_desc;
        } keyring;

        struct {
            guint8 *volume_key;
            gsize volume_key_size;
        } volume_key;
    } u;
};

void bd_crypto_keyslot_context_free (BDCryptoKeyslotContext *context) {
    if (context == NULL)
        return;

    if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE)
        g_free (context->u.passphrase.pass_data);
    else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE)
        g_free (context->u.keyfile.keyfile);
    else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYRING)
        g_free (context->u.keyring.key_desc);
    else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_VOLUME_KEY)
        g_free (context->u.volume_key.volume_key);

    g_free (context);
}

BDCryptoKeyslotContext* bd_crypto_keyslot_context_copy (BDCryptoKeyslotContext *context) {
    if (context == NULL)
        return NULL;

    BDCryptoKeyslotContext *new_context = g_new0 (BDCryptoKeyslotContext, 1);
    new_context->type = context->type;

    if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
        new_context->u.passphrase.pass_data = g_new0 (guint8, context->u.passphrase.data_len);
        memcpy (new_context->u.passphrase.pass_data, context->u.passphrase.pass_data, context->u.passphrase.data_len);
        new_context->u.passphrase.data_len = context->u.passphrase.data_len;
    } else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE) {
        new_context->u.keyfile.keyfile = g_strdup (context->u.keyfile.keyfile);
        new_context->u.keyfile.keyfile_offset = context->u.keyfile.keyfile_offset;
        new_context->u.keyfile.key_size = context->u.keyfile.key_size;
    } else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYRING)
        new_context->u.keyring.key_desc = g_strdup (context->u.keyring.key_desc);
    else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_VOLUME_KEY) {
        new_context->u.volume_key.volume_key = g_new0 (guint8, context->u.volume_key.volume_key_size);
        memcpy (new_context->u.volume_key.volume_key, context->u.volume_key.volume_key, context->u.volume_key.volume_key_size);
        new_context->u.volume_key.volume_key_size = context->u.volume_key.volume_key_size;
    }

    return new_context;
}

/**
 * bd_crypto_keyslot_context_new_passphrase:
 * @pass_data: (array length=data_len): a passphrase for the new context (may contain arbitrary binary data)
 * @data_len: length of the @pass_data buffer
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns (transfer full): new %BDCryptoKeyslotContext initialized by passphrase or
 *                          %NULL in case of error
 *
 * Tech category: always available
 */
BDCryptoKeyslotContext* bd_crypto_keyslot_context_new_passphrase (const guint8 *pass_data, gsize data_len, GError **error) {
    BDCryptoKeyslotContext *context = NULL;

    if (!pass_data || data_len == 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NO_KEY,
                     "No passphrase specified.");
        return NULL;
    }

    context = g_new0 (BDCryptoKeyslotContext, 1);

    context->type = BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE;

    context->u.passphrase.pass_data = g_new0 (guint8, data_len);
    memcpy (context->u.passphrase.pass_data, pass_data, data_len);

    context->u.passphrase.data_len = data_len;

    return context;
}

/**
 * bd_crypto_keyslot_context_new_keyfile:
 * @keyfile: a key file for the new context
 * @keyfile_offset: number of bytes to skip at start of @keyfile
 * @key_size: number of bytes to read from @keyfile or 0 for unlimited
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns (transfer full): new %BDCryptoKeyslotContext initialized by key file or
 *                          %NULL in case of error
 *
 * Tech category: always available
 */
BDCryptoKeyslotContext* bd_crypto_keyslot_context_new_keyfile (const gchar *keyfile, guint64 keyfile_offset, gsize key_size, GError **error G_GNUC_UNUSED) {
    BDCryptoKeyslotContext *context = NULL;

    context = g_new0 (BDCryptoKeyslotContext, 1);

    context->type = BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE;

    context->u.keyfile.keyfile = g_strdup (keyfile);
    context->u.keyfile.keyfile_offset = keyfile_offset;
    context->u.keyfile.key_size = key_size;

    return context;
}

/**
 * bd_crypto_keyslot_context_new_keyring:
 * @key_desc: kernel keyring key description
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns (transfer full): new %BDCryptoKeyslotContext initialized by @key_desc or
 *                          %NULL in case of error
 *
 * Note: Keyslot passphrase must be stored in 'user' key type and the key has to be reachable
 *       by process context on behalf of which this function is called.
 *
 * Tech category: always available
 */
BDCryptoKeyslotContext* bd_crypto_keyslot_context_new_keyring (const gchar *key_desc, GError **error G_GNUC_UNUSED) {
    BDCryptoKeyslotContext *context = NULL;

    context = g_new0 (BDCryptoKeyslotContext, 1);

    context->type = BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYRING;

    context->u.keyring.key_desc = g_strdup (key_desc);

    return context;
}

/**
 * bd_crypto_keyslot_context_new_volume_key:
 * @volume_key: (array length=volume_key_size): a volume key for the new context (may contain arbitrary binary data)
 * @volume_key_size: length of the @volume_key_size buffer
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns (transfer full): new %BDCryptoKeyslotContext initialized by volume key or
 *                          %NULL in case of error
 *
 * Tech category: always available
 */
BDCryptoKeyslotContext* bd_crypto_keyslot_context_new_volume_key (const guint8 *volume_key, gsize volume_key_size, GError **error) {
    BDCryptoKeyslotContext *context = NULL;

    if (!volume_key || volume_key_size == 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NO_KEY,
                     "No volume key specified.");
        return NULL;
    }

    context = g_new0 (BDCryptoKeyslotContext, 1);

    context->type = BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_VOLUME_KEY;

    context->u.volume_key.volume_key = g_new0 (guint8, volume_key_size);
    memcpy (context->u.volume_key.volume_key, volume_key, volume_key_size);

    context->u.volume_key.volume_key_size = volume_key_size;

    return context;
}

/**
 * bd_crypto_luks_format:
 * @device: a device to format as LUKS
 * @cipher: (nullable): cipher specification (type-mode, e.g. "aes-xts-plain64") or %NULL to use the default
 * @key_size: size of the volume key in bits or 0 to use the default
 * @context: key slot context (passphrase/keyfile/token...) for this LUKS device
 * @min_entropy: minimum random data entropy (in bits) required to format @device as LUKS
 * @luks_version: whether to use LUKS v1 or LUKS v2
 * @extra: (nullable): extra arguments for LUKS format creation
 * @error: (out) (optional): place to store error (if any)
 *
 * Formats the given @device as LUKS according to the other parameters given. If
 * @min_entropy is specified (greater than 0), the function waits for enough
 * entropy to be available in the random data pool (WHICH MAY POTENTIALLY TAKE
 * FOREVER).
 *
 * Supported @context types for this function: passphrase, key file
 *
 * Returns: whether the given @device was successfully formatted as LUKS or not
 * (the @error) contains the error in such cases)
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_CREATE
 */
gboolean bd_crypto_luks_format (const gchar *device, const gchar *cipher, guint64 key_size, BDCryptoKeyslotContext *context, guint64 min_entropy, BDCryptoLUKSVersion luks_version, BDCryptoLUKSExtra *extra,GError **error) {
    struct crypt_device *cd = NULL;
    gint ret;
    gchar **cipher_specs = NULL;
    guint32 current_entropy = 0;
    gint dev_random_fd = -1;
    gchar *key_buffer = NULL;
    gsize buf_len = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    const gchar* crypt_version = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started formatting '%s' as LUKS device", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (luks_version == BD_CRYPTO_LUKS_VERSION_LUKS1)
        crypt_version = CRYPT_LUKS1;
    else if (luks_version == BD_CRYPTO_LUKS_VERSION_LUKS2)
        crypt_version = CRYPT_LUKS2;
    else {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                     "Unknown or unsupported LUKS version specified");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    cipher = cipher ? cipher : DEFAULT_LUKS_CIPHER;
    cipher_specs = g_strsplit (cipher, "-", 2);
    if (g_strv_length (cipher_specs) != 2) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_SPEC,
                     "Invalid cipher specification: '%s'", cipher);
        crypt_free (cd);
        g_strfreev (cipher_specs);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (key_size == 0) {
        if (g_str_has_prefix (cipher_specs[1], "xts-"))
            key_size = DEFAULT_LUKS_KEYSIZE_BITS * 2;
        else
            key_size = DEFAULT_LUKS_KEYSIZE_BITS;
    }

    /* key_size should be in bytes */
    key_size = key_size / 8;

    /* wait for enough random data entropy (if requested) */
    if (min_entropy > 0) {
        dev_random_fd = open ("/dev/random", O_RDONLY);
        if (dev_random_fd >= 0) {
            ioctl (dev_random_fd, RNDGETENTCNT, &current_entropy);
            while (current_entropy < min_entropy) {
                bd_utils_report_progress (progress_id, 0, "Waiting for enough random data entropy");
                sleep (1);
                ioctl (dev_random_fd, RNDGETENTCNT, &current_entropy);
            }
            close (dev_random_fd);
        } else {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_FORMAT_FAILED,
                         "Failed to check random data entropy level");
            crypt_free (cd);
            g_strfreev (cipher_specs);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    }

    if (extra) {
        if (luks_version == BD_CRYPTO_LUKS_VERSION_LUKS1) {

            if (extra->integrity || extra->sector_size || extra->label || extra->subsystem || extra->pbkdf) {
                g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_PARAMS,
                             "Invalid extra arguments specified. Only `data_alignment`"
                             "and `data_device` are valid for LUKS 1.");
                crypt_free (cd);
                g_strfreev (cipher_specs);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return FALSE;
            }

            struct crypt_params_luks1 params = ZERO_INIT;
            params.data_alignment = extra->data_alignment;
            params.data_device = extra->data_device;
            ret = crypt_format (cd, crypt_version, cipher_specs[0], cipher_specs[1],
                                NULL, NULL, key_size, &params);
        }
        else if (luks_version == BD_CRYPTO_LUKS_VERSION_LUKS2) {
            struct crypt_params_luks2 params = ZERO_INIT;
            struct crypt_pbkdf_type *pbkdf = get_pbkdf_params (extra->pbkdf, &l_error);

            if (pbkdf == NULL && l_error != NULL) {
                crypt_free (cd);
                g_strfreev (cipher_specs);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_prefixed_error (error, l_error,
                                            "Failed to get PBKDF parameters for '%s'.", device);
                return FALSE;
            }

            params.pbkdf = pbkdf;
            params.integrity = extra->integrity;
            params.integrity_params = NULL;
            params.data_alignment = extra->data_alignment;
            params.data_device = extra->data_device;
            params.sector_size = extra->sector_size ? extra->sector_size : DEFAULT_LUKS2_SECTOR_SIZE;
            params.label = extra->label;
            params.subsystem = extra->subsystem;
            ret = crypt_format (cd, crypt_version, cipher_specs[0], cipher_specs[1],
                                NULL, NULL, key_size, &params);
            g_free (pbkdf);
        }
    } else
        ret = crypt_format (cd, crypt_version, cipher_specs[0], cipher_specs[1],
                            NULL, NULL, key_size, NULL);
    g_strfreev (cipher_specs);

    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_FORMAT_FAILED,
                     "Failed to format device: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    bd_utils_report_progress (progress_id, 50, "Format created");
    if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
        ret = crypt_keyslot_add_by_volume_key (cd, CRYPT_ANY_SLOT, NULL, 0,
                                               (const char *) context->u.passphrase.pass_data,
                                               context->u.passphrase.data_len);
        if (ret < 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ADD_KEY,
                         "Failed to add passphrase: %s", strerror_l (-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        bd_utils_report_progress (progress_id, 100, "Added key");
    } else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE) {
        ret = crypt_keyfile_device_read (cd, context->u.keyfile.keyfile, &key_buffer, &buf_len,
                                         context->u.keyfile.keyfile_offset, context->u.keyfile.key_size, 0);
        if (ret != 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEYFILE_FAILED,
                         "Failed to read key from file '%s': %s", context->u.keyfile.keyfile,
                         strerror_l (-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        ret = crypt_keyslot_add_by_volume_key (cd, CRYPT_ANY_SLOT, NULL, 0,
                                               (const char*) key_buffer, buf_len);
        crypt_safe_free (key_buffer);
        if (ret < 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ADD_KEY,
                         "Failed to add key file: %s", strerror_l (-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        bd_utils_report_progress (progress_id, 100, "Added key");
    } else {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                     "Only 'passphrase' and 'key file' context types are valid for LUKS format.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        crypt_free (cd);
        return FALSE;
    }

    crypt_free (cd);

    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_open:
 * @device: the device to open
 * @name: name for the LUKS device
 * @context: key slot context (passphrase/keyfile/token...) to open this LUKS @device
 * @read_only: whether to open as read-only or not (meaning read-write)
 * @error: (out) (optional): place to store error (if any)
 *
 * Supported @context types for this function: passphrase, key file, keyring
 *
 * Returns: whether the @device was successfully opened or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 *
 * Example of using %bd_crypto_luks_open with %BDCryptoKeyslotContext:
 *
 * |[<!-- language="C" -->
 * BDCryptoKeyslotContext *context = NULL;
 *
 * context = bd_crypto_keyslot_context_new_passphrase ("passphrase", 10, NULL);
 * bd_crypto_luks_open ("/dev/vda1", "luks-device", context, FALSE, NULL);
 * ]|
 */
gboolean bd_crypto_luks_open (const gchar *device, const gchar *name, BDCryptoKeyslotContext *context, gboolean read_only, GError **error) {
    struct crypt_device *cd = NULL;
    gchar *key_buffer = NULL;
    gsize buf_len = 0;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started opening '%s' LUKS device", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
        ret = crypt_activate_by_passphrase (cd, name, CRYPT_ANY_SLOT,
                                            (const char *) context->u.passphrase.pass_data,
                                            context->u.passphrase.data_len,
                                            read_only ? CRYPT_ACTIVATE_READONLY : 0);
    } else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE) {
        ret = crypt_keyfile_device_read (cd, context->u.keyfile.keyfile, &key_buffer, &buf_len,
                                         context->u.keyfile.keyfile_offset, context->u.keyfile.key_size, 0);
        if (ret != 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEYFILE_FAILED,
                         "Failed to read key from file '%s: %s", context->u.keyfile.keyfile, strerror_l (-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        ret = crypt_activate_by_passphrase (cd, name, CRYPT_ANY_SLOT, key_buffer, buf_len,
                                            read_only ? CRYPT_ACTIVATE_READONLY : 0);
        crypt_safe_free (key_buffer);
    } else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYRING)
        ret = crypt_activate_by_keyring (cd, name, context->u.keyring.key_desc, CRYPT_ANY_SLOT,
                                         read_only ? CRYPT_ACTIVATE_READONLY : 0);
    else {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                     "Only 'passphrase', 'key file' and 'keyring' context types are valid for LUKS open.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        crypt_free (cd);
        return FALSE;
    }

    if (ret < 0) {
        if (ret == -EPERM)
          g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                       "Failed to activate device: Incorrect passphrase.");
        else
          g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                       "Failed to activate device: %s", strerror_l (-ret, c_locale));

        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

static gboolean _crypto_close (const gchar *device, const gchar *tech_name, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started closing %s device '%s'", tech_name, device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init_by_name (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_deactivate (cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to deactivate device: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_close:
 * @luks_device: LUKS device to close
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given @luks_device was successfully closed or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_luks_close (const gchar *luks_device, GError **error) {
    return _crypto_close (luks_device, "LUKS", error);
}

/**
 * bd_crypto_luks_add_key:
 * @device: device to add new key to
 * @context: key slot context (passphrase/keyfile/token...) for this LUKS @device
 * @ncontext: new key slot context (passphrase/keyfile/token...) to add to this LUKS @device
 * @error: (out) (optional): place to store error (if any)
 *
 * Supported @context types for this function: passphrase, key file
 *
 * Returns: whether the @ncontext was successfully added to @device
 * or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_ADD_KEY
 */
gboolean bd_crypto_luks_add_key (const gchar *device, BDCryptoKeyslotContext *context, BDCryptoKeyslotContext *ncontext, GError **error) {
    gchar *key_buf = NULL;
    gsize buf_len = 0;
    gchar *nkey_buf = NULL;
    gsize nbuf_len = 0;
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    GError *l_error = NULL;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started adding key to the LUKS device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE) {
        ret = crypt_keyfile_device_read (cd, context->u.keyfile.keyfile, &key_buf, &buf_len,
                                         context->u.keyfile.keyfile_offset, context->u.keyfile.key_size, 0);
        if (ret != 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEYFILE_FAILED,
                         "Failed to load key from file '%s': %s", context->u.keyfile.keyfile,
                         strerror_l (-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    } else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
        key_buf = (char *) context->u.passphrase.pass_data;
        buf_len = context->u.passphrase.data_len;
    } else {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                     "Only 'passphrase' and 'key file' context types are valid for LUKS add key.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        crypt_free (cd);
        return FALSE;
    }

    if (ncontext->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE) {
        ret = crypt_keyfile_device_read (cd, ncontext->u.keyfile.keyfile, &nkey_buf, &nbuf_len,
                                         ncontext->u.keyfile.keyfile_offset, ncontext->u.keyfile.key_size, 0);
        if (ret != 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEYFILE_FAILED,
                         "Failed to load key from file '%s': %s", ncontext->u.keyfile.keyfile,
                         strerror_l (-ret, c_locale));
            crypt_free (cd);
            if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE)
                crypt_safe_free (key_buf);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    } else if (ncontext->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
        nkey_buf = (char *) ncontext->u.passphrase.pass_data;
        nbuf_len = ncontext->u.passphrase.data_len;
    } else {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                     "Only 'passphrase' and 'key file' context types are valid for LUKS add key.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        crypt_free (cd);
        return FALSE;
    }

    ret = crypt_keyslot_add_by_passphrase (cd, CRYPT_ANY_SLOT, key_buf, buf_len, nkey_buf, nbuf_len);

    if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE)
        crypt_safe_free (key_buf);
    if (ncontext->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE)
        crypt_safe_free (nkey_buf);
    crypt_free (cd);

    if (ret < 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ADD_KEY,
                     "Failed to add key: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_remove_key:
 * @device: device to add new key to
 * @context: key slot context (passphrase/keyfile/token...) to remove from this LUKS @device
 * @error: (out) (optional): place to store error (if any)
 *
 * Supported @context types for this function: passphrase, key file
 *
 * Returns: whether the key was successfully removed or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_REMOVE_KEY
 */
gboolean bd_crypto_luks_remove_key (const gchar *device, BDCryptoKeyslotContext *context, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    gchar *key_buf = NULL;
    gsize buf_len = 0;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started removing key from the LUKS device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
        ret = crypt_activate_by_passphrase (cd, NULL, CRYPT_ANY_SLOT,
                                            (const char *) context->u.passphrase.pass_data,
                                            context->u.passphrase.data_len, 0);
    } else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE) {
        ret = crypt_keyfile_device_read (cd, context->u.keyfile.keyfile, &key_buf, &buf_len,
                                         context->u.keyfile.keyfile_offset, context->u.keyfile.key_size, 0);
        if (ret != 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEYFILE_FAILED,
                         "Failed to read key from file '%s: %s", context->u.keyfile.keyfile, strerror_l (-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        ret = crypt_activate_by_passphrase (cd, NULL, CRYPT_ANY_SLOT, key_buf, buf_len, 0);
        crypt_safe_free (key_buf);
    } else {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                     "Only 'passphrase' and 'key file' context types are valid for LUKS remove key.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        crypt_free (cd);
        return FALSE;
    }

    if (ret < 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEY_SLOT,
                     "Failed to determine key slot: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_keyslot_destroy (cd, ret);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_REMOVE_KEY,
                     "Failed to remove key: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_safe_free (key_buf);
    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_change_key:
 * @device: device to change key of
 * @context: key slot context (passphrase/keyfile/token...) for this LUKS @device
 * @ncontext: new key slot context (passphrase/keyfile/token...) to add to this LUKS @device
 * @error: (out) (optional): place to store error (if any)
 *
 * Supported @context types for this function: passphrase, key file
 *
 * Returns: whether the key was successfully changed or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_ADD_KEY&%BD_CRYPTO_TECH_MODE_REMOVE_KEY
 */
gboolean bd_crypto_luks_change_key (const gchar *device, BDCryptoKeyslotContext *context, BDCryptoKeyslotContext *ncontext, GError **error) {
    struct crypt_device *cd = NULL;
    gchar *key_buf = NULL;
    gsize buf_len = 0;
    gchar *nkey_buf = NULL;
    gsize nbuf_len = 0;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started changing key on the LUKS device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE) {
        ret = crypt_keyfile_device_read (cd, context->u.keyfile.keyfile, &key_buf, &buf_len,
                                         context->u.keyfile.keyfile_offset, context->u.keyfile.key_size, 0);
        if (ret != 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEYFILE_FAILED,
                         "Failed to load key from file '%s': %s", context->u.keyfile.keyfile,
                         strerror_l (-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    } else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
        key_buf = (char *) context->u.passphrase.pass_data;
        buf_len = context->u.passphrase.data_len;
    } else {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                     "Only 'passphrase' and 'key file' context types are valid for LUKS change key.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        crypt_free (cd);
        return FALSE;
    }

    if (ncontext->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE) {
        ret = crypt_keyfile_device_read (cd, ncontext->u.keyfile.keyfile, &key_buf, &buf_len,
                                         ncontext->u.keyfile.keyfile_offset, ncontext->u.keyfile.key_size, 0);
        if (ret != 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEYFILE_FAILED,
                         "Failed to load key from file '%s': %s", ncontext->u.keyfile.keyfile,
                         strerror_l (-ret, c_locale));
            crypt_free (cd);
            if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE)
                crypt_safe_free (key_buf);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    } else if (ncontext->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
        nkey_buf = (char *) ncontext->u.passphrase.pass_data;
        nbuf_len = ncontext->u.passphrase.data_len;
    } else {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                     "Only 'passphrase' and 'key file' context types are valid for LUKS change key.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        crypt_free (cd);
        return FALSE;
    }

    ret = crypt_keyslot_change_by_passphrase (cd, CRYPT_ANY_SLOT, CRYPT_ANY_SLOT,
                                              key_buf, buf_len, nkey_buf, nbuf_len);

    if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE)
        crypt_safe_free (key_buf);
    if (ncontext->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE)
        crypt_safe_free (nkey_buf);
    crypt_free (cd);

    if (ret < 0) {
        if (ret == -EPERM)
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                         "Failed to change the passphrase: No keyslot with given passphrase found.");
        else
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ADD_KEY,
                         "Failed to change the passphrase: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_resize:
 * @luks_device: opened LUKS device to resize
 * @size: requested size in sectors or 0 to adapt to the backing device
 * @context: (nullable): key slot context (passphrase/keyfile/token...) for this LUKS @device
 * @error: (out) (optional): place to store error (if any)
 *
 * Supported @context types for this function: passphrase, key file
 *
 * Returns: whether the @luks_device was successfully resized or not
 *
 * You need to specify either @context for LUKS 2 devices that
 * don't have verified key loaded in kernel.
 * For LUKS 1 devices you can set @context %NULL.
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_RESIZE
 */
gboolean bd_crypto_luks_resize (const gchar *luks_device, guint64 size, BDCryptoKeyslotContext *context, GError **error) {
    struct crypt_device *cd = NULL;
    struct crypt_active_device cad;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    gchar *key_buffer = NULL;
    gsize buf_len = 0;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started resizing LUKS device '%s'", luks_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init_by_name (&cd, luks_device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_get_active_device (cd, luks_device, &cad);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to get information about '%s': %s",
                     luks_device, strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (context) {
        if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
            ret = crypt_activate_by_passphrase (cd, NULL, CRYPT_ANY_SLOT,
                                                (const char *) context->u.passphrase.pass_data,
                                                context->u.passphrase.data_len,
                                                cad.flags & CRYPT_ACTIVATE_KEYRING_KEY);
        } else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE) {
            ret = crypt_keyfile_device_read (cd, context->u.keyfile.keyfile, &key_buffer, &buf_len,
                                             context->u.keyfile.keyfile_offset, context->u.keyfile.key_size, 0);
            if (ret != 0) {
                g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEYFILE_FAILED,
                            "Failed to read key from file '%s: %s", context->u.keyfile.keyfile, strerror_l (-ret, c_locale));
                crypt_free (cd);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return FALSE;
            }
            ret = crypt_activate_by_passphrase (cd, NULL, CRYPT_ANY_SLOT, key_buffer, buf_len,
                                                cad.flags & CRYPT_ACTIVATE_KEYRING_KEY);
            crypt_safe_free (key_buffer);
        } else {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                        "Only 'passphrase' and 'key file' context types are valid for LUKS resize.");
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            crypt_free (cd);
            return FALSE;
        }

        if (ret < 0) {
            if (ret == -EPERM)
              g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                           "Failed to activate device: Incorrect passphrase.");
            else
              g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                           "Failed to activate device: %s", strerror_l (-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    }

    ret = crypt_resize (cd, luks_device, size);
    if (ret != 0) {
        if (ret == -EPERM && g_strcmp0 (crypt_get_type (cd), CRYPT_LUKS2) == 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_RESIZE_PERM,
                         "Insufficient permissions to resize device. You need to specify"
                         " passphrase or keyfile to resize LUKS 2 devices that don't"
                         " have verified key loaded in kernel.");
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;

        }
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_RESIZE_FAILED,
                     "Failed to resize device: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_suspend:
 * @luks_device: LUKS device to suspend
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given @luks_device was successfully suspended or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_SUSPEND_RESUME
 */
gboolean bd_crypto_luks_suspend (const gchar *luks_device, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started suspending LUKS device '%s'", luks_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init_by_name (&cd, luks_device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_suspend (cd, luks_device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to suspend device: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_resume:
 * @luks_device: LUKS device to resume
 * @context: (nullable): key slot context (passphrase/keyfile/token...) for @luks_device
 * @error: (out) (optional): place to store error (if any)
 *
 * Supported @context types for this function: passphrase, key file
 *
 * Returns: whether the given @luks_device was successfully resumed or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_SUSPEND_RESUME
 */
gboolean bd_crypto_luks_resume (const gchar *luks_device, BDCryptoKeyslotContext *context, GError **error) {
    struct crypt_device *cd = NULL;
    gchar *key_buffer = NULL;
    gsize buf_len = 0;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started resuming '%s' LUKS device", luks_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init_by_name (&cd, luks_device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
        ret = crypt_resume_by_passphrase (cd, luks_device, CRYPT_ANY_SLOT,
                                          (const char *) context->u.passphrase.pass_data,
                                          context->u.passphrase.data_len);
    } else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE) {
        ret = crypt_keyfile_device_read (cd, context->u.keyfile.keyfile, &key_buffer, &buf_len,
                                         context->u.keyfile.keyfile_offset, context->u.keyfile.key_size, 0);
        if (ret != 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEYFILE_FAILED,
                         "Failed to read key from file '%s: %s", context->u.keyfile.keyfile, strerror_l (-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        ret = crypt_resume_by_passphrase (cd, luks_device, CRYPT_ANY_SLOT, key_buffer, buf_len);
        crypt_safe_free (key_buffer);
    } else {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                        "Only 'passphrase' and 'key file' context types are valid for LUKS resume.");
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            crypt_free (cd);
            return FALSE;
    }

    if (ret < 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to resume device: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_kill_slot:
 * @device: device to kill slot on
 * @slot: keyslot to destroy
 * @error: (out) (optional): place to store error (if any)
 *
 * Note: This can destroy last remaining keyslot without confirmation making
 *       the LUKS device permanently inaccessible.
 *
 * Returns: whether the given @slot was successfully destroyed or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_REMOVE_KEY
 */
gboolean bd_crypto_luks_kill_slot (const gchar *device, gint slot, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started killing slot %d on LUKS device '%s'", slot, device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_keyslot_destroy (cd, slot);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to destroy keyslot: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_header_backup:
 * @device: device to backup the LUKS header
 * @backup_file: file to save the header backup to
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given backup of @device was successfully written to
 *          @backup_file or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_BACKUP_RESTORE
 */
gboolean bd_crypto_luks_header_backup (const gchar *device, const gchar *backup_file, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started header backup of LUKS device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_header_backup (cd, NULL, backup_file);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to backup LUKS header: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_header_restore:
 * @device: device to restore the LUKS header to
 * @backup_file: existing file with a LUKS header backup
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given @device LUKS header was successfully restored
 *          from @backup_file
 *
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_BACKUP_RESTORE
 */
gboolean bd_crypto_luks_header_restore (const gchar *device, const gchar *backup_file, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started LUKS header restore on device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_header_restore (cd, NULL, backup_file);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to restore LUKS header: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_set_label:
 * @device: device to set label on
 * @label: (nullable): label to set
 * @subsystem: (nullable): subsystem to set
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given @label and @subsystem were successfully set or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_MODIFY
 */
gboolean bd_crypto_luks_set_label (const gchar *device, const gchar *label, const gchar *subsystem, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    if (g_strcmp0 (crypt_get_type (cd), CRYPT_LUKS2) != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                     "Label can be set only on LUKS 2 devices: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    ret = crypt_set_label (cd, label, subsystem);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to set label: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    crypt_free (cd);

    return TRUE;
}

/**
 * bd_crypto_luks_set_uuid:
 * @device: device to set UUID on
 * @uuid: (nullable): UUID to set or %NULL to generate a new one
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given @uuid was successfully set or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_MODIFY
 */
gboolean bd_crypto_luks_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    ret = crypt_set_uuid (cd, uuid);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to set UUID: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        return FALSE;
    }

    crypt_free (cd);

    return TRUE;
}

static gint synced_close (gint fd) {
    gint ret = 0;
    ret = fsync (fd);
    if (close (fd) != 0)
        ret = 1;
    return ret;
}

static gboolean get_subsystem_label (const gchar *device, gchar **subsystem, gchar **label, GError **error) {
    blkid_probe probe = NULL;
    gint fd = 0;
    gint status = 0;
    const gchar *value = NULL;

    probe = blkid_new_probe ();
    if (!probe) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to create a probe for the device '%s'", device);
        return FALSE;
    }

    fd = open (device, O_RDONLY|O_CLOEXEC);
    if (fd == -1) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to create a probe for the device '%s'", device);
        blkid_free_probe (probe);
        return FALSE;
    }

    status = blkid_probe_set_device (probe, fd, 0, 0);
    if (status != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to create a probe for the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        return FALSE;
    }

    blkid_probe_enable_partitions (probe, 1);

    status = blkid_do_probe (probe);
    if (status != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to probe the device '%s'", device);
        blkid_free_probe (probe);
        synced_close (fd);
        return FALSE;
    }

    status = blkid_probe_has_value (probe, "LABEL");

    if (status == 0)
        *label = g_strdup ("");
    else {
        status = blkid_probe_lookup_value (probe, "LABEL", &value, NULL);
        if (status != 0) {
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                         "Failed to get label for the device '%s'", device);
            blkid_free_probe (probe);
            synced_close (fd);
            return FALSE;
        }

        if (value)
            *label = g_strdup (value);
        else
            *label = g_strdup ("");
    }

    status = blkid_probe_has_value (probe, "SUBSYSTEM");
    if (status == 0)
        *subsystem = g_strdup ("");
    else {
        status = blkid_probe_lookup_value (probe, "SUBSYSTEM", &value, NULL);
        if (status != 0) {
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                         "Failed to get subsystem for the device '%s'", device);
            blkid_free_probe (probe);
            synced_close (fd);
            return FALSE;
        }

        if (value)
            *subsystem = g_strdup (value);
        else
            *subsystem = g_strdup ("");
    }

    blkid_free_probe (probe);
    synced_close (fd);

    return TRUE;
}

/**
 * bd_crypto_luks_info:
 * @device: a device to get information about
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns (transfer full): information about the @device or %NULL in case of error
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_QUERY
 */
BDCryptoLUKSInfo* bd_crypto_luks_info (const gchar *device, GError **error) {
    struct crypt_device *cd = NULL;
    BDCryptoLUKSInfo *info = NULL;
    const gchar *version = NULL;
    gint ret;
    gboolean success = FALSE;

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        /* not a block device, try init_by_name */
        crypt_free (cd);
        ret = crypt_init_by_name (&cd, device);
    } else {
        ret = crypt_load (cd, CRYPT_LUKS, NULL);
        if (ret != 0) {
            /* not a LUKS device, try init_by_name */
            crypt_free (cd);
            ret = crypt_init_by_name (&cd, device);
        }
    }

    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        return NULL;
    }

    info = g_new0 (BDCryptoLUKSInfo, 1);

    version = crypt_get_type (cd);
    if (g_strcmp0 (version, CRYPT_LUKS1) == 0)
        info->version = BD_CRYPTO_LUKS_VERSION_LUKS1;
    else if (g_strcmp0 (version, CRYPT_LUKS2) == 0)
        info->version = BD_CRYPTO_LUKS_VERSION_LUKS2;
    else {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                     "Unknown or unsupported LUKS version");
        bd_crypto_luks_info_free (info);
        return NULL;
    }

    info->cipher = g_strdup (crypt_get_cipher (cd));
    info->mode = g_strdup (crypt_get_cipher_mode (cd));
    info->uuid = g_strdup (crypt_get_uuid (cd));
    info->backing_device = g_strdup (crypt_get_device_name (cd));
    ret = crypt_get_sector_size (cd);
    info->sector_size = ret > 0 ? ret : 0;
    info->metadata_size = SECTOR_SIZE * crypt_get_data_offset (cd);

    if (info->version == BD_CRYPTO_LUKS_VERSION_LUKS2) {
        success = get_subsystem_label (crypt_get_device_name (cd) , &(info->subsystem), &(info->label), error);
        if (!success) {
            crypt_free (cd);
            bd_crypto_luks_info_free (info);
            return NULL;
        }
    } else {
        info->label = g_strdup ("");
        info->subsystem = g_strdup ("");
    }

    crypt_free (cd);

    return info;
}

/**
 * bd_crypto_bitlk_info:
 * @device: a device to get information about
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns (transfer full): information about the @device or %NULL in case of error
 *
 * Tech category: %BD_CRYPTO_TECH_BITLK-%BD_CRYPTO_TECH_MODE_QUERY
 */
BDCryptoBITLKInfo* bd_crypto_bitlk_info (const gchar *device, GError **error) {
    struct crypt_device *cd = NULL;
    BDCryptoBITLKInfo *info = NULL;
    gint ret;

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        /* not a block device, try init_by_name */
        crypt_free (cd);
        cd = NULL;
        ret = crypt_init_by_name (&cd, device);
    } else {
        ret = crypt_load (cd, CRYPT_BITLK, NULL);
        if (ret != 0) {
            /* not a BITLK device, try init_by_name */
            crypt_free (cd);
            cd = NULL;
            ret = crypt_init_by_name (&cd, device);
        }
    }

    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        return NULL;
    }

    info = g_new0 (BDCryptoBITLKInfo, 1);

    info->cipher = g_strdup (crypt_get_cipher (cd));
    info->mode = g_strdup (crypt_get_cipher_mode (cd));
    info->uuid = g_strdup (crypt_get_uuid (cd));
    info->backing_device = g_strdup (crypt_get_device_name (cd));
    ret = crypt_get_sector_size (cd);
    info->sector_size = ret > 0 ? ret : 0;

    crypt_free (cd);

    return info;
}

/**
 * bd_crypto_integrity_info:
 * @device: a device to get information about
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns (transfer full): information about the @device or %NULL in case of error
 *
 * Tech category: %BD_CRYPTO_TECH_INTEGRITY%BD_CRYPTO_TECH_MODE_QUERY
 */
BDCryptoIntegrityInfo* bd_crypto_integrity_info (const gchar *device, GError **error) {
    struct crypt_device *cd = NULL;
    struct crypt_params_integrity ip = ZERO_INIT;
    BDCryptoIntegrityInfo *info = NULL;
    gint ret;

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        /* not a block device, try init_by_name */
        crypt_free (cd);
        ret = crypt_init_by_name (&cd, device);
    } else {
        ret = crypt_load (cd, CRYPT_LUKS, NULL);
        if (ret != 0) {
            /* not a LUKS device, try integrity */
            ret = crypt_load (cd, CRYPT_INTEGRITY, NULL);
            if (ret != 0) {
                crypt_free (cd);
                ret = crypt_init_by_name (&cd, device);
            }
        }
    }

    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        return NULL;
    }

    ret = crypt_get_integrity_info (cd, &ip);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to get information about device: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        return NULL;
    }

    info = g_new0 (BDCryptoIntegrityInfo, 1);

    info->algorithm = g_strdup (ip.integrity);
    info->key_size = ip.integrity_key_size;
    info->sector_size = ip.sector_size;
    info->tag_size = ip.tag_size;
    info->interleave_sectors = ip.interleave_sectors;
    info->journal_size = ip.journal_size;
    info->journal_crypt = g_strdup (ip.journal_crypt);
    info->journal_integrity = g_strdup (ip.journal_integrity);

    crypt_free (cd);
    return info;
}

/* added in cryptsetup 2.4.0 */
#ifndef LIBCRYPTSETUP_24
static int crypt_token_max (const char *type G_GNUC_UNUSED) {
    return 32;
}
#endif


/**
 * bd_crypto_luks_token_info:
 * @device: a device to get LUKS2 token information about
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (array zero-terminated=1) (transfer full): information about tokens on @device
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_QUERY
 */
BDCryptoLUKSTokenInfo** bd_crypto_luks_token_info (const gchar *device, GError **error) {
    struct crypt_device *cd = NULL;
    GPtrArray *tokens = NULL;
    BDCryptoLUKSTokenInfo *info = NULL;
    crypt_token_info token_info;
    const gchar *type = NULL;
    gint ret;
    gint token_it, keyslot_it;

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        /* not a block device, try init_by_name */
        crypt_free (cd);
        ret = crypt_init_by_name (&cd, device);
    } else {
        ret = crypt_load (cd, CRYPT_LUKS, NULL);
        if (ret != 0) {
            /* not a LUKS device, try init_by_name */
            crypt_free (cd);
            ret = crypt_init_by_name (&cd, device);
        }
    }

    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        return NULL;
    }

    if (g_strcmp0 (crypt_get_type (cd), CRYPT_LUKS2) != 0) {
        crypt_free (cd);
        return NULL;
    }

    tokens = g_ptr_array_new ();

    for (token_it = 0; token_it < crypt_token_max (CRYPT_LUKS2); token_it++) {
        token_info = crypt_token_status (cd, token_it, &type);
        if (token_info == CRYPT_TOKEN_INVALID || token_info == CRYPT_TOKEN_INACTIVE)
            continue;

        info = g_new0 (BDCryptoLUKSTokenInfo, 1);
        info->id = token_it;
        info->type = g_strdup (type);
        info->keyslot = -1;

        for (keyslot_it = 0; keyslot_it < crypt_keyslot_max (CRYPT_LUKS2); keyslot_it++) {
            ret = crypt_token_is_assigned (cd, token_it, keyslot_it);
            if (ret == 0) {
                info->keyslot = keyslot_it;
                break;
            }
        }

        g_ptr_array_add (tokens, info);
    }

    crypt_free (cd);

    /* returning NULL-terminated array of BDCryptoLUKSTokenInfo */
    g_ptr_array_add (tokens, NULL);
    return (BDCryptoLUKSTokenInfo **) g_ptr_array_free (tokens, FALSE);
}

static int _wipe_progress (guint64 size, guint64 offset, void *usrptr) {
    /* "convert" the progress from 0-100 to 50-100 because wipe starts at 50 in bd_crypto_integrity_format */
    gdouble progress = 50 + (((gdouble) offset / size) * 100) / 2;
    bd_utils_report_progress (*(guint64 *) usrptr, progress, "Integrity device wipe in progress");

    return 0;
}

/**
 * bd_crypto_integrity_format:
 * @device: a device to format as integrity
 * @algorithm: integrity algorithm specification (e.g. "crc32c" or "sha256")
 * @wipe: whether to wipe the device after format; a device that is not initially wiped will contain invalid checksums
 * @context: (nullable): key slot context (passphrase/keyfile/token...) for this device
 * @extra: (nullable): extra arguments for integrity format creation
 * @error: (out) (optional): place to store error (if any)
 *
 * Formats the given @device as integrity according to the other parameters given.
 *
 * Supported @context types for this function: volume key
 *
 * Returns: whether the given @device was successfully formatted as integrity or not
 * (the @error) contains the error in such cases)
 *
 * Tech category: %BD_CRYPTO_TECH_INTEGRITY-%BD_CRYPTO_TECH_MODE_CREATE
 */
gboolean bd_crypto_integrity_format (const gchar *device, const gchar *algorithm, gboolean wipe, BDCryptoKeyslotContext *context, BDCryptoIntegrityExtra *extra, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    struct crypt_params_integrity params = ZERO_INIT;
    g_autofree gchar *tmp_name = NULL;
    g_autofree gchar *tmp_path = NULL;
    g_autofree gchar *dev_name = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started formatting '%s' as integrity device", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (context && context->type != BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_VOLUME_KEY) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                    "Only 'volume key' context type is valid for integrity format.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (extra) {
        params.sector_size = extra->sector_size;
        params.journal_size = extra->journal_size;
        params.journal_watermark = extra->journal_watermark;
        params.journal_commit_time = extra->journal_commit_time;
        params.interleave_sectors = extra->interleave_sectors;
        params.tag_size = extra->tag_size;
        params.buffer_sectors = extra->buffer_sectors;
    }

    params.integrity_key_size = context ? context->u.volume_key.volume_key_size : 0;
    params.integrity = algorithm;
    params.tag_size = params.tag_size ? params.tag_size : 0;

    ret = crypt_format (cd, CRYPT_INTEGRITY, NULL, NULL, NULL, NULL, 0, &params);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_FORMAT_FAILED,
                     "Failed to format device: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (wipe) {
        bd_utils_report_progress (progress_id, 50, "Format created");

        dev_name = g_path_get_basename (device);
        tmp_name = g_strdup_printf ("bd-temp-integrity-%s-%d", dev_name, g_random_int ());
        tmp_path = g_strdup_printf ("%s/%s", crypt_get_dir (), tmp_name);

        ret = crypt_activate_by_volume_key (cd, tmp_name,
                                            context ? (const char *) context->u.volume_key.volume_key : NULL,
                                            context ? context->u.volume_key.volume_key_size : 0,
                                            CRYPT_ACTIVATE_PRIVATE | CRYPT_ACTIVATE_NO_JOURNAL);
        if (ret != 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                         "Failed to activate the newly created integrity device for wiping: %s",
                         strerror_l (-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }

        bd_utils_report_progress (progress_id, 50, "Starting to wipe the newly created integrity device");
        ret = crypt_wipe (cd, tmp_path, CRYPT_WIPE_ZERO, 0, 0, 1048576,
                          0, &_wipe_progress, &progress_id);
        bd_utils_report_progress (progress_id, 100, "Wipe finished");
        if (ret != 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                         "Failed to wipe the newly created integrity device: %s",
                         strerror_l (-ret, c_locale));

            ret = crypt_deactivate (cd, tmp_name);
            if (ret != 0)
                bd_utils_log_format (BD_UTILS_LOG_ERR, "Failed to deactivate temporary device %s", tmp_name);

            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }

        ret = crypt_deactivate (cd, tmp_name);
        if (ret != 0)
            bd_utils_log_format (BD_UTILS_LOG_ERR, "Failed to deactivate temporary device %s", tmp_name);

    } else
        bd_utils_report_finished (progress_id, "Completed");

    crypt_free (cd);

    return TRUE;
}

/**
 * bd_crypto_integrity_open:
 * @device: integrity device to open
 * @name: name for the opened @device
 * @algorithm: integrity algorithm specification (e.g. "crc32c" or "sha256")
 * @context: (nullable): key slot context (passphrase/keyfile/token...) for this device
 * @flags: flags for the integrity device activation
 * @extra: (nullable): extra arguments for integrity open
 * @error: (out) (optional): place to store error (if any)
 *
 * Supported @context types for this function: volume key
 *
 * Returns: whether the @device was successfully opened or not
 *
 * Tech category: %BD_CRYPTO_TECH_INTEGRITY-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_integrity_open (const gchar *device, const gchar *name, const gchar *algorithm, BDCryptoKeyslotContext *context, BDCryptoIntegrityOpenFlags flags, BDCryptoIntegrityExtra *extra, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    struct crypt_params_integrity params = ZERO_INIT;
    guint32 activate_flags = 0;
    GError *l_error = NULL;

    if (context && context->type != BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_VOLUME_KEY) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                     "Only 'volume key' context type is valid for integrity format.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    params.integrity = algorithm;
    params.integrity_key_size = context ? context->u.volume_key.volume_key_size : 0;

    if (extra) {
        params.sector_size = extra->sector_size;
        params.journal_size = extra->journal_size;
        params.journal_watermark = extra->journal_watermark;
        params.journal_commit_time = extra->journal_commit_time;
        params.interleave_sectors = extra->interleave_sectors;
        params.tag_size = extra->tag_size;
        params.buffer_sectors = extra->buffer_sectors;
    }


    if (flags & BD_CRYPTO_INTEGRITY_OPEN_NO_JOURNAL)
        activate_flags |= CRYPT_ACTIVATE_NO_JOURNAL;
    if (flags & BD_CRYPTO_INTEGRITY_OPEN_RECOVERY)
        activate_flags |= CRYPT_ACTIVATE_RECOVERY;
    if (flags & BD_CRYPTO_INTEGRITY_OPEN_RECALCULATE)
        activate_flags |= CRYPT_ACTIVATE_RECALCULATE;
    if (flags & BD_CRYPTO_INTEGRITY_OPEN_ALLOW_DISCARDS)
        activate_flags |= CRYPT_ACTIVATE_ALLOW_DISCARDS;
    if (flags & BD_CRYPTO_INTEGRITY_OPEN_NO_JOURNAL_BITMAP)
        activate_flags |= CRYPT_ACTIVATE_NO_JOURNAL_BITMAP;

    if (flags & BD_CRYPTO_INTEGRITY_OPEN_RECALCULATE_RESET) {
#ifndef CRYPT_ACTIVATE_RECALCULATE_RESET
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                     "Cannot reset integrity recalculation while activating %s, installed version of cryptsetup doesn't support this option.", device);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
#else
        activate_flags |= CRYPT_ACTIVATE_RECALCULATE_RESET;
#endif
    }

    msg = g_strdup_printf ("Started opening '%s' integrity device", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_INTEGRITY, &params);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_activate_by_volume_key (cd, name,
                                        context ? (const char *) context->u.volume_key.volume_key : NULL,
                                        context ? context->u.volume_key.volume_key_size : 0,
                                        activate_flags);
    if (ret < 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to activate device: %s", strerror_l (-ret, c_locale));

        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_integrity_close:
 * @integrity_device: integrity device to close
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given @integrity_device was successfully closed or not
 *
 * Tech category: %BD_CRYPTO_TECH_INTEGRITY-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_integrity_close (const gchar *integrity_device, GError **error) {
    return _crypto_close (integrity_device, "integrity", error);
}

/**
 * bd_crypto_keyring_add_key:
 * @key_desc: kernel keyring key description
 * @key_data: (array length=data_len): a key to add to kernel keyring (may contain arbitrary binary data)
 * @data_len: length of the @key_data buffer
 * @error: (out) (optional): place to store error (if any)
 * *
 * Returns: whether the given key was successfully saved to kernel keyring or not
 *
 * Tech category: %BD_CRYPTO_TECH_KEYRING-%BD_CRYPTO_TECH_MODE_ADD_KEY
 */
gboolean bd_crypto_keyring_add_key (const gchar *key_desc, const guint8 *key_data, gsize data_len, GError **error) {
    key_serial_t ret;

    ret = add_key ("user", key_desc, key_data, data_len, KEY_SPEC_SESSION_KEYRING);
    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEYRING,
                     "Failed to add key to kernel keyring: %s", strerror_l (errno, c_locale));
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_crypto_device_seems_encrypted:
 * @device: the queried device
 * @error: (out) (optional): place to store error (if any)
 *
 * Determines whether a block device seems to be encrypted.
 *
 * TCRYPT volumes are not easily identifiable, because they have no
 * cleartext header, but are completely encrypted. This function is
 * used to determine whether a block device is a candidate for being
 * TCRYPT encrypted.
 *
 * To achieve this, we calculate the chi square value of the first
 * 512 Bytes and treat devices with a chi square value between 136
 * and 426 as candidates for being encrypted.
 * For the reasoning, see: https://tails.boum.org/blueprint/veracrypt/#detection
 *
 * Returns: %TRUE if the given @device seems to be encrypted or %FALSE if not or
 * failed to determine (the @error) is populated with the error in such
 * cases)
 *
 * Tech category: %BD_CRYPTO_TECH_TRUECRYPT-%BD_CRYPTO_TECH_MODE_QUERY
 */
gboolean bd_crypto_device_seems_encrypted (const gchar *device, GError **error) {
    gint fd = -1;
    guchar buf[SQUARE_BYTES_TO_CHECK];
    guint symbols[256] = {0};
    gfloat chi_square = 0.0;
    gfloat e = (gfloat) sizeof (buf) / (gfloat) 256.0;
    guint i;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started determining if device '%s' seems to be encrypted", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    fd = open (device, O_RDONLY);
    if (fd == -1) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE, "Failed to open device");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (read (fd, buf, sizeof (buf)) != sizeof (buf)) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE, "Failed to read device");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        close (fd);
        return FALSE;
    }

    close (fd);

    /* Calculate Chi Square */
    for (i = 0; i < sizeof (buf); i++)
        /* This is safe because the max value of buf[i] is < sizeof (symbols). */
        symbols[buf[i]]++;
    for (i = 0; i < 256; i++)
        chi_square += (symbols[i] - e) * (symbols[i] - e);
    chi_square /= e;

    bd_utils_report_finished (progress_id, "Completed");
    return SQUARE_LOWER_LIMIT < chi_square && chi_square < SQUARE_UPPER_LIMIT;
}

/**
 * bd_crypto_tc_open:
 * @device: the device to open
 * @name: name for the TrueCrypt/VeraCrypt device
 * @context: (nullable): passphrase key slot context for this TrueCrypt/VeraCrypt volume
 * @read_only: whether to open as read-only or not (meaning read-write)
 * @keyfiles: (nullable) (array zero-terminated=1): paths to the keyfiles for the TrueCrypt/VeraCrypt volume
 * @hidden: whether a hidden volume inside the volume should be opened
 * @system: whether to try opening as an encrypted system (with boot loader)
 * @veracrypt: whether to try VeraCrypt modes (TrueCrypt modes are tried anyway)
 * @veracrypt_pim: VeraCrypt PIM value (only used if @veracrypt is %TRUE)
 * @error: (out) (optional): place to store error (if any)
 *
 * Supported @context types for this function: passphrase
 *
 * Returns: whether the @device was successfully opened or not
 *
 * Tech category: %BD_CRYPTO_TECH_TRUECRYPT-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_tc_open (const gchar *device, const gchar *name, BDCryptoKeyslotContext *context, const gchar **keyfiles, gboolean hidden, gboolean system, gboolean veracrypt, guint32 veracrypt_pim, gboolean read_only, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    struct crypt_params_tcrypt params = ZERO_INIT;
    gsize keyfiles_count = 0;
    guint i;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started opening '%s' TrueCrypt/VeraCrypt device", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (keyfiles) {
        for (i=0; *(keyfiles + i); i++);
        keyfiles_count = i;
    }

    if (context && context->type != BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                     "Only 'passphrase' context type is valid for TC open.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        crypt_free (cd);
        return FALSE;
    }

    if ((context == NULL) && (keyfiles_count == 0)) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NO_KEY,
                     "No passphrase nor key file specified, cannot open.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    params.passphrase = context ? (const char*) context->u.passphrase.pass_data : NULL;
    params.passphrase_size = context ? context->u.passphrase.data_len : 0;
    params.keyfiles = keyfiles;
    params.keyfiles_count = keyfiles_count;

    if (veracrypt)
        params.flags |= CRYPT_TCRYPT_VERA_MODES;
    if (hidden)
        params.flags |= CRYPT_TCRYPT_HIDDEN_HEADER;
    if (system)
        params.flags |= CRYPT_TCRYPT_SYSTEM_HEADER;
    if (veracrypt && veracrypt_pim != 0)
        params.veracrypt_pim = veracrypt_pim;

    ret = crypt_load (cd, CRYPT_TCRYPT, &params);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_activate_by_volume_key (cd, name, NULL, 0,
                                        read_only ? CRYPT_ACTIVATE_READONLY : 0);

    if (ret < 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to activate device: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_tc_close:
 * @tc_device: TrueCrypt/VeraCrypt device to close
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given @tc_device was successfully closed or not
 *
 * Tech category: %BD_CRYPTO_TECH_TRUECRYPT-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_tc_close (const gchar *tc_device, GError **error) {
    return _crypto_close (tc_device, "TrueCrypt/VeraCrypt", error);
}

#ifdef WITH_BD_ESCROW
static gchar *always_fail_cb (gpointer data G_GNUC_UNUSED, const gchar *prompt G_GNUC_UNUSED, int echo G_GNUC_UNUSED) {
    return NULL;
}

static gchar *give_passphrase_cb (gpointer data, const gchar *prompt G_GNUC_UNUSED, unsigned failed_attempts) {
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
 * Replaces all occurrences of @orig in @str with @new (in place).
 */
static gchar *replace_char (gchar *str, gchar orig, gchar new) {
    gchar *pos = str;
    if (!str)
        return str;

    for (pos=str; *pos; pos++)
        *pos = *pos == orig ? new : *pos;

    return str;
}

static gboolean write_escrow_data_file (struct libvk_volume *volume, struct libvk_ui *ui, enum libvk_secret secret_type, const gchar *out_path,
                                        CERTCertificate *cert, GError **error) {
    gpointer packet_data = NULL;
    gsize packet_data_size = 0;
    GIOChannel *out_file = NULL;
    GIOStatus status = G_IO_STATUS_ERROR;
    gsize bytes_written = 0;
    GError *l_error = NULL;

    packet_data = libvk_volume_create_packet_asymmetric_with_format (volume, &packet_data_size, secret_type, cert,
                                                                     ui, LIBVK_PACKET_FORMAT_ASYMMETRIC_WRAP_SECRET_ONLY, &l_error);
    if (!packet_data) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ESCROW_FAILED,
                     "Failed to get escrow data: %s", l_error->message);
        g_clear_error (&l_error);
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
        g_free (packet_data);

        /* try to shutdown the channel, but if it fails, we cannot do anything about it here */
        g_io_channel_shutdown (out_file, TRUE, NULL);

        /* error is already populated */
        g_io_channel_unref (out_file);
        return FALSE;
    }

    status = g_io_channel_write_chars (out_file, (const gchar *) packet_data, (gssize) packet_data_size,
                                       &bytes_written, error);
    g_free (packet_data);
    if (status != G_IO_STATUS_NORMAL) {
        /* try to shutdown the channel, but if it fails, we cannot do anything about it here */
        g_io_channel_shutdown (out_file, TRUE, NULL);

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
#endif /* WITH_BD_ESCROW */

/**
 * bd_crypto_escrow_device:
 * @device: path of the device to create escrow data for
 * @passphrase: passphrase used for the device
 * @cert_data: (array zero-terminated=1) (element-type gchar): certificate data to use for escrow
 * @directory: directory to put escrow data into
 * @backup_passphrase: (nullable): backup passphrase for the device or %NULL
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the escrow data was successfully created for @device or not
 *
 * Tech category: %BD_CRYPTO_TECH_ESCROW-%BD_CRYPTO_TECH_MODE_CREATE
 */
#ifndef WITH_BD_ESCROW
gboolean bd_crypto_escrow_device (const gchar *device G_GNUC_UNUSED, const gchar *passphrase G_GNUC_UNUSED,const gchar *cert_data G_GNUC_UNUSED,
                                  const gchar *directory G_GNUC_UNUSED, const gchar *backup_passphrase G_GNUC_UNUSED, GError **error) {
    /* this will return FALSE and set error, because escrow technology is not available */
    return bd_crypto_is_tech_avail (BD_CRYPTO_TECH_ESCROW, BD_CRYPTO_TECH_MODE_CREATE, error);
}
#else
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
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started creating escrow data for the LUKS device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (!NSS_IsInitialized ())
        if (NSS_NoDB_Init (NULL) != SECSuccess) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NSS_INIT_FAILED,
                         "Failed to initialize NSS");
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }

    volume = libvk_volume_open (device, &l_error);
    if (!volume) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ui = libvk_ui_new ();
    /* not supposed to be called -> always fail */
    libvk_ui_set_generic_cb (ui, always_fail_cb, NULL, NULL);

    /* Create a copy of the passphrase to be used by the passphrase callback.
     * The passphrase will be freed by volume_key via the free callback.
     */
    passphrase_copy = g_strdup (passphrase);
    libvk_ui_set_passphrase_cb (ui, give_passphrase_cb, passphrase_copy, free_passphrase_cb);

    if (libvk_volume_get_secret (volume, LIBVK_SECRET_DEFAULT, ui, &l_error) != 0) {
        /* error is already populated */
        libvk_volume_free (volume);
        libvk_ui_free (ui);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    cert_data_copy = g_strdup (cert_data);
    cert = CERT_DecodeCertFromPackage (cert_data_copy, strlen (cert_data_copy));
    if (!cert) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_CERT_DECODE,
                     "Failed to decode the certificate data");
        libvk_volume_free (volume);
        libvk_ui_free (ui);
        g_free (cert_data_copy);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
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
    ret = write_escrow_data_file (volume, ui, LIBVK_SECRET_DEFAULT, out_path, cert, &l_error);
    g_free (out_path);

    if (!ret) {
        CERT_DestroyCertificate (cert);
        libvk_volume_free (volume);
        libvk_ui_free (ui);
        g_free (volume_ident);
        g_free (cert_data_copy);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (backup_passphrase) {
        if (libvk_volume_add_secret (volume, LIBVK_SECRET_PASSPHRASE, backup_passphrase, strlen (backup_passphrase), &l_error) != 0) {
            /* error is already populated */
            CERT_DestroyCertificate (cert);
            libvk_volume_free (volume);
            libvk_ui_free (ui);
            g_free (volume_ident);
            g_free (cert_data_copy);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }

        out_path = g_strdup_printf ("%s/%s-escrow-backup-passphrase", directory, volume_ident);
        ret = write_escrow_data_file (volume, ui, LIBVK_SECRET_PASSPHRASE, out_path, cert, &l_error);
        g_free (out_path);

        if (!ret) {
            CERT_DestroyCertificate (cert);
            libvk_volume_free (volume);
            libvk_ui_free (ui);
            g_free (volume_ident);
            g_free (cert_data_copy);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    }

    CERT_DestroyCertificate (cert);
    libvk_volume_free (volume);
    libvk_ui_free (ui);
    g_free (volume_ident);
    g_free (cert_data_copy);
    bd_utils_report_finished (progress_id, "Completed");
    return ret;
}
#endif /* WITH_BD_ESCROW */

/**
 * bd_crypto_bitlk_open:
 * @device: the device to open
 * @name: name for the BITLK device
 * @context: key slot context (passphrase/keyfile/token...) for this BITLK device
 * @read_only: whether to open as read-only or not (meaning read-write)
 * @error: (out) (optional): place to store error (if any)
 *
 * Supported @context types for this function: passphrase, key file
 *
 * Returns: whether the @device was successfully opened or not
 *
 * Tech category: %BD_CRYPTO_TECH_BITLK-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_bitlk_open (const gchar *device, const gchar *name, BDCryptoKeyslotContext *context, gboolean read_only, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;
    gchar *key_buffer = NULL;
    gsize buf_len = 0;

    msg = g_strdup_printf ("Started opening '%s' BITLK device", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_BITLK, NULL);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
        ret = crypt_activate_by_passphrase (cd, name, CRYPT_ANY_SLOT,
                                            (const char *) context->u.passphrase.pass_data,
                                            context->u.passphrase.data_len,
                                            read_only ? CRYPT_ACTIVATE_READONLY : 0);
    } else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE) {
        ret = crypt_keyfile_device_read (cd, context->u.keyfile.keyfile, &key_buffer, &buf_len,
                                         context->u.keyfile.keyfile_offset, context->u.keyfile.key_size, 0);
        if (ret != 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEYFILE_FAILED,
                         "Failed to read key from file '%s: %s", context->u.keyfile.keyfile, strerror_l (-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        ret = crypt_activate_by_passphrase (cd, name, CRYPT_ANY_SLOT, key_buffer, buf_len,
                                            read_only ? CRYPT_ACTIVATE_READONLY : 0);
        crypt_safe_free (key_buffer);
    } else {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                     "Only 'passphrase' and 'key file' context types are valid for BITLK open.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        crypt_free (cd);
        return FALSE;
    }

    if (ret < 0) {
        if (ret == -EPERM)
          g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                       "Failed to activate device: Incorrect passphrase.");
        else
          g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                       "Failed to activate device: %s", strerror_l (-ret, c_locale));

        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_bitlk_close:
 * @bitlk_device: BITLK device to close
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given @bitlk_device was successfully closed or not
 *
 * Tech category: %BD_CRYPTO_TECH_BITLK-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_bitlk_close (const gchar *bitlk_device, GError **error) {
    return _crypto_close (bitlk_device, "BITLK", error);
}

/**
 * bd_crypto_fvault2_open:
 * @device: the device to open
 * @name: name for the FVAULT2 device
 * @context: key slot context (passphrase/keyfile/token...) for this FVAULT2 volume
 * @read_only: whether to open as read-only or not (meaning read-write)
 * @error: (out) (optional): place to store error (if any)
 *
 * Supported @context types for this function: passphrase, key file
 *
 * Returns: whether the @device was successfully opened or not
 *
 * Tech category: %BD_CRYPTO_TECH_FVAULT2-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
#ifndef LIBCRYPTSETUP_26
gboolean bd_crypto_fvault2_open (const gchar *device G_GNUC_UNUSED, const gchar *name G_GNUC_UNUSED, BDCryptoKeyslotContext *context G_GNUC_UNUSED,
                                 gboolean read_only G_GNUC_UNUSED, GError **error) {
    /* this will return FALSE and set error, because FVAULT2 technology is not available */
    return bd_crypto_is_tech_avail (BD_CRYPTO_TECH_FVAULT2, BD_CRYPTO_TECH_MODE_OPEN_CLOSE, error);
#else
gboolean bd_crypto_fvault2_open (const gchar *device, const gchar *name, BDCryptoKeyslotContext *context, gboolean read_only, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;
    gchar *key_buffer = NULL;
    gsize buf_len = 0;

    msg = g_strdup_printf ("Started opening '%s' FVAULT2 device", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_FVAULT2, NULL);
    if (ret != 0) {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_PASSPHRASE) {
        ret = crypt_activate_by_passphrase (cd, name, CRYPT_ANY_SLOT,
                                            (const char *) context->u.passphrase.pass_data,
                                            context->u.passphrase.data_len,
                                            read_only ? CRYPT_ACTIVATE_READONLY : 0);
    } else if (context->type == BD_CRYPTO_KEYSLOT_CONTEXT_TYPE_KEYFILE) {
        ret = crypt_keyfile_device_read (cd, context->u.keyfile.keyfile, &key_buffer, &buf_len,
                                         context->u.keyfile.keyfile_offset, context->u.keyfile.key_size, 0);
        if (ret != 0) {
            g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEYFILE_FAILED,
                         "Failed to read key from file '%s: %s", context->u.keyfile.keyfile, strerror_l (-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
        ret = crypt_activate_by_passphrase (cd, name, CRYPT_ANY_SLOT, key_buffer, buf_len,
                                            read_only ? CRYPT_ACTIVATE_READONLY : 0);
        crypt_safe_free (key_buffer);
    } else {
        g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_CONTEXT,
                     "Only 'passphrase' and 'key file' context types are valid for FVAULT2 open.");
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        crypt_free (cd);
        return FALSE;
    }

    if (ret < 0) {
        if (ret == -EPERM)
          g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                       "Failed to activate device: Incorrect passphrase.");
        else
          g_set_error (&l_error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                       "Failed to activate device: %s", strerror_l (-ret, c_locale));

        crypt_free (cd);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
#endif
}

/**
 * bd_crypto_fvault2_close:
 * @fvault2_device: FVAULT2 device to close
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the given @fvault2_device was successfully closed or not
 *
 * Tech category: %BD_CRYPTO_TECH_FVAULT2-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
#ifndef LIBCRYPTSETUP_26
gboolean bd_crypto_fvault2_close (const gchar *fvault2_device G_GNUC_UNUSED, GError **error) {
    /* this will return FALSE and set error, because FVAULT2 technology is not available */
    return bd_crypto_is_tech_avail (BD_CRYPTO_TECH_FVAULT2, BD_CRYPTO_TECH_MODE_OPEN_CLOSE, error);
}
#else
gboolean bd_crypto_fvault2_close (const gchar *fvault2_device, GError **error) {
    return _crypto_close (fvault2_device, "FVAULT2", error);
}
#endif
