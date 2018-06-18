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
#include <luksmeta.h>
#include <json.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/random.h>
#include <locale.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <blockdev/utils.h>

#ifdef WITH_BD_ESCROW
#include <nss.h>
#include <volume_key/libvolume_key.h>
#endif

#include "crypto.h"

#ifndef CRYPT_LUKS
#define CRYPT_LUKS NULL
#endif

#ifdef __clang__
#define ZERO_INIT {}
#else
#define ZERO_INIT {0}
#endif

#define SECTOR_SIZE 512

#define UNUSED __attribute__((unused))

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

BDCryptoLUKSExtra* bd_crypto_luks_extra_copy (BDCryptoLUKSExtra *extra) {
   BDCryptoLUKSExtra *new_extra = g_new0 (BDCryptoLUKSExtra, 1);

   new_extra->integrity = g_strdup (extra->integrity);
   new_extra->data_alignment = extra->data_alignment;
   new_extra->data_device = g_strdup (extra->data_device);
   new_extra->sector_size = extra->sector_size;
   new_extra->label = g_strdup (extra->label);
   new_extra->subsystem = g_strdup (extra->subsystem);

   return new_extra;
}

void bd_crypto_luks_extra_free (BDCryptoLUKSExtra *extra) {
   g_free (extra->integrity);
   g_free (extra->data_device);
   g_free (extra->label);
   g_free (extra->subsystem);
   g_free (extra);
}

void bd_crypto_luks_info_free (BDCryptoLUKSInfo *info) {
    g_free (info->cipher);
    g_free (info->mode);
    g_free (info->uuid);
    g_free (info->backing_device);
    g_free (info);
}

BDCryptoLUKSInfo* bd_crypto_luks_info_copy (BDCryptoLUKSInfo *info) {
    BDCryptoLUKSInfo *new_info = g_new0 (BDCryptoLUKSInfo, 1);

    new_info->version = info->version;
    new_info->cipher = g_strdup (info->cipher);
    new_info->mode = g_strdup (info->mode);
    new_info->uuid = g_strdup (info->uuid);
    new_info->backing_device = g_strdup (info->backing_device);
    new_info->sector_size = info->sector_size;

    return new_info;
}

void bd_crypto_integrity_info_free (BDCryptoIntegrityInfo *info) {
    g_free (info->algorithm);
    g_free (info->journal_crypt);
    g_free (info->journal_integrity);
    g_free (info);
}

BDCryptoIntegrityInfo* bd_crypto_integrity_info_copy (BDCryptoIntegrityInfo *info) {
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

/* "C" locale to get the locale-agnostic error messages */
static locale_t c_locale = (locale_t) 0;

/**
 * bd_crypto_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_crypto_check_deps () {
    /* nothing to do here */
    return TRUE;
}

static void crypto_log_redirect (gint level, const gchar *msg, void *usrptr __attribute__((unused))) {
    gchar *message = NULL;

    switch (level) {
        case CRYPT_LOG_DEBUG:
        case CRYPT_LOG_VERBOSE:
            message = g_strdup_printf ("[cryptsetup] %s", msg);
            bd_utils_log (LOG_DEBUG, message);
            g_free (message);
            break;
        case CRYPT_LOG_NORMAL:
        case CRYPT_LOG_ERROR:
            message = g_strdup_printf ("[cryptsetup] %s", msg);
            bd_utils_log (LOG_INFO, message);
            g_free (message);
            break;
        default:
            g_warning ("Unknown cryptsetup log level %d.", level);
            message = g_strdup_printf ("[cryptsetup] %s", msg);
            bd_utils_log (LOG_INFO, message);
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
gboolean bd_crypto_init () {
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
void bd_crypto_close () {
    c_locale = (locale_t) 0;
    crypt_set_log_callback (NULL, NULL, NULL);
}

/**
 * bd_crypto_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDCryptoTechMode) for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
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
        case BD_CRYPTO_TECH_LUKS2:
#ifndef LIBCRYPTSETUP_2
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                         "LUKS 2 technology requires libcryptsetup >= 2.0");
            return FALSE;
#endif
            ret = mode & (BD_CRYPTO_TECH_MODE_CREATE|BD_CRYPTO_TECH_MODE_OPEN_CLOSE|BD_CRYPTO_TECH_MODE_QUERY|
                          BD_CRYPTO_TECH_MODE_ADD_KEY|BD_CRYPTO_TECH_MODE_REMOVE_KEY|BD_CRYPTO_TECH_MODE_RESIZE|
                          BD_CRYPTO_TECH_MODE_SUSPEND_RESUME|BD_CRYPTO_TECH_MODE_BACKUP_RESTORE);
            if (ret != mode) {
                g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                             "Only 'create', 'open', 'query', 'add-key', 'remove-key', 'resize', 'suspend-resume', 'backup-restore' supported for LUKS 2");
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
#ifndef LIBCRYPTSETUP_2
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                         "Integrity technology requires libcryptsetup >= 2.0");
            return FALSE;
#endif
            ret = mode & (BD_CRYPTO_TECH_MODE_QUERY);
            if (ret != mode) {
                g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                             "Only 'query' supported for Integrity");
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
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): A newly generated %BD_CRYPTO_BACKUP_PASSPHRASE_LENGTH-long passphrase.
 *
 * See %BD_CRYPTO_BACKUP_PASSPHRASE_CHARSET for the definition of the charset used for the passphrase.
 *
 * Tech category: always available
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
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_QUERY
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

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    crypt_free (cd);
    return (ret == 0);
}

/**
 * bd_crypto_luks_uuid:
 * @device: the queried device
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): UUID of the @device or %NULL if failed to determine (@error
 * is populated with the error in such cases)
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_QUERY
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

    ret_num = crypt_load (cd, CRYPT_LUKS, NULL);
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
 * bd_crypto_get_luks_metadata_size:
 * @device: the queried device
 * @error: (out): place to store error (if any)
 *
 * Returns: luks device metadata size of the @device
 *          or 0 if failed to determine (@error is populated
 *          with the error in such cases)
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_QUERY
 */
guint64 bd_crypto_luks_get_metadata_size (const gchar *device, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret_num;
    guint64 ret;

    ret_num = crypt_init (&cd, device);
    if (ret_num != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret_num, c_locale));
        return 0;
    }

    ret_num = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret_num != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device: %s", strerror_l(-ret_num, c_locale));
        crypt_free (cd);
        return 0;
    }

    ret = SECTOR_SIZE * crypt_get_data_offset (cd);
    crypt_free (cd);

    return ret;
}

