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

#include <string.h>
#include <parted/parted.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/file.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <blockdev/utils.h>
#include <part_err.h>

#include "part.h"
#include "check_deps.h"

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
 * This particular implementation of the part plugin uses libparted for
 * manipulations of both the MBR and GPT disk label types together with the
 * sgdisk utility for some extra GPT-specific features libparted doesn't
 * support. In the future, there's likely to be another implementation of this
 * plugin based on libfdisk which provides full support for both MBR and GPT
 * tables (and possibly some others).
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
    ret->type_guid = g_strdup (data->type_guid);
    ret->type = data->type;
    ret->start = data->start;
    ret->size = data->size;

    return ret;
}

void bd_part_spec_free (BDPartSpec *data) {
    if (data == NULL)
        return;

    g_free (data->path);
    g_free (data->name);
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
    ret->flags = data->flags;

    return ret;
}

void bd_part_disk_spec_free (BDPartDiskSpec *data) {
    if (data == NULL)
        return;

    g_free (data->path);
    g_free (data);
}

/**
 * set_parted_error: (skip)
 *
 * Set error from the parted error stored in 'error_msg'. In case there is none,
 * the error is set up with an empty string. Otherwise it is set up with the
 * parted's error message and is a subject to later g_prefix_error() call.
 *
 * Returns: whether there was some message from parted or not
 */
static gboolean set_parted_error (GError **error, BDPartError type) {
    gchar *error_msg = NULL;
    error_msg = bd_get_error_msg ();
    if (error_msg) {
        g_set_error (error, BD_PART_ERROR, type,
                     " (%s)", error_msg);
        g_free (error_msg);
        error_msg = NULL;
        return TRUE;
    } else {
        g_set_error_literal (error, BD_PART_ERROR, type, "");
        return FALSE;
    }
}


static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_SGDISK 0
#define DEPS_SGDISK_MASK (1 << DEPS_SGDISK)
#define DEPS_SFDISK 1
#define DEPS_SFDISK_MASK (1 << DEPS_SFDISK)
#define DEPS_LAST 2

static const UtilDep deps[DEPS_LAST] = {
    {"sgdisk", "0.8.6", NULL, "GPT fdisk \\(sgdisk\\) version ([\\d\\.]+)"},
    {"sfdisk", NULL, NULL, NULL},
};


/**
 * bd_part_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_part_check_deps (void) {
    GError *error = NULL;
    guint i = 0;
    gboolean status = FALSE;
    gboolean ret = TRUE;

    for (i=0; i < DEPS_LAST; i++) {
        status = bd_utils_check_util_version (deps[i].name, deps[i].version,
                                              deps[i].ver_arg, deps[i].ver_regexp, &error);
        if (!status)
            g_warning ("%s", error->message);
        else
            g_atomic_int_or (&avail_deps, 1 << i);
        g_clear_error (&error);
        ret = ret && status;
    }

    if (!ret)
        g_warning("Cannot load the part plugin");

    return ret;
}

/**
 * bd_part_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_part_init (void) {
    ped_exception_set_handler ((PedExceptionHandler*) bd_exc_handler);
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
    ped_exception_set_handler (NULL);
}

/**
 * bd_part_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDPartTechMode) for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_part_is_tech_avail (BDPartTech tech, guint64 mode, GError **error) {
    switch (tech) {
    case BD_PART_TECH_MBR:
        /* all MBR-mode combinations are supported by this implementation of the
         * plugin, nothing extra is needed */
        return TRUE;
    case BD_PART_TECH_GPT:
        if (mode & (BD_PART_TECH_MODE_MODIFY_PART|BD_PART_TECH_MODE_QUERY_PART))
            return check_deps (&avail_deps, DEPS_SGDISK_MASK|DEPS_SFDISK_MASK,
                               deps, DEPS_LAST, &deps_check_lock, error);
        else
            return TRUE;
    default:
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_TECH_UNAVAIL, "Unknown technology");
        return FALSE;
    }
}

static const gchar *table_type_str[BD_PART_TABLE_UNDEF] = {"msdos", "gpt"};

static gboolean disk_commit (PedDisk *disk, const gchar *path, GError **error) {
    gint ret = 0;
    gint dev_fd = 0;
    guint num_tries = 1;

    /* XXX: try to grab a lock for the device so that udev doesn't step in
       between the two operations we need to perform (see below) with its
       BLKRRPART ioctl() call which makes the device busy */
    dev_fd = open (disk->dev->path, O_RDONLY|O_CLOEXEC);
    if (dev_fd >= 0) {
        ret = flock (dev_fd, LOCK_SH|LOCK_NB);
        while ((ret != 0) && (num_tries <= 5)) {
            g_usleep (100 * 1000); /* microseconds */
            ret = flock (dev_fd, LOCK_SH|LOCK_NB);
            num_tries++;
        }
    }
    /* Just continue even in case we don't get the lock, there's still a
       chance things will just work. If not, an error will be reported
       anyway with no harm. */

    /* XXX: Sometimes it happens that when we try to commit the partition table
       to disk below, libparted kills the process due to the
       assert(disk->dev->open_count > 0). This looks like a bug to me, but we
       have no reproducer for it. Let's just try to (re)open the device in such
       cases. It is later closed by the ped_device_destroy() call. */
    if (disk->dev->open_count <= 0)
        ped_device_open (disk->dev);

    ret = ped_disk_commit_to_dev (disk);
    if (ret == 0) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to commit changes to device '%s'", path);
        if (dev_fd >= 0)
            close (dev_fd);
        return FALSE;
    }

    ret = ped_disk_commit_to_os (disk);
    if (ret == 0) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to inform OS about changes on the '%s' device", path);
        if (dev_fd >= 0)
            close (dev_fd);
        return FALSE;
    }

    if (dev_fd >= 0)
        close (dev_fd);
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
 *
 * Tech category: %BD_PART_TECH_MODE_CREATE_TABLE + the tech according to @type
 */
