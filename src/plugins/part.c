/*
 * Copyright (C) 2016  Red Hat, Inc.
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
#include <parted/parted.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <sizes.h>

#include "part.h"

/**
 * SECTION: part
 * @short_description: plugin for operations with partition tables
 * @title: Part
 * @include: part.h
 *
 * A plugin for operations with partition tables.
 */

/**
 * bd_part_error_quark: (skip)
 */
GQuark bd_part_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-part-error-quark");
}

BDPartSpec* bd_part_spec_copy (BDPartSpec *data) {
    BDPartSpec *ret = g_new0 (BDPartSpec, 1);

    ret->path = g_strdup (data->path);
    ret->type = data->type;
    ret->start = data->start;
    ret->size = data->size;

    return ret;
}

void bd_part_spec_free (BDPartSpec *data) {
    g_free (data->path);
    g_free (data);
}

/* in order to be thread-safe, we need to make sure every thread has this
   variable for its own use */
static __thread gchar *error_msg = NULL;

static PedExceptionOption exc_handler (PedException *ex) {
    error_msg = g_strdup (ex->message);
    return PED_EXCEPTION_UNHANDLED;
}

/**
 * set_parted_error: (skip)
 *
 * Set error from the parted error stored in 'error_msg'. In case there is none,
 * the error is set up with an empty string. Otherwise it is set up with the
 * parted's error message and is a subject to later g_prefix_error() call.
 */
static void set_parted_error (GError **error, BDPartError type) {
    if (error_msg) {
        g_set_error (error, BD_PART_ERROR, type,
                     " (%s)", error_msg);
        g_free (error_msg);
    } else
        g_set_error_literal (error, BD_PART_ERROR, type, "");
}

/**
 * init: (skip)
 */
gboolean init() {
    ped_exception_set_handler ((PedExceptionHandler*) exc_handler);
    return TRUE;
}

static const gchar *table_type_str[BD_PART_TABLE_UNDEF] = {"msdos", "gpt"};

static gboolean disk_commit (PedDisk *disk, gchar *path, GError **error) {
    gint ret = 0;

    ret = ped_disk_commit_to_dev (disk);
    if (ret == 0) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to commit changes to device '%s'", path);
        return FALSE;
    }

    ret = ped_disk_commit_to_os (disk);
    if (ret == 0) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to inform OS about changes on the '%s' device", path);
        return FALSE;
    }

    return TRUE;
}

/**
 * bd_part_create_table:
 * @disk: path of the disk block device to create partition table on
 * @type: type of the partition table to create
 * @ignore_existing: whether to ignore/overwrite the existing table or not
 *                   (reports an error if %FALSE and there's some table on @disk)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the partition table was successfully created or not
 */
gboolean bd_part_create_table (gchar *disk, BDPartTableType type, gboolean ignore_existing, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedDiskType *disk_type = NULL;
    gboolean ret = FALSE;

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        return FALSE;
    }

    if (!ignore_existing) {
        ped_disk = ped_disk_new (dev);
        if (ped_disk) {
            /* no parted error */
            g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_EXISTS,
                         "Device '%s' already contains a partition table", disk);
            ped_disk_destroy (ped_disk);
            ped_device_destroy (dev);
            return FALSE;
        }
    }

    disk_type = ped_disk_type_get (table_type_str[type]);
    ped_disk = ped_disk_new_fresh (dev, disk_type);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to create a new partition table of type '%s' on device '%s'",
                        table_type_str[type], disk);
        ped_device_destroy (dev);
        return FALSE;
    }

    /* commit changes to disk */
    ret = disk_commit (ped_disk, disk, error);

    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    /* just return what we got (error may be set) */
    return ret;
}


static BDPartSpec* get_part_spec (PedDevice *dev, PedPartition *part) {
    BDPartSpec *ret = NULL;
    PedPartitionFlag flag = PED_PARTITION_FIRST_FLAG;

    ret = g_new0 (BDPartSpec, 1);
    if (isdigit (dev->path[strlen(dev->path) - 1]))
        ret->path = g_strdup_printf ("%sp%d", dev->path, part->num);
    else
        ret->path = g_strdup_printf ("%s%d", dev->path, part->num);
    ret->type = (BDPartType) part->type;
    ret->start = part->geom.start * dev->sector_size;
    ret->size = part->geom.length * dev->sector_size;
    for (flag=PED_PARTITION_FIRST_FLAG; flag<PED_PARTITION_LAST_FLAG; flag=ped_partition_flag_next (flag)) {
        /* beware of partition types that segfault when asked for flags */
        if ((part->type <= PED_PARTITION_EXTENDED) &&
            ped_partition_is_flag_available (part, flag) && ped_partition_get_flag (part, flag))
            /* our flags are 1s shifted to the bit determined by parted's flags
             * (i.e. 1 << 3 instead of 3, etc.) */
            ret->flags = ret->flags | (1 << flag);
    }

    return ret;
}

