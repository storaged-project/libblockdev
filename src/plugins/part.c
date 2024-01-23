/*
 * Copyright (C) 2016  Red Hat, Inc.
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

#include <ctype.h>
#include <sys/file.h>
#include <fcntl.h>
#include <blockdev/utils.h>
#include <libfdisk.h>
#include <locale.h>

#include "part.h"

/**
 * SECTION: part
 * @short_description: plugin for operations with partition tables
 * @title: Part
 * @include: part.h
 *
 * A plugin for operations with partition tables. Currently supported table
 * (disk label) types are MBR and GPT. See the functions below to get an
 * overview of which operations are supported. If there's anything missing,
 * please don't hesitate to report it as this plugin (just like all the others)
 * is subject to future development and enhancements.
 *
 * This particular implementation of the part plugin uses libfdisk for
 * manipulations of both the MBR and GPT disk label types.
 */

/**
 * bd_part_error_quark: (skip)
 */
GQuark bd_part_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-part-error-quark");
}

BDPartSpec* bd_part_spec_copy (BDPartSpec *data) {
    if (data == NULL)
        return NULL;

    BDPartSpec *ret = g_new0 (BDPartSpec, 1);

    ret->path = g_strdup (data->path);
    ret->name = g_strdup (data->name);
    ret->id = g_strdup (data->id);
    ret->uuid = g_strdup (data->uuid);
    ret->type_guid = g_strdup (data->type_guid);
    ret->type = data->type;
    ret->start = data->start;
    ret->size = data->size;
    ret->bootable = data->bootable;
    ret->attrs = data->attrs;

    return ret;
}

void bd_part_spec_free (BDPartSpec *data) {
    if (data == NULL)
        return;

    g_free (data->path);
    g_free (data->name);
    g_free (data->uuid);
    g_free (data->id);
    g_free (data->type_guid);
    g_free (data);
}

BDPartDiskSpec* bd_part_disk_spec_copy (BDPartDiskSpec *data) {
    if (data == NULL)
        return NULL;

    BDPartDiskSpec *ret = g_new0 (BDPartDiskSpec, 1);

    ret->path = g_strdup (data->path);
    ret->table_type = data->table_type;
    ret->size = data->size;
    ret->sector_size = data->sector_size;

    return ret;
}

void bd_part_disk_spec_free (BDPartDiskSpec *data) {
    if (data == NULL)
        return;

    g_free (data->path);
    g_free (data);
}

/* "C" locale to get the locale-agnostic error messages */
static locale_t c_locale = (locale_t) 0;

static int fdisk_version = 0;

/* base 2 logarithm of x */
static gint log2i (guint x) {
    gint ret = 0;

    if (x == 0)
        return -1;

    while (x >>= 1)
        ret++;

    return ret;
}


/**
 * get_part_num: (skip)
 *
 * Extract partition number from it's name (e.g. sda1).
 *
 * Returns: partition number or -1 in case of an error
 */
static gint get_part_num (const gchar *part, GError **error) {
    const gchar *part_num_str = NULL;
    gint part_num = -1;

    if (!part || *part == '\0') {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        return -1;
    }

    part_num_str = part + (strlen (part) - 1);
    while (isdigit (*part_num_str) || (*part_num_str == '-')) {
        part_num_str--;
    }
    part_num_str++;

    part_num = atoi (part_num_str);
    if (part_num == 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'. Cannot extract partition number", part);
        return -1;
    } else if (part_num < 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'.", part);
        return -1;
    }

    return part_num;
}

static int fdisk_ask_callback (struct fdisk_context *cxt G_GNUC_UNUSED, struct fdisk_ask *ask, void *data G_GNUC_UNUSED) {
    gint type = 0;
    const gchar *fdisk_msg = NULL;
    gchar *message = NULL;

    type = fdisk_ask_get_type (ask);
    fdisk_msg = fdisk_ask_print_get_mesg (ask);

    switch (type) {
        case FDISK_ASKTYPE_INFO:
            message = g_strdup_printf ("[fdisk] %s", fdisk_msg);
            bd_utils_log (BD_UTILS_LOG_INFO, message);
            g_free (message);
            break;
        case FDISK_ASKTYPE_WARNX:
        case FDISK_ASKTYPE_WARN:
            message = g_strdup_printf ("[fdisk] %s", fdisk_msg);
            bd_utils_log (BD_UTILS_LOG_WARNING, message);
            g_free (message);
            break;
        default:
            break;
    }

    return 0;
}

static struct fdisk_context* get_device_context (const gchar *disk, gboolean read_only, GError **error) {
    struct fdisk_context *cxt = fdisk_new_context ();
    gint ret = 0;

    if (!cxt) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to create a new context");
        return NULL;
    }

    ret = fdisk_assign_device (cxt, disk, read_only);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to assign the new context to disk '%s': %s", disk, strerror_l (-ret, c_locale));
        fdisk_unref_context (cxt);
        return NULL;
    }

    fdisk_disable_dialogs (cxt, 1);
    fdisk_set_ask (cxt, fdisk_ask_callback, NULL);
    return cxt;
}

static void close_context (struct fdisk_context *cxt) {
    gint ret = 0;

    ret = fdisk_deassign_device (cxt, 0); /* context, nosync */

    if (ret != 0)
        /* XXX: should report error here? */
        bd_utils_log_format (BD_UTILS_LOG_WARNING,
                             "Failed to close and sync the device: %s",
                             strerror_l (-ret, c_locale));

    fdisk_unref_context (cxt);
}

static gboolean write_label (struct fdisk_context *cxt, struct fdisk_table *orig, const gchar *disk, gboolean force, GError **error) {
    gint ret = 0;
    gint dev_fd = 0;
    guint num_tries = 1;

    /* XXX: try to grab a lock for the device so that udev doesn't step in
       between the two operations we need to perform (see below) with its
       BLKRRPART ioctl() call which makes the device busy
       see https://systemd.io/BLOCK_DEVICE_LOCKING */
    dev_fd = open (disk, O_RDONLY|O_CLOEXEC);
    if (dev_fd >= 0) {
        ret = flock (dev_fd, LOCK_EX|LOCK_NB);
        while ((ret != 0) && (num_tries <= 5)) {
            g_usleep (100 * 1000); /* microseconds */
            ret = flock (dev_fd, LOCK_EX|LOCK_NB);
            num_tries++;
        }
    }

    /* Just continue even in case we don't get the lock, there's still a
       chance things will just work. If not, an error will be reported
       anyway with no harm. */

    ret = fdisk_write_disklabel (cxt);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to write the new disklabel to disk '%s': %s", disk, strerror_l (-ret, c_locale));
        if (dev_fd >= 0)
            close (dev_fd);
        return FALSE;
    }

    if (force) {
        /* force kernel to re-read entire partition table, not only changed partitions */
        ret = fdisk_reread_partition_table (cxt);
        if (ret != 0) {
            g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                         "Failed to inform kernel about changes on the '%s' device: %s", disk, strerror_l (-ret, c_locale));
            if (dev_fd >= 0)
                close (dev_fd);
            return FALSE;
        }
    } else if (orig) {
        /* We have original table layout -- reread changed partitions */
        ret = fdisk_reread_changes (cxt, orig);
        if (ret != 0) {
            g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                         "Failed to inform kernel about changes on the '%s' device: %s", disk, strerror_l (-ret, c_locale));
            if (dev_fd >= 0)
                close (dev_fd);
            return FALSE;
        }
    }

    if (dev_fd >= 0)
        close (dev_fd);

    return TRUE;
}

