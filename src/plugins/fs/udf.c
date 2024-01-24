/*
 * Copyright (C) 2020  Red Hat, Inc.
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
 * Author: Vojtech Trefny <vtrefny@redhat.com>
 */

#include <blockdev/utils.h>
#include <check_deps.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "udf.h"
#include "fs.h"
#include "common.h"

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_MKUDFFS 0
#define DEPS_MKUDFFS_MASK (1 << DEPS_MKUDFFS)
#define DEPS_UDFLABEL 1
#define DEPS_UDFLABEL_MASK (1 <<  DEPS_UDFLABEL)
#define DEPS_UDFINFO 2
#define DEPS_UDFINFO_MASK (1 <<  DEPS_UDFINFO)

#define DEPS_LAST 3

static const UtilDep deps[DEPS_LAST] = {
    {"mkudffs", NULL, NULL, NULL},
    {"udflabel", NULL, NULL, NULL},
    {"udfinfo", NULL, NULL, NULL},
};

static guint32 fs_mode_util[BD_FS_MODE_LAST+1] = {
    DEPS_MKUDFFS_MASK,      /* mkfs */
    0,                      /* wipe */
    0,                      /* check */
    0,                      /* repair */
    DEPS_UDFLABEL_MASK,     /* set-label */
    DEPS_UDFINFO_MASK,      /* query */
    0,                      /* resize */
    DEPS_UDFLABEL_MASK,     /* set-uuid */
};


/**
 * bd_fs_udf_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDFSTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
G_GNUC_INTERNAL gboolean
bd_fs_udf_is_tech_avail (BDFSTech tech G_GNUC_UNUSED, guint64 mode, GError **error) {
    guint32 required = 0;
    guint i = 0;

    if (mode & BD_FS_TECH_MODE_CHECK || mode & BD_FS_TECH_MODE_REPAIR) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_TECH_UNAVAIL,
                     "UDF doesn't support checking and reparing.");
        return FALSE;
    } else if (mode & BD_FS_TECH_MODE_RESIZE) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_TECH_UNAVAIL,
                     "UDF currently doesn't support resizing.");
        return FALSE;
    }

    for (i = 0; i <= BD_FS_MODE_LAST; i++)
        if (mode & (1 << i))
            required |= fs_mode_util[i];

    return check_deps (&avail_deps, required, deps, DEPS_LAST, &deps_check_lock, error);
}

/**
 * bd_fs_udf_info_copy: (skip)
 *
 * Creates a new copy of @data.
 */
BDFSUdfInfo* bd_fs_udf_info_copy (BDFSUdfInfo *data) {
    if (data == NULL)
        return NULL;

    BDFSUdfInfo *ret = g_new0 (BDFSUdfInfo, 1);

    ret->label = g_strdup (data->label);
    ret->uuid = g_strdup (data->uuid);
    ret->revision = g_strdup (data->revision);
    ret->lvid = g_strdup (data->lvid);
    ret->vid = g_strdup (data->vid);
    ret->block_size = data->block_size;
    ret->block_count = data->block_count;
    ret->free_blocks = data->free_blocks;

    return ret;
}

/**
 * bd_fs_udf_info_free: (skip)
 *
 * Frees @data.
 */
void bd_fs_udf_info_free (BDFSUdfInfo *data) {
    if (data == NULL)
        return;

    g_free (data->label);
    g_free (data->uuid);
    g_free (data->revision);
    g_free (data->lvid);
    g_free (data->vid);
    g_free (data);
}

/* get a valid UDF Volume Identifier from label */
static gchar* get_vid (const gchar *label) {
    gchar *vid = NULL;
    const gchar *next_p = NULL;
    gunichar unichar;
    guint pos = 0;

    if (!g_utf8_validate (label, -1, NULL))
        return NULL;

    if (g_utf8_strlen (label, -1) <= 15)
        vid = g_strdup (label);
    else {
        /* vid can be at most 30 characters (or 15 > 0xFF) so we'll truncate the label if needed */
        next_p = label;
        while (next_p && *next_p) {
            unichar = g_utf8_get_char (next_p);
            if (unichar > 0xFF) {
                if (pos < 15) {
                    /* vid can have at most 15 characters > 0xFF */
                    vid = g_utf8_substring (label, 0, 15);
                    break;
                } else if (pos < 30) {
                    /* cut before the "problematic" character */
                    vid = g_utf8_substring (label, 0, pos);
                    break;
                }
            }

            next_p = g_utf8_next_char (next_p);
            pos++;
        }

        if (!vid) {
            /* we can't have more that 30 characters in vid so cut at 30 */
            vid = g_utf8_substring (label, 0, 30);
        }
    }

    return vid;
}

