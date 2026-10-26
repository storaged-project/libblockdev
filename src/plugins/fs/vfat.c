/*
 * Copyright (C) 2017  Red Hat, Inc.
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

#include <blockdev/utils.h>
#include <check_deps.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#include "vfat.h"
#include "fs.h"
#include "common.h"

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

G_GNUC_INTERNAL
void _fs_vfat_reset_avail_deps (void) {
    g_atomic_int_set (&avail_deps, 0);
}

#define DEPS_MKFSVFAT 0
#define DEPS_MKFSVFAT_MASK (1 << DEPS_MKFSVFAT)
#define DEPS_FATLABEL 1
#define DEPS_FATLABEL_MASK (1 << DEPS_FATLABEL)
#define DEPS_FSCKVFAT 2
#define DEPS_FSCKVFAT_MASK (1 << DEPS_FSCKVFAT)
#define DEPS_RESIZEVFAT 3
#define DEPS_RESIZEVFAT_MASK (1 << DEPS_RESIZEVFAT)
#define DEPS_FATLABELUUID 4
#define DEPS_FATLABELUUID_MASK (1 << DEPS_FATLABELUUID)

#define DEPS_LAST 5

static const UtilDep deps[DEPS_LAST] = {
    {"mkfs.vfat", NULL, NULL, NULL},
    {"fatlabel", NULL, NULL, NULL},
    {"fsck.vfat", NULL, NULL, NULL},
    {"vfat-resize", NULL, NULL, NULL},
    {"fatlabel", "4.2", "--version", "fatlabel\\s+([\\d\\.]+).+"},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    DEPS_MKFSVFAT_MASK,     /* mkfs */
    0,                      /* wipe */
    DEPS_FSCKVFAT_MASK,     /* check */
    DEPS_FSCKVFAT_MASK,     /* repair */
    DEPS_FATLABEL_MASK,     /* set-label */
    DEPS_FSCKVFAT_MASK,     /* query */
    DEPS_RESIZEVFAT_MASK,   /* resize */
    DEPS_FATLABELUUID_MASK, /* set-uuid */
};


#ifdef __clang__
#define ZERO_INIT {}
#else
#define ZERO_INIT {0}
#endif

/**
 * bd_fs_vfat_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
G_GNUC_INTERNAL gboolean
bd_fs_vfat_is_tech_avail (BDFSTech tech G_GNUC_UNUSED, guint64 mode, GError **error) {
    guint32 required = 0;
    guint i = 0;

    for (i = 0; i <= BD_FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * bd_fs_vfat_info_copy: (skip)
 * @data: (nullable): %BDFSVfatInfo to copy
 *
 * Creates a new copy of @data.
 */
BDFSVfatInfo* bd_fs_vfat_info_copy (BDFSVfatInfo *data) {
    if (data == NULL)
        return NULL;

    BDFSVfatInfo *ret = g_new0 (BDFSVfatInfo, 1);

    ret->label = g_strdup (data->label);
    ret->uuid = g_strdup (data->uuid);
    ret->cluster_size = data->cluster_size;
    ret->cluster_count = data->cluster_count;
    ret->free_cluster_count = data->free_cluster_count;

    return ret;
}

/**
 * bd_fs_vfat_info_free: (skip)
 * @data: (nullable): %BDFSVfatInfo to free
 *
 * Frees @data.
 */
void bd_fs_vfat_info_free (BDFSVfatInfo *data) {
    if (data == NULL)
        return;

    g_free (data->label);
    g_free (data->uuid);
    g_free (data);
}

/* we want to support vol ID in the "udev format", e.g. "2E24-EC82" */
static gchar *_fix_uuid (const gchar *uuid) {
    gchar *new_uuid = NULL;
    size_t len = 0;

    len = strlen (uuid);
    if (len == 9 && uuid[4] == '-') {
        new_uuid = g_new0 (gchar, 9);
        memcpy (new_uuid, uuid, 4);
        memcpy (new_uuid + 4, uuid + 5, 4);
    } else
        new_uuid = g_strdup (uuid);

    return new_uuid;
}