/**
 * bd_part_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_part_init (void) {
    c_locale = newlocale (LC_ALL_MASK, "C", c_locale);
    fdisk_init_debug (0);
    fdisk_version = fdisk_get_library_version (NULL);
    return TRUE;
}

/**
 * bd_part_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_part_close (void) {
    c_locale = (locale_t) 0;
}

/**
 * bd_part_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDPartTechMode) for @tech
 * @error: (out) (optional): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_part_is_tech_avail (BDPartTech tech, guint64 mode G_GNUC_UNUSED, GError **error) {
    switch (tech) {
    case BD_PART_TECH_MBR:
    case BD_PART_TECH_GPT:
        /* all MBR and GPT-mode combinations are supported by this implementation of the
         * plugin, nothing extra is needed */
        return TRUE;
    default:
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_TECH_UNAVAIL, "Unknown technology");
        return FALSE;
    }
}

static const gchar *table_type_str[BD_PART_TABLE_UNDEF] = {"dos", "gpt"};

/**
 * bd_part_create_table:
 * @disk: path of the disk block device to create partition table on
 * @type: type of the partition table to create
 * @ignore_existing: whether to ignore/overwrite the existing table or not
 *                   (reports an error if %FALSE and there's some table on @disk)
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the partition table was successfully created or not
 *
 * Tech category: %BD_PART_TECH_MODE_CREATE_TABLE + the tech according to @type
 */
gboolean bd_part_create_table (const gchar *disk, BDPartTableType type, gboolean ignore_existing, GError **error) {
    struct fdisk_context *cxt = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Starting creation of a new partition table on '%s'", disk);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    cxt = get_device_context (disk, FALSE, &l_error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (!ignore_existing && fdisk_has_label (cxt)) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_EXISTS,
                     "Device '%s' already contains a partition table", disk);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        close_context (cxt);
        return FALSE;
    }

    ret = fdisk_create_disklabel (cxt, table_type_str[type]);
    if (ret != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to create a new disklabel for disk '%s': %s", disk, strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        close_context (cxt);
        return FALSE;
    }

    if (!write_label (cxt, NULL, disk, FALSE, &l_error)) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        close_context (cxt);
        return FALSE;
    }

    close_context (cxt);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

static gchar* get_part_type_guid_and_gpt_flags (const gchar *device, int part_num, guint64 *attrs, GError **error) {
    struct fdisk_context *cxt = NULL;
    struct fdisk_label *lb = NULL;
    struct fdisk_partition *pa = NULL;
    struct fdisk_parttype *ptype = NULL;
    const gchar *label_name = NULL;
    const gchar *ptype_string = NULL;
    gchar *ret = NULL;
    gint status = 0;

    /* first partition in fdisk is 0 */
    part_num--;

    cxt = get_device_context (device, TRUE, error);
    if (!cxt) {
        /* error is already populated */
        return NULL;
    }

    lb = fdisk_get_label (cxt, NULL);
    if (!lb) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read partition table on device '%s'", device);
        close_context (cxt);
        return NULL;
    }

    label_name = fdisk_label_get_name (lb);
    if (g_strcmp0 (label_name, table_type_str[BD_PART_TABLE_GPT]) != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Setting GPT flags is not supported on '%s' partition table", label_name);
        close_context (cxt);
        return NULL;
    }

    if (attrs) {
        status = fdisk_gpt_get_partition_attrs (cxt, part_num, attrs);
        if (status < 0) {
            g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                         "Failed to read GPT attributes");
            close_context (cxt);
            return NULL;
        }
    }

    status = fdisk_get_partition (cxt, part_num, &pa);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition %d on device '%s'", part_num, device);
        close_context (cxt);
        return NULL;
    }

    ptype = fdisk_partition_get_type (pa);
    if (!ptype) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition type for partition %d on device '%s'", part_num, device);
        fdisk_unref_partition (pa);
        close_context (cxt);
        return NULL;
    }

    ptype_string = fdisk_parttype_get_string (ptype);
    if (!ptype_string) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition type for partition %d on device '%s'", part_num, device);
        fdisk_unref_partition (pa);
        close_context (cxt);
        return NULL;
    }

    ret = g_strdup (ptype_string);

    fdisk_unref_partition (pa);
    close_context (cxt);
    return ret;
}

static BDPartSpec* get_part_spec_fdisk (struct fdisk_context *cxt, struct fdisk_partition *pa, GError **error) {
    struct fdisk_label *lb = NULL;
    struct fdisk_parttype *ptype = NULL;
    BDPartSpec *ret = NULL;
    const gchar *devname = NULL;
    const gchar *partname = NULL;
    const gchar *partuuid = NULL;
    GError *l_error = NULL;

    ret = g_new0 (BDPartSpec, 1);

    devname = fdisk_get_devname (cxt);

    if (fdisk_partition_has_partno (pa)) {
        if (isdigit (devname[strlen (devname) - 1]))
            ret->path = g_strdup_printf ("%sp%zu", devname, fdisk_partition_get_partno (pa) + 1);
        else
            ret->path = g_strdup_printf ("%s%zu", devname, fdisk_partition_get_partno (pa) + 1);
    }

    partname = fdisk_partition_get_name (pa);
    if (partname)
        ret->name = g_strdup (partname);

    partuuid = fdisk_partition_get_uuid (pa);
    if (partuuid)
        ret->uuid = g_strdup (partuuid);

    if (fdisk_partition_is_container (pa))
        ret->type = BD_PART_TYPE_EXTENDED;
    else if (fdisk_partition_is_nested (pa))
        ret->type = BD_PART_TYPE_LOGICAL;
    else
        ret->type = BD_PART_TYPE_NORMAL;

    if (fdisk_partition_is_freespace (pa))
        ret->type |= BD_PART_TYPE_FREESPACE;

    if (fdisk_partition_has_start (pa))
        ret->start = (guint64) fdisk_partition_get_start (pa) * fdisk_get_sector_size (cxt);

    if (fdisk_partition_has_size (pa))
        ret->size = (guint64) fdisk_partition_get_size (pa) * fdisk_get_sector_size (cxt);

    lb = fdisk_get_label (cxt, NULL);
    if (!lb) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read partition table.");
        bd_part_spec_free (ret);
        return NULL;
    }

    if (g_strcmp0 (fdisk_label_get_name (lb), "gpt") == 0) {
        if (ret->type == BD_PART_TYPE_NORMAL) {
          /* only 'normal' partitions have GUIDs */
          ret->type_guid = get_part_type_guid_and_gpt_flags (devname, fdisk_partition_get_partno (pa) + 1, &(ret->attrs), &l_error);
          if (!ret->type_guid && l_error) {
              g_propagate_error (error, l_error);
              bd_part_spec_free (ret);
              return NULL;
          }
        }
    } else if (g_strcmp0 (fdisk_label_get_name (lb), "dos") == 0) {
        /* freespace and extended have no type/ids */
        if (ret->type == BD_PART_TYPE_NORMAL || ret->type == BD_PART_TYPE_LOGICAL || ret->type == BD_PART_TYPE_EXTENDED) {
            ptype = fdisk_partition_get_type (pa);
            if (!ptype) {
                g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                             "Failed to get partition type.");
                bd_part_spec_free (ret);
                return NULL;
            }
            ret->id = g_strdup_printf ("0x%02x", fdisk_parttype_get_code (ptype));
        }
        if (fdisk_partition_is_bootable (pa) == 1)
            ret->bootable = TRUE;
    }

    return ret;
}

/**
 * bd_part_get_part_spec:
 * @disk: disk to remove the partition from
 * @part: partition to get spec for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): spec of the @part partition from @disk or %NULL in case of error
 *
 * Tech category: %BD_PART_TECH_MODE_QUERY_PART + the tech according to the partition table type
 */