gboolean bd_part_create_table (const gchar *disk, BDPartTableType type, gboolean ignore_existing, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedDiskType *disk_type = NULL;
    gboolean ret = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Starting creation of a new partition table on '%s'", disk);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        bd_utils_report_finished (progress_id, (*error)->message);
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
            bd_utils_report_finished (progress_id, (*error)->message);
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
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* commit changes to disk */
    ret = disk_commit (ped_disk, disk, error);

    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    bd_utils_report_finished (progress_id, "Completed");

    /* just return what we got (error may be set) */
    return ret;
}

static gchar* get_part_type_guid_and_gpt_flags (const gchar *device, int part_num, guint64 *flags, GError **error) {
    const gchar *args[4] = {"sgdisk", NULL, device, NULL};
    gchar *output = NULL;
    gchar **lines = NULL;
    gchar **line_p = NULL;
    gchar *guid_line = NULL;
    gchar *attrs_line = NULL;
    gchar *guid_start = NULL;
    gchar *attrs_start = NULL;
    guint64 flags_mask = 0;
    gboolean success = FALSE;
    gchar *space = NULL;
    gchar *ret = NULL;

    if (!check_deps (&avail_deps, DEPS_SGDISK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    args[1] = g_strdup_printf ("-i%d", part_num);
    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    g_free ((gchar *) args[1]);
    if (!success)
        return FALSE;

    lines = g_strsplit (output, "\n", 0);
    g_free (output);
    for (line_p=lines; *line_p && (!guid_line || !attrs_line); line_p++) {
        if (g_str_has_prefix (*line_p, "Partition GUID code: "))
            guid_line = *line_p;
        else if (g_str_has_prefix (*line_p, "Attribute flags: "))
            attrs_line = *line_p;
    }
    if (!guid_line && !attrs_line) {
        g_strfreev (lines);
        return NULL;
    }

    if (guid_line) {
        guid_start = guid_line + 21; /* strlen("Partition GUID...") */
        space = strchr (guid_start, ' '); /* find the first space after the GUID */
        if (space)
            *space = '\0';
        ret = g_strdup (guid_start);
    }

    if (attrs_line) {
        attrs_start = attrs_line + 17; /* strlen("Attribute flags: ") */
        flags_mask = strtoull (attrs_start, NULL, 16);

        if (flags_mask & 1) /* 1 << 0 */
            *flags |= BD_PART_FLAG_GPT_SYSTEM_PART;
        if (flags_mask & 4) /* 1 << 2 */
            *flags |= BD_PART_FLAG_LEGACY_BOOT;
        if (flags_mask & 0x1000000000000000) /* 1 << 60 */
            *flags |= BD_PART_FLAG_GPT_READ_ONLY;
        if (flags_mask & 0x4000000000000000) /* 1 << 62 */
            *flags |= BD_PART_FLAG_GPT_HIDDEN;
        if (flags_mask & 0x8000000000000000) /* 1 << 63 */
            *flags |= BD_PART_FLAG_GPT_NO_AUTOMOUNT;
    }

    g_strfreev (lines);
    return ret;
}

static BDPartSpec* get_part_spec (PedDevice *dev, PedDisk *disk, PedPartition *part, GError **error) {
    BDPartSpec *ret = NULL;
    PedPartitionFlag flag = PED_PARTITION_FIRST_FLAG;

    ret = g_new0 (BDPartSpec, 1);
    /* the no-partition "partitions" have num equal to -1 which never really
       creates a valid block device path, so let's just not set path to
       nonsense */
    if (part->num != -1) {
        if (isdigit (dev->path[strlen(dev->path) - 1]))
            ret->path = g_strdup_printf ("%sp%d", dev->path, part->num);
        else
            ret->path = g_strdup_printf ("%s%d", dev->path, part->num);
    }
    if (ped_partition_is_active (part) && disk->type->features & PED_DISK_TYPE_PARTITION_NAME)
        ret->name = g_strdup (ped_partition_get_name (part));
    if (g_strcmp0 (disk->type->name, "gpt") == 0) {
        ret->type_guid = get_part_type_guid_and_gpt_flags (dev->path, part->num, &(ret->flags), error);
        if (!ret->type_guid && *error) {
            bd_part_spec_free (ret);
            return NULL;
        }
    }
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
 * @part: partition to get spec for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): spec of the @part partition from @disk or %NULL in case of error
 *
 * Tech category: %BD_PART_TECH_MODE_QUERY_PART + the tech according to the partition table type
 */
BDPartSpec* bd_part_get_part_spec (const gchar *disk, const gchar *part, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    const gchar *part_num_str = NULL;
    gint part_num = 0;
    BDPartSpec *ret = NULL;

    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        return NULL;
    }

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
        return NULL;
    }

    ped_part = ped_disk_get_partition (ped_disk, part_num);
    if (!ped_part) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to get partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return NULL;
    }

    ret = get_part_spec (dev, ped_disk, ped_part, error);

    /* the partition gets destroyed together with the disk */
    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    return ret;
}