G_GNUC_INTERNAL BDExtraArg **
bd_fs_vfat_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    GPtrArray *options_array = g_ptr_array_new ();
    const BDExtraArg **extra_p = NULL;
    gchar *label;
    UtilDep dep = {"mkfs.vfat", "4.2", "--help", "mkfs.fat\\s+([\\d\\.]+).+"};
    gboolean new_vfat = FALSE;
    gchar *new_uuid = NULL;

    if (options->label && g_strcmp0 (options->label, "") != 0) {
        /* convert the label uppercase */
        label = g_ascii_strup (options->label, -1);
        g_ptr_array_add (options_array, bd_extra_arg_new ("-n", label));
        g_free (label);
    }

    if (options->uuid && g_strcmp0 (options->uuid, "") != 0) {
        new_uuid = _fix_uuid (options->uuid);
        g_ptr_array_add (options_array, bd_extra_arg_new ("-i", new_uuid));
        g_free (new_uuid);
    }

    if (options->force)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-I", ""));

    if (options->no_pt) {
        /* only mkfs.vfat >= 4.2 (sometimes) creates the partition table */
        new_vfat = bd_utils_check_util_version (dep.name, dep.version,
                                                dep.ver_arg, dep.ver_regexp,
                                                NULL);
        if (new_vfat)
            g_ptr_array_add (options_array, bd_extra_arg_new ("--mbr=no", ""));
    }

    if (extra) {
        for (extra_p = extra; *extra_p; extra_p++)
            g_ptr_array_add (options_array, bd_extra_arg_copy ((BDExtraArg *) *extra_p));
    }

    g_ptr_array_add (options_array, NULL);

    return (BDExtraArg **) g_ptr_array_free (options_array, FALSE);
}

/**
 * bd_fs_vfat_mkfs:
 * @device: the device to create a new vfat fs on
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkfs.vfat' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Please remember that FAT labels should always be uppercase.
 *
 * Returns: whether a new vfat fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_vfat_mkfs (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[3] = {"mkfs.vfat", device, NULL};

    if (!check_deps (&avail_deps, DEPS_MKFSVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    return bd_utils_exec_and_report_error (args, extra, error);
}

/**
 * bd_fs_vfat_check:
 * @device: the device containing the file system to check
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'fsck.vfat' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether an vfat file system on the @device is clean or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_CHECK
 */
gboolean bd_fs_vfat_check (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"fsck.vfat", "-n", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_FSCKVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    if (!ret && (status == 1)) {
        /* no error should be reported for exit code 1 -- Recoverable errors have been detected */
        g_clear_error (error);
    }
    return ret;
}

/**
 * bd_fs_vfat_repair:
 * @device: the device containing the file system to repair
 * @extra: (nullable) (array zero-terminated=1): extra options for the repair (right now
 *                                                 passed to the 'fsck.vfat' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether an vfat file system on the @device was successfully repaired
 *          (if needed) or not (error is set in that case)
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_REPAIR
 */
gboolean bd_fs_vfat_repair (const gchar *device, const BDExtraArg **extra, GError **error) {
    const gchar *args[4] = {"fsck.vfat", "-a", device, NULL};
    gint status = 0;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_FSCKVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
    if (!ret) {
        if (status == 1) {
            /* exit code 1 can also mean "errors have been detected and corrected" so we need
               to run fsck again to make sure the filesystem is now clean */
            g_clear_error (error);
            ret = bd_utils_exec_and_report_status_error (args, extra, &status, error);
        } else
            /* FALSE and exit code other than 1 always means error  */
            ret = FALSE;
    }

    return ret;
}

/**
 * _vfat_locale_codepage:
 *
 * Determine the DOS/OEM codepage that matches the system locale, mirroring
 * how Windows derives the OEM codepage from the system locale. FAT volume
 * labels are stored as 8-bit OEM codepage bytes, so the codepage used to
 * write a label must match the locale of the system that created it.
 *
 * The locale is read from the environment (LC_ALL > LC_CTYPE > LANG) rather
 * than calling setlocale(), to avoid side effects on the host process
 * (libblockdev runs inside udisksd).
 *
 * Returns: the codepage number, or 850 (the dosfstools default) if the locale
 *          cannot be determined or has no known mapping.
 */
