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
#include <libfdisk.h>
#include <locale.h>
#include <blkid.h>
#include <syslog.h>

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

/* "C" locale to get the locale-agnostic error messages */
static locale_t c_locale = (locale_t) 0;

/* base 2 logarithm of x */
static gint log2i (guint x) {
    gint ret = 0;

    if (x == 0)
        return -1;

    while (x >>= 1)
        ret++;

    return ret;
}

/* Parted invented a lot of flags that are actually not flags, some of these are
 * partition IDs (for MSDOS), some GUIDs (for GPT) and some both or something
 * completely different. libfdisk logically doesn't support these "flags" so we
 * need some way how to "translate" the flag to something that makes sense.
 */

/**
 * PartFlag: (skip)
 * @id: partition ID or 0 if not supported on MSDOS
 * @guid: GUID or NULL if not supported on GPT
 * @name: name of the flag
 */
typedef struct PartFlag {
    const gchar *id;
    const gchar *guid;
    const gchar *name;
} PartFlag;

static const PartFlag part_flags[18] = {
    {NULL, NULL, "boot"},                                         // BD_PART_FLAG_BOOT -- managed separately (DOS_FLAG_ACTIVE)
    {NULL, NULL, "root"},                                         // BD_PART_FLAG_ROOT  -- supported only on MAC
    {"0x82", "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", "swap"},     // BD_PART_FLAG_SWAP
    {NULL, NULL, "hidden"},                                       // BD_PART_FLAG_HIDDEN -- managed separately (part IDs according to FS)
    {"0xfd", "A19D880F-05FC-4D3B-A006-743F0F84911E", "raid"},     // BD_PART_FLAG_RAID
    {"0x8e", "E6D6D379-F507-44C2-A23C-238F2A3DF928", "lvm"},      // BD_PART_FLAG_LVM
    {NULL, NULL, "lba"},                                          // BD_PART_FLAG_LBA -- managed separately (part IDs according to FS)
    {NULL, "E2A1E728-32E3-11D6-A682-7B03A0000000", "hp-service"}, // BD_PART_FLAG_HPSERVICE
    {"0xf0", NULL, "palo"},                                       // BD_PART_FLAG_CPALO
    {"0x41", "9E1A2D38-C612-4316-AA26-8B49521E5A8B", "prep"},     // BD_PART_FLAG_PREP
    {NULL, "E3C9E316-0B5C-4DB8-817D-F92DF00215AE", "msftres"},    // BD_PART_FLAG_MSFT_RESERVED
    {NULL, "21686148-6449-6E6F-744E-656564454649", "bios_grub"},  // BD_PART_FLAG_BIOS_GRUB
    {NULL, "5265636F-7665-11AA-AA11-00306543ECAC", "atvrecv"},    // BD_PART_FLAG_APPLE_TV_RECOVERY
    {"0x12", "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", "diag"},     // BD_PART_FLAG_DIAG
    {NULL, NULL, "legacy_boot"},                                  // BD_PART_FLAG_LEGACY_BOOT -- managed separately (GPT_FLAG_LEGACYBOOT)
    {NULL, "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "msftdata"},   // BD_PART_FLAG_MSFT_DATA
    {"0x84", "D3BFE2DE-3DAF-11DF-BA40-E3A556D89593", "irst"},     // BD_PART_FLAG_IRST
    {"0xef", "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", "esp"},      // BD_PART_FLAG_ESP
};

/* default part id/guid when removing existing "flag" */
#define DEFAULT_PART_ID "0x83"
#define DEFAULT_PART_GUID "0FC63DAF-8483-4772-8E79-3D69D8477DE4"

/* helper "flags" for calculating parted flags for hidden and lba partitions */
#define _PART_FAT12       0x01
#define _PART_FAT16       0x06
#define _PART_FAT16_LBA   0x0e
#define _PART_FAT32       0x0b
#define _PART_FAT32_LBA   0x0c
#define _PART_NTFS        0x07
#define _PART_FLAG_HIDDEN 0x10

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
    }

    return part_num;
}

static struct fdisk_context* get_device_context (const gchar *disk, GError **error) {
    struct fdisk_context *cxt = fdisk_new_context ();
    gint ret = 0;

    if (!cxt) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to create a new context");
        return NULL;
    }

    ret = fdisk_assign_device (cxt, disk, FALSE);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to assign the new context to disk '%s': %s", disk, strerror_l (-ret, c_locale));
        fdisk_unref_context (cxt);
        return NULL;
    }

    fdisk_disable_dialogs(cxt, 1);
    return cxt;
}

static void close_context (struct fdisk_context *cxt) {
    gint ret = 0;

    ret = fdisk_deassign_device (cxt, 0); /* context, nosync */

    if (ret != 0)
        /* XXX: should report error here? */
        g_warning ("Failed to close and sync the device: %s", strerror_l (-ret, c_locale));

    fdisk_unref_context (cxt);
}

static gboolean write_label (struct fdisk_context *cxt, const gchar *disk, GError **error) {
    gint ret = 0;
    ret = fdisk_write_disklabel (cxt);

    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to write the new disklabel to disk '%s': %s", disk, strerror_l (-ret, c_locale));
        return FALSE;
    }

    return TRUE;
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