/**
 * bd_part_get_part_spec:
 * @disk: disk to remove the partition from
 * @part: partition to remove
 * @error: (out): place to store error (if any)
 *
 * Returns: spec of the @part partition from @disk or %NULL in case of error
 */
BDPartSpec* bd_part_get_part_spec (gchar *disk, gchar *part, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    gchar *part_num_str = NULL;
    gint part_num = 0;
    BDPartSpec *ret = NULL;

    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        return FALSE;
    }

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        return FALSE;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        return FALSE;
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
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return FALSE;
    }

    ped_part = ped_disk_get_partition (ped_disk, part_num);
    if (!ped_part) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to get partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return FALSE;
    }

    ret = get_part_spec (dev, ped_part);

    /* the partition gets destroyed together with the disk */
    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    return ret;
}

/**
 * bd_part_get_disk_parts:
 * @disk: disk to get information about partitions for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full) (array zero-terminated=1): specs of the partitions from @disk or %NULL in case of error
 */
BDPartSpec** bd_part_get_disk_parts (gchar *disk, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    guint num_parts = 0;
    BDPartSpec **ret = NULL;
    guint i = 0;

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        return FALSE;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        return FALSE;
    }

    /* count the partitions we care about (ignoring FREESPACE, METADATA and PROTECTED */
    ped_part = ped_disk_next_partition (ped_disk, NULL);
    while (ped_part) {
        if (ped_part->type <= PED_PARTITION_EXTENDED)
            num_parts++;
        ped_part = ped_disk_next_partition (ped_disk, ped_part);
    }

    ret = g_new0 (BDPartSpec*, num_parts + 1);
    i = 0;
    ped_part = ped_disk_next_partition (ped_disk, NULL);
    while (ped_part) {
        /* only include partitions we care about */
        if (ped_part->type <= PED_PARTITION_EXTENDED)
            ret[i++] = get_part_spec (dev, ped_part);
        ped_part = ped_disk_next_partition (ped_disk, ped_part);
    }
    ret[i] = NULL;

    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    return ret;
}

static PedPartition* add_part_to_disk (PedDevice *dev, PedDisk *disk, BDPartTypeReq type, guint64 start, guint64 size, BDPartAlign align, GError **error) {
    PedPartition *part = NULL;
    PedConstraint *constr = NULL;
    PedGeometry *geom;
    gint orig_flag_state = 0;
    gint status = 0;

    /* convert start to sectors */
    start = (start + (guint64)dev->sector_size - 1) / dev->sector_size;
    if (size == 0) {
        constr = ped_device_get_constraint (dev);
        size = constr->max_size - 1;
        ped_constraint_destroy (constr);
        constr = NULL;
    } else
        size = size / dev->sector_size;

    if (align == BD_PART_ALIGN_OPTIMAL) {
        /* cylinder alignment does really weird things when turned on, let's not
           deal with it in 21st century (the flag is reset back in the end) */
        if (ped_disk_is_flag_available (disk, PED_DISK_CYLINDER_ALIGNMENT)) {
            orig_flag_state = ped_disk_get_flag (disk, PED_DISK_CYLINDER_ALIGNMENT);
            ped_disk_set_flag (disk, PED_DISK_CYLINDER_ALIGNMENT, 0);
        }
        constr = ped_device_get_optimal_aligned_constraint (dev);
    } else if (align == BD_PART_ALIGN_MINIMAL)
        constr = ped_device_get_minimal_aligned_constraint (dev);

    if (constr)
        start = ped_alignment_align_up (constr->start_align, constr->start_range, (PedSector) start);

    geom = ped_geometry_new (dev, (PedSector) start, (PedSector) size);
    if (!geom) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to create geometry for a new partition on device '%s'", dev->path);
        if (constr)
            ped_constraint_destroy (constr);
        return NULL;
    }

    if (!constr)
        constr = ped_constraint_exact (geom);

    part = ped_partition_new (disk, type, NULL, geom->start, geom->end);
    if (!part) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to create new partition on device '%s'", dev->path);
        ped_constraint_destroy (constr);
        ped_geometry_destroy (geom);
        return NULL;
    }

    status = ped_disk_add_partition (disk, part, constr);
    if (status == 0) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed add partition to device '%s'", dev->path);
        ped_constraint_destroy (constr);
        ped_partition_destroy (part);
        ped_geometry_destroy (geom);
        return NULL;
    }

    if (ped_disk_is_flag_available (disk, PED_DISK_CYLINDER_ALIGNMENT)) {
        orig_flag_state = ped_disk_get_flag (disk, PED_DISK_CYLINDER_ALIGNMENT);
        ped_disk_set_flag (disk, PED_DISK_CYLINDER_ALIGNMENT, orig_flag_state);
    }

    ped_geometry_destroy (geom);
    ped_constraint_destroy (constr);

    return part;
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
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): specification of the created partition or %NULL in case of error
 *
 * NOTE: The resulting partition may start at a different position than given by
 *       @start and can have different size than @size due to alignment.
 */