/**
 * bd_part_get_part_by_pos:
 * @disk: disk to remove the partition from
 * @position: position (in bytes) determining the partition
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): spec of the partition from @disk spanning over the @position or %NULL if no such
 *          partition exists or in case of error (@error is set)
 *
 * Tech category: %BD_PART_TECH_MODE_QUERY_PART + the tech according to the partition table type
 */
BDPartSpec* bd_part_get_part_by_pos (const gchar *disk, guint64 position, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    BDPartSpec *ret = NULL;
    PedSector sector = 0;

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

    sector = (PedSector) (position / dev->sector_size);
    ped_part = ped_disk_get_partition_by_sector (ped_disk, sector);
    if (!ped_part) {
        if (set_parted_error (error, BD_PART_ERROR_FAIL))
            g_prefix_error (error, "Failed to get partition at position %"G_GUINT64_FORMAT" (device '%s')",
                            position, disk);
        else
            /* no such partition, but no error */
            g_clear_error (error);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        return NULL;
    }

    ret = get_part_spec (dev, ped_disk, ped_part, error);

    /* the partition gets destroyed together with the disk */
    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    return ret;
}

/**
 * bd_part_get_disk_spec:
 * @disk: disk to get information about
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full): information about the given @disk or %NULL (in case of error)
 *
 * Tech category: %BD_PART_TECH_MODE_QUERY_TABLE + the tech according to the partition table type
 */
BDPartDiskSpec* bd_part_get_disk_spec (const gchar *disk, GError **error) {
    PedDevice *dev = NULL;
    BDPartDiskSpec *ret = NULL;
    PedConstraint *constr = NULL;
    PedDisk *ped_disk = NULL;
    BDPartTableType type = BD_PART_TABLE_UNDEF;
    gboolean found = FALSE;

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        return NULL;
    }

    ret = g_new0 (BDPartDiskSpec, 1);
    ret->path = g_strdup (dev->path);
    ret->sector_size = (guint64) dev->sector_size;
    constr = ped_device_get_constraint (dev);
    ret->size = (constr->max_size - 1) * dev->sector_size;
    ped_constraint_destroy (constr);

    ped_disk = ped_disk_new (dev);
    if (ped_disk) {
        for (type=BD_PART_TABLE_MSDOS; !found && type < BD_PART_TABLE_UNDEF; type++) {
            if (g_strcmp0 (ped_disk->type->name, table_type_str[type]) == 0) {
                ret->table_type = type;
                found = TRUE;
            }
        }
        if (!found)
            ret->table_type = BD_PART_TABLE_UNDEF;
        if (ped_disk_is_flag_available (ped_disk, PED_DISK_GPT_PMBR_BOOT) &&
            ped_disk_get_flag (ped_disk, PED_DISK_GPT_PMBR_BOOT))
            ret->flags = BD_PART_DISK_FLAG_GPT_PMBR_BOOT;
        ped_disk_destroy (ped_disk);
    } else {
        ret->table_type = BD_PART_TABLE_UNDEF;
        ret->flags = 0;
    }

    ped_device_destroy (dev);

    return ret;
}

static BDPartSpec** get_disk_parts (const gchar *disk, guint64 incl, guint64 excl, gboolean incl_normal, GError **error) {
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
        return NULL;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        return NULL;
    }

    /* count the partitions we care about */
    ped_part = ped_disk_next_partition (ped_disk, NULL);
    while (ped_part) {
        if (((ped_part->type & incl) && !(ped_part->type & excl)) ||
            ((ped_part->type == 0) && incl_normal))
            num_parts++;
        ped_part = ped_disk_next_partition (ped_disk, ped_part);
    }

    ret = g_new0 (BDPartSpec*, num_parts + 1);
    i = 0;
    ped_part = ped_disk_next_partition (ped_disk, NULL);
    while (ped_part) {
        if (((ped_part->type & incl) && !(ped_part->type & excl)) ||
            ((ped_part->type == 0) && incl_normal))
            ret[i++] = get_part_spec (dev, ped_disk, ped_part, error);
        ped_part = ped_disk_next_partition (ped_disk, ped_part);
    }
    ret[i] = NULL;

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
 *
 * Tech category: %BD_PART_TECH_MODE_QUERY_TABLE + the tech according to the partition table type
 */
BDPartSpec** bd_part_get_disk_parts (const gchar *disk, GError **error) {
    return get_disk_parts (disk, BD_PART_TYPE_NORMAL|BD_PART_TYPE_LOGICAL|BD_PART_TYPE_EXTENDED,
                           BD_PART_TYPE_FREESPACE|BD_PART_TYPE_METADATA|BD_PART_TYPE_PROTECTED, TRUE, error);
}