/**
 * bd_crypto_luks_get_policies
 * @device: the queried device
 * @include_secrets: whether to include sensitive information
 * @error: (out): place to store error (if any)
 *
 * Returns: Array of policies
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_QUERY
 */

static luksmeta_uuid_t luksmeta_clevis_uuid = { 0xcb, 0x6e, 0x89, 0x04, 0x81, 0xff, 0x40, 0xda,
                                                0xa8, 0x4a, 0x07, 0xab, 0x9a, 0xb5, 0x71, 0x5e };

static void add_error_policy (struct json_object *policies, int slot, int err) {
  struct json_object *pol = json_object_new_object ();
  json_object_object_add (pol, "slot", json_object_new_int (slot));
  json_object_object_add (pol, "errno", json_object_new_int (err));
  json_object_array_add (policies, pol);
}

static void decode_header_inplace (gchar *buf) {
    gchar *ptr;
    gsize len;
    for (ptr = buf; *ptr; ptr++) {
      if (*ptr == '-')
        *ptr = '+';
      else if (*ptr == '_')
        *ptr = '/';
      else if (*ptr == '.') {
        *ptr = '\0';
        break;
      }
    }
    while ((ptr - buf) % 4 > 0)
        *ptr++ = '=';
    *ptr = '\0';
    g_base64_decode_inplace (buf, &len);
    buf[len] = '\0';
}

static struct json_object *get_clevis_config (struct json_object *header, gboolean include_secrets) {
  struct json_object *clevis;
  struct json_object *pin;
  const gchar *pin_str;

  // "include_secrets" is unused right now because we have no code
  // that deals with configs that contain secrets.
  //
  (void)include_secrets;

  clevis = json_object_object_get (header, "clevis");
  if (clevis == NULL)
      return NULL;

  pin = json_object_object_get (clevis, "pin");
  if (pin == NULL)
      return json_object_new_object ();

  pin_str = json_object_get_string (pin);
  if (g_strcmp0 (pin_str, "tang") == 0) {
      /* For "tang", the configuration is stored directly in the
         header, and there is nothing sensitive in it.
      */
      return json_object_get (clevis);
  } else if (g_strcmp0 (pin_str, "sss") == 0) {
      /* A "sss" config needs to be decoded and cleaned recursively.
       */
      struct json_object *pin_header;
      struct json_object *config;
      struct json_object *sss_config;
      struct json_object *pin_configs;
      struct json_object *jwes;

      pin_header = json_object_object_get (clevis, pin_str);
      if (pin_header == NULL)
        return NULL;

      config = json_object_new_object ();
      json_object_object_add (config, "pin", json_object_get (pin));

      sss_config = json_object_new_object ();
      json_object_object_add (sss_config, "t", json_object_get (json_object_object_get (pin_header, "t")));

      pin_configs = json_object_new_object ();
      jwes = json_object_object_get (pin_header, "jwe");
      if (jwes) {
          for (size_t i = 0; i < json_object_array_length (jwes); i++) {
              const char *jwe;
              gchar *buf;
              struct json_object *header, *subpin_config, *subpin_pin, *subpin_data;
              struct json_object *bucket;

              jwe = json_object_get_string (json_object_array_get_idx (jwes, i));
              buf = g_new0(gchar, strlen(jwe) + 3 + 1); // 3 characters extra base64 padding, 1 zero terminator
              strcpy (buf, jwe);
              decode_header_inplace (buf);
              header = json_tokener_parse (buf);
              g_free (buf);
              if (header == NULL)
                  continue; // XXX
              subpin_config = get_clevis_config (header, include_secrets);

              subpin_pin = json_object_object_get (subpin_config, "pin");
              if (subpin_pin) {
                  subpin_data = json_object_object_get (subpin_config, json_object_get_string (subpin_pin));
                  if (subpin_data) {
                      bucket = json_object_object_get (pin_configs, json_object_get_string (subpin_pin));
                      if (bucket == NULL) {
                          bucket = json_object_new_array ();
                          json_object_object_add (pin_configs, json_object_get_string (subpin_pin), bucket);
                      }
                      json_object_array_add (bucket, subpin_data);
                  }
              }
          }
          json_object_object_add (sss_config, "pins", pin_configs);
      }

      json_object_object_add (config, "sss", sss_config);
      return config;
  } else {
      /* For unknown pins, we only acknowlegde that they exist.
       */
      struct json_object *config = json_object_new_object ();
      json_object_object_add (config, "pin", json_object_get (pin));
      json_object_object_add (config, json_object_get_string (pin), json_object_new_object ());
      return config;
  }
}

gchar *bd_crypto_luks_get_policies (const gchar *device, gboolean include_secrets, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret_num;
    luksmeta_uuid_t uuid = { };
    gchar *buf = NULL;
    struct json_object *header;
    struct json_object *policy, *policies;
    gchar *data;

    ret_num = crypt_init (&cd, device);
    if (ret_num != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret_num, c_locale));
        return NULL;
    }

    ret_num = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret_num != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device: %s", strerror_l(-ret_num, c_locale));
        crypt_free (cd);
        return NULL;
    }

    policies = json_object_new_array ();

    for (int slot = 0; slot < crypt_keyslot_max(CRYPT_LUKS1); slot++) {
        ret_num = luksmeta_load (cd, slot, uuid, NULL, 0);
        if (ret_num == -ENOENT || ret_num == -ENODATA || ret_num == -ENOTSUP)
            continue;

        if (ret_num < 0) {
            add_error_policy (policies, slot, -ret_num);
            continue;
        }

        if (memcmp (luksmeta_clevis_uuid, uuid, sizeof(luksmeta_uuid_t)) != 0)
            continue;

        buf = g_new0(gchar, ret_num + 3 + 1); // 3 characters extra base64 padding, 1 zero terminator
        ret_num = luksmeta_load (cd, slot, uuid, buf, ret_num);
        if (ret_num < 0) {
            add_error_policy (policies, slot, -ret_num);
            g_free (buf);
            continue;
        }

        decode_header_inplace (buf);

        header = json_tokener_parse (buf);
        g_free (buf);
        if (header == NULL) {
            continue;
        }

        policy = json_object_new_object ();
        json_object_object_add (policy, "slot", json_object_new_int (slot));
        json_object_object_add (policy, "clevis", get_clevis_config (header, include_secrets));
        json_object_array_add (policies, policy);
        json_object_put (header);
    }

    data = g_strdup (json_object_to_json_string_ext (policies, JSON_C_TO_STRING_NOSLASHESCAPE));
    json_object_put (policies);
    crypt_free (cd);
    return data;
}