/**
 * bd_part_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 *
 */
gboolean bd_part_check_deps (void) {
    /* no runtime dependencies */
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
    ped_exception_set_handler ((PedExceptionHandler*) bd_exc_handler);
    c_locale = newlocale (LC_ALL_MASK, "C", c_locale);
    fdisk_init_debug (0);
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
    c_locale = (locale_t) 0;
}

#define UNUSED __attribute__((unused))

/**
 * bd_part_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDPartTechMode) for @tech
 * @error: (out): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_part_is_tech_avail (BDPartTech tech, guint64 mode UNUSED, GError **error) {
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

static const gchar *table_type_str_parted[BD_PART_TABLE_UNDEF] = {"msdos", "gpt"};
static const gchar *table_type_str_fdisk[BD_PART_TABLE_UNDEF] = {"dos", "gpt"};

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
    struct fdisk_context *cxt = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Starting creation of a new partition table on '%s'", disk);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    cxt = get_device_context (disk, error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (!ignore_existing && fdisk_has_label (cxt)) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_EXISTS,
                     "Device '%s' already contains a partition table", disk);
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return FALSE;
    }

    ret = fdisk_create_disklabel (cxt, table_type_str_fdisk[type]);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to create a new disklabel for disk '%s': %s", disk, strerror_l (-ret, c_locale));
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return FALSE;
    }

    if (!write_label (cxt, disk, error)) {
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return FALSE;
    }

    close_context (cxt);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

static gchar* get_part_type_guid_and_gpt_flags (const gchar *device, int part_num, guint64 *flags, GError **error) {
    struct fdisk_context *cxt = NULL;
    struct fdisk_label *lb = NULL;
    struct fdisk_partition *pa = NULL;
    struct fdisk_parttype *ptype = NULL;
    const gchar *label_name = NULL;
    const gchar *ptype_string = NULL;
    gchar *ret = NULL;
    guint64 gpt_flags = 0;
    gint status = 0;

    /* first partition in fdisk is 0 */
    part_num--;

    cxt = get_device_context (device, error);
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
    if (g_strcmp0 (label_name, table_type_str_fdisk[BD_PART_TABLE_GPT]) != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Setting GPT flags is not supported on '%s' partition table", label_name);
        close_context (cxt);
        return NULL;
    }

    status = fdisk_gpt_get_partition_attrs (cxt, part_num, &gpt_flags);
    if (status < 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read GPT flags");
        close_context (cxt);
        return NULL;
    }

    if (gpt_flags & 1) /* 1 << 0 */
        *flags |= BD_PART_FLAG_GPT_SYSTEM_PART;
    if (gpt_flags & 4) /* 1 << 2 */
        *flags |= BD_PART_FLAG_LEGACY_BOOT;
    if (gpt_flags & 0x1000000000000000) /* 1 << 60 */
        *flags |= BD_PART_FLAG_GPT_READ_ONLY;
    if (gpt_flags & 0x4000000000000000) /* 1 << 62 */
        *flags |= BD_PART_FLAG_GPT_HIDDEN;
    if (gpt_flags & 0x8000000000000000) /* 1 << 63 */
        *flags |= BD_PART_FLAG_GPT_NO_AUTOMOUNT;


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
        fdisk_unref_parttype (ptype);
        fdisk_unref_partition (pa);
        close_context (cxt);
        return NULL;
    }

    ret = g_strdup (ptype_string);

    fdisk_unref_parttype (ptype);
    fdisk_unref_partition (pa);
    close_context (cxt);
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

static BDPartFlag get_flag_from_guid (const gchar *guid) {
    guint last_flag = 0;

    if (!guid)
        return BD_PART_FLAG_BASIC_LAST;

    last_flag = log2i (BD_PART_FLAG_BASIC_LAST);

    for (guint i = 1; i < last_flag; i++) {
      if (!part_flags[i - 1].guid)
          continue;

      if (g_ascii_strcasecmp (part_flags[i - 1].guid, guid) == 0)
          return 1 << i;
    }

    return BD_PART_FLAG_BASIC_LAST;
}

static BDPartFlag get_flag_from_id (guint id) {
    gchar *id_str = NULL;
    guint last_flag = 0;

    id_str = g_strdup_printf ("0x%.2x", id);
    last_flag = log2i (BD_PART_FLAG_BASIC_LAST);

    for (guint i = 1; i < last_flag; i++) {
        if (!part_flags[i - 1].id)
            continue;

        if (g_strcmp0 (part_flags[i - 1].id, id_str) == 0) {
            g_free (id_str);
            return 1 << i;
        }
    }

    g_free (id_str);
    return BD_PART_FLAG_BASIC_LAST;
}