/**
 * bd_part_get_disk_free_regions:
 * @disk: disk to get free regions for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer full) (array zero-terminated=1): specs of the free regions from @disk or %NULL in case of error
 *
 * Tech category: %BD_PART_TECH_MODE_QUERY_TABLE + the tech according to the partition table type
 */
BDPartSpec** bd_part_get_disk_free_regions (const gchar *disk, GError **error) {
    return get_disk_parts (disk, BD_PART_TYPE_FREESPACE, 0, FALSE, error);
}

/**
 * bd_part_get_best_free_region:
 * @disk: disk to get the best free region for
 * @type: type of the partition that is planned to be added
 * @size: size of the partition to be added
 * @error: (out): place to store error (if any)
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

static PedConstraint* prepare_alignment_constraint (PedDevice *dev, PedDisk *disk, BDPartAlign align, gint *orig_flag_state) {
    if (align == BD_PART_ALIGN_OPTIMAL) {
        /* cylinder alignment does really weird things when turned on, let's not
           deal with it in 21st century (the flag is reset back in the end) */
        if (ped_disk_is_flag_available (disk, PED_DISK_CYLINDER_ALIGNMENT)) {
            *orig_flag_state = ped_disk_get_flag (disk, PED_DISK_CYLINDER_ALIGNMENT);
            ped_disk_set_flag (disk, PED_DISK_CYLINDER_ALIGNMENT, 0);
        }
        return ped_device_get_optimal_aligned_constraint (dev);
    } else if (align == BD_PART_ALIGN_MINIMAL)
        return ped_device_get_minimal_aligned_constraint (dev);
    else
        return NULL;
}

static void finish_alignment_constraint (PedDisk *disk, gint orig_flag_state) {
    if (ped_disk_is_flag_available (disk, PED_DISK_CYLINDER_ALIGNMENT)) {
        ped_disk_set_flag (disk, PED_DISK_CYLINDER_ALIGNMENT, orig_flag_state);
    }
}

static gboolean resize_part (PedPartition *part, PedDevice *dev, PedDisk *disk, guint64 size, BDPartAlign align, GError **error) {
    PedConstraint *constr = NULL;
    PedGeometry *geom;
    gint orig_flag_state = 0;
    PedSector start;
    PedSector end;
    PedSector max_end;
    PedSector new_size = 0;
    gint status = 0;
    PedSector tolerance = 0;

    /* It should be possible to pass the whole drive size a partition size,
     * so -1 MiB for the first partition alignment,
     * -1 MiB for creating this here as a logial partition
     * and -1 MiB for end alingment.
     * But only if the caller doesn't request no alignment which also means
     * they strictly care about precise numbers. */
    if (align != BD_PART_ALIGN_NONE)
        tolerance = (PedSector) (4 MiB /  dev->sector_size);

    constr = prepare_alignment_constraint (dev, disk, align, &orig_flag_state);
    start = part->geom.start;

    if (!constr)
        constr = ped_constraint_any (dev);

    geom = ped_disk_get_max_partition_geometry (disk, part, constr);
    if (!ped_geometry_set_start (geom, start)) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to set partition start on device '%s'", dev->path);
        ped_constraint_destroy (constr);
        ped_geometry_destroy (geom);
        finish_alignment_constraint (disk, orig_flag_state);
        return FALSE;
    }
    if (size == 0) {
        new_size = geom->length;
    } else {
        new_size = (size + dev->sector_size - 1) / dev->sector_size;
    }

    /* If the maximum partition geometry is smaller than the requested size, but
       the difference is acceptable, just adapt the size. */
    if (new_size > geom->length && (new_size - geom->length) < tolerance)
        new_size = geom->length;

    max_end = geom->end;
    ped_geometry_destroy (geom);
    geom = ped_geometry_new (dev, start, new_size);
    if (!geom) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to create geometry for partition on device '%s'", dev->path);
        ped_constraint_destroy (constr);
        finish_alignment_constraint (disk, orig_flag_state);
        return FALSE;
    }

    if (size != 0) {
        end = ped_alignment_align_up (constr->end_align, constr->end_range, geom->end);
        if (end > max_end && end < max_end + tolerance) {
            end = max_end;
        }
    } else {
        end = geom->end;
    }
    ped_constraint_destroy (constr);
    if (!ped_geometry_set_end (geom, end)) {
       set_parted_error (error, BD_PART_ERROR_FAIL);
       g_prefix_error (error, "Failed to change geometry for partition on device '%s'", dev->path);
       ped_constraint_destroy (constr);
       ped_geometry_destroy (geom);
       finish_alignment_constraint (disk, orig_flag_state);
       return FALSE;
    }
    constr = ped_constraint_exact (geom);
    status = ped_disk_set_partition_geom (disk, part, constr, start, end);

    if (status == 0) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to set partition size on device '%s'", dev->path);
        ped_geometry_destroy (geom);
        ped_constraint_destroy (constr);
        finish_alignment_constraint (disk, orig_flag_state);
        return FALSE;
    } else if (part->geom.start != start) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL, "Failed to meet partition start on device '%s'", dev->path);
        ped_geometry_destroy (geom);
        ped_constraint_destroy (constr);
        finish_alignment_constraint (disk, orig_flag_state);
        return FALSE;
    } else if (part->geom.length < new_size - tolerance) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL, "Failed to meet partition size on device '%s'", dev->path);
        ped_geometry_destroy (geom);
        ped_constraint_destroy (constr);
        finish_alignment_constraint (disk, orig_flag_state);
        return FALSE;
    }

    ped_geometry_destroy (geom);
    ped_constraint_destroy (constr);
    finish_alignment_constraint (disk, orig_flag_state);
    return TRUE;
}