BDPartSpec* bd_part_get_part_spec (const gchar *disk, const gchar *part, GError **error) {
    struct fdisk_context *cxt = NULL;
    struct fdisk_partition *pa = NULL;
    gint status = 0;
    gint part_num = 0;
    BDPartSpec *ret = NULL;

    part_num = get_part_num (part, error);
    if (part_num == -1)
        return NULL;

    /* first partition in fdisk is 0 */
    part_num--;

    cxt = get_device_context (disk, TRUE, error);
    if (!cxt) {
        /* error is already populated */
        return NULL;
    }

    status = fdisk_get_partition (cxt, part_num, &pa);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition %d on device '%s'", part_num, disk);
        close_context (cxt);
        return NULL;
    }

    ret = get_part_spec_fdisk (cxt, pa, error);

    fdisk_unref_partition (pa);
    close_context (cxt);

    return ret;
}

static BDPartSpec** get_disk_parts (const gchar *disk, gboolean parts, gboolean freespaces, gboolean metadata, GError **error) {
    struct fdisk_context *cxt = NULL;
    struct fdisk_table *table = NULL;
    struct fdisk_partition *pa = NULL;
    struct fdisk_iter *itr = NULL;
    BDPartSpec *spec = NULL;
    BDPartSpec *prev_spec = NULL;
    GPtrArray *array = NULL;
    gint status = 0;

    cxt = get_device_context (disk, TRUE, error);
    if (!cxt) {
        /* error is already populated */
        return NULL;
    }

    table = fdisk_new_table ();
    if (!table) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to create a new table");
        close_context (cxt);
        return NULL;
    }

    itr = fdisk_new_iter (FDISK_ITER_FORWARD);
    if (!itr) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to create a new iterator");
        close_context (cxt);
        return NULL;
    }

    if (parts) {
        status = fdisk_get_partitions (cxt, &table);
        if (status != 0) {
            g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                         "Failed to get partitions");
            fdisk_free_iter (itr);
            fdisk_unref_table (table);
            close_context (cxt);
            return NULL;
        }
    }

    if (freespaces) {
        status = fdisk_get_freespaces (cxt, &table);
        if (status != 0) {
            g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                         "Failed to get free spaces");
            fdisk_free_iter (itr);
            fdisk_unref_table (table);
            close_context (cxt);
            return NULL;
        }
    }

    /* sort partitions by start */
    status = fdisk_table_sort_partitions (table, fdisk_partition_cmp_start);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to sort partitions");
        fdisk_free_iter (itr);
        fdisk_unref_table (table);
        close_context (cxt);
        return NULL;
    }

    array = g_ptr_array_new_with_free_func ((GDestroyNotify) (void *) bd_part_spec_free);

    while (fdisk_table_next_partition (table, itr, &pa) == 0) {
        spec = get_part_spec_fdisk (cxt, pa, error);
        if (!spec) {
            g_ptr_array_free (array, TRUE);
            fdisk_free_iter (itr);
            fdisk_unref_table (table);
            close_context (cxt);
            return NULL;
        }

        /* libfdisk doesn't have a special partition for metadata so we need to add
           a special metadata partition to the "empty" spaces between partitions
           and free spaces to mimic behaviour of parted
           metadata partitions should be present in the extended partition in front
           of every logical partition */
        if (prev_spec && metadata) {
            if ((spec->start > prev_spec->start + prev_spec->size) ||
                (prev_spec->type == BD_PART_TYPE_EXTENDED && spec->start > prev_spec->start) ) {
                BDPartSpec *ext_meta = g_new0 (BDPartSpec, 1);
                ext_meta->name = NULL;
                ext_meta->path = NULL;

                if (prev_spec->type == BD_PART_TYPE_EXTENDED) {
                    ext_meta->start = prev_spec->start;
                    ext_meta->size = spec->start - ext_meta->start;
                    ext_meta->type = BD_PART_TYPE_METADATA | BD_PART_TYPE_LOGICAL;
                } else {
                    ext_meta->start = prev_spec->start + prev_spec->size;
                    ext_meta->size = spec->start - ext_meta->start;
                    if (spec->type & BD_PART_TYPE_LOGICAL)
                        ext_meta->type = BD_PART_TYPE_METADATA | BD_PART_TYPE_LOGICAL;
                    else
                        ext_meta->type = BD_PART_TYPE_METADATA;
                }
                ext_meta->type_guid = NULL;

                g_ptr_array_add (array, ext_meta);
            }
        }

        prev_spec = spec;
        g_ptr_array_add (array, spec);
    }

    fdisk_free_iter (itr);
    fdisk_unref_table (table);
    close_context (cxt);

    g_ptr_array_add (array, NULL);
    return (BDPartSpec **) g_ptr_array_free (array, FALSE);
}

/**
 * bd_part_get_part_by_pos:
 * @disk: disk to remove the partition from
 * @position: position (in bytes) determining the partition
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): spec of the partition from @disk spanning over the @position or %NULL if no such
 *          partition exists or in case of error (@error is set)
 *
 * Tech category: %BD_PART_TECH_MODE_QUERY_PART + the tech according to the partition table type
 */
BDPartSpec* bd_part_get_part_by_pos (const gchar *disk, guint64 position, GError **error) {
    BDPartSpec **parts = NULL;
    BDPartSpec *ret = NULL;

    parts = get_disk_parts (disk, TRUE, TRUE, TRUE, error);
    if (!parts)
        return NULL;

    for (BDPartSpec **parts_p = parts; *parts_p; parts_p++) {
        if ((*parts_p)->start <= position && ((*parts_p)->start + (*parts_p)->size) > position) {
            if ((*parts_p)->type == BD_PART_TYPE_EXTENDED) {
                /* we don't want to return extended partition here -- there
                   is either another logical or free space at this position */
                continue;
            }

            ret = *parts_p;
            break;
        }
    }

    /* free the array except the selected element */
    for (BDPartSpec **parts_p = parts; *parts_p; parts_p++)
        if (*parts_p != ret)
            bd_part_spec_free (*parts_p);
    g_free (parts);

    return ret;
}

/**
 * bd_part_get_disk_spec:
 * @disk: disk to get information about
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): information about the given @disk or %NULL (in case of error)
 *
 * Tech category: %BD_PART_TECH_MODE_QUERY_TABLE + the tech according to the partition table type
 */
BDPartDiskSpec* bd_part_get_disk_spec (const gchar *disk, GError **error) {
    struct fdisk_context *cxt = NULL;
    struct fdisk_label *lb = NULL;
    BDPartDiskSpec *ret = NULL;
    const gchar *label_name = NULL;
    BDPartTableType type = BD_PART_TABLE_UNDEF;
    gboolean found = FALSE;

    cxt = get_device_context (disk, TRUE, error);
    if (!cxt) {
        /* error is already populated */
        return NULL;
    }

    ret = g_new0 (BDPartDiskSpec, 1);
    ret->path = g_strdup (fdisk_get_devname (cxt));
    ret->sector_size = (guint64) fdisk_get_sector_size (cxt);
    ret->size = fdisk_get_nsectors (cxt) * ret->sector_size;

    lb = fdisk_get_label (cxt, NULL);
    if (lb) {
        label_name = fdisk_label_get_name (lb);
        for (type=BD_PART_TABLE_MSDOS; !found && type < BD_PART_TABLE_UNDEF; type++) {
            if (g_strcmp0 (label_name, table_type_str[type]) == 0) {
                ret->table_type = type;
                found = TRUE;
            }
        }
        if (!found)
            ret->table_type = BD_PART_TABLE_UNDEF;
    } else
        ret->table_type = BD_PART_TABLE_UNDEF;

    close_context (cxt);

    return ret;
}

/**
 * bd_part_get_disk_parts:
 * @disk: disk to get information about partitions for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full) (array zero-terminated=1): specs of the partitions from @disk or %NULL in case of error
 *
 * Tech category: %BD_PART_TECH_MODE_QUERY_TABLE + the tech according to the partition table type
 */