static BDPartSpec* get_part_spec_fdisk (struct fdisk_context *cxt, struct fdisk_partition *pa, GError **error) {
    struct fdisk_label *lb = NULL;
    struct fdisk_parttype *ptype = NULL;
    guint part_id = 0;
    BDPartSpec *ret = NULL;
    const gchar *devname = NULL;
    const gchar *partname = NULL;
    BDPartFlag bd_flag;

    ret = g_new0 (BDPartSpec, 1);

    devname = fdisk_get_devname (cxt);

    if (fdisk_partition_has_partno (pa)) {
        if (isdigit (devname[strlen(devname) - 1]))
            ret->path = g_strdup_printf ("%sp%zu", devname, fdisk_partition_get_partno (pa) + 1);
        else
            ret->path = g_strdup_printf ("%s%zu", devname, fdisk_partition_get_partno (pa) + 1);
    }

    partname = fdisk_partition_get_name (pa);
    if (partname)
        ret->name = g_strdup (partname);

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
          ret->type_guid = get_part_type_guid_and_gpt_flags (devname, fdisk_partition_get_partno (pa) + 1, &(ret->flags), error);
          if (!ret->type_guid && *error) {
              bd_part_spec_free (ret);
              return NULL;
          }

          bd_flag = get_flag_from_guid (ret->type_guid);
          if (bd_flag != BD_PART_FLAG_BASIC_LAST)
              ret->flags |= bd_flag;
        }
    } else if (g_strcmp0 (fdisk_label_get_name (lb), "dos") == 0) {
        if (fdisk_partition_is_bootable (pa) == 1)
            ret->flags |= BD_PART_FLAG_BOOT;

        /* freespace and extended have no type/ids */
        if (ret->type == BD_PART_TYPE_NORMAL || ret->type == BD_PART_TYPE_LOGICAL) {
            ptype = fdisk_partition_get_type (pa);
            if (!ptype) {
                g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                             "Failed to get partition type.");
                bd_part_spec_free (ret);
                return NULL;
            }

            part_id = fdisk_parttype_get_code (ptype);
            if (part_id & _PART_FLAG_HIDDEN)
                ret->flags = ret->flags | BD_PART_FLAG_HIDDEN;
            if (part_id == _PART_FAT16_LBA || part_id == (_PART_FAT16_LBA | _PART_FLAG_HIDDEN) ||
                part_id == _PART_FAT32_LBA || part_id == (_PART_FAT32_LBA | _PART_FLAG_HIDDEN))
                ret->flags = ret->flags | BD_PART_FLAG_LBA;

            bd_flag = get_flag_from_id (part_id);
            if (bd_flag != BD_PART_FLAG_BASIC_LAST)
                ret->flags = ret->flags | bd_flag;

            fdisk_unref_parttype (ptype);
        }
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

    cxt = get_device_context (disk, error);
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

    cxt = get_device_context (disk, error);
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

    while (fdisk_table_next_partition(table, itr, &pa) == 0) {
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
                ext_meta->flags = 0;
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
 * @error: (out): place to store error (if any)
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
                bd_part_spec_free (*parts_p);
                continue;
            }

            ret = *parts_p;
            break;
        } else
            bd_part_spec_free (*parts_p);
    }

    g_free (parts);

    return ret;
}