G_GNUC_INTERNAL BDExtraArg **
bd_fs_udf_mkfs_options (BDFSMkfsOptions *options, const BDExtraArg **extra) {
    GPtrArray *options_array = g_ptr_array_new ();
    const BDExtraArg **extra_p = NULL;
    g_autofree gchar *vid = NULL;

    if (options->label && g_strcmp0 (options->label, "") != 0) {
        vid = get_vid (options->label);

        g_ptr_array_add (options_array, bd_extra_arg_new ("--lvid", options->label));
        g_ptr_array_add (options_array, bd_extra_arg_new ("--vid", vid));
    }

    if (options->uuid && g_strcmp0 (options->uuid, "") != 0)
        g_ptr_array_add (options_array, bd_extra_arg_new ("-u", options->uuid));

    if (extra) {
        for (extra_p = extra; *extra_p; extra_p++)
            g_ptr_array_add (options_array, bd_extra_arg_copy ((BDExtraArg *) *extra_p));
    }

    g_ptr_array_add (options_array, NULL);

    return (BDExtraArg **) g_ptr_array_free (options_array, FALSE);
}

static gint get_blocksize (const gchar *device, GError **error) {
    gint fd = -1;
    gint blksize = 0;

    fd = open (device, O_RDONLY);
    if (fd < 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                    "Failed to open the device '%s' to get its block size: %s",
                    device, strerror_l (errno, _C_LOCALE));
        return -1;
    }

    if (ioctl (fd, BLKSSZGET, &blksize) < 0) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_FAIL,
                     "Failed to get block size of the device '%s': %s",
                     device, strerror_l (errno, _C_LOCALE));
        close (fd);
        return -1;
    }

    close (fd);

    return blksize;
}

/**
 * bd_fs_udf_mkfs:
 * @device: the device to create a new UDF fs on
 * @media_type: (nullable): specify the media type or %NULL for default ('hd')
 * @revision: (nullable): UDF revision to use or %NULL for default ('2.01')
 * @block_size: block size in bytes or 0 for auto detection (device logical block size)
 * @extra: (nullable) (array zero-terminated=1): extra options for the creation (right now
 *                                                 passed to the 'mkudffs' utility)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether a new UDF fs was successfully created on @device or not
 *
 * Tech category: %BD_FS_TECH_UDF-%BD_FS_TECH_MODE_MKFS
 */
gboolean bd_fs_udf_mkfs (const gchar *device, const gchar *media_type, gchar *revision, guint64 block_size, const BDExtraArg **extra, GError **error) {
    const gchar *args[7] = {"mkudffs", "--utf8", NULL, NULL, NULL, device, NULL};
    gint detected_bs = -1;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_MKUDFFS_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (block_size != 0)
        args[2] = g_strdup_printf ("--blocksize=%"G_GUINT64_FORMAT"", block_size);
    else {
        detected_bs = get_blocksize (device, error);
        if (detected_bs < 0)
            return FALSE;

        args[2] = g_strdup_printf ("--blocksize=%d", detected_bs);
    }

    if (media_type)
        args[3] = g_strdup_printf ("--media-type=%s", media_type);
    else
        args[3] = g_strdup ("--media-type=hd");

    if (revision)
        args[4] = g_strdup_printf ("--udfrev=%s", revision);
    else
        args[4] = g_strdup ("--udfrev=0x201");

    ret = bd_utils_exec_and_report_error (args, extra, error);

    g_free ((gchar *) args[2]);
    g_free ((gchar *) args[3]);
    g_free ((gchar *) args[4]);

    return ret;
}