BDPartSpec** bd_part_get_disk_parts (const gchar *disk, GError **error) {
    return get_disk_parts (disk, TRUE, FALSE, FALSE, error);
}

/**
 * bd_part_get_disk_free_regions:
 * @disk: disk to get free regions for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full) (array zero-terminated=1): specs of the free regions from @disk or %NULL in case of error
 *
 * Tech category: %BD_PART_TECH_MODE_QUERY_TABLE + the tech according to the partition table type
 */
BDPartSpec** bd_part_get_disk_free_regions (const gchar *disk, GError **error) {
    return get_disk_parts (disk, FALSE, TRUE, FALSE, error);
}

/**
 * bd_part_get_best_free_region:
 * @disk: disk to get the best free region for
 * @type: type of the partition that is planned to be added
 * @size: size of the partition to be added
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): spec of the best free region on @disk for a new partition of type @type
 *                           with the size of @size or %NULL if there is none such region or if
 *                           there was an error (@error gets populated)
 *
 * Note: For the @type %BD_PART_TYPE_NORMAL, the smallest possible space that *is not* in an extended partition
 *       is found. For the @type %BD_PART_TYPE_LOGICAL, the smallest possible space that *is* in an extended
 *       partition is found. For %BD_PART_TYPE_EXTENDED, the biggest possible space is found as long as there
 *       is no other extended partition (there can only be one).
 *
 * Tech category: %BD_PART_TECH_MODE_QUERY_TABLE + the tech according to the partition table type
 */
BDPartSpec* bd_part_get_best_free_region (const gchar *disk, BDPartType type, guint64 size, GError **error) {
    BDPartSpec **free_regs = NULL;
    BDPartSpec **free_reg_p = NULL;
    BDPartSpec *ret = NULL;

    free_regs = bd_part_get_disk_free_regions (disk, error);
    if (!free_regs)
        /* error should be populated */
        return NULL;
    if (!(*free_regs)) {
        /* no free regions */
        g_free (free_regs);
        return NULL;
    }

    if (type == BD_PART_TYPE_NORMAL) {
        for (free_reg_p=free_regs; *free_reg_p; free_reg_p++) {
            /* check if it has enough space and is not inside an extended partition */
            if ((*free_reg_p)->size > size && !((*free_reg_p)->type & BD_PART_TYPE_LOGICAL))
                /* if it is the first that would fit or if it is smaller than
                   what we found earlier, it is a better match */
                if (!ret || ((*free_reg_p)->size < ret->size))
                    ret = *free_reg_p;
        }
    } else if (type == BD_PART_TYPE_EXTENDED) {
        for (free_reg_p=free_regs; *free_reg_p; free_reg_p++) {
            /* if there already is an extended partition, there cannot be another one */
            if ((*free_reg_p)->type & BD_PART_TYPE_LOGICAL) {
                for (free_reg_p=free_regs; *free_reg_p; free_reg_p++)
                    bd_part_spec_free (*free_reg_p);
                g_free (free_regs);
                return NULL;
            }
            /* check if it has enough space */
            if ((*free_reg_p)->size > size)
                /* if it is the first that would fit or if it is bigger than
                   what we found earlier, it is a better match */
                if (!ret || ((*free_reg_p)->size > ret->size))
                    ret = *free_reg_p;
        }
    } else if (type == BD_PART_TYPE_LOGICAL) {
        for (free_reg_p=free_regs; *free_reg_p; free_reg_p++) {
            /* check if it has enough space and is inside an extended partition */
            if ((*free_reg_p)->size > size && ((*free_reg_p)->type & BD_PART_TYPE_LOGICAL))
                /* if it is the first that would fit or if it is smaller than
                   what we found earlier, it is a better match */
                if (!ret || ((*free_reg_p)->size < ret->size))
                    ret = *free_reg_p;
        }
    }

    /* free all the other specs and return the best one */
    for (free_reg_p=free_regs; *free_reg_p; free_reg_p++)
        if (*free_reg_p != ret)
            bd_part_spec_free (*free_reg_p);
    g_free (free_regs);

    return ret;
}

/**
 * bd_part_create_part:
 * @disk: disk to create partition on
 * @type: type of the partition to create (if %BD_PART_TYPE_REQ_NEXT, the
 *        partition type will be determined automatically based on the existing
 *        partitions)
 * @start: where the partition should start (i.e. offset from the disk start)
 * @size: desired size of the partition (if 0, a max-sized partition is created)
 * @align: alignment to use for the partition
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer full): specification of the created partition or %NULL in case of error
 *
 * NOTE: The resulting partition may start at a different position than given by
 *       @start and can have different size than @size due to alignment.
 *
 * Tech category: %BD_PART_TECH_MODE_MODIFY_TABLE + the tech according to the partition table type
 */