static guint
_vfat_locale_codepage (void) {
    /* Locale-to-codepage table based on the DOS CODEPAGES table in
     * fatlabel(8), which mirrors how Windows maps the "Language for
     * non-Unicode programs" setting to an OEM codepage. Entries are matched
     * by longest-prefix so region variants (e.g. "zh_TW" over "zh",
     * "en_GB" over "en") win. When nothing matches, the fatlabel default
     * CP850 is returned. */
    typedef struct {
        const gchar *prefix;
        guint codepage;
    } VfatLocaleCodepage;
    static const VfatLocaleCodepage codepage_table[] = {
        /* --- CJK (multi-byte) --- */
        { "zh_CN", 936 }, { "zh_SG", 936 },   /* Chinese (Simplified) / GBK */
        { "zh_TW", 950 }, { "zh_HK", 950 },   /* Chinese (Traditional) / Big5 */
        { "zh", 936 },                         /* Chinese default — Simplified */
        { "ja",    932 },                       /* Japanese / Shift-JIS */
        { "ko",    949 },                       /* Korean / UHC */
        /* --- Cyrillic (CP866) --- */
        { "ba", 866 }, { "be", 866 }, { "bg", 866 },
        { "ky", 866 }, { "mk", 866 }, { "mn", 866 }, { "ru", 866 },
        { "tg", 866 }, { "tt", 866 }, { "uk", 866 },
        { "az@cyrillic", 866 }, { "uz@cyrillic", 866 },
        /* --- Serbian / Bosnian Cyrillic (CP855) --- */
        { "sr", 855 }, { "bs@cyrillic", 855 },
        /* --- Central/Eastern Europe (CP852) --- */
        { "bs", 852 }, { "cs", 852 }, { "hr", 852 }, { "hu", 852 },
        { "pl", 852 }, { "ro", 852 }, { "sk", 852 }, { "sl", 852 },
        { "sq", 852 }, { "sr@latin", 852 }, { "tk", 852 },
        /* --- Turkish / Azeri / Uzbek (Latin) (CP857) --- */
        { "tr", 857 }, { "az", 857 }, { "uz", 857 },
        /* --- Greek (CP737) --- */
        { "el", 737 },
        /* --- Baltic (CP775) --- */
        { "et", 775 }, { "lt", 775 }, { "lv", 775 },
        /* --- Hebrew (CP862) --- */
        { "he", 862 }, { "iw", 862 },
        /* --- Arabic / Persian / Urdu / Uyghur (CP720) --- */
        { "ar", 720 }, { "fa", 720 }, { "ps", 720 }, { "ur", 720 }, { "ug", 720 },
        /* --- Thai (CP874) --- */
        { "th", 874 },
        /* --- Vietnamese (CP1258) --- */
        { "vi", 1258 },
        /* --- English variants (region-specific) --- */
        { "en_GB", 850 }, { "en_IE", 850 }, { "en_AU", 850 },
        { "en_CA", 850 }, { "en_NZ", 850 }, { "en_JM", 850 },
        { "en_BZ", 850 }, { "en_TT", 850 }, { "en_BB", 850 },
        { "en_AG", 850 }, { "en_BS", 850 }, { "en_BW", 850 }, { "en_GH", 850 },
        { "en_GM", 850 }, { "en_GY", 850 }, { "en_HK", 850 },
        { "en_IN", 437 }, { "en_MY", 437 }, { "en_PH", 437 }, { "en_SG", 437 },
        { "en_US", 437 }, { "en_ZA", 437 }, { "en_ZW", 437 },
        /* --- Western Europe (CP850) — the dosfstools fatlabel default --- */
        { "af", 850 }, { "ca", 850 }, { "da", 850 }, { "de", 850 },
        { "es", 850 }, { "eu", 850 }, { "fi", 850 }, { "fo", 850 },
        { "fr", 850 }, { "fy", 850 }, { "gl", 850 }, { "is", 850 },
        { "id", 850 }, { "it", 850 }, { "kl", 850 }, { "ms", 850 },
        { "nb", 850 }, { "nl", 850 }, { "nn", 850 }, { "no", 850 },
        { "pt", 850 }, { "rm", 850 }, { "se", 850 }, { "sv", 850 },
        { "cy", 850 }, { "wo", 850 }, { "xh", 850 }, { "zu", 850 },
        /* --- US-English fallback when no region matches (CP437) --- */
        { "en", 437 },
    };
    const gchar *locale;
    g_autofree gchar *lang = NULL;
    g_autofree gchar *modifier = NULL;
    gchar *p;
    gsize i;
    gsize best = G_MAXSIZE;
    gsize best_len = 0;

    locale = g_getenv ("LC_ALL");
    if (locale == NULL || *locale == '\0')
        locale = g_getenv ("LC_CTYPE");
    if (locale == NULL || *locale == '\0')
        locale = g_getenv ("LANG");

    if (locale == NULL || *locale == '\0' ||
        g_strcmp0 (locale, "C") == 0 || g_strcmp0 (locale, "POSIX") == 0 ||
        g_str_has_prefix (locale, "C."))
        return 850;

    /* Extract language[_territory][@modifier], stripping the codeset (".").
     * The @modifier is kept because it distinguishes script variants that
     * map to different codepages (e.g. Serbian Cyrillic "sr" → 855 vs
     * Serbian Latin "sr@latin" → 852). Since the codeset (".") precedes
     * the modifier ("@") in locale names, save the modifier first. */
    lang = g_strdup (locale);
    p = strchr (lang, '@');
    if (p != NULL) {
        modifier = g_strdup (p);
        *p = '\0';
    }
    p = strchr (lang, '.');
    if (p) *p = '\0';
    if (modifier != NULL) {
        gchar *tmp = lang;
        lang = g_strconcat (tmp, modifier, NULL);
        g_free (tmp);
    }

    /* Longest-prefix match so region variants win: "zh_TW" over "zh",
     * "en_US" over "en". */
    for (i = 0; i < G_N_ELEMENTS (codepage_table); i++) {
        const gchar *pfx = codepage_table[i].prefix;
        if (g_str_has_prefix (lang, pfx) && strlen (pfx) > best_len) {
            best = i;
            best_len = strlen (pfx);
        }
    }

    /* If a @modifier is present (e.g. "sr_RS@latin"), also try matching
     * "language@modifier" (territory stripped) against @-bearing entries,
     * which take precedence over the plain language match. */
    p = strchr (lang, '@');
    if (p != NULL) {
        g_autofree gchar *lang_mod = NULL;
        gchar *underscore = strchr (lang, '_');
        if (underscore != NULL && underscore < p)
            lang_mod = g_strdup_printf ("%.*s%s", (gint)(underscore - lang), lang, p);
        else
            lang_mod = g_strdup (lang);

        gsize mod_best = G_MAXSIZE;
        gsize mod_best_len = 0;
        for (i = 0; i < G_N_ELEMENTS (codepage_table); i++) {
            const gchar *pfx = codepage_table[i].prefix;
            if (strchr (pfx, '@') != NULL &&
                g_str_has_prefix (lang_mod, pfx) && strlen (pfx) > mod_best_len) {
                mod_best = i;
                mod_best_len = strlen (pfx);
            }
        }
        if (mod_best != G_MAXSIZE)
            return codepage_table[mod_best].codepage;
    }

    if (best != G_MAXSIZE)
        return codepage_table[best].codepage;

    return 850;
}