static gboolean get_pmbr_boot_flag (struct fdisk_context *cxt) {
    struct fdisk_context *mbr = NULL;
    struct fdisk_partition *pa = NULL;
    gint status = 0;

    /* try to get the pmbr record */
    mbr = fdisk_new_nested_context (cxt, "dos");
    if (!mbr)
        return FALSE;

    /* try to get first partition -- first partition on pmbr is the "gpt partition" */
    status = fdisk_get_partition (mbr, 0, &pa);
    if (status != 0) {
        fdisk_unref_context (mbr);
        return FALSE;
    }

    status = fdisk_partition_is_bootable (pa);

    fdisk_unref_context (mbr);
    fdisk_unref_partition (pa);

    return status == 1;
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
    struct fdisk_context *cxt = NULL;
    struct fdisk_label *lb = NULL;
    BDPartDiskSpec *ret = NULL;
    const gchar *label_name = NULL;
    BDPartTableType type = BD_PART_TABLE_UNDEF;
    gboolean found = FALSE;

    cxt = get_device_context (disk, error);
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
            if (g_strcmp0 (label_name, table_type_str_fdisk[type]) == 0) {
                ret->table_type = type;
                found = TRUE;
            }
        }
        if (!found)
            ret->table_type = BD_PART_TABLE_UNDEF;
        if (ret->table_type == BD_PART_TABLE_GPT && get_pmbr_boot_flag (cxt))
            ret->flags = BD_PART_DISK_FLAG_GPT_PMBR_BOOT;
    } else {
        ret->table_type = BD_PART_TABLE_UNDEF;
        ret->flags = 0;
    }

    close_context (cxt);

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
    return get_disk_parts (disk, TRUE, FALSE, FALSE, error);
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
    return get_disk_parts (disk, FALSE, TRUE, FALSE, error);
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
    gint part_num = 0;
    struct fdisk_context *cxt = NULL;
    gint ret = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started deleting partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    part_num = get_part_num (part, error);
    if (part_num == -1) {
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* /dev/sda1 is the partition number 0 in libfdisk */
    part_num--;
    cxt = get_device_context (disk, error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    ret = fdisk_delete_partition (cxt, (size_t) part_num);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to delete partition '%d' on device '%s': %s", part_num+1, disk, strerror_l (-ret, c_locale));
        close_context (cxt);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (!write_label (cxt, disk, error)) {
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return FALSE;
    }

    close_context (cxt);

    bd_utils_report_finished (progress_id, "Completed");

    return TRUE;
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


static gboolean set_gpt_flag (struct fdisk_context *cxt, int part_num, BDPartFlag flag, gboolean state, GError **error) {
    struct fdisk_label *lb = NULL;
    const gchar *label_name = NULL;
    int bit_num = 0;
    guint64 gpt_flags = 0;
    gint status = 0;

    lb = fdisk_get_label (cxt, NULL);
    if (!lb) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read partition table.");
        return FALSE;
    }

    label_name = fdisk_label_get_name (lb);
    if (g_strcmp0 (label_name, table_type_str_fdisk[BD_PART_TABLE_GPT]) != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Setting GPT flags is not supported on '%s' partition table", label_name);
        return FALSE;
    }

    status = fdisk_gpt_get_partition_attrs (cxt, part_num, &gpt_flags);
    if (status < 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read GPT flags");
        return FALSE;
    }

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

    if (state)
        gpt_flags |= (guint64) 1 << bit_num;
    else
        gpt_flags &= ~((guint64) 1 << bit_num);

    status = fdisk_gpt_set_partition_attrs (cxt, part_num, gpt_flags);
    if (status < 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set new GPT flags");
        return FALSE;
    }

    return TRUE;
}

static gboolean set_gpt_flags (struct fdisk_context *cxt, int part_num, guint64 flags, GError **error) {
    guint64 gpt_flags = 0;
    gint status = 0;

    if (flags & BD_PART_FLAG_GPT_SYSTEM_PART)
        gpt_flags |=  1;       /* 1 << 0 */
    if (flags & BD_PART_FLAG_LEGACY_BOOT)
        gpt_flags |=  4;       /* 1 << 2 */
    if (flags & BD_PART_FLAG_GPT_READ_ONLY)
        gpt_flags |= 0x1000000000000000; /* 1 << 60 */
    if (flags & BD_PART_FLAG_GPT_HIDDEN)
        gpt_flags |= 0x4000000000000000; /* 1 << 62 */
    if (flags & BD_PART_FLAG_GPT_NO_AUTOMOUNT)
        gpt_flags |= 0x8000000000000000; /* 1 << 63 */

    status = fdisk_gpt_set_partition_attrs (cxt, part_num, gpt_flags);
    if (status < 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set new GPT flags");
        return FALSE;
    }

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
    if (g_strcmp0 (label_name, table_type_str_fdisk[table_type]) != 0) {
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

static gint synced_close (gint fd) {
    gint ret = 0;
    ret = fsync (fd);
    if (close (fd) != 0)
        ret = 1;
    return ret;
}

static gchar* get_lba_hidden_id (const gchar *part, gboolean hidden, gboolean lba, gboolean state, GError **error) {
    blkid_probe probe = NULL;
    gint fd = 0;
    gint status = 0;
    const gchar *value = NULL;
    g_autofree gchar *fstype = NULL;
    g_autofree gchar *fsversion = NULL;
    guint partid = 0;
    gchar *message = NULL;
    guint ret = 0;
    guint n_try = 0;

    probe = blkid_new_probe ();
    if (!probe) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to create a new probe");
        return NULL;
    }

    /* we may need to try mutliple times with some delays in case the device is
       busy at the very moment */
    for (n_try=5, fd=-1; (fd < 0) && (n_try > 0); n_try--) {
        fd = open (part, O_RDONLY|O_CLOEXEC);
        if (fd == -1)
            g_usleep (100 * 1000); /* microseconds */
    }
    if (fd == -1) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to open the device '%s'", part);
        blkid_free_probe (probe);
        return NULL;
    }

    /* we may need to try mutliple times with some delays in case the device is
       busy at the very moment */
    for (n_try=5, status=-1; (status != 0) && (n_try > 0); n_try--) {
        status = blkid_probe_set_device (probe, fd, 0, 0);
        if (status != 0)
            g_usleep (100 * 1000); /* microseconds */
    }
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to create a probe for the device '%s'", part);
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    }

    blkid_probe_enable_superblocks (probe, 1);
    blkid_probe_set_superblocks_flags (probe, BLKID_SUBLKS_USAGE | BLKID_SUBLKS_TYPE | BLKID_SUBLKS_VERSION);
    blkid_probe_enable_partitions (probe, 1);
    blkid_probe_set_partitions_flags (probe, BLKID_PARTS_ENTRY_DETAILS);

    /* we may need to try mutliple times with some delays in case the device is
       busy at the very moment */
    for (n_try=5, status=-1; !(status == 0 || status == 1) && (n_try > 0); n_try--) {
        status = blkid_do_safeprobe (probe);
        if (status < 0)
            g_usleep (100 * 1000); /* microseconds */
    }
    if (status < 0) {
        /* -1 or -2 = error during probing*/
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to probe the device '%s'", part);
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    } else if (status == 1) {
        /* 1 = nothing detected */
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    }

    if (!blkid_probe_has_value (probe, "USAGE")) {
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    }

    status = blkid_probe_lookup_value (probe, "USAGE", &value, NULL);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get usage for the device '%s'", part);
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    }

    if (strncmp (value, "filesystem", 10) != 0) {
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    }

    status = blkid_probe_lookup_value (probe, "TYPE", &value, NULL);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get filesystem type for the device '%s'", part);
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    }

    fstype = g_strdup (value);

    status = blkid_probe_lookup_value (probe, "VERSION", &value, NULL);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get filesystem version for the device '%s'", part);
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    }

    fsversion = g_strdup (value);

    status = blkid_probe_lookup_value (probe, "PART_ENTRY_TYPE", &value, NULL);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition type for the device '%s'", part);
        blkid_free_probe (probe);
        synced_close (fd);
        return NULL;
    }

    partid = g_ascii_strtoull (value, NULL, 16);

    blkid_free_probe (probe);
    synced_close (fd);

    /* hidden and lba "flags" are actually partition IDs that depend on both
     * filesystem type and version so we need to pick the right value based
     * on the FS type and version and add LBA and/or hidden "flag" to it
     */
    if (g_strcmp0 (fstype, "vfat") == 0) {
        if (g_strcmp0 (fsversion, "FAT12") == 0) {
            ret = _PART_FAT12;
        } else if (g_strcmp0 (fsversion, "FAT16") == 0) {
            if ((lba && state) || (!lba && (partid & _PART_FAT16_LBA)))
              ret = _PART_FAT16_LBA;
            else
              ret = _PART_FAT16;
        } else if (g_strcmp0 (fsversion, "FAT32") == 0) {
            if ((lba && state) || (!lba && (partid & _PART_FAT32_LBA)))
              ret = _PART_FAT32_LBA;
            else
              ret = _PART_FAT32;
        }
    } else if (g_strcmp0 (fstype, "ntfs") == 0 || g_strcmp0 (fstype, "hpfs") == 0 || g_strcmp0 (fstype, "exfat") == 0) {
        ret = _PART_NTFS;
    } else {
        /* hidden and lba flags are supported only with (v)fat, ntfs or hpfs
         * libparted just ignores the request without returning error, so we
         * need to do the same
         */
        message = g_strdup_printf ("Ignoring requested flag: setting hidden/lba flag is supported only "\
                                   "on partitions with FAT, NTFS or HPFS filesystem.");
        bd_utils_log (LOG_INFO, message);
        g_free (message);
        return NULL;
    }

    /* "add" the hidden flag if requested or if it was set before and unsetting
     * was not requested */
    if ((hidden && state) || (!hidden && (partid & _PART_FLAG_HIDDEN)))
        ret |= _PART_FLAG_HIDDEN;

    /* both lba and hidden were removed -> set default ID */
    if (ret == _PART_NTFS || ret == _PART_FAT12 || ret == _PART_FAT16 || ret == _PART_FAT32)
        return g_strdup (DEFAULT_PART_ID);

    return g_strdup_printf ("0x%.2x", ret);
}