static PedPartition* add_part_to_disk (PedDevice *dev, PedDisk *disk, BDPartTypeReq type, guint64 start, guint64 size, BDPartAlign align, GError **error) {
    PedPartition *part = NULL;
    PedConstraint *constr = NULL;
    PedGeometry *geom;
    gint orig_flag_state = 0;
    gint status = 0;

    /* convert start to sectors */
    start = (start + (guint64)dev->sector_size - 1) / dev->sector_size;

    constr = prepare_alignment_constraint (dev, disk, align, &orig_flag_state);

    if (constr)
        start = ped_alignment_align_up (constr->start_align, constr->start_range, (PedSector) start);

    geom = ped_geometry_new (dev, (PedSector) start, (PedSector) 1 MiB / dev->sector_size);
    if (!geom) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to create geometry for a new partition on device '%s'", dev->path);
        ped_constraint_destroy (constr);
        finish_alignment_constraint (disk, orig_flag_state);
        return NULL;
    }

    part = ped_partition_new (disk, (PedPartitionType)type, NULL, geom->start, geom->end);
    if (!part) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to create new partition on device '%s'", dev->path);
        ped_constraint_destroy (constr);
        ped_geometry_destroy (geom);
        finish_alignment_constraint (disk, orig_flag_state);
        return NULL;
    }

    if (!constr)
        constr = ped_constraint_exact (geom);

    status = ped_disk_add_partition (disk, part, constr);
    if (status == 0) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed add partition to device '%s'", dev->path);
        ped_geometry_destroy (geom);
        ped_constraint_destroy (constr);
        ped_partition_destroy (part);
        finish_alignment_constraint (disk, orig_flag_state);
        return NULL;
    }

    if (!resize_part (part, dev, disk, size, align, error)) {
        ped_geometry_destroy (geom);
        ped_constraint_destroy (constr);
        ped_disk_delete_partition (disk, part);
        finish_alignment_constraint (disk, orig_flag_state);
        return NULL;
    }

    finish_alignment_constraint (disk, orig_flag_state);
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
 *
 * Tech category: %BD_PART_TECH_MODE_MODIFY_TABLE + the tech according to the partition table type
 */
BDPartSpec* bd_part_create_part (const gchar *disk, BDPartTypeReq type, guint64 start, guint64 size, BDPartAlign align, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    PedPartition *ext_part = NULL;
    PedSector start_sector = 0;
    gint last_num = 0;
    gboolean succ = FALSE;
    BDPartSpec *ret = NULL;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started adding partition to '%s'", disk);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        bd_utils_report_finished (progress_id, (*error)->message);
        return NULL;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
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
                bd_utils_report_finished (progress_id, (*error)->message);
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

        if (!ped_part) {
            g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                         "Failed to find suitable free region for a new logical partition.");
            ped_disk_destroy (ped_disk);
            ped_device_destroy (dev);
            bd_utils_report_finished (progress_id, (*error)->message);
            return NULL;
        }

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
        bd_utils_report_finished (progress_id, (*error)->message);
        return NULL;
    }

    succ = disk_commit (ped_disk, disk, error);
    if (succ)
        ret = get_part_spec (dev, ped_disk, ped_part, error);

    /* the partition gets destroyed together with the disk*/
    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    bd_utils_report_finished (progress_id, "Completed");

    return ret;
}

/**
 * bd_part_delete_part:
 * @disk: disk to remove the partition from
 * @part: partition to remove
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @part partition was successfully deleted from @disk
 *
 * Tech category: %BD_PART_TECH_MODE_MODIFY_TABLE + the tech according to the partition table type
 */
gboolean bd_part_delete_part (const gchar *disk, const gchar *part, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    const gchar *part_num_str = NULL;
    gint part_num = 0;
    gint status = 0;
    gboolean ret = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started deleting partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
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
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ped_part = ped_disk_get_partition (ped_disk, part_num);
    if (!ped_part) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to get partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    status = ped_disk_delete_partition (ped_disk, ped_part);
    if (status == 0) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to delete partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = disk_commit (ped_disk, disk, error);

    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    bd_utils_report_finished (progress_id, "Completed");

    return ret;
}

/**
 * bd_part_resize_part:
 * @disk: disk containing the paritition
 * @part: partition to resize
 * @size: new partition size, 0 for maximal size
 * @align: alignment to use for the partition end
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @part partition was successfully resized on @disk to @size
 *
 * NOTE: The resulting partition may be slightly bigger than requested due to alignment.
 *
 * Tech category: %BD_PART_TECH_MODE_MODIFY_TABLE + the tech according to the partition table type
 */