/**
 * _vfat_label_from_codepage:
 * @label: (nullable): raw OEM codepage bytes from the filesystem
 *
 * Convert a label read from a VFAT filesystem (raw OEM codepage bytes, as
 * returned by blkid) to UTF-8, using the locale-derived codepage. Pure
 * ASCII labels need no conversion.
 *
 * Note: FAT volumes do not carry encoding metadata. The codepage is
 * inferred from the daemon's locale, so this only guarantees correct
 * round-tripping when the volume was written on a system with the same
 * locale. A volume written under locale A and read under locale B may
 * produce garbled (but valid UTF-8) text.
 *
 * Returns: (transfer full): the UTF-8 label, or the original bytes on
 *          conversion failure. Never %NULL.
 */
static gchar *
_vfat_label_from_codepage (const gchar *label) {
    if (label == NULL || *label == '\0')
        return g_strdup (label ? label : "");

    if (g_str_is_ascii (label))
        return g_strdup (label);

    guint cp = _vfat_locale_codepage ();
    gchar cp_name[16];
    g_snprintf (cp_name, sizeof (cp_name), "CP%u", cp);

    GError *conv_error = NULL;
    gchar *utf8 = g_convert (label, -1, "UTF-8", cp_name, NULL, NULL, &conv_error);
    if (utf8 != NULL)
        return utf8;

    /* Conversion failed; return a valid UTF-8 string instead of raw bytes
     * that may not be valid UTF-8. */
    g_error_free (conv_error);
    return g_utf8_make_valid (label, -1);
}