BDPartSpec* bd_part_create_part (const gchar *disk, BDPartTypeReq type, guint64 start, guint64 size, BDPartAlign align, GError **error) {
    struct fdisk_context *cxt = NULL;
    struct fdisk_partition *npa = NULL;
    gint status = 0;
    BDPartSpec *ret = NULL;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    guint64 sector_size = 0;
    guint64 grain_size = 0;
    guint64 end = 0;
    struct fdisk_parttype *ptype = NULL;
    struct fdisk_label *lbl = NULL;
    struct fdisk_table *table = NULL;
    struct fdisk_iter *iter = NULL;
    struct fdisk_partition *pa = NULL;
    struct fdisk_partition *epa = NULL;
    struct fdisk_partition *in_pa = NULL;
    struct fdisk_partition *n_epa = NULL;
    guint n_parts = 0;
    gboolean on_gpt = FALSE;
    size_t partno = 0;
    gchar *ppath = NULL;
    gboolean new_extended = FALSE;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started adding partition to '%s'", disk);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    cxt = get_device_context (disk, FALSE, &l_error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return NULL;
    }

    status = fdisk_get_partitions (cxt, &table);
    if (status != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get existing partitions on the device: %s", strerror_l (-status, c_locale));
        fdisk_unref_partition (npa);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return NULL;
   }

    npa = fdisk_new_partition ();
    if (!npa) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to create new partition object");
        fdisk_unref_table (table);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return NULL;
    }

    sector_size = (guint64) fdisk_get_sector_size (cxt);
    grain_size = (guint64) fdisk_get_grain_size (cxt);

    if (align == BD_PART_ALIGN_NONE)
        grain_size = sector_size;
    else if (align == BD_PART_ALIGN_MINIMAL)
        grain_size = (guint64) fdisk_get_minimal_iosize (cxt);
    /* else OPTIMAL or unknown -> nothing to do */

    status = fdisk_save_user_grain (cxt, grain_size);
    if (status != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to setup alignment");
        fdisk_unref_table (table);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return NULL;
    }

    /* this is needed so that the saved grain size from above becomes
     * effective */
    status = fdisk_reset_device_properties (cxt);
    if (status != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to setup alignment");
        fdisk_unref_table (table);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return NULL;
    }

    /* set first usable sector to 1 for none and minimal alignments
       we need to set this here, because fdisk_reset_device_properties */
    if (align == BD_PART_ALIGN_NONE || align == BD_PART_ALIGN_MINIMAL)
        fdisk_set_first_lba (cxt, 1);

    grain_size = (guint64) fdisk_get_grain_size (cxt);

    /* align start up to sectors, we will align it further based on grain_size
       using libfdisk later */
    start = (start + sector_size - 1) / sector_size;

    /* start on sector 0 doesn't work with libfdisk alignment -- everything
       else is properly aligned, but zero is aligned to zero */
    if (start == 0)
        start = 1;

    start = fdisk_align_lba (cxt, (fdisk_sector_t) start, FDISK_ALIGN_UP);

    if (size == 0)
        /* no size specified, set the end to default (maximum) */
        fdisk_partition_end_follow_default (npa, 1);
    else {
        /* align size down */
        size = (size / grain_size) * grain_size;
        size = size / sector_size;

        /* use libfdisk to align the end sector */
        end = start + size;
        end = fdisk_align_lba (cxt, (fdisk_sector_t) end, FDISK_ALIGN_DOWN);
        size = end - start;

        if (fdisk_partition_set_size (npa, size) != 0) {
            g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                         "Failed to set partition size");
            fdisk_unref_table (table);
            fdisk_unref_partition (npa);
            close_context (cxt);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return NULL;
        }
    }

    fdisk_partition_partno_follow_default (npa, 1);
    lbl = fdisk_get_label (cxt, NULL);
    on_gpt = g_strcmp0 (fdisk_label_get_name (lbl), "gpt") == 0;

    /* GPT is easy, all partitions are the same (NORMAL) */
    if (on_gpt && type == BD_PART_TYPE_REQ_NEXT)
      type = BD_PART_TYPE_REQ_NORMAL;

    if (on_gpt && type != BD_PART_TYPE_REQ_NORMAL) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Only normal partitions are supported on GPT.");
        fdisk_unref_table (table);
        fdisk_unref_partition (npa);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return NULL;
    }

    /* on DOS we may have to decide if requested */
    if (type == BD_PART_TYPE_REQ_NEXT) {
        iter = fdisk_new_iter (FDISK_ITER_FORWARD);
        while (fdisk_table_next_partition (table, iter, &pa) == 0) {
            if (fdisk_partition_is_freespace (pa))
                continue;
            if (!epa && fdisk_partition_is_container (pa))
                epa = pa;
            if (!in_pa && fdisk_partition_has_start (pa) && fdisk_partition_has_size (pa) &&
                fdisk_partition_get_start (pa) <= start &&
                (start < (fdisk_partition_get_start (pa) + fdisk_partition_get_size (pa))))
                in_pa = pa;
            n_parts++;
        }
          if (in_pa) {
            if (epa == in_pa)
                /* creating a partition inside an extended partition -> LOGICAL */
                type = BD_PART_TYPE_REQ_LOGICAL;
            else {
                /* trying to create a partition inside an existing one, but not
                   an extended one -> error */
                g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                             "Cannot create a partition inside an existing non-extended one");
                fdisk_unref_partition (npa);
                fdisk_free_iter (iter);
                fdisk_unref_table (table);
                close_context (cxt);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return NULL;
            }
        } else if (epa)
            /* there's an extended partition already and we are creating a new
               one outside of it */
            type = BD_PART_TYPE_REQ_NORMAL;
        else if (n_parts == 3) {
            /* already 3 primary partitions -> create an extended partition of
               the biggest possible size and a logical partition as requested in
               it */
            new_extended = TRUE;
            n_epa = fdisk_new_partition ();
            if (!n_epa) {
                g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                             "Failed to create new partition object");
                fdisk_unref_partition (npa);
                close_context (cxt);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return NULL;
            }
            if (fdisk_partition_set_start (n_epa, start) != 0) {
                g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                             "Failed to set partition start");
                fdisk_unref_partition (n_epa);
                fdisk_unref_partition (npa);
                fdisk_free_iter (iter);
                fdisk_unref_table (table);
                close_context (cxt);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return NULL;
            }

            fdisk_partition_partno_follow_default (n_epa, 1);

            status = fdisk_partition_next_partno (npa, cxt, &partno);
            if (status != 0) {
                g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                             "Failed to get new extended partition number");
                fdisk_unref_partition (npa);
                close_context (cxt);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return NULL;
            }

            status = fdisk_partition_set_partno (npa, partno);
            if (status != 0) {
                g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                             "Failed to set new extended partition number");
                fdisk_unref_partition (npa);
                close_context (cxt);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return NULL;
            }

            /* set the end to default (maximum) */
            fdisk_partition_end_follow_default (n_epa, 1);

            /* "05" for extended partition */
            ptype = fdisk_label_parse_parttype (fdisk_get_label (cxt, NULL), "05");
            if (fdisk_partition_set_type (n_epa, ptype) != 0) {
                g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                             "Failed to set partition type");
                fdisk_unref_partition (n_epa);
                fdisk_unref_partition (npa);
                fdisk_free_iter (iter);
                fdisk_unref_table (table);
                close_context (cxt);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return NULL;
            }
            fdisk_unref_parttype (ptype);

            status = fdisk_add_partition (cxt, n_epa, NULL);
            fdisk_unref_partition (n_epa);
            if (status != 0) {
                g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                             "Failed to add new partition to the table: %s", strerror_l (-status, c_locale));
                fdisk_unref_partition (npa);
                fdisk_free_iter (iter);
                fdisk_unref_table (table);
                close_context (cxt);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return NULL;
            }
            /* shift the start 2 MiB further as that's where the first logical
               partition inside an extended partition can start */
            start += (2 MiB / sector_size);
            type = BD_PART_TYPE_REQ_LOGICAL;
        } else
            /* no extended partition and not 3 primary partitions -> just create
               another primary (NORMAL) partition*/
            type = BD_PART_TYPE_REQ_NORMAL;

        fdisk_free_iter (iter);
    }

    if (type == BD_PART_TYPE_REQ_EXTENDED) {
        new_extended = TRUE;
        /* "05" for extended partition */
        ptype = fdisk_label_parse_parttype (fdisk_get_label (cxt, NULL), "05");
        if (fdisk_partition_set_type (npa, ptype) != 0) {
            g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                         "Failed to set partition type");
            fdisk_unref_table (table);
            fdisk_unref_partition (npa);
            close_context (cxt);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return NULL;
        }

        fdisk_unref_parttype (ptype);
    }

    if (fdisk_partition_set_start (npa, start) != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set partition start");
        fdisk_unref_table (table);
        fdisk_unref_partition (npa);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return NULL;
    }

    if (type == BD_PART_TYPE_REQ_LOGICAL) {
        /* next_partno doesn't work for logical partitions, for these the
           current maximal number of partitions supported by the label
           is the next (logical) partition number */
        partno = fdisk_get_npartitions (cxt);

    } else {
        status = fdisk_partition_next_partno (npa, cxt, &partno);
        if (status != 0) {
            g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                         "Failed to get new partition number");
            fdisk_unref_table (table);
            fdisk_unref_partition (npa);
            close_context (cxt);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return NULL;
        }
    }

    status = fdisk_partition_set_partno (npa, partno);
    if (status != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set new partition number");
        fdisk_unref_table (table);
        fdisk_unref_partition (npa);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return NULL;
    }

    status = fdisk_add_partition (cxt, npa, NULL);
    if (status != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to add new partition to the table: %s", strerror_l (-status, c_locale));
        fdisk_unref_table (table);
        fdisk_unref_partition (npa);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return NULL;
    }

    /* for new extended partition we need to force reread whole partition table with
       libfdisk < 2.36.1 */
    if (!write_label (cxt, table, disk, new_extended && fdisk_version < 2361, &l_error)) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        fdisk_unref_table (table);
        fdisk_unref_partition (npa);
        close_context (cxt);
        return NULL;
    }

    if (fdisk_partition_has_partno (npa)) {
        if (isdigit (disk[strlen (disk) - 1]))
            ppath = g_strdup_printf ("%sp%zu", disk, fdisk_partition_get_partno (npa) + 1);
        else
            ppath = g_strdup_printf ("%s%zu", disk, fdisk_partition_get_partno (npa) + 1);
    }

    /* close the context now, we no longer need it */
    fdisk_unref_table (table);
    fdisk_unref_partition (npa);
    close_context (cxt);

    /* the in-memory model of the new partition is not updated, we need to
       read the spec manually
       if we get NULL and error here, just propagate it further */
    ret = bd_part_get_part_spec (disk, ppath, error);
    g_free (ppath);

    bd_utils_report_finished (progress_id, "Completed");

    return ret;
}