/**
 * bd_crypto_luks_status:
 * @luks_device: the queried LUKS device
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer none): one of "invalid", "inactive", "active" or "busy" or
 * %NULL if failed to determine (@error is populated with the error in
 * such cases)
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_QUERY
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

static gboolean luks_format (const gchar *device, const gchar *cipher, guint64 key_size, const guint8 *pass_data, gsize data_size, const gchar *key_file, guint64 min_entropy, BDCryptoLUKSVersion luks_version, BDCryptoLUKSExtra *extra, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret;
    gchar **cipher_specs = NULL;
    guint32 current_entropy = 0;
    gint dev_random_fd = -1;
    gboolean success = FALSE;
    gchar *key_buffer = NULL;
    gsize buf_len = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    const gchar* crypt_version = NULL;

    msg = g_strdup_printf ("Started formatting '%s' as LUKS device", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (luks_version == BD_CRYPTO_LUKS_VERSION_LUKS1)
        crypt_version = CRYPT_LUKS1;
#ifdef LIBCRYPTSETUP_2
    else if (luks_version == BD_CRYPTO_LUKS_VERSION_LUKS2)
        crypt_version = CRYPT_LUKS2;
#endif
    else {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                     "Unknown or unsupported LUKS version specified");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if ((data_size == 0) && !key_file) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NO_KEY,
                     "At least one of passphrase and key file have to be specified!");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    cipher = cipher ? cipher : DEFAULT_LUKS_CIPHER;
    cipher_specs = g_strsplit (cipher, "-", 2);
    if (g_strv_length (cipher_specs) != 2) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_SPEC,
                     "Invalid cipher specification: '%s'", cipher);
        crypt_free (cd);
        g_strfreev (cipher_specs);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* resolve requested/default key_size (should be in bytes) */
    key_size = (key_size != 0) ? (key_size / 8) : (DEFAULT_LUKS_KEYSIZE_BITS / 8);

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
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_FORMAT_FAILED,
                         "Failed to check random data entropy level");
            crypt_free (cd);
            g_strfreev (cipher_specs);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }

    if (extra) {
        if (luks_version == BD_CRYPTO_LUKS_VERSION_LUKS1) {

            if (extra->integrity || extra->sector_size || extra->label || extra->subsystem) {
                g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_INVALID_PARAMS,
                             "Invalid extra arguments specified. Only `data_alignment`"
                             "and `data_device` are valid for LUKS 1.");
                crypt_free (cd);
                g_strfreev (cipher_specs);
                bd_utils_report_finished (progress_id, (*error)->message);
                return FALSE;
            }

            struct crypt_params_luks1 params = ZERO_INIT;
            params.data_alignment = extra->data_alignment;
            params.data_device = extra->data_device;
            ret = crypt_format (cd, crypt_version, cipher_specs[0], cipher_specs[1],
                                NULL, NULL, key_size, &params);
        }
#ifdef LIBCRYPTSETUP_2
        else if (luks_version == BD_CRYPTO_LUKS_VERSION_LUKS2) {
            struct crypt_params_luks2 params = ZERO_INIT;
            params.pbkdf = NULL;
            params.integrity = extra->integrity;
            params.integrity_params = NULL;
            params.data_alignment = extra->data_alignment;
            params.data_device = extra->data_device;
            params.sector_size = extra->sector_size ? extra->sector_size : DEFAULT_LUKS2_SECTOR_SIZE;
            params.label = extra->label;
            params.subsystem = extra->subsystem;
            ret = crypt_format (cd, crypt_version, cipher_specs[0], cipher_specs[1],
                                NULL, NULL, key_size, &params);
        }