/**
 * bd_fs_vfat_set_label:
 * @device: the device containing the file system to set label for
 * @label: label to set
 * @error: (out) (optional): place to store error (if any)
 *
 * Sets the label of a VFAT filesystem. For labels containing non-ASCII
 * characters, the OEM codepage matching the system locale is passed to
 * fatlabel via the -c option, so that labels in CJK, Cyrillic and other
 * non-Western-European scripts are encoded correctly.
 *
 * Note: the codepage is derived from the daemon's locale environment
 * (LC_ALL/LC_CTYPE/LANG), not the calling user's session. fatlabel uses
 * setlocale(LC_CTYPE, "") internally, so the daemon must be running under
 * a UTF-8 locale whose glibc locale data has been generated (locale-gen);
 * otherwise the conversion may fail with "Cannot convert input sequence".
 *
 * Returns: whether the label was successfully set or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_vfat_set_label (const gchar *device, const gchar *label, GError **error) {
    /* "fatlabel" "-c" "<cp>" <device> <label> -- at most 5 non-NULL entries */
    const gchar *args[6] = {"fatlabel", NULL, NULL, NULL, NULL, NULL};
    UtilDep dep = {"fatlabel", "4.2", "--version", "fatlabel\\s+([\\d\\.]+).+"};
    gchar *label_up = NULL;
    gchar *codepage_str = NULL;
    gboolean new_vfat = FALSE;
    gboolean ret;
    gint idx = 1;

    if (!check_deps (&avail_deps, DEPS_FATLABEL_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    /* For labels with non-ASCII characters, pass the OEM codepage matching
     * the system locale to fatlabel so it can encode them correctly. */
    if (label && *label && !g_str_is_ascii (label)) {
        guint cp = _vfat_locale_codepage ();
        codepage_str = g_strdup_printf ("%u", cp);
        args[idx++] = "-c";
        args[idx++] = codepage_str;
    }

    args[idx++] = device;

    if (!label || g_strcmp0 (label, "") == 0) {
        /* fatlabel >= 4.2 refuses to set empty label */
        new_vfat = bd_utils_check_util_version (dep.name, dep.version,
                                                dep.ver_arg, dep.ver_regexp,
                                                NULL);
        if (new_vfat)
            args[idx++] = "--reset";
    } else {
        /* forcefully convert the label uppercase; g_ascii_strup only
         * uppercases a-z, leaving multi-byte UTF-8 sequences unchanged */
        label_up = g_ascii_strup (label, -1);
        args[idx++] = label_up;
    }

    ret = bd_utils_exec_and_report_error (args, NULL, error);
    g_free (label_up);
    g_free (codepage_str);

    return ret;
}

/**
 * bd_fs_vfat_check_label:
 * @label: label to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @label is a valid label for the vfat file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_vfat_check_label (const gchar *label, GError **error) {
    const gchar *forbidden = "\"*/:<>?\\|";
    guint n;

    if (label == NULL) {
        g_set_error_literal (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                             "Label cannot be NULL.");
        return FALSE;
    }

    if (strlen (label) > 11) {
        g_set_error_literal (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                             "Label for VFAT filesystem must be at most 11 characters long.");
        return FALSE;
    }

    /* VFAT does not allow some characters; as dosfslabel does not enforce this,
     * check in advance; also, VFAT only knows upper-case characters, dosfslabel
     * enforces this */
    for (n = 0; forbidden[n] != 0; n++)
        if (strchr (label, forbidden[n]) != NULL) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                         "Invalid label: character '%c' not supported in VFAT labels.",
                         forbidden[n]);
            return FALSE;
        }

    return TRUE;
}