/**
 * bd_part_delete_part:
 * @disk: disk to remove the partition from
 * @part: partition to remove
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @part partition was successfully deleted from @disk
 *
 * Tech category: %BD_PART_TECH_MODE_MODIFY_TABLE + the tech according to the partition table type
 */
gboolean bd_part_delete_part (const gchar *disk, const gchar *part, GError **error) {
    gint part_num = 0;
    struct fdisk_context *cxt = NULL;
    struct fdisk_table *table = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started deleting partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    part_num = get_part_num (part, &l_error);
    if (part_num == -1) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* /dev/sda1 is the partition number 0 in libfdisk */
    part_num--;
    cxt = get_device_context (disk, FALSE, &l_error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = fdisk_get_partitions (cxt, &table);
    if (ret != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get existing partitions on the device: %s", strerror_l (-ret, c_locale));
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
   }

    ret = fdisk_delete_partition (cxt, (size_t) part_num);
    if (ret != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to delete partition '%d' on device '%s': %s", part_num+1, disk, strerror_l (-ret, c_locale));
        fdisk_unref_table (table);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (!write_label (cxt, table, disk, FALSE, &l_error)) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        fdisk_unref_table (table);
        close_context (cxt);
        return FALSE;
    }

    fdisk_unref_table (table);
    close_context (cxt);

    bd_utils_report_finished (progress_id, "Completed");

    return TRUE;
}

/* get maximal size for partition when resizing
 * this is a simplified copy of 'resize_get_last_possible' function from
 * libfdisk which is unfortunately not public
 */
static gboolean get_max_part_size (struct fdisk_table *tb, guint partno, guint64 *max_size, GError **error) {
    struct fdisk_partition *pa = NULL;
    struct fdisk_partition *cur = NULL;
    struct fdisk_iter *itr = NULL;
    guint64 start = 0;
    gboolean found = FALSE;

    itr = fdisk_new_iter (FDISK_ITER_FORWARD);
    if (!itr) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to create a new iterator");
        return FALSE;
    }

    cur = fdisk_table_get_partition_by_partno (tb, partno);
    if (!cur) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to locate partition '%d' in table.", partno);
        fdisk_free_iter (itr);
        return FALSE;
    }

    start = fdisk_partition_get_start (cur);

    while (!found && fdisk_table_next_partition (tb, itr, &pa) == 0) {
        if (!fdisk_partition_has_start (pa) || !fdisk_partition_has_size (pa) ||
            (fdisk_partition_is_container (pa) && pa != cur)) {
            /* partition has no size/start or is an extended partition */
            continue;
        }

        if (fdisk_partition_is_nested (pa) && fdisk_partition_is_container (cur)) {
            /* ignore logical partitions inside if we are checking an extended partition */
            continue;
        }

        if (fdisk_partition_is_nested (cur) && !fdisk_partition_is_nested (pa)) {
            /* current partition is nested, we are looking for a nested free space */
            continue;
        }

        if (pa == cur) {
            /* found our current partition */
            found = TRUE;
        }
    }

    if (found && fdisk_table_next_partition (tb, itr, &pa) == 0) {
        /* check next partition after our current we found */
        if (fdisk_partition_is_freespace (pa)) {
            /* we found a free space after current partition */
            if (LIBFDISK_MINOR_VERSION <= 32)
                /* XXX: older versions of libfdisk doesn't count free space between
                    partitions as usable so we need to do the same here, see
                    https://github.com/karelzak/util-linux/commit/2f35c1ead621f42f32f7777232568cb03185b473 */
                *max_size  = fdisk_partition_get_size (cur) + fdisk_partition_get_size (pa);
            else
                *max_size = fdisk_partition_get_size (pa) - (start - fdisk_partition_get_start (pa));
        }
    }

    /* no free space found: set max_size to current size */
    if (*max_size == 0)
        *max_size = fdisk_partition_get_size (cur);

    fdisk_free_iter (itr);
    return TRUE;
}

/**
 * bd_part_resize_part:
 * @disk: disk containing the partition
 * @part: partition to resize
 * @size: new partition size, 0 for maximal size
 * @align: alignment to use for the partition end
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @part partition was successfully resized on @disk to @size
 *
 * NOTE: The resulting partition may be slightly bigger than requested due to alignment.
 *
 * Tech category: %BD_PART_TECH_MODE_MODIFY_TABLE + the tech according to the partition table type
 */