gboolean bd_part_resize_part (const gchar *disk, const gchar *part, guint64 size, BDPartAlign align, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    const gchar *part_num_str = NULL;
    gint part_num = 0;
    gboolean ret = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    guint64 old_size = 0;
    guint64 new_size = 0;

    msg = g_strdup_printf ("Started resizing partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
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
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ped_part = ped_disk_get_partition (ped_disk, part_num);
    if (!ped_part) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to get partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    old_size = ped_part->geom.length * dev->sector_size;
    if (!resize_part (ped_part, dev, ped_disk, size, align, error)) {
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    new_size = ped_part->geom.length * dev->sector_size;
    if (old_size != new_size) {
        gint fd = 0;
        gint wait_us = 10 * 1000 * 1000; /* 10 seconds */
        gint step_us = 100 * 1000; /* 100 microseconds */
        guint64 block_size = 0;

        ret = disk_commit (ped_disk, disk, error);
        /* wait for partition to appear with new size */
        while (wait_us > 0) {
            fd = open (part, O_RDONLY);
            if (fd != -1) {
                if (ioctl (fd, BLKGETSIZE64, &block_size) != -1 && block_size == new_size) {
                    close (fd);
                    break;
                }

                close (fd);
            }

            g_usleep (step_us);
            wait_us -= step_us;
        }
    } else {
        ret = TRUE; /* not committing to disk */
    }

    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);
    bd_utils_report_finished (progress_id, "Completed");

    return ret;
}


static gboolean set_gpt_flag (const gchar *device, int part_num, BDPartFlag flag, gboolean state, GError **error) {
    const gchar *args[5] = {"sgdisk", "--attributes", NULL, device, NULL};
    int bit_num = 0;
    gboolean success = FALSE;

    if (!check_deps (&avail_deps, DEPS_SGDISK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (flag == BD_PART_FLAG_GPT_SYSTEM_PART)
        bit_num = 0;
    else if (flag == BD_PART_FLAG_LEGACY_BOOT)
        bit_num = 2;
    else if (flag == BD_PART_FLAG_GPT_READ_ONLY)
        bit_num = 60;
    else if (flag == BD_PART_FLAG_GPT_HIDDEN)
        bit_num = 62;
    else if (flag == BD_PART_FLAG_GPT_NO_AUTOMOUNT)
        bit_num = 63;

    args[2] = g_strdup_printf ("%d:%s:%d", part_num, state ? "set" : "clear", bit_num);

    success = bd_utils_exec_and_report_error (args, NULL, error);
    g_free ((gchar *) args[2]);
    return success;
}

static gboolean set_gpt_flags (const gchar *device, int part_num, guint64 flags, GError **error) {
    const gchar *args[5] = {"sgdisk", "--attributes", NULL, device, NULL};
    guint64 real_flags = 0;
    gchar *mask_str = NULL;
    gboolean success = FALSE;

    if (!check_deps (&avail_deps, DEPS_SGDISK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (flags & BD_PART_FLAG_GPT_SYSTEM_PART)
        real_flags |=  1;       /* 1 << 0 */
    if (flags & BD_PART_FLAG_LEGACY_BOOT)
        real_flags |=  4;       /* 1 << 2 */
    if (flags & BD_PART_FLAG_GPT_READ_ONLY)
        real_flags |= 0x1000000000000000; /* 1 << 60 */
    if (flags & BD_PART_FLAG_GPT_HIDDEN)
        real_flags |= 0x4000000000000000; /* 1 << 62 */
    if (flags & BD_PART_FLAG_GPT_NO_AUTOMOUNT)
        real_flags |= 0x8000000000000000; /* 1 << 63 */
    mask_str = g_strdup_printf ("%.16"PRIx64, real_flags);

    args[2] = g_strdup_printf ("%d:=:%s", part_num, mask_str);
    g_free (mask_str);

    success = bd_utils_exec_and_report_error (args, NULL, error);
    g_free ((gchar *) args[2]);
    return success;
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
 *
 * Tech category: %BD_PART_TECH_MODE_MODIFY_PART + the tech according to the partition table type
 */
gboolean bd_part_set_part_flag (const gchar *disk, const gchar *part, BDPartFlag flag, gboolean state, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    PedPartitionFlag ped_flag = PED_PARTITION_FIRST_FLAG;
    const gchar *part_num_str = NULL;
    gint part_num = 0;
    gint status = 0;
    gboolean ret = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started setting flag on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    /* TODO: share this code with the other functions modifying a partition */
    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
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
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ped_part = ped_disk_get_partition (ped_disk, part_num);
    if (!ped_part) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to get partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* our flags are 1s shifted to the bit determined by parted's flags
     * (i.e. 1 << 3 instead of 3, etc.) */
    if (flag < BD_PART_FLAG_BASIC_LAST) {
        ped_flag = (PedPartitionFlag) log2 ((double) flag);
        status = ped_partition_set_flag (ped_part, ped_flag, (int) state);
        if (status == 0) {
            set_parted_error (error, BD_PART_ERROR_FAIL);
            g_prefix_error (error, "Failed to set flag on partition '%d' on device '%s'", part_num, disk);
            ped_disk_destroy (ped_disk);
            ped_device_destroy (dev);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }

        ret = disk_commit (ped_disk, disk, error);
    } else {
        if (g_strcmp0 (ped_disk->type->name, "gpt") == 0)
            ret = set_gpt_flag (disk, part_num, flag, state, error);
        else
            g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                         "Cannot set a GPT flag on a non-GPT disk");
    }

    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    bd_utils_report_finished (progress_id, "Completed");

    return ret;
}

/**
 * bd_part_set_disk_flag:
 * @disk: disk the partition belongs to
 * @flag: flag to set
 * @state: state to set for the @flag (%TRUE = enabled)
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the flag @flag was successfully set on the @disk or not
 *
 * Tech category: %BD_PART_TECH_MODE_MODIFY_TABLE + the tech according to the partition table type
 */
gboolean bd_part_set_disk_flag (const gchar *disk, BDPartDiskFlag flag, gboolean state, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    gint status = 0;
    gboolean ret = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started setting flag on the disk '%s'", disk);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* right now we only support this one flag */
    if (flag == BD_PART_DISK_FLAG_GPT_PMBR_BOOT) {
        status = ped_disk_set_flag (ped_disk, PED_DISK_GPT_PMBR_BOOT, (int) state);
        if (status == 0) {
            set_parted_error (error, BD_PART_ERROR_FAIL);
            g_prefix_error (error, "Failed to set flag on disk '%s'", disk);
            ped_disk_destroy (ped_disk);
            ped_device_destroy (dev);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }

        ret = disk_commit (ped_disk, disk, error);
    } else {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid or unsupported flag given: %d", flag);
        ret = FALSE;
    }

    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    bd_utils_report_finished (progress_id, "Completed");

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
 *       Only GPT-specific flags and the legacy boot flag are supported on GPT
 *       partition tables.
 *
 * Tech category: %BD_PART_TECH_MODE_MODIFY_PART + the tech according to the partition table type
 */
gboolean bd_part_set_part_flags (const gchar *disk, const gchar *part, guint64 flags, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    const gchar *part_num_str = NULL;
    gint part_num = 0;
    int i = 0;
    gint status = 0;
    gboolean ret = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started setting flags on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    /* TODO: share this code with the other functions modifying a partition */
    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
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
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* Do not let libparted touch gpt partition tables */
    if (g_strcmp0 (ped_disk->type->name, "gpt") == 0) {
        ret = set_gpt_flags (disk, part_num, flags, error);
    } else {
        ped_part = ped_disk_get_partition (ped_disk, part_num);
        if (!ped_part) {
            set_parted_error (error, BD_PART_ERROR_FAIL);
            g_prefix_error (error, "Failed to get partition '%d' on device '%s'", part_num, disk);
            ped_disk_destroy (ped_disk);
            ped_device_destroy (dev);
            bd_utils_report_finished (progress_id, (*error)->message);
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
                bd_utils_report_finished (progress_id, (*error)->message);
                return FALSE;
            }
        }

        ret = disk_commit (ped_disk, disk, error);
    }

    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    bd_utils_report_finished (progress_id, "Completed");

    return ret;
}


/**
 * bd_part_set_part_name:
 * @disk: device the partition belongs to
 * @part: partition the should be set for
 * @name: name to set
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the name was successfully set or not
 *
 * Tech category: %BD_PART_TECH_MODE_MODIFY_PART + the tech according to the partition table type
 */
gboolean bd_part_set_part_name (const gchar *disk, const gchar *part, const gchar *name, GError **error) {
    PedDevice *dev = NULL;
    PedDisk *ped_disk = NULL;
    PedPartition *ped_part = NULL;
    const gchar *part_num_str = NULL;
    gint part_num = 0;
    gint status = 0;
    gboolean ret = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started setting name on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    /* TODO: share this code with the other functions modifying a partition */
    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    dev = ped_device_get (disk);
    if (!dev) {
        set_parted_error (error, BD_PART_ERROR_INVAL);
        g_prefix_error (error, "Device '%s' invalid or not existing", disk);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ped_disk = ped_disk_new (dev);
    if (!ped_disk) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to read partition table on device '%s'", disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }
    if (!(ped_disk->type->features & PED_DISK_TYPE_PARTITION_NAME)) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Partition names unsupported on the device '%s' ('%s')", disk,
                     ped_disk->type->name);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
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
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ped_part = ped_disk_get_partition (ped_disk, part_num);
    if (!ped_part) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to get partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    status = ped_partition_set_name (ped_part, name);
    if (status == 0) {
        set_parted_error (error, BD_PART_ERROR_FAIL);
        g_prefix_error (error, "Failed to set name on the partition '%d' on device '%s'", part_num, disk);
        ped_disk_destroy (ped_disk);
        ped_device_destroy (dev);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = disk_commit (ped_disk, disk, error);

    ped_disk_destroy (ped_disk);
    ped_device_destroy (dev);

    bd_utils_report_finished (progress_id, "Completed");

    return ret;
}

/**
 * bd_part_set_part_type:
 * @disk: device the partition belongs to
 * @part: partition the should be set for
 * @type_guid: GUID of the type
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @type_guid type was successfully set for @part or not
 *
 * Tech category: %BD_PART_TECH_GPT-%BD_PART_TECH_MODE_MODIFY_PART
 */
gboolean bd_part_set_part_type (const gchar *disk, const gchar *part, const gchar *type_guid, GError **error) {
    const gchar *args[5] = {"sgdisk", "--typecode", NULL, disk, NULL};
    const gchar *part_num_str = NULL;
    gboolean success = FALSE;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    if (!check_deps (&avail_deps, DEPS_SGDISK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    msg = g_strdup_printf ("Started setting type on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    part_num_str = part + (strlen (part) - 1);
    while (isdigit (*part_num_str) || (*part_num_str == '-')) {
        part_num_str--;
    }
    part_num_str++;

    if ((g_strcmp0 (part_num_str, "0") != 0) && (atoi (part_num_str) == 0)) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'. Cannot extract partition number", part);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    args[2] = g_strdup_printf ("%s:%s", part_num_str, type_guid);

    success = bd_utils_exec_and_report_error (args, NULL, error);
    g_free ((gchar*) args[2]);

    bd_utils_report_finished (progress_id, "Completed");

    return success;
}

/**
 * bd_part_set_part_id:
 * @disk: device the partition belongs to
 * @part: partition the should be set for
 * @part_id: partition Id
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @part_id type was successfully set for @part or not
 *
 * Tech category: %BD_PART_TECH_MODE_MODIFY_PART + the tech according to the partition table type
 */
gboolean bd_part_set_part_id (const gchar *disk, const gchar *part, const gchar *part_id, GError **error) {
    const gchar *args[6] = {"sfdisk", "--id", disk, NULL, part_id, NULL};
    const gchar *part_num_str = NULL;
    gboolean success = FALSE;
    guint64 progress_id = 0;
    guint64 part_id_int = 0;
    gchar *msg = NULL;

    if (!check_deps (&avail_deps, DEPS_SFDISK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    msg = g_strdup_printf ("Started setting id on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    part_num_str = part + (strlen (part) - 1);
    while (isdigit (*part_num_str) || (*part_num_str == '-')) {
        part_num_str--;
    }
    part_num_str++;

    part_id_int = g_ascii_strtoull (part_id, NULL, 0);

    if (part_id_int == 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition id given: '%s'.", part_id);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (part_id_int == 0x05 || part_id_int == 0x0f || part_id_int == 0x85) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Cannot change partition id to extended.");
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if ((g_strcmp0 (part_num_str, "0") != 0) && (atoi (part_num_str) == 0)) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'. Cannot extract partition number", part);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    args[3] = g_strdup (part_num_str);

    success = bd_utils_exec_and_report_error (args, NULL, error);
    g_free ((gchar*) args[3]);

    bd_utils_report_finished (progress_id, "Completed");

    return success;
}

/**
 * bd_part_get_part_id:
 * @disk: device the partition belongs to
 * @part: partition the should be set for
 * @error: (out): place to store error (if any)
 *
 * Returns (transfer full): partition id type or %NULL in case of error
 *
 * Tech category: %BD_PART_TECH_MODE_QUERY_PART + the tech according to the partition table type
 */
gchar* bd_part_get_part_id (const gchar *disk, const gchar *part, GError **error) {
    const gchar *args[5] = {"sfdisk", "--id", disk, NULL, NULL};
    const gchar *part_num_str = NULL;
    gchar *output = NULL;
    gchar *ret = NULL;
    gboolean success = FALSE;

    if (!check_deps (&avail_deps, DEPS_SFDISK_MASK, deps, DEPS_LAST, &deps_check_lock, error))
        return FALSE;

    if (!part || (part && (*part == '\0'))) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'", part);
        return NULL;
    }

    part_num_str = part + (strlen (part) - 1);
    while (isdigit (*part_num_str) || (*part_num_str == '-')) {
        part_num_str--;
    }
    part_num_str++;

    if ((g_strcmp0 (part_num_str, "0") != 0) && (atoi (part_num_str) == 0)) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid partition path given: '%s'. Cannot extract partition number", part);
        return NULL;
    }

    args[3] = g_strdup (part_num_str);

    success = bd_utils_exec_and_capture_output (args, NULL, &output, error);
    if (!success) {
        g_free ((gchar *) args[3]);
        return NULL;
    }

    output =  g_strstrip (output);
    ret = g_strdup_printf ("0x%s", output);

    g_free (output);
    g_free ((gchar*) args[3]);

    return ret;
}

/**
 * bd_part_get_part_table_type_str:
 * @type: table type to get string representation for
 * @error: (out): place to store error (if any)
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

/**
 * bd_part_get_flag_str:
 * @flag: flag to get string representation for
 * @error: (out): place to store error (if any)
 *
 * Returns: (transfer none): string representation of @flag
 *
 * Tech category: always available
 */
const gchar* bd_part_get_flag_str (BDPartFlag flag, GError **error) {
    if (flag < BD_PART_FLAG_BASIC_LAST)
        return ped_partition_flag_get_name ((PedPartitionFlag) log2 ((double) flag));
    if (flag == BD_PART_FLAG_GPT_SYSTEM_PART)
        return "system partition";
    if (flag == BD_PART_FLAG_GPT_READ_ONLY)
        return "read-only";
    if (flag == BD_PART_FLAG_GPT_HIDDEN)
        return "hidden";
    if (flag == BD_PART_FLAG_GPT_NO_AUTOMOUNT)
        return "do not automount";

    /* no other choice */
    g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL, "Invalid flag given");
    return NULL;
}

/**
 * bd_part_get_type_str:
 * @type: type to get string representation for
 * @error: (out): place to store error (if any)
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

    return ped_partition_type_get_name ((PedPartitionType) type);
}