/**
 * bd_fs_udf_set_label:
 * @device: the device containing the file system to set label for
 * @label: label to set
 * @error: (out) (optional): place to store error (if any)
 *
 * Note: This sets both Volume Identifier and Logical Volume Identifier. Volume Identifier
 *       is truncated to 30 or 15 characters to accommodate to the different length limits
 *       of these labels.
 *
 * Returns: whether the label of UDF file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_UDF-%BD_FS_TECH_MODE_SET_LABEL
 */
gboolean bd_fs_udf_set_label (const gchar *device, const gchar *label, GError **error) {
    const gchar *args[6] = {"udflabel", "--utf8", NULL, NULL, device, NULL};
    g_autofree gchar *vid = NULL;
    gboolean ret = FALSE;

    if (!check_deps (&avail_deps, DEPS_UDFLABEL_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (!bd_fs_udf_check_label (label, error))
        return FALSE;

    vid = get_vid (label);

    args[2] = g_strdup_printf ("--lvid=%s", label);
    args[3] = g_strdup_printf ("--vid=%s", vid);

    ret = bd_utils_exec_and_report_error (args, NULL, error);

    g_free ((gchar *) args[2]);
    g_free ((gchar *) args[3]);

    return ret;
}

/**
 * bd_fs_udf_check_label:
 * @label: label to check
 * @error: (out) (optional): place to store error
 *
 * Note: This checks only whether @label adheres the length limits for Logical Volume Identifier,
 *       not the stricter limits for Volume Identifier.
 *
 * Returns: whether @label is a valid label for the UDF file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_udf_check_label (const gchar *label, GError **error) {
    const gchar *next_p = NULL;
    gunichar unichar;
    gint len = 0;

    if (g_str_is_ascii (label)) {
        if (strlen (label) > 126) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                         "Label for UDF filesystem can be at most 126 characters long.");
            return FALSE;
        }

        return TRUE;
    }

    if (g_utf8_validate (label, -1, NULL)) {
        len = g_utf8_strlen (label, -1);
        if (len <= 63)
            /* utf-8 and <= 63 will be always valid */
            return TRUE;

        if (len > 126) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                         "Label for UDF filesystem can be at most 126 characters long.");
            return FALSE;
        }

        next_p = label;
        while (next_p && *next_p) {
            unichar = g_utf8_get_char (next_p);
            if (unichar > 0xFF) {
                g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                             "Label for UDF filesystem containing unicode characters above U+FF can "\
                             "be at most 63 characters long.");
                return FALSE;
            }

            next_p = g_utf8_next_char (next_p);
        }
    } else {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_LABEL_INVALID,
                     "Label for UDF filesystem must be a valid UTF-8 string.");
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_fs_udf_set_uuid:
 * @device: the device containing the file system to set the UUID (serial number) for
 * @uuid: (nullable): UUID to set or %NULL to generate a new one
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the UUID of the UDF file system on the @device was
 *          successfully set or not
 *
 * Tech category: %BD_FS_TECH_UDF-%BD_FS_TECH_MODE_SET_UUID
 */