gboolean bd_part_resize_part (const gchar *disk, const gchar *part, guint64 size, BDPartAlign align, GError **error) {
    gint part_num = 0;
    struct fdisk_context *cxt = NULL;
    struct fdisk_table *table = NULL;
    struct fdisk_partition *pa = NULL;
    gint ret = 0;
    guint64 old_size = 0;
    guint64 sector_size = 0;
    guint64 grain_size = 0;
    guint64 progress_id = 0;
    guint64 max_size = 0;
    guint64 start = 0;
    guint64 end = 0;
    gint version = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started resizing partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    part_num = get_part_num (part, &l_error);
    if (part_num == -1) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* /dev/sda1 is the partition number 0 in libfdisk */
    part_num--;
    cxt = get_device_context (disk, FALSE, &l_error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* get existing partitions and free spaces and sort the table */
    ret = fdisk_get_partitions (cxt, &table);
    if (ret != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get existing partitions on the device: %s", strerror_l (-ret, c_locale));
        fdisk_unref_table (table);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    ret = fdisk_get_freespaces (cxt, &table);
    if (ret != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get free spaces on the device: %s", strerror_l (-ret, c_locale));
        fdisk_unref_table (table);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    fdisk_table_sort_partitions (table, fdisk_partition_cmp_start);

    ret = fdisk_get_partition (cxt, part_num, &pa);
    if (ret != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition %d on device '%s'", part_num, disk);
        fdisk_unref_table (table);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (fdisk_partition_has_size (pa))
        old_size = (guint64) fdisk_partition_get_size (pa);
    else {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get size for partition %d on device '%s'", part_num, disk);
        fdisk_unref_partition (pa);
        fdisk_unref_table (table);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* set grain_size based on user alignment preferences */
    sector_size = (guint64) fdisk_get_sector_size (cxt);
    grain_size = (guint64) fdisk_get_grain_size (cxt);

    if (align == BD_PART_ALIGN_NONE)
        grain_size = sector_size;
    else if (align == BD_PART_ALIGN_MINIMAL)
        grain_size = (guint64) fdisk_get_minimal_iosize (cxt);
    /* else OPTIMAL or unknown -> nothing to do */

    if (!get_max_part_size (table, part_num, &max_size, &l_error)) {
        g_prefix_error (&l_error, "Failed to get maximal size for '%s': ", part);
        fdisk_unref_table (table);
        fdisk_unref_partition (pa);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (size == 0) {
        if (max_size == old_size) {
            bd_utils_log_format (BD_UTILS_LOG_INFO, "Not resizing, partition '%s' is already at its maximum size.", part);
            fdisk_unref_table (table);
            fdisk_unref_partition (pa);
            close_context (cxt);
            bd_utils_report_finished (progress_id, "Completed");
            return TRUE;
        }

        /* latest libfdisk introduces default end alignment for new partitions, we should
           do the same for resizes where we calculate the size ourselves */
        version = fdisk_get_library_version (NULL);
        if (version >= 2380 && align != BD_PART_ALIGN_NONE) {
            start = fdisk_partition_get_start (pa);
            end = start + max_size;
            end = fdisk_align_lba_in_range (cxt, end, start, end);
            max_size = end - start;
        }

        if (fdisk_partition_set_size (pa, max_size) != 0) {
            g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                         "Failed to set size for partition %d on device '%s'", part_num, disk);
            fdisk_unref_table (table);
            fdisk_unref_partition (pa);
            close_context (cxt);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    } else {
        /* align size up */
        if (size % grain_size != 0)
            size = ((size + grain_size) / grain_size) * grain_size;
        size = size / sector_size;

        if (size == old_size) {
            bd_utils_log_format (BD_UTILS_LOG_INFO, "Not resizing, new size after alignment is the same as the old size.");
            fdisk_unref_table (table);
            fdisk_unref_partition (pa);
            close_context (cxt);
            bd_utils_report_finished (progress_id, "Completed");
            return TRUE;
        }

        if (size > old_size && size > max_size) {
            if (size - max_size <= 4 MiB / sector_size) {
                bd_utils_log_format (BD_UTILS_LOG_INFO,
                                     "Requested size %"G_GUINT64_FORMAT" is bigger than max size for partition '%s', adjusting to %"G_GUINT64_FORMAT".",
                                     size * sector_size, part, max_size * sector_size);
                size = max_size;
            } else {
                g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                             "Requested size %"G_GUINT64_FORMAT" is bigger than max size (%"G_GUINT64_FORMAT") for partition '%s'",
                             size * sector_size, max_size * sector_size, part);
                fdisk_unref_table (table);
                fdisk_unref_partition (pa);
                close_context (cxt);
                bd_utils_report_finished (progress_id, l_error->message);
                g_propagate_error (error, l_error);
                return FALSE;
            }
        }

        if (fdisk_partition_set_size (pa, size) != 0) {
            g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                         "Failed to set partition size");
            fdisk_unref_table (table);
            fdisk_unref_partition (pa);
            close_context (cxt);
            bd_utils_report_finished (progress_id, l_error->message);
            g_propagate_error (error, l_error);
            return FALSE;
        }
    }

    ret = fdisk_set_partition (cxt, part_num, pa);
    if (ret != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to resize partition '%s': %s", part, strerror_l (-ret, c_locale));
        fdisk_unref_table (table);
        fdisk_unref_partition (pa);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (!write_label (cxt, table, disk, FALSE, &l_error)) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        fdisk_unref_table (table);
        fdisk_unref_partition (pa);
        close_context (cxt);
        return FALSE;
    }

    fdisk_unref_table (table);

    /* XXX: double free in libfdisk, see https://github.com/karelzak/util-linux/pull/822
    fdisk_unref_partition (pa); */
    close_context (cxt);

    bd_utils_report_finished (progress_id, "Completed");

    return TRUE;
}

static gboolean set_part_type (struct fdisk_context *cxt, gint part_num, const gchar *type_str, BDPartTableType table_type, GError **error) {
    struct fdisk_label *lb = NULL;
    struct fdisk_partition *pa = NULL;
    struct fdisk_parttype *ptype = NULL;
    const gchar *label_name = NULL;
    gint status = 0;
    gint part_id_int = 0;

    /* check if part type/id is valid for MBR */
    if (table_type == BD_PART_TABLE_MSDOS) {
        part_id_int = g_ascii_strtoull (type_str, NULL, 0);

        if (part_id_int == 0) {
            g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                         "Invalid partition id given: '%s'.", type_str);
            return FALSE;
        }

        if (part_id_int == 0x05 || part_id_int == 0x0f || part_id_int == 0x85) {
            g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                         "Cannot change partition id to extended.");
            return FALSE;
        }
    }

    lb = fdisk_get_label (cxt, NULL);
    if (!lb) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read partition table.");
        return FALSE;
    }

    label_name = fdisk_label_get_name (lb);
    if (g_strcmp0 (label_name, table_type_str[table_type]) != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Setting partition type is not supported on '%s' partition table", label_name);
        return FALSE;
    }

    status = fdisk_get_partition (cxt, part_num, &pa);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Failed to get partition %d.", part_num);
        return FALSE;
    }

    ptype = fdisk_label_parse_parttype (lb, type_str);
    if (!ptype) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Failed to parse partition type.");
        fdisk_unref_partition (pa);
        return FALSE;
    }

    status = fdisk_set_partition_type (cxt, part_num, ptype);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set partition type for partition %d.", part_num);
        fdisk_unref_parttype (ptype);
        fdisk_unref_partition (pa);
        return FALSE;
    }

    fdisk_unref_parttype (ptype);
    fdisk_unref_partition (pa);
    return TRUE;
}

/**
 * bd_part_set_part_name:
 * @disk: device the partition belongs to
 * @part: partition the name should be set for
 * @name: name to set
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the name was successfully set or not
 *
 * Tech category: %BD_PART_TECH_MODE_MODIFY_PART + the tech according to the partition table type
 */