BDPartSpec* bd_part_create_part (gchar *disk, BDPartTypeReq type, guint64 start, guint64 size, BDPartAlign align, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    PedPartition *ext_part = NULL;
    PedSector start_sector = 0;
    gint last_num = 0;
    gboolean succ = FALSE;
    BDPartSpec *ret = NULL;

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        return NULL;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        return NULL;
    }

    if (type == BD_PART_TYPE_REQ_NEXT) {
        ext_part = ped_disk_extended_partition (ped_disk);
        start_sector = (PedSector) (start + dev->sector_size - 1) / dev->sector_size;
        if (ext_part && (start_sector > ext_part->geom.start) && (start_sector < ext_part->geom.end)) {
            /* partition's start is in the extended partition -> must be logical */
            type = BD_PART_TYPE_REQ_LOGICAL;
        } else if ((ped_disk_get_max_primary_partition_count (ped_disk) - 1 > ped_disk_get_primary_partition_count (ped_disk)) || ext_part) {
            /* we have room for another primary partition or there already is an extended partition -> should/must be primary */
            type = BD_PART_TYPE_REQ_NORMAL;
        } else {
            ped_part = add_part_to_disk (dev, ped_disk, BD_PART_TYPE_REQ_EXTENDED, start, 0, align, error);
            if (!ped_part) {
                /* error is already populated */
                ped_disk_destroy (ped_disk);
                ped_device_destroy (dev);
                return NULL;
            }
            type = BD_PART_TYPE_REQ_LOGICAL;
        }
    }

    if (type == BD_PART_TYPE_REQ_LOGICAL) {
        /* Find the previous logical partition (if there's any) because we need
           its end. If there's no such logical partition, we are creating the
           first one and thus should only care about the extended partition's
           start*/
        last_num = ped_disk_get_last_partition_num (ped_disk);
        ped_part = ped_disk_get_partition (ped_disk, last_num);
        while (ped_part && (ped_part->type != PED_PARTITION_EXTENDED) &&
               (ped_part->geom.start > (PedSector) (start / dev->sector_size)))
            ped_part = ped_part->prev;

        if (ped_part->type == PED_PARTITION_EXTENDED) {
            /* can at minimal start where the first logical partition can start - the start of the extended partition + 1 MiB aligned up */
            if (start < ((ped_part->geom.start * dev->sector_size) + 1 MiB + dev->sector_size - 1))
                start = (ped_part->geom.start * dev->sector_size) + 1 MiB + dev->sector_size - 1;
        } else {
            /* can at minimal start where the next logical partition can start - the end of the previous partition + 1 MiB aligned up */
            if (start < ((ped_part->geom.end * dev->sector_size) + 1 MiB + dev->sector_size - 1))
                start = (ped_part->geom.end * dev->sector_size) + 1 MiB + dev->sector_size - 1;
        }
    }

    ped_part = add_part_to_disk (dev, ped_disk, type, start, size, align, error);
    if (!ped_part) {
        /* error is already populated */
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return NULL;
    }

    succ = disk_commit (ped_disk, disk, error);
    if (succ)
        ret = get_part_spec (dev, ped_part);

    /* the partition gets destroyed together with the disk*/
    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    return ret;
}

/**
 * bd_part_delete_part:
 * @disk: disk to remove the partition from
 * @part: partition to remove
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @part partition was successfully deleted from @disk
 */
gboolean bd_part_delete_part (gchar *disk, gchar *part, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    gchar *part_num_str = NULL;
    gint part_num = 0;
    gint status = 0;
    gboolean ret = FALSE;

    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        return FALSE;
    }

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        return FALSE;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        return FALSE;
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
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return FALSE;
    }

    ped_part = ped_disk_get_partition (ped_disk, part_num);
    if (!ped_part) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to get partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return FALSE;
    }

    status = ped_disk_delete_partition (ped_disk, ped_part);
    if (status == 0) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to get partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return FALSE;
    }

    ret = disk_commit (ped_disk, disk, error);

    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    return ret;
}