gboolean bd_fs_udf_set_uuid (const gchar *device, const gchar *uuid, GError **error) {
    gboolean ret = FALSE;
    const gchar *args[4] = {"udflabel", NULL, device, NULL};

    if (!check_deps (&avail_deps, DEPS_UDFLABEL_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (!uuid)
        args[1] = g_strdup ("--uuid=random");
    else
        args[1] = g_strdup_printf ("--uuid=%s", uuid);

    ret = bd_utils_exec_and_report_error (args, NULL, error);

    g_free ((gchar *) args[1]);
    return ret;
}

/**
 * bd_fs_udf_check_uuid:
 * @uuid: UUID to check
 * @error: (out) (optional): place to store error
 *
 * Returns: whether @uuid is a valid UUID for the UDF file system or not
 *          (reason is provided in @error)
 *
 * Tech category: always available
 */
gboolean bd_fs_udf_check_uuid (const gchar *uuid, GError **error) {
    size_t len = 0;

    len = strlen (uuid);
    if (len != 16) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UUID_INVALID,
                     "UUID for UDF filesystem must be 16 characters long.");
        return FALSE;
    }

    for (size_t i = 0; i < len; i++) {
        if (!g_ascii_isxdigit (uuid[i]) || (!g_ascii_isdigit (uuid[i]) && !g_ascii_islower (uuid[i]))) {
            g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_UUID_INVALID,
                         "UUID for UDF filesystem must be a lowercase hexadecimal number.");
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * parse_udf_vars:
 * @str: string to parse
 * @num_items: (out): number of parsed items
 *
 * Returns: (transfer full): a GHashTable containing key-value items parsed from the @string
 */
static GHashTable* parse_udf_vars (const gchar *str, guint *num_items) {
    GHashTable *table = NULL;
    gchar **items = NULL;
    gchar **item_p = NULL;
    gchar **key_val = NULL;

    table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    *num_items = 0;

    items = g_strsplit (str, "\n", 0);
    for (item_p=items; *item_p; item_p++) {
        if (g_str_has_prefix (*item_p, "start="))
            continue;
        key_val = g_strsplit (*item_p, "=", 2);
        if (g_strv_length (key_val) == 2) {
            /* we only want to process valid lines (with the '=' character) */
            g_hash_table_insert (table, key_val[0], key_val[1]);
            g_free (key_val);
            (*num_items)++;
        } else
            /* invalid line, just free key_val */
            g_strfreev (key_val);
    }

    g_strfreev (items);
    return table;
}

static BDFSUdfInfo* get_udf_data_from_table (GHashTable *table) {
    BDFSUdfInfo *data = g_new0 (BDFSUdfInfo, 1);
    gchar *value = NULL;

    data->revision = g_strdup ((gchar*) g_hash_table_lookup (table, "udfrev"));
    data->vid = g_strdup ((gchar*) g_hash_table_lookup (table, "vid"));
    data->lvid = g_strdup ((gchar*) g_hash_table_lookup (table, "lvid"));

    value = (gchar*) g_hash_table_lookup (table, "blocksize");
    if (value)
        data->block_size = g_ascii_strtoull (value, NULL, 0);
    else
        data->block_size = 0;

    value = (gchar*) g_hash_table_lookup (table, "blocks");
    if (value)
        data->block_count = g_ascii_strtoull (value, NULL, 0);
    else
        data->block_count = 0;

    value = (gchar*) g_hash_table_lookup (table, "freeblocks");
    if (value)
        data->free_blocks = g_ascii_strtoull (value, NULL, 0);
    else
        data->free_blocks = 0;

    g_hash_table_destroy (table);

    return data;
}

/**
 * bd_fs_udf_get_info:
 * @device: the device containing the file system to get info for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the file system on @device or
 *                           %NULL in case of error
 *
 * Tech category: %BD_FS_TECH_UDF-%BD_FS_TECH_MODE_QUERY
 */
BDFSUdfInfo* bd_fs_udf_get_info (const gchar *device, GError **error) {
    const gchar *args[4] = {"udfinfo", "--utf8", device, NULL};
    gboolean success = FALSE;
    gchar *output = NULL;
    BDFSUdfInfo *ret = NULL;
    GHashTable *table = NULL;
    guint num_items = 0;

    if (!check_deps (&avail_deps, DEPS_UDFINFO_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return NULL;

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        /* error is already populated */
        return NULL;
    }

    table = parse_udf_vars (output, &num_items);
    g_free (output);
    if (!table || (num_items == 0)) {
        /* something bad happened or some expected items were missing  */
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse UDF file system information");
        if (table)
            g_hash_table_destroy (table);
        return NULL;
    }

    ret = get_udf_data_from_table (table);
    if (!ret) {
        g_set_error (error, BD_FS_ERROR, BD_FS_ERROR_PARSE, "Failed to parse UDF file system information");
        return NULL;
    }

    success = get_uuid_label (device, &(ret->uuid), &(ret->label), error);
    if (!success) {
        /* error is already populated */
        bd_fs_udf_info_free (ret);
        return NULL;
    }

    return ret;
}