#endif
    } else
        ret = crypt_format (cd, crypt_version, cipher_specs[0], cipher_specs[1],
                            NULL, NULL, key_size, NULL);
    g_strfreev (cipher_specs);

    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_FORMAT_FAILED,
                     "Failed to format device: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    bd_utils_report_progress (progress_id, ((data_size != 0) && key_file) ? 40 : 50, "Format created");
    if (data_size != 0) {
        ret = crypt_keyslot_add_by_volume_key (cd, CRYPT_ANY_SLOT, NULL, 0,
                                               (const char*) pass_data, data_size);
        if (ret < 0) {
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ADD_KEY,
                         "Failed to add passphrase: %s", strerror_l(-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
        bd_utils_report_progress (progress_id, ((data_size != 0) && key_file) ? 70 : 100, "Added key");
    }

    if (key_file) {
        success = g_file_get_contents (key_file, &key_buffer, &buf_len, error);
        if (!success) {
            g_prefix_error (error, "Failed to add key file: %s", strerror_l(-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
        ret = crypt_keyslot_add_by_volume_key (cd, CRYPT_ANY_SLOT, NULL, 0,
                                               (const char*) key_buffer, buf_len);
        g_free (key_buffer);
        if (ret < 0) {
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ADD_KEY,
                         "Failed to add key file: %s", strerror_l(-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }

    crypt_free (cd);

    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
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
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_CREATE
 */
gboolean bd_crypto_luks_format (const gchar *device, const gchar *cipher, guint64 key_size, const gchar *passphrase, const gchar *key_file, guint64 min_entropy, GError **error) {
    return luks_format (device, cipher, key_size, (const guint8*) passphrase, passphrase ? strlen(passphrase) : 0, key_file, min_entropy, BD_CRYPTO_LUKS_VERSION_LUKS1, NULL, error);
}

/**
 * bd_crypto_luks_format_blob:
 * @device: a device to format as LUKS
 * @cipher: (allow-none): cipher specification (type-mode, e.g. "aes-xts-plain64") or %NULL to use the default
 * @key_size: size of the volume key in bits or 0 to use the default
 * @pass_data: (array length=data_len): a passphrase for the new LUKS device (may contain arbitrary binary data)
 * @data_len: (allow-none): length of the @pass_data buffer
 * @min_entropy: minimum random data entropy (in bits) required to format @device as LUKS
 * @error: (out): place to store error (if any)
 *
 * Formats the given @device as LUKS according to the other parameters given. If
 * @min_entropy is specified (greater than 0), the function waits for enough
 * entropy to be available in the random data pool (WHICH MAY POTENTIALLY TAKE
 * FOREVER).
 *
 * Returns: whether the given @device was successfully formatted as LUKS or not
 * (the @error) contains the error in such cases)
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_CREATE
 */
gboolean bd_crypto_luks_format_blob (const gchar *device, const gchar *cipher, guint64 key_size, const guint8 *pass_data, gsize data_len, guint64 min_entropy, GError **error) {
    return luks_format (device, cipher, key_size, pass_data, data_len, NULL, min_entropy, BD_CRYPTO_LUKS_VERSION_LUKS1, NULL, error);
}

/**
 * bd_crypto_luks_format_luks2:
 * @device: a device to format as LUKS
 * @cipher: (allow-none): cipher specification (type-mode, e.g. "aes-xts-plain64") or %NULL to use the default
 * @key_size: size of the volume key in bits or 0 to use the default
 * @passphrase: (allow-none): a passphrase for the new LUKS device or %NULL if not requested
 * @key_file: (allow-none): a key file for the new LUKS device or %NULL if not requested
 * @min_entropy: minimum random data entropy (in bits) required to format @device as LUKS
 * @luks_version: whether to use LUKS v1 or LUKS v2
 * @extra: (allow-none): extra arguments for LUKS format creation
 * @error: (out): place to store error (if any)
 *
 * Formats the given @device as LUKS according to the other parameters given. If
 * @min_entropy is specified (greater than 0), the function waits for enough
 * entropy to be available in the random data pool (WHICH MAY POTENTIALLY TAKE
 * FOREVER).
 *
 * Either @passhphrase or @key_file has to be != %NULL.
 *
 * Using this function with @luks_version set to %BD_CRYPTO_LUKS_VERSION_LUKS1 and
 * @extra to %NULL is the same as calling %bd_crypto_luks_format.
 *
 * Returns: whether the given @device was successfully formatted as LUKS or not
 * (the @error) contains the error in such cases)
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS2-%BD_CRYPTO_TECH_MODE_CREATE
 */
gboolean bd_crypto_luks_format_luks2 (const gchar *device, const gchar *cipher, guint64 key_size, const gchar *passphrase, const gchar *key_file, guint64 min_entropy, BDCryptoLUKSVersion luks_version, BDCryptoLUKSExtra *extra,GError **error) {
    return luks_format (device, cipher, key_size, (const guint8*) passphrase, passphrase ? strlen(passphrase) : 0, key_file, min_entropy, luks_version, extra, error);
}

/**
 * bd_crypto_luks_format_luks2_blob:
 * @device: a device to format as LUKS
 * @cipher: (allow-none): cipher specification (type-mode, e.g. "aes-xts-plain64") or %NULL to use the default
 * @key_size: size of the volume key in bits or 0 to use the default
 * @pass_data: (array length=data_len): a passphrase for the new LUKS device (may contain arbitrary binary data)
 * @data_len: length of the @pass_data buffer
 * @min_entropy: minimum random data entropy (in bits) required to format @device as LUKS
 * @luks_version: whether to use LUKS v1 or LUKS v2
 * @extra: (allow-none): extra arguments for LUKS format creation
 * @error: (out): place to store error (if any)
 *
 * Formats the given @device as LUKS according to the other parameters given. If
 * @min_entropy is specified (greater than 0), the function waits for enough
 * entropy to be available in the random data pool (WHICH MAY POTENTIALLY TAKE
 * FOREVER).
 *
 * Using this function with @luks_version set to %BD_CRYPTO_LUKS_VERSION_LUKS1 and
 * @extra to %NULL is the same as calling %bd_crypto_luks_format_blob.
 *
 * Returns: whether the given @device was successfully formatted as LUKS or not
 * (the @error) contains the error in such cases)
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS2-%BD_CRYPTO_TECH_MODE_CREATE
 */
gboolean bd_crypto_luks_format_luks2_blob (const gchar *device, const gchar *cipher, guint64 key_size, const guint8 *pass_data, gsize data_len, guint64 min_entropy, BDCryptoLUKSVersion luks_version, BDCryptoLUKSExtra *extra, GError **error) {
    return luks_format (device, cipher, key_size, pass_data, data_len, NULL, min_entropy, luks_version, extra, error);
}

static gboolean luks_open (const gchar *device, const gchar *name, const guint8 *pass_data, gsize data_len, const gchar *key_file, gboolean read_only, GError **error) {
    struct crypt_device *cd = NULL;
    gboolean success = FALSE;
    gchar *key_buffer = NULL;
    gsize buf_len = 0;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started opening '%s' LUKS device", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if ((data_len == 0) && !key_file) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NO_KEY,
                     "No passphrase nor key file specified, cannot open.");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (key_file) {
        success = g_file_get_contents (key_file, &key_buffer, &buf_len, error);
        if (!success) {
            g_prefix_error (error, "Failed to add key file: %s", strerror_l(-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    } else
        buf_len = data_len;

    ret = crypt_activate_by_passphrase (cd, name, CRYPT_ANY_SLOT, key_buffer ? key_buffer : (char*) pass_data,
                                        buf_len, read_only ? CRYPT_ACTIVATE_READONLY : 0);
    g_free (key_buffer);

    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to activate device: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
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
 * @passphrase: (allow-none): passphrase to open the @device or %NULL
 * @key_file: (allow-none): key file path to use for opening the @device or %NULL
 * @read_only: whether to open as read-only or not (meaning read-write)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully opened or not
 *
 * One of @passphrase, @key_file has to be != %NULL.
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_luks_open (const gchar *device, const gchar *name, const gchar *passphrase, const gchar *key_file, gboolean read_only, GError **error) {
    return luks_open (device, name, (const guint8*) passphrase, passphrase ? strlen (passphrase) : 0, key_file, read_only, error);
}

/**
 * bd_crypto_luks_open_blob:
 * @device: the device to open
 * @name: name for the LUKS device
 * @pass_data: (array length=data_len): a passphrase for the new LUKS device (may contain arbitrary binary data)
 * @data_len: length of the @pass_data buffer
 * @read_only: whether to open as read-only or not (meaning read-write)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully opened or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_luks_open_blob (const gchar *device, const gchar *name, const guint8* pass_data, gsize data_len, gboolean read_only, GError **error) {
    return luks_open (device, name, (const guint8*) pass_data, data_len, NULL, read_only, error);
}

/**
 * bd_crypto_luks_close:
 * @luks_device: LUKS device to close
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given @luks_device was successfully closed or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_luks_close (const gchar *luks_device, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started closing LUKS device '%s'", luks_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init_by_name (&cd, luks_device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_deactivate (cd, luks_device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to deactivate device: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_add_key_blob:
 * @device: device to add new key to
 * @pass_data: (array length=data_len): a passphrase for the new LUKS device (may contain arbitrary binary data)
 * @data_len: length of the @pass_data buffer
 * @npass_data: (array length=ndata_len): a new passphrase for the new LUKS device (may contain arbitrary binary data)
 * @ndata_len: length of the @npass_data buffer
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @npass_data was successfully added to @device or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_ADD_KEY
 */
gboolean bd_crypto_luks_add_key_blob (const gchar *device, const guint8 *pass_data, gsize data_len, const guint8 *npass_data, gsize ndata_len, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started adding key to the LUKS device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_keyslot_add_by_passphrase (cd, CRYPT_ANY_SLOT, (char*) pass_data, data_len, (char*) npass_data, ndata_len);

    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ADD_KEY,
                     "Failed to add key: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
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
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_ADD_KEY
 */
gboolean bd_crypto_luks_add_key (const gchar *device, const gchar *pass, const gchar *key_file, const gchar *npass, const gchar *nkey_file, GError **error) {
    gboolean success = FALSE;
    gchar *key_buf = NULL;
    gsize buf_len = 0;
    gchar *nkey_buf = NULL;
    gsize nbuf_len = 0;

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

    if (key_file) {
        success = g_file_get_contents (key_file, &key_buf, &buf_len, error);
        if (!success) {
            g_prefix_error (error, "Failed to load key from file '%s': ", key_file);
            return FALSE;
        }
    } else
        buf_len = strlen (pass);

    if (nkey_file) {
        success = g_file_get_contents (nkey_file, &nkey_buf, &nbuf_len, error);
        if (!success) {
            g_prefix_error (error, "Failed to load key from file '%s': ", nkey_file);
            return FALSE;
        }
    } else
        nbuf_len = strlen (npass);

    success = bd_crypto_luks_add_key_blob (device,
                                           key_buf ? (const guint8*) key_buf : (const guint8*) pass, buf_len,
                                           nkey_buf ? (const guint8*) nkey_buf : (const guint8*) npass, nbuf_len,
                                           error);
    g_free (key_buf);
    g_free (nkey_buf);

    return success;
}

/**
 * bd_crypto_luks_remove_key_blob:
 * @device: device to add new key to
 * @pass_data: (array length=data_len): a passphrase for the new LUKS device (may contain arbitrary binary data) to remove
 * @data_len: length of the @pass_data buffer
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the key was successfully removed or not
 *
 * Either @pass or @key_file has to be != %NULL.
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_REMOVE_KEY
 */
gboolean bd_crypto_luks_remove_key_blob (const gchar *device, const guint8 *pass_data, gsize data_len, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started removing key from the LUKS device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_activate_by_passphrase (cd, NULL, CRYPT_ANY_SLOT, (char*) pass_data, data_len, 0);
    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_KEY_SLOT,
                     "Failed to determine key slot: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_keyslot_destroy (cd, ret);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_REMOVE_KEY,
                     "Failed to remove key: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
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
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_REMOVE_KEY
 */
gboolean bd_crypto_luks_remove_key (const gchar *device, const gchar *pass, const gchar *key_file, GError **error) {
    gboolean success = FALSE;
    gchar *key_buf = NULL;
    gsize buf_len = 0;

    if (!pass && !key_file) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_REMOVE_KEY,
                     "No passphrase nor key file given, cannot remove key.");
        return FALSE;
    }

    if (key_file) {
        success = g_file_get_contents (key_file, &key_buf, &buf_len, error);
        if (!success) {
            g_prefix_error (error, "Failed to load key from file '%s': ", key_file);
            return FALSE;
        }
    } else
        buf_len = strlen (pass);

    success = bd_crypto_luks_remove_key_blob (device, key_buf ? (const guint8*) key_buf : (const guint8*) pass, buf_len, error);
    g_free (key_buf);

    return success;
}

/**
 * bd_crypto_luks_change_key_blob:
 * @device: device to change key of
 * @pass_data: (array length=data_len): a passphrase for the new LUKS device (may contain arbitrary binary data)
 * @data_len: length of the @pass_data buffer
 * @npass_data: (array length=ndata_len): a new passphrase for the new LUKS device (may contain arbitrary binary data)
 * @ndata_len: length of the @npass_data buffer
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the key was successfully changed or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_ADD_KEY&%BD_CRYPTO_TECH_MODE_REMOVE_KEY
 */
gboolean bd_crypto_luks_change_key_blob (const gchar *device, const guint8 *pass_data, gsize data_len, const guint8 *npass_data, gsize ndata_len, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    gchar *volume_key = NULL;
    gsize vk_size = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started changing key on the LUKS device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    vk_size = crypt_get_volume_key_size(cd);
    volume_key = (gchar *) g_malloc (vk_size);

    ret = crypt_volume_key_get (cd, CRYPT_ANY_SLOT, volume_key, &vk_size, (char*) pass_data, data_len);
    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's volume key: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        g_free (volume_key);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* ret is the number of the slot with the given pass */
    ret = crypt_keyslot_destroy (cd, ret);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_REMOVE_KEY,
                     "Failed to remove the old passphrase: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        g_free (volume_key);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_keyslot_add_by_volume_key (cd, ret, volume_key, vk_size, (char*) npass_data, ndata_len);
    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_ADD_KEY,
                     "Failed to add the new passphrase: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        g_free (volume_key);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    g_free (volume_key);
    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
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
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_ADD_KEY&%BD_CRYPTO_TECH_MODE_REMOVE_KEY
 */
gboolean bd_crypto_luks_change_key (const gchar *device, const gchar *pass, const gchar *npass, GError **error) {
    return bd_crypto_luks_change_key_blob (device, (guint8*) pass, strlen (pass), (guint8*) npass, strlen (npass), error);
}

static gboolean luks_resize (const gchar *luks_device, guint64 size, const guint8 *pass_data, gsize data_len, const gchar *key_file, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    gboolean success = FALSE;
    gchar *key_buffer = NULL;
    gsize buf_len = 0;

    msg = g_strdup_printf ("Started resizing LUKS device '%s'", luks_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init_by_name (&cd, luks_device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (pass_data || key_file) {
        if (key_file) {
            success = g_file_get_contents (key_file, &key_buffer, &buf_len, error);
            if (!success) {
                g_prefix_error (error, "Failed to add key file: %s", strerror_l(-ret, c_locale));
                crypt_free (cd);
                bd_utils_report_finished (progress_id, (*error)->message);
                return FALSE;
            }
        } else
            buf_len = data_len;

#ifdef LIBCRYPTSETUP_2
        ret = crypt_activate_by_passphrase (cd, NULL, CRYPT_ANY_SLOT,
                                            key_buffer ? key_buffer : (char*) pass_data,
                                            buf_len, CRYPT_ACTIVATE_KEYRING_KEY);
#else
        ret = crypt_activate_by_passphrase (cd, NULL, CRYPT_ANY_SLOT,
                                            key_buffer ? key_buffer : (char*) pass_data,
                                            buf_len, 0);
#endif
        g_free (key_buffer);

        if (ret < 0) {
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                         "Failed to activate device: %s", strerror_l(-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    }

    ret = crypt_resize (cd, luks_device, size);
    if (ret != 0) {
#ifdef LIBCRYPTSETUP_2
        if (ret == -EPERM && g_strcmp0 (crypt_get_type (cd), CRYPT_LUKS2) == 0) {
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_RESIZE_PERM,
                         "Insufficient persmissions to resize device. You need to specify"
                         " passphrase or keyfile to resize LUKS 2 devices that don't"
                         " have verified key loaded in kernel.");
            crypt_free (cd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;

        }
#endif
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_RESIZE_FAILED,
                     "Failed to resize device: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}


/**
 * bd_crypto_luks_resize:
 * @luks_device: opened LUKS device to resize
 * @size: requested size in sectors or 0 to adapt to the backing device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @luks_device was successfully resized or not
 *
 * You need to specify passphrase when resizing LUKS 2 devices that don't have
 * verified key loaded in kernel. If you don't specify a passphrase, resize
 * will fail with %BD_CRYPTO_ERROR_RESIZE_PERM. Use %bd_crypto_luks_resize_luks2
 * or %bd_crypto_luks_resize_luks2_blob for these devices.
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_RESIZE
 */
gboolean bd_crypto_luks_resize (const gchar *luks_device, guint64 size, GError **error) {
    return luks_resize (luks_device, size, NULL, 0, NULL, error);
}

/**
 * bd_crypto_luks_resize_luks2:
 * @luks_device: opened LUKS device to resize
 * @passphrase: (allow-none): passphrase to resize the @luks_device or %NULL
 * @key_file: (allow-none): key file path to use for resizinh the @luks_device or %NULL
 * @size: requested size in sectors or 0 to adapt to the backing device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @luks_device was successfully resized or not
 *
 * You need to specify either @passphrase or @keyfile for LUKS 2 devices that
 * don't have verified key loaded in kernel.
 * For LUKS 1 devices you can set both @passphrase and @keyfile to %NULL to
 * achieve the same as calling %bd_crypto_luks_resize.
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS2-%BD_CRYPTO_TECH_MODE_RESIZE
 */
gboolean bd_crypto_luks_resize_luks2 (const gchar *luks_device, guint64 size, const gchar *passphrase, const gchar *key_file, GError **error) {
    return luks_resize (luks_device, size, (const guint8*) passphrase, passphrase ? strlen (passphrase) : 0, key_file, error);
}

/**
 * bd_crypto_luks_resize_luks2_blob:
 * @luks_device: opened LUKS device to resize
 * @pass_data: (array length=data_len): a passphrase for the new LUKS device (may contain arbitrary binary data)
 * @data_len: length of the @pass_data buffer
 * @size: requested size in sectors or 0 to adapt to the backing device
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @luks_device was successfully resized or not
 *
 * You need to specify @pass_data for LUKS 2 devices that don't have
 * verified key loaded in kernel.
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS2-%BD_CRYPTO_TECH_MODE_RESIZE
 */
gboolean bd_crypto_luks_resize_luks2_blob (const gchar *luks_device, guint64 size, const guint8* pass_data, gsize data_len, GError **error) {
    return luks_resize (luks_device, size, pass_data, data_len, NULL, error);
}

/**
 * bd_crypto_luks_suspend:
 * @luks_device: LUKS device to suspend
 * @error: (out): place to store error (if any)
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

    msg = g_strdup_printf ("Started suspending LUKS device '%s'", luks_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init_by_name (&cd, luks_device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_suspend (cd, luks_device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to suspend device: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

static gboolean luks_resume (const gchar *luks_device, const guint8 *pass_data, gsize data_len, const gchar *key_file, GError **error) {
    struct crypt_device *cd = NULL;
    gboolean success = FALSE;
    gchar *key_buffer = NULL;
    gsize buf_len = 0;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started resuming '%s' LUKS device", luks_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if ((data_len == 0) && !key_file) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NO_KEY,
                     "No passphrase nor key file specified, cannot resume.");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_init_by_name (&cd, luks_device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (key_file) {
        success = g_file_get_contents (key_file, &key_buffer, &buf_len, error);
        if (!success) {
            g_prefix_error (error, "Failed to add key file: %s", strerror_l(-ret, c_locale));
            crypt_free (cd);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    } else
        buf_len = data_len;

    ret = crypt_resume_by_passphrase (cd, luks_device, CRYPT_ANY_SLOT,
                                      key_buffer ? key_buffer : (char*) pass_data,
                                      buf_len);

    if (key_buffer) {
      memset (key_buffer, 0, buf_len);
      g_free (key_buffer);
    }

    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to resume device: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_resume_blob:
 * @luks_device: LUKS device to resume
 * @pass_data: (array length=data_len): a passphrase for the LUKS device (may contain arbitrary binary data)
 * @data_len: length of the @pass_data buffer
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given @luks_device was successfully resumed or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_SUSPEND_RESUME
 */
gboolean bd_crypto_luks_resume_blob (const gchar *luks_device, const guint8 *pass_data, gsize data_len, GError **error) {
    return luks_resume (luks_device, pass_data, data_len, NULL, error);
}

/**
 * bd_crypto_luks_resume:
 * @luks_device: LUKS device to resume
 * @passphrase: (allow-none): passphrase to resume the @device or %NULL
 * @key_file: (allow-none): key file path to use for resuming the @device or %NULL
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the give @luks_device was successfully resumed or not
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS-%BD_CRYPTO_TECH_MODE_SUSPEND_RESUME
 */
gboolean bd_crypto_luks_resume (const gchar *luks_device, const gchar *passphrase, const gchar *key_file, GError **error) {
    return luks_resume (luks_device, (guint8*) passphrase, passphrase ? strlen (passphrase) : 0, key_file, error);
}

/**
 * bd_crypto_luks_kill_slot:
 * @device: device to kill slot on
 * @slot: keyslot to destroy
 * @error: (out): place to store error (if any)
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

    msg = g_strdup_printf ("Started killing slot %d on LUKS device '%s'", slot, device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_keyslot_destroy (cd, slot);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to destroy keyslot: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
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
 * @error: (out): place to store error (if any)
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

    msg = g_strdup_printf ("Started header backup of LUKS device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_load (cd, CRYPT_LUKS, NULL);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_header_backup (cd, NULL, backup_file);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to backup LUKS header: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
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
 * @error: (out): place to store error (if any)
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

    msg = g_strdup_printf ("Started LUKS header restore on device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_header_restore (cd, NULL, backup_file);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to restore LUKS header: %s", strerror_l (-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_luks_info:
 * @luks_device: a device to get information about
 * @error: (out): place to store error (if any)
 *
 * Returns: information about the @luks_device or %NULL in case of error
 *
 * Tech category: %BD_CRYPTO_TECH_LUKS%BD_CRYPTO_TECH_MODE_QUERY
 */
BDCryptoLUKSInfo* bd_crypto_luks_info (const gchar *luks_device, GError **error) {
    struct crypt_device *cd = NULL;
    BDCryptoLUKSInfo *info = NULL;
    const gchar *version = NULL;
    gint ret;

    ret = crypt_init_by_name (&cd, luks_device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l (-ret, c_locale));
        return NULL;
    }

    info = g_new0 (BDCryptoLUKSInfo, 1);

    version = crypt_get_type (cd);
    if (g_strcmp0 (version, CRYPT_LUKS1) == 0)
        info->version = BD_CRYPTO_LUKS_VERSION_LUKS1;
#ifdef LIBCRYPTSETUP_2
    else if (g_strcmp0 (version, CRYPT_LUKS2) == 0)
        info->version = BD_CRYPTO_LUKS_VERSION_LUKS2;
#endif
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

#ifdef LIBCRYPTSETUP_2
    info->sector_size = crypt_get_sector_size (cd);
#else
    info->sector_size = 0;
#endif

    crypt_free (cd);
    return info;
}

/**
 * bd_crypto_integrity_info:
 * @device: a device to get information about
 * @error: (out): place to store error (if any)
 *
 * Returns: information about the @device or %NULL in case of error
 *
 * Tech category: %BD_CRYPTO_TECH_INTEGRITY%BD_CRYPTO_TECH_MODE_QUERY
 */
#ifndef LIBCRYPTSETUP_2
BDCryptoIntegrityInfo* bd_crypto_integrity_info (const gchar *device __attribute__((unused)), GError **error) {
    g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                 "Integrity technology requires libcryptsetup >= 2.0");
    return NULL;
}
#else
BDCryptoIntegrityInfo* bd_crypto_integrity_info (const gchar *device, GError **error) {
    struct crypt_device *cd = NULL;
    struct crypt_params_integrity ip = ZERO_INIT;
    BDCryptoIntegrityInfo *info = NULL;
    gint ret;

    ret = crypt_init_by_name (&cd, device);
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
#endif

/**
 * bd_crypto_device_seems_encrypted:
 * @device: the queried device
 * @error: (out): place to store error (if any)
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
    guchar buf[BD_CRYPTO_CHI_SQUARE_BYTES_TO_CHECK];
    guint symbols[256] = {0};
    gfloat chi_square = 0.0;
    gfloat e = (gfloat) sizeof(buf) / (gfloat) 256.0;
    guint i;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started determining if device '%s' seems to be encrypted", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    fd = open (device, O_RDONLY);
    if (fd == -1) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE, "Failed to open device");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (read (fd, buf, sizeof(buf)) != sizeof(buf)) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE, "Failed to read device");
        bd_utils_report_finished (progress_id, (*error)->message);
        close(fd);
        return FALSE;
    }

    close(fd);

    /* Calculate Chi Square */
    for (i = 0; i < sizeof(buf); i++)
        /* This is safe because the max value of buf[i] is < sizeof(symbols). */
        symbols[buf[i]]++;
    for (i = 0; i < 256; i++)
        chi_square += (symbols[i] - e) * (symbols[i] - e);
    chi_square /= e;

    bd_utils_report_finished (progress_id, "Completed");
    return BD_CRYPTO_CHI_SQUARE_LOWER_LIMIT < chi_square && chi_square < BD_CRYPTO_CHI_SQUARE_UPPER_LIMIT;
}

/**
 * bd_crypto_tc_open:
 * @device: the device to open
 * @name: name for the TrueCrypt/VeraCrypt device
 * @pass_data: (array length=data_len): a passphrase for the TrueCrypt/VeraCrypt volume (may contain arbitrary binary data)
 * @data_len: length of the @pass_data buffer
 * @read_only: whether to open as read-only or not (meaning read-write)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully opened or not
 *
 * Tech category: %BD_CRYPTO_TECH_TRUECRYPT-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_tc_open (const gchar *device, const gchar *name, const guint8* pass_data, gsize data_len, gboolean read_only, GError **error) {
    return bd_crypto_tc_open_full (device, name, pass_data, data_len, NULL, FALSE, FALSE, FALSE, 0, read_only, error);
}

/**
 * bd_crypto_tc_open_full:
 * @device: the device to open
 * @name: name for the TrueCrypt/VeraCrypt device
 * @pass_data: (array length=data_len): a passphrase for the TrueCrypt/VeraCrypt volume (may contain arbitrary binary data)
 * @data_len: length of the @pass_data buffer
 * @read_only: whether to open as read-only or not (meaning read-write)
 * @keyfiles: (allow-none) (array zero-terminated=1): paths to the keyfiles for the TrueCrypt/VeraCrypt volume
 * @hidden: whether a hidden volume inside the volume should be opened
 * @system: whether to try opening as an encrypted system (with boot loader)
 * @veracrypt: whether to try VeraCrypt modes (TrueCrypt modes are tried anyway)
 * @veracrypt_pim: VeraCrypt PIM value (only used if @veracrypt is %TRUE; only supported if compiled against libcryptsetup >= 2.0)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @device was successfully opened or not
 *
 * Tech category: %BD_CRYPTO_TECH_TRUECRYPT-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_tc_open_full (const gchar *device, const gchar *name, const guint8* pass_data, gsize data_len, const gchar **keyfiles, gboolean hidden, gboolean system, gboolean veracrypt, guint32 veracrypt_pim, gboolean read_only, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    struct crypt_params_tcrypt params = ZERO_INIT;
    gsize keyfiles_count = 0;
    guint i;

    msg = g_strdup_printf ("Started opening '%s' TrueCrypt/VeraCrypt device", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (keyfiles) {
        for (i=0; *(keyfiles + i); i++);
        keyfiles_count = i;
    }

    if ((data_len == 0) && (keyfiles_count == 0)) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NO_KEY,
                     "No passphrase nor key file specified, cannot open.");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_init (&cd, device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    params.passphrase = (const char*) pass_data;
    params.passphrase_size = data_len;
    params.keyfiles = keyfiles;
    params.keyfiles_count = keyfiles_count;

    if (veracrypt)
        params.flags |= CRYPT_TCRYPT_VERA_MODES;
    if (hidden)
        params.flags |= CRYPT_TCRYPT_HIDDEN_HEADER;
    if (system)
        params.flags |= CRYPT_TCRYPT_SYSTEM_HEADER;

#ifndef LIBCRYPTSETUP_2
    if (veracrypt && veracrypt_pim != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_TECH_UNAVAIL,
                     "Compiled against a version of libcryptsetup that does not support the VeraCrypt PIM setting.");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
#else
    if (veracrypt && veracrypt_pim != 0)
        params.veracrypt_pim = veracrypt_pim;
#endif

    ret = crypt_load (cd, CRYPT_TCRYPT, &params);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to load device's parameters: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_activate_by_volume_key (cd, name, NULL, 0,
                                        read_only ? CRYPT_ACTIVATE_READONLY : 0);

    if (ret < 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to activate device: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_crypto_tc_close:
 * @tc_device: TrueCrypt/VeraCrypt device to close
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the given @tc_device was successfully closed or not
 *
 * Tech category: %BD_CRYPTO_TECH_TRUECRYPT-%BD_CRYPTO_TECH_MODE_OPEN_CLOSE
 */
gboolean bd_crypto_tc_close (const gchar *tc_device, GError **error) {
    struct crypt_device *cd = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started closing TrueCrypt/VeraCrypt device '%s'", tc_device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    ret = crypt_init_by_name (&cd, tc_device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to initialize device: %s", strerror_l(-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = crypt_deactivate (cd, tc_device);
    if (ret != 0) {
        g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_DEVICE,
                     "Failed to deactivate device: %s", strerror_l(-ret, c_locale));
        crypt_free (cd);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    crypt_free (cd);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

#ifdef WITH_BD_ESCROW
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
    GError *tmp_error = NULL;

    packet_data = libvk_volume_create_packet_asymmetric_with_format (volume, &packet_data_size, secret_type, cert,
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

    status = g_io_channel_write_chars (out_file, (const gchar *) packet_data, (gssize) packet_data_size,
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
#endif // WITH_BD_ESCROW

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
 *
 * Tech category: %BD_CRYPTO_TECH_ESCROW-%BD_CRYPTO_TECH_MODE_CREATE
 */
#ifndef WITH_BD_ESCROW
gboolean bd_crypto_escrow_device (const gchar *device UNUSED, const gchar *passphrase UNUSED, const gchar *cert_data UNUSED, const gchar *directory UNUSED, const gchar *backup_passphrase UNUSED, GError **error) {
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
    msg = g_strdup_printf ("Started creating escrow data for the LUKS device '%s'", device);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (!NSS_IsInitialized())
        if (NSS_NoDB_Init(NULL) != SECSuccess) {
            g_set_error (error, BD_CRYPTO_ERROR, BD_CRYPTO_ERROR_NSS_INIT_FAILED,
                         "Failed to initialize NSS");
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }

    volume = libvk_volume_open (device, error);
    if (!volume) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, (*error)->message);
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

    if (libvk_volume_get_secret (volume, LIBVK_SECRET_DEFAULT, ui, error) != 0) {
        /* error is already populated */
        libvk_volume_free (volume);
        libvk_ui_free (ui);
        bd_utils_report_finished (progress_id, (*error)->message);
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
        bd_utils_report_finished (progress_id, (*error)->message);
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
        bd_utils_report_finished (progress_id, (*error)->message);
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
            bd_utils_report_finished (progress_id, (*error)->message);
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
    bd_utils_report_finished (progress_id, "Completed");
    return ret;
}
#endif // WITH_BD_ESCROW