static gboolean set_boot_flag (struct fdisk_context *cxt, guint part_num, gboolean state, GError **error) {
    struct fdisk_partition *pa = NULL;
    gint ret = 0;

    ret = fdisk_get_partition (cxt, part_num, &pa);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition '%d'.", part_num);
        return FALSE;
    }

    ret = fdisk_partition_is_bootable (pa);
    if ((ret == 1 && state) || (ret != 1 && !state)) {
        /* boot flag is already set as desired, no change needed */
        fdisk_unref_partition (pa);
        return TRUE;
    }

    ret = fdisk_toggle_partition_flag (cxt, part_num, DOS_FLAG_ACTIVE);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "%s", strerror_l (-ret, c_locale));
        fdisk_unref_partition (pa);
        return FALSE;
    }

    ret = fdisk_write_disklabel (cxt);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to write changes to the disk: %s", strerror_l (-ret, c_locale));
        fdisk_unref_partition (pa);
        return FALSE;
    }

    fdisk_unref_partition (pa);

    return TRUE;
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
    struct fdisk_context *cxt = NULL;
    struct fdisk_label *lb = NULL;
    const gchar *label_name = NULL;
    gint part_num = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    PartFlag flag_info;
    gchar *part_id = NULL;

    msg = g_strdup_printf ("Started setting flag on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    part_num = get_part_num (part, error);
    if (part_num == -1) {
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* first partition in fdisk is 0 */
    part_num--;

    cxt = get_device_context (disk, error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* GPT flags */
    if (flag > BD_PART_FLAG_BASIC_LAST || flag == BD_PART_FLAG_LEGACY_BOOT) {
        if (!set_gpt_flag (cxt, part_num, flag, state, error)) {
            bd_utils_report_finished (progress_id, (*error)->message);
            close_context (cxt);
            return FALSE;
        }

        if (!write_label (cxt, disk, error)) {
            bd_utils_report_finished (progress_id, (*error)->message);
            close_context (cxt);
            return FALSE;
        }

        bd_utils_report_finished (progress_id, "Completed");
        close_context (cxt);
        return TRUE;
    }

    lb = fdisk_get_label (cxt, NULL);
    if (!lb) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read partition table on device '%s'", disk);
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return FALSE;
    }

    label_name = fdisk_label_get_name (lb);

    /* boot flag is a real flag, not an ID/GUID */
    if (flag == BD_PART_FLAG_BOOT) {
        if (!set_boot_flag (cxt, part_num, state, error)) {
            close_context (cxt);
            g_prefix_error (error, "Failed to set boot flag on partition '%s': ", part);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    /* hidden and lba flags are in fact special partition IDs */
    } else if (flag == BD_PART_FLAG_HIDDEN || flag == BD_PART_FLAG_LBA) {
        part_id = get_lba_hidden_id (part, flag == BD_PART_FLAG_HIDDEN, flag == BD_PART_FLAG_LBA, state, error);
        if (part_id) {
            if (!set_part_type (cxt, part_num, part_id, BD_PART_TABLE_MSDOS, error)) {
                bd_utils_report_finished (progress_id, (*error)->message);
                g_free (part_id);
                close_context (cxt);
                return FALSE;
            }
        } else {
            if (*error == NULL) {
                /* NULL as part ID, but no error (e.g. unsupported FS) -> do nothing */
                bd_utils_report_finished (progress_id, "Completed");
                close_context (cxt);
                return TRUE;
            } else {
                g_prefix_error (error, "Failed to calculate partition ID to set: ");
                bd_utils_report_finished (progress_id, (*error)->message);
                close_context (cxt);
                return FALSE;
            }
        }
    /* parition types/GUIDs (GPT) or IDs (MSDOS) */
    } else {
        flag_info = part_flags[log2i (flag) - 1];
        if (g_strcmp0 (label_name, table_type_str_fdisk[BD_PART_TABLE_MSDOS]) == 0 && flag_info.id) {
            if (!set_part_type (cxt, part_num, state ? flag_info.id : DEFAULT_PART_ID, BD_PART_TABLE_MSDOS, error)) {
                bd_utils_report_finished (progress_id, (*error)->message);
                close_context (cxt);
                return FALSE;
            }
        } else if (g_strcmp0 (label_name, table_type_str_fdisk[BD_PART_TABLE_GPT]) == 0 && flag_info.guid) {
            if (!set_part_type (cxt, part_num, state ? flag_info.id : DEFAULT_PART_GUID, BD_PART_TABLE_GPT, error)) {
                bd_utils_report_finished (progress_id, (*error)->message);
                close_context (cxt);
                return FALSE;
            }
        } else {
            g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                         "Setting flag '%s' is not supported on '%s' partition table", flag_info.name, label_name);
            bd_utils_report_finished (progress_id, (*error)->message);
            close_context (cxt);
            return FALSE;
        }
    }

    if (!write_label (cxt, disk, error)) {
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return FALSE;
    }

    close_context (cxt);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
}

static gboolean set_pmbr_boot_flag (struct fdisk_context *cxt, gboolean state, GError **error) {
    struct fdisk_context *mbr = NULL;
    struct fdisk_partition *pa = NULL;
    gint ret = 0;

    /* try to get the pmbr record */
    mbr = fdisk_new_nested_context (cxt, "dos");
    if (!mbr) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "No PMBR label found.");
        return FALSE;
    }

    /* try to get first partition -- first partition on pmbr is the "gpt partition" */
    ret = fdisk_get_partition (mbr, 0, &pa);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get the GPT partition.");
        fdisk_unref_context (mbr);
        return FALSE;
    }

    ret = fdisk_partition_is_bootable (pa);
    if ((ret == 1 && state) || (ret != 1 && !state)) {
        /* boot flag is already set as desired, no change needed */
        fdisk_unref_partition (pa);
        fdisk_unref_context (mbr);
        return TRUE;
    }

    ret = fdisk_toggle_partition_flag (mbr, 0, DOS_FLAG_ACTIVE);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "%s", strerror_l (-ret, c_locale));
        fdisk_unref_partition (pa);
        fdisk_unref_context (mbr);
        return FALSE;
    }

    ret = fdisk_write_disklabel (mbr);
    if (ret != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to write the new PMBR disklabel: %s", strerror_l (-ret, c_locale));
        fdisk_unref_partition (pa);
        fdisk_unref_context (mbr);
        return FALSE;
    }

    fdisk_unref_partition (pa);
    fdisk_unref_context (mbr);
    return TRUE;
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
    struct fdisk_context *cxt = NULL;
    struct fdisk_label *lb = NULL;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started setting flag on the disk '%s'", disk);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    cxt = get_device_context (disk, error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    lb = fdisk_get_label (cxt, NULL);
    if (!lb) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read partition table on device '%s'", disk);
        close_context (cxt);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* right now we only support this one flag */
    if (flag == BD_PART_DISK_FLAG_GPT_PMBR_BOOT) {
        if (!set_pmbr_boot_flag (cxt, state, error)) {
            g_prefix_error (error, "Failed to set flag on disk '%s': ", disk);
            close_context (cxt);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }
    } else {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Invalid or unsupported flag given: %d", flag);
        close_context (cxt);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    close_context (cxt);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
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
    struct fdisk_context *cxt = NULL;
    struct fdisk_label *lb = NULL;
    const gchar *label_name = NULL;
    gint part_num = 0;
    gint last_flag = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;
    gchar *part_id = NULL;
    BDPartTableType table_type;

    msg = g_strdup_printf ("Started setting flags on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    part_num = get_part_num (part, error);
    if (part_num == -1) {
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* first partition in fdisk is 0 */
    part_num--;

    cxt = get_device_context (disk, error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    lb = fdisk_get_label (cxt, NULL);
    if (!lb) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read partition table.");
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return FALSE;
    }

    label_name = fdisk_label_get_name (lb);
    if (g_strcmp0 (label_name, table_type_str_fdisk[BD_PART_TABLE_MSDOS]) == 0)
        table_type = BD_PART_TABLE_MSDOS;
    else if (g_strcmp0 (label_name, table_type_str_fdisk[BD_PART_TABLE_GPT]) == 0)
        table_type = BD_PART_TABLE_GPT;
    else {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Setting partition flags is not supported on '%s' partition table", label_name);
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return FALSE;
    }

    /* first unset all the flags on MSDOS */
    if (table_type == BD_PART_TABLE_MSDOS) {
        if (!set_boot_flag (cxt, part_num, FALSE, error)) {
            close_context (cxt);
            g_prefix_error (error, "Failed to unset boot flag on partition '%s': ", part);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }

        if (!set_part_type (cxt, part_num, DEFAULT_PART_ID, BD_PART_TABLE_MSDOS, error)) {
            g_prefix_error (error, "Failed to reset partition ID on partition '%s': ", part);
            bd_utils_report_finished (progress_id, (*error)->message);
            close_context (cxt);
            return FALSE;
        }
    }

    /* and now go through all the flags and set the required ones */
    if (table_type == BD_PART_TABLE_MSDOS) {
        /* special cases first */
        if (flags & BD_PART_FLAG_BOOT && !set_boot_flag (cxt, part_num, TRUE, error)) {
            close_context (cxt);
            g_prefix_error (error, "Failed to set boot flag on partition '%s': ", part);
            bd_utils_report_finished (progress_id, (*error)->message);
            return FALSE;
        }

        if (flags & BD_PART_FLAG_HIDDEN || flags & BD_PART_FLAG_LBA) {
            part_id = get_lba_hidden_id (part, flags & BD_PART_FLAG_HIDDEN, flags & BD_PART_FLAG_LBA, TRUE, error);
            if (part_id) {
                if (!set_part_type (cxt, part_num, part_id, BD_PART_TABLE_MSDOS, error)) {
                    bd_utils_report_finished (progress_id, (*error)->message);
                    g_free (part_id);
                    close_context (cxt);
                    return FALSE;
                }
            } else {
                if (*error) {
                    g_prefix_error (error, "Failed to calculate partition ID to set: ");
                    bd_utils_report_finished (progress_id, (*error)->message);
                    close_context (cxt);
                    return FALSE;
                }
            }
        }

        last_flag = log2i (BD_PART_FLAG_BASIC_LAST);

        /* flags that are actually partition IDs */
        for (gint i = 1; i < last_flag; i++) {
            if (i == 1 || i == 4 || i == 7)
                /* skip flags we handled above -- boot, hidden and lba */
                continue;
            if ((1 << i) & flags) {
                if (!part_flags[i - 1].id) {
                    g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                                 "Setting flag '%s' is not supported on '%s' partition table", part_flags[i - 1].name, label_name);
                    bd_utils_report_finished (progress_id, (*error)->message);
                    close_context (cxt);
                    return FALSE;
                }

                if (!set_part_type (cxt, part_num, part_flags[i - 1].id, BD_PART_TABLE_MSDOS, error)) {
                    g_prefix_error (error, "Failed to set partition ID on partition '%s': ", part);
                    bd_utils_report_finished (progress_id, (*error)->message);
                    close_context (cxt);
                    return FALSE;
                }
            }
        }
    } else if (table_type == BD_PART_TABLE_GPT) {
        if (!set_gpt_flags (cxt, part_num, flags, error)) {
            g_prefix_error (error, "Failed to set partition type on partition '%s': ", part);
            bd_utils_report_finished (progress_id, (*error)->message);
            close_context (cxt);
            return FALSE;
        }
    }

    if (!write_label (cxt, disk, error)) {
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return FALSE;
    }

    bd_utils_report_finished (progress_id, "Completed");
    close_context (cxt);
    return TRUE;
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
    struct fdisk_context *cxt = NULL;
    struct fdisk_label *lb = NULL;
    struct fdisk_partition *pa = NULL;
    const gchar *label_name = NULL;
    gint part_num = 0;
    gint status = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started setting name on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    cxt = get_device_context (disk, error);
    if (!cxt) {
        /* error is already populated */
        return FALSE;
    }

    lb = fdisk_get_label (cxt, NULL);
    if (!lb) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read partition table on device '%s'", disk);
        close_context (cxt);
        return FALSE;
    }

    label_name = fdisk_label_get_name (lb);
    if (g_strcmp0 (label_name, table_type_str_fdisk[BD_PART_TABLE_GPT]) != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Partition names unsupported on the device '%s' ('%s')", disk,
                     label_name);
        close_context (cxt);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    part_num = get_part_num (part, error);
    if (part_num == -1) {
        close_context (cxt);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* /dev/sda1 is the partition number 0 in libfdisk */
    part_num--;

    status = fdisk_get_partition (cxt, part_num, &pa);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition '%s' on device '%s': %s",
                     part, disk, strerror_l (-status, c_locale));
        close_context (cxt);
        return FALSE;
    }

    status = fdisk_partition_set_name (pa, name);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set name on the partition '%s' on device '%s': %s",
                     part, disk, strerror_l (-status, c_locale));
        close_context (cxt);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    status = fdisk_set_partition (cxt, part_num, pa);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to set name on the partition '%s' on device '%s': %s",
                     part, disk, strerror_l (-status, c_locale));
        fdisk_unref_partition (pa);
        close_context (cxt);
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    fdisk_unref_partition (pa);

    if (!write_label (cxt, disk, error)) {
        bd_utils_report_finished (progress_id, (*error)->message);
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
 * @part: partition the should be set for
 * @type_guid: GUID of the type
 * @error: (out): place to store error (if any)
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

    msg = g_strdup_printf ("Started setting type on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    part_num = get_part_num (part, error);
    if (part_num == -1)
        return FALSE;

    /* /dev/sda1 is the partition number 0 in libfdisk */
    part_num--;

    cxt = get_device_context (disk, error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (!set_part_type (cxt, part_num, type_guid, BD_PART_TABLE_GPT, error)) {
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return FALSE;
    }

    if (!write_label (cxt, disk, error)) {
        bd_utils_report_finished (progress_id, (*error)->message);
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
 * @part: partition the should be set for
 * @part_id: partition Id
 * @error: (out): place to store error (if any)
 *
 * Returns: whether the @part_id type was successfully set for @part or not
 *
 * Tech category: %BD_PART_TECH_MSDOS-%BD_PART_TECH_MODE_MODIFY_PART
 */
gboolean bd_part_set_part_id (const gchar *disk, const gchar *part, const gchar *part_id, GError **error) {
    guint64 progress_id = 0;
    gchar *msg = NULL;
    struct fdisk_context *cxt = NULL;
    gint part_num = 0;

    msg = g_strdup_printf ("Started setting id on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    part_num = get_part_num (part, error);
    if (part_num == -1) {
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    /* /dev/sda1 is the partition number 0 in libfdisk */
    part_num--;

    cxt = get_device_context (disk, error);
    if (!cxt) {
        /* error is already populated */
        bd_utils_report_finished (progress_id, (*error)->message);
        return FALSE;
    }

    if (!set_part_type (cxt, part_num, part_id, BD_PART_TABLE_MSDOS, error)) {
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return FALSE;
    }

    if (!write_label (cxt, disk, error)) {
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return FALSE;
    }

    close_context (cxt);
    bd_utils_report_finished (progress_id, "Completed");
    return TRUE;
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
    struct fdisk_context *cxt = NULL;
    struct fdisk_label *lb = NULL;
    struct fdisk_partition *pa = NULL;
    struct fdisk_parttype *ptype = NULL;
    const gchar *label_name = NULL;
    guint part_id = 0;
    gchar *ret = NULL;
    gint status = 0;
    gint part_num = 0;
    guint64 progress_id = 0;
    gchar *msg = NULL;

    msg = g_strdup_printf ("Started getting id on the partition '%s'", part);
    progress_id = bd_utils_report_started (msg);
    g_free (msg);

    part_num = get_part_num (part, error);
    if (part_num == -1) {
        bd_utils_report_finished (progress_id, (*error)->message);
        return NULL;
    }

    /* first partition in fdisk is 0 */
    part_num--;

    cxt = get_device_context (disk, error);
    if (!cxt) {
        /* error is already populated */
        return NULL;
    }

    lb = fdisk_get_label (cxt, NULL);
    if (!lb) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to read partition table on device '%s'", disk);
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return NULL;
    }

    label_name = fdisk_label_get_name (lb);
    if (g_strcmp0 (label_name, table_type_str_fdisk[BD_PART_TABLE_MSDOS]) != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_INVAL,
                     "Partition ID is not supported on '%s' partition table", label_name);
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return NULL;
    }

    status = fdisk_get_partition (cxt, part_num, &pa);
    if (status != 0) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition %d on device '%s'", part_num, disk);
        bd_utils_report_finished (progress_id, (*error)->message);
        close_context (cxt);
        return NULL;
    }

    ptype = fdisk_partition_get_type (pa);
    if (!ptype) {
        g_set_error (error, BD_PART_ERROR, BD_PART_ERROR_FAIL,
                     "Failed to get partition type for partition %d on device '%s'", part_num, disk);
        bd_utils_report_finished (progress_id, (*error)->message);
        fdisk_unref_partition (pa);
        close_context (cxt);
        return NULL;
    }

    part_id = fdisk_parttype_get_code (ptype);
    ret = g_strdup_printf ("0x%.2x", part_id);

    fdisk_unref_parttype (ptype);
    fdisk_unref_partition (pa);
    close_context (cxt);

    bd_utils_report_finished (progress_id, "Completed");

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

    return table_type_str_parted[type];
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
        return part_flags[log2i (flag) - 1].name;
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

/* string for BD_PART_TYPE_PROTECTED is "primary", that's what parted returns... */
static const gchar*const part_types[6] = { "primary", "logical", "extended", "free", "metadata", "primary" };

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

    return part_types[log2i (type) + 1];
}