gboolean bd_part_set_part_name (const gchar *disk, const gchar *part, const gchar *name, GError **error) {
    struct fdisk_context *cxt = NULL;
    struct fdisk_label *lb = NULL;
    struct fdisk_partition *pa = NULL;
    const gchar *label_name = NULL;
    gint part_num = 0;
    gint status = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started setting name on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    cxt = get_device_context (disk, FALSE, error);
    if (!cxt) {
        /* error is already populated */
        return FALSE;
    }

    lb = fdisk_get_label (cxt, NULL);
    if (!lb) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read partition table on device '%s'", disk);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    label_name = fdisk_label_get_name (lb);
    if (g_strcmp0 (label_name, table_type_str[BD_PART_TABLE_GPT]) != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Partition names unsupported on the device '%s' ('%s')", disk,
                     label_name);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    part_num = get_part_num (part, &l_error);
    if (part_num == -1) {
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* /dev/sda1 is the partition number 0 in libfdisk */
    part_num--;

    status = fdisk_get_partition (cxt, part_num, &pa);
    if (status != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition '%s' on device '%s': %s",
                     part, disk, strerror_l (-status, c_locale));
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    status = fdisk_partition_set_name (pa, name);
    if (status != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set name on the partition '%s' on device '%s': %s",
                     part, disk, strerror_l (-status, c_locale));
        fdisk_unref_partition (pa);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    status = fdisk_set_partition (cxt, part_num, pa);
    if (status != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set name on the partition '%s' on device '%s': %s",
                     part, disk, strerror_l (-status, c_locale));
        fdisk_unref_partition (pa);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    fdisk_unref_partition (pa);

    if (!write_label (cxt, NULL, disk, FALSE, &l_error)) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        close_context (cxt);
        return FALSE;
    }

    close_context (cxt);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_part_set_part_type:
 * @disk: device the partition belongs to
 * @part: partition the type should be set for
 * @type_guid: GUID of the type
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @type_guid type was successfully set for @part or not
 *
 * Tech category: %BD_PART_TECH_GPT-%BD_PART_TECH_MODE_MODIFY_PART
 */
gboolean bd_part_set_part_type (const gchar *disk, const gchar *part, const gchar *type_guid, GError **error) {
    guint64 progress_id = 0;
    gchar *msg = NULL;
    struct fdisk_context *cxt = NULL;
    gint part_num = 0;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started setting type on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    part_num = get_part_num (part, &l_error);
    if (part_num == -1) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* /dev/sda1 is the partition number 0 in libfdisk */
    part_num--;

    cxt = get_device_context (disk, FALSE, &l_error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (!set_part_type (cxt, part_num, type_guid, BD_PART_TABLE_GPT, &l_error)) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        close_context (cxt);
        return FALSE;
    }

    if (!write_label (cxt, NULL, disk, FALSE, &l_error)) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        close_context (cxt);
        return FALSE;
    }

    close_context (cxt);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_part_set_part_id:
 * @disk: device the partition belongs to
 * @part: partition the ID should be set for
 * @part_id: partition Id
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @part_id type was successfully set for @part or not
 *
 * Tech category: %BD_PART_TECH_MBR-%BD_PART_TECH_MODE_MODIFY_PART
 */
gboolean bd_part_set_part_id (const gchar *disk, const gchar *part, const gchar *part_id, GError **error) {
    guint64 progress_id = 0;
    gchar *msg = NULL;
    struct fdisk_context *cxt = NULL;
    gint part_num = 0;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started setting id on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    part_num = get_part_num (part, &l_error);
    if (part_num == -1) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* /dev/sda1 is the partition number 0 in libfdisk */
    part_num--;

    cxt = get_device_context (disk, FALSE, &l_error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    if (!set_part_type (cxt, part_num, part_id, BD_PART_TABLE_MSDOS, &l_error)) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        close_context (cxt);
        return FALSE;
    }

    if (!write_label (cxt, NULL, disk, FALSE, &l_error)) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        close_context (cxt);
        return FALSE;
    }

    close_context (cxt);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_part_set_part_uuid:
 * @disk: device the partition belongs to
 * @part: partition the UUID should be set for
 * @uuid: partition UUID to set
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @uuid type was successfully set for @part or not
 *
 * Tech category: %BD_PART_TECH_GPT-%BD_PART_TECH_MODE_MODIFY_PART
 */
gboolean bd_part_set_part_uuid (const gchar *disk, const gchar *part, const gchar *uuid, GError **error) {
    struct fdisk_context *cxt = NULL;
    struct fdisk_partition *pa = NULL;
    struct fdisk_label *lb = NULL;
    const gchar *label_name = NULL;
    gint part_num = 0;
    gint status = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    GError *l_error = NULL;

    msg = g_strdup_printf ("Started setting UUID on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    cxt = get_device_context (disk, FALSE, error);
    if (!cxt) {
        /* error is already populated */
        return FALSE;
    }

    lb = fdisk_get_label (cxt, NULL);
    if (!lb) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read partition table on device '%s'", disk);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    label_name = fdisk_label_get_name (lb);
    if (g_strcmp0 (label_name, table_type_str[BD_PART_TABLE_GPT]) != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Partition UUIDs unsupported on the device '%s' ('%s')", disk,
                     label_name);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    part_num = get_part_num (part, &l_error);
    if (part_num == -1) {
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    /* /dev/sda1 is the partition number 0 in libfdisk */
    part_num--;

    status = fdisk_get_partition (cxt, part_num, &pa);
    if (status != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition '%s' on device '%s': %s",
                     part, disk, strerror_l (-status, c_locale));
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    status = fdisk_partition_set_uuid (pa, uuid);
    if (status != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set UUID on the partition '%s' on device '%s': %s",
                     part, disk, strerror_l (-status, c_locale));
        fdisk_unref_partition (pa);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    status = fdisk_set_partition (cxt, part_num, pa);
    if (status != 0) {
        g_set_error (&l_error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set UUID on the partition '%s' on device '%s': %s",
                     part, disk, strerror_l (-status, c_locale));
        fdisk_unref_partition (pa);
        close_context (cxt);
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        return FALSE;
    }

    fdisk_unref_partition (pa);

    if (!write_label (cxt, NULL, disk, FALSE, &l_error)) {
        bd_utils_report_finished (progress_id, l_error->message);
        g_propagate_error (error, l_error);
        close_context (cxt);
        return FALSE;
    }

    close_context (cxt);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

/**
 * bd_part_set_part_bootable:
 * @disk: device the partition belongs to
 * @part: partition the bootable flag should be set for
 * @bootable: whether to set or unset the bootable flag
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @bootable flag was successfully set for @part or not
 *
 * Tech category: %BD_PART_TECH_MBR-%BD_PART_TECH_MODE_MODIFY_PART
 */
gboolean bd_part_set_part_bootable (const gchar *disk, const gchar *part, gboolean bootable, GError **error) {
    struct fdisk_context *cxt = NULL;
    gint part_num = 0;
    struct fdisk_partition *pa = NULL;
    gint ret = 0;

    part_num = get_part_num (part, error);
    if (part_num == -1)
        return FALSE;

    /* /dev/sda1 is the partition number 0 in libfdisk */
    part_num--;

    cxt = get_device_context (disk, FALSE, error);
    if (!cxt)
        return FALSE;

    ret = fdisk_get_partition (cxt, part_num, &pa);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition '%d'.", part_num);
        close_context (cxt);
        return FALSE;
    }

    ret = fdisk_partition_is_bootable (pa);
    if ((ret == 1 && bootable) || (ret != 1 && !bootable)) {
        /* boot flag is already set as desired, no change needed */
        fdisk_unref_partition (pa);
        close_context (cxt);
        return TRUE;
    }

    ret = fdisk_toggle_partition_flag (cxt, part_num, DOS_FLAG_ACTIVE);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set partition bootable flag: %s", strerror_l (-ret, c_locale));
        fdisk_unref_partition (pa);
        close_context (cxt);
        return FALSE;
    }

    if (!write_label (cxt, NULL, disk, FALSE, error)) {
        fdisk_unref_partition (pa);
        close_context (cxt);
        return FALSE;
    }

    fdisk_unref_partition (pa);
    close_context (cxt);

    return TRUE;
}

/**
 * bd_part_set_part_attributes:
 * @disk: device the partition belongs to
 * @part: partition the attributes should be set for
 * @attrs: GPT attributes to set on @part
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: whether the @attrs GPT attributes were successfully set for @part or not
 *
 * Tech category: %BD_PART_TECH_GPT-%BD_PART_TECH_MODE_MODIFY_PART
 */
gboolean bd_part_set_part_attributes (const gchar *disk, const gchar *part, guint64 attrs, GError **error) {
    struct fdisk_context *cxt = NULL;
    gint part_num = 0;
    gint ret = 0;

    part_num = get_part_num (part, error);
    if (part_num == -1)
        return FALSE;

    /* /dev/sda1 is the partition number 0 in libfdisk */
    part_num--;

    cxt = get_device_context (disk, FALSE, error);
    if (!cxt)
        return FALSE;

    ret = fdisk_gpt_set_partition_attrs (cxt, part_num, attrs);
    if (ret < 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set GPT attributes: %s", strerror_l (-ret, c_locale));
        return FALSE;
    }

    if (!write_label (cxt, NULL, disk, FALSE, error)) {
        close_context (cxt);
        return FALSE;
    }

    close_context (cxt);

    return TRUE;
}

/**
 * bd_part_get_part_table_type_str:
 * @type: table type to get string representation for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer none): string representation of @table_type
 *
 * Tech category: the tech according to @type
 */
const gchar* bd_part_get_part_table_type_str (BDPartTableType type, GError **error) {
    if (type >= BD_PART_TABLE_UNDEF) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition table type given");
        return NULL;
    }

    return table_type_str[type];
}

/* string for BD_PART_TYPE_PROTECTED is "primary", that's what parted returns... */
static const gchar* const part_types[6] = { "primary", "logical", "extended", "free", "metadata", "primary" };

/**
 * bd_part_get_type_str:
 * @type: type to get string representation for
 * @error: (out) (optional): place to store error (if any)
 *
 * Returns: (transfer none): string representation of @type
 *
 * Tech category: always available
 */
const gchar* bd_part_get_type_str (BDPartType type, GError **error) {
    if (type > BD_PART_TYPE_PROTECTED) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL, "Invalid partition type given");
        return NULL;
    }

    return part_types[log2i (type) + 1];
}