/**
 * bd_fs_vfat_set_uuid:
 * @device: the device containing the file system to set uuid for
 * @uuid: (nullable): volume ID to set or %NULL to generate a new one
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the volume ID of vfat file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_vfat_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    const gchar *args[5] = {"fatlabel", "-i", device, NULL, NULL};
    g_autofree gchar *new_uuid = NULL;

    if (!check_deps (&avail_deps, DEPS_FATLABELUUID_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (!uuid || g_strcmp0 (uuid, "") == 0)
        args[3] = "--reset";
    else {
        new_uuid = _fix_uuid (uuid);
        args[3] = new_uuid;
    }

    return bd_utils_exec_and_report_error (args, NULL, error);
}

/**
 * bd_fs_vfat_check_uuid:
 * @uuid: UUID to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @uuid is a valid UUID for the vfat file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_vfat_check_uuid (const gchar *uuid, GError **error) {
    guint64 vol_id;
    gchar *new_uuid = NULL;
    gchar *endptr = NULL;

    if (!uuid)
        return TRUE;

    new_uuid = _fix_uuid (uuid);

    vol_id = g_ascii_strtoull (new_uuid, &endptr, 16);
    if ((vol_id == 0 && endptr == new_uuid) || (endptr && *endptr)) {
        g_set_error_literal (error, BD_FS_ERROR, BD_FS_ERROR_UUID_INVALID,
                             "UUID for VFAT filesystem must be a hexadecimal number.");
        g_free (new_uuid);
        return FALSE;
    }

    if (vol_id > G_MAXUINT32) {
        g_set_error_literal (error, BD_FS_ERROR, BD_FS_ERROR_UUID_INVALID,
                             "UUID for VFAT filesystem must fit into 32 bits.");
        g_free (new_uuid);
        return FALSE;
    }

    g_free (new_uuid);
    return TRUE;
}

/**
 * bd_fs_vfat_get_info:
 * @device: the device containing the file system to get info for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_QUERY
 */
BDFSVfatInfo* bd_fs_vfat_get_info (const gchar *device, GError **error) {
    const gchar *args[4] = {"fsck.vfat", "-nv", device, NULL};
    gboolean success = FALSE;
    BDFSVfatInfo *ret = NULL;
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gboolean have_cluster_size = FALSE;
    gboolean have_cluster_count = FALSE;
    guint64 full_cluster_count = 0;
    guint64 cluster_count = 0;
    gchar **key_val = NULL;
    gint scanned = 0;

    if (!check_deps (&avail_deps, DEPS_FSCKVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return NULL;

    ret = g_new0 (BDFSVfatInfo, 1);

    success = get_uuid_label (device, &(ret->uuid), &(ret->label), error);
    if (!success) {
        /* error is already populated */
        bd_fs_vfat_info_free (ret);
        return NULL;
    }

    /* blkid returns raw OEM codepage bytes; convert to UTF-8 so the label
     * round-trips with bd_fs_vfat_set_label(). */
    {
        gchar *utf8_label = _vfat_label_from_codepage (ret->label);
        g_free (ret->label);
        ret->label = utf8_label;
    }

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        bd_fs_vfat_info_free (ret);
        return NULL;
    }

    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    for (line_p=lines; *line_p && (!have_cluster_size || !have_cluster_count); line_p++) {
        if (!have_cluster_size && g_str_has_suffix (*line_p, "bytes per cluster")) {
            ret->cluster_size = g_ascii_strtoull (*line_p, NULL, 0);
            have_cluster_size = TRUE;
        } else if (!have_cluster_count && g_str_has_prefix (*line_p, device)) {
            key_val = g_strsplit (*line_p, ",", 2);
            scanned = sscanf (key_val[1], " %" G_GUINT64_FORMAT "/" "%" G_GUINT64_FORMAT " clusters",
                              &full_cluster_count, &cluster_count);
            if (scanned != 2) {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                             "Failed to get number of FAT clusters for '%s'", device);
                bd_fs_vfat_info_free (ret);
                g_strfreev (key_val);
                g_strfreev (lines);
                return NULL;
            }
            ret->cluster_count = cluster_count;
            ret->free_cluster_count = cluster_count - full_cluster_count;
            have_cluster_count = TRUE;
            g_strfreev (key_val);
        }
    }
    g_strfreev (lines);

    return ret;
}

/**
 * bd_fs_vfat_resize:
 * @device: the device the file system of which to resize
 * @new_size: new requested size for the file system (if 0, the file system is
 *            adapted to the underlying block device)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the file system on @device was successfully resized or not
 *
 * Tech category: %BD_FS_TECH_VFAT-%BD_FS_TECH_MODE_RESIZE
 */
gboolean bd_fs_vfat_resize (const gchar *device, guint64 new_size, GError **error) {
    g_autofree gchar *size_str = NULL;
    const gchar *args[4] = {"vfat-resize", device, NULL, NULL};

    if (!check_deps (&avail_deps, DEPS_RESIZEVFAT_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (new_size != 0) {
        size_str = g_strdup_printf ("%"G_GUINT64_FORMAT, new_size);
        args[2] = size_str;
    }

    return bd_utils_exec_and_report_error (args, NULL, error);
}