/**
 * bd_part_set_part_flag:
 * @disk: disk the partition belongs to
 * @part: partition to set the flag on
 * @flag: flag to set
 * @state: state to set for the @flag (%TRUE = enabled)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the flag @flag was successfully set on the @part partition
 * or not.
 */
gboolean bd_part_set_part_flag (gchar *disk, gchar *part, BDPartFlag flag, gboolean state, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    PedPartitionFlag ped_flag = PED_PARTITION_FIRST_FLAG;
    gchar *part_num_str = NULL;
    gint part_num = 0;
    gint status = 0;
    gboolean ret = FALSE;

    /* TODO: share this code with the other functions modifying a partition */
    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        return FALSE;
    }

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        return FALSE;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        return FALSE;
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
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return FALSE;
    }

    ped_part = ped_disk_get_partition (ped_disk, part_num);
    if (!ped_part) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to get partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return FALSE;
    }

    /* our flags are 1s shifted to the bit determined by parted's flags
     * (i.e. 1 << 3 instead of 3, etc.) */
    ped_flag = (PedPartitionFlag) log2 ((double) flag);
    status = ped_partition_set_flag (ped_part, ped_flag, (int) state);
    if (status == 0) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to get partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return FALSE;
    }

    ret = disk_commit (ped_disk, disk, error);

    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    return ret;
}

/**
 * bd_part_set_part_flags:
 * @disk: disk the partition belongs to
 * @part: partition to set the flag on
 * @flags: flags to set (mask combined from #BDPartFlag numbers)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @flags were successfully set on the @part partition or
 *          not
 *
 * Note: Unsets all the other flags on the partition.
 *
 */
gboolean bd_part_set_part_flags (gchar *disk, gchar *part, guint64 flags, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    const gchar *part_num_str = NULL;
    gint part_num = 0;
    guint64 i = 0;
    gint status = 0;
    gboolean ret = FALSE;

    /* TODO: share this code with the other functions modifying a partition */
    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        return FALSE;
    }

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        return FALSE;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        return FALSE;
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
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return FALSE;
    }

    ped_part = ped_disk_get_partition (ped_disk, part_num);
    if (!ped_part) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to get partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return FALSE;
    }

    /* our flags are 1s shifted to the bit determined by parted's flags
     * (i.e. 1 << 3 instead of 3, etc.) */
    for (i=1; i <= (int) log2 ((double)BD_PART_FLAG_BASIC_LAST); i++) {
        if ((1 << i) & flags)
            status = ped_partition_set_flag (ped_part, (PedPartitionFlag) i, (int) 1);
        else if (ped_partition_is_flag_available (ped_part, (PedPartitionFlag) i))
            status = ped_partition_set_flag (ped_part, (PedPartitionFlag) i, (int) 0);
        if (status == 0) {
            set_parted_error (error, BD_PART_ERROR_FAIL);
            g_prefix_error (error, "Failed to set flag on the partition '%d' on device '%s'", part_num, disk);
            ped_disk_destroy (ped_disk);
            ped_device_destroy (dev);
            return FALSE;
        }
    }

    ret = disk_commit (ped_disk, disk, error);

    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    return ret;
}

/**
 * bd_part_get_part_table_type_str:
 * @type: table type to get string representation for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer none): string representation of @table_type
 */
const gchar* bd_part_get_part_table_type_str (BDPartTableType type, GError **error) {
    if (type >= BD_PART_TABLE_UNDEF) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition table type given");
        return NULL;
    }

    return table_type_str[type];
}

/**
 * bd_part_get_flag_str:
 * @flag: flag to get string representation for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer none): string representation of @flag
 */
const gchar* bd_part_get_flag_str (BDPartFlag flag, GError **error) {
    if (flag > BD_PART_FLAG_ESP) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL, "Invalid flag given");
        return NULL;
    }

    return ped_partition_flag_get_name ((PedPartitionFlag) log2 ((double) flag));
}

/**
 * bd_part_get_type_str:
 * @type: type to get string representation for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer none): string representation of @type
 */
const gchar* bd_part_get_type_str (BDPartType type, GError **error) {
    if (type > BD_PART_TYPE_PROTECTED) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL, "Invalid partition type given");
        return NULL;
    }

    return ped_partition_type_get_name ((PedPartitionType) type);
}
