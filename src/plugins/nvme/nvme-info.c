/*
 * Copyright (C) 2014-2021 Red Hat, Inc.
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
 * Author: Tomas Bzatek <tbzatek@redhat.com>
 */

#include <glib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>

#include <libnvme.h>
#include <uuid/uuid.h>

#include <blockdev/utils.h>
#include <check_deps.h>
#include "nvme.h"
#include "nvme-private.h"


/* "C" locale to get the locale-agnostic error messages */
static locale_t c_locale = (locale_t) 0;


/**
 * bd_nvme_controller_info_free: (skip)
 * @info: (allow-none): %BDNVMEControllerInfo to free
 *
 * Frees @info.
 */
void bd_nvme_controller_info_free (BDNVMEControllerInfo *info) {
    if (info == NULL)
        return;

    g_free (info->fguid);
    g_free (info->subsysnqn);
    g_free (info);
}

/**
 * bd_nvme_controller_info_copy: (skip)
 * @info: (allow-none): %BDNVMEControllerInfo to copy
 *
 * Creates a new copy of @info.
 */
BDNVMEControllerInfo * bd_nvme_controller_info_copy (BDNVMEControllerInfo *info) {
    BDNVMEControllerInfo *new_info;

    if (info == NULL)
        return NULL;

    new_info = g_new0 (BDNVMEControllerInfo, 1);
    memcpy (new_info, info, sizeof (BDNVMEControllerInfo));
    new_info->fguid = g_strdup (info->fguid);
    new_info->subsysnqn = g_strdup (info->subsysnqn);

    return new_info;
}

/**
 * bd_nvme_lba_format_free: (skip)
 * @fmt: (allow-none): %BDNVMELBAFormat to free
 *
 * Frees @fmt.
 */
void bd_nvme_lba_format_free (BDNVMELBAFormat *fmt) {
    g_free (fmt);
}

/**
 * bd_nvme_lba_format_copy: (skip)
 * @fmt: (allow-none): %BDNVMELBAFormat to copy
 *
 * Creates a new copy of @fmt.
 */
BDNVMELBAFormat * bd_nvme_lba_format_copy (BDNVMELBAFormat *fmt) {
    BDNVMELBAFormat *new_fmt;

    if (fmt == NULL)
        return NULL;

    new_fmt = g_new0 (BDNVMELBAFormat, 1);
    new_fmt->data_size = fmt->data_size;
    new_fmt->relative_performance = fmt->relative_performance;

    return new_fmt;
}

/**
 * bd_nvme_namespace_info_free: (skip)
 * @info: (allow-none): %BDNVMENamespaceInfo to free
 *
 * Frees @info.
 */
void bd_nvme_namespace_info_free (BDNVMENamespaceInfo *info) {
    BDNVMELBAFormat **lba_formats;

    if (info == NULL)
        return;

    g_free (info->eui64);
    g_free (info->uuid);
    g_free (info->nguid);

    for (lba_formats = info->lba_formats; lba_formats && *lba_formats; lba_formats++)
        bd_nvme_lba_format_free (*lba_formats);
    g_free (info->lba_formats);
    g_free (info);
}

/**
 * bd_nvme_namespace_info_copy: (skip)
 * @info: (allow-none): %BDNVMENamespaceInfo to copy
 *
 * Creates a new copy of @info.
 */
BDNVMENamespaceInfo * bd_nvme_namespace_info_copy (BDNVMENamespaceInfo *info) {
    BDNVMENamespaceInfo *new_info;
    BDNVMELBAFormat **lba_formats;
    GPtrArray *ptr_array;

    if (info == NULL)
        return NULL;

    new_info = g_new0 (BDNVMENamespaceInfo, 1);
    memcpy (new_info, info, sizeof (BDNVMENamespaceInfo));
    new_info->eui64 = g_strdup (info->eui64);
    new_info->uuid = g_strdup (info->uuid);
    new_info->nguid = g_strdup (info->nguid);

    ptr_array = g_ptr_array_new ();
    for (lba_formats = info->lba_formats; lba_formats && *lba_formats; lba_formats++)
        g_ptr_array_add (ptr_array, bd_nvme_lba_format_copy (*lba_formats));
    g_ptr_array_add (ptr_array, NULL);
    new_info->lba_formats = (BDNVMELBAFormat **) g_ptr_array_free (ptr_array, FALSE);

    return new_info;
}

/**
 * bd_nvme_smart_log_free: (skip)
 * @log: (allow-none): %BDNVMESmartLog to free
 *
 * Frees @log.
 */
void bd_nvme_smart_log_free (BDNVMESmartLog *log) {
    g_free (log);
}

/**
 * bd_nvme_smart_log_copy: (skip)
 * @log: (allow-none): %BDNVMESmartLog to copy
 *
 * Creates a new copy of @log.
 */
BDNVMESmartLog * bd_nvme_smart_log_copy (BDNVMESmartLog *log) {
    BDNVMESmartLog *new_log;

    if (log == NULL)
        return NULL;

    new_log = g_new0 (BDNVMESmartLog, 1);
    memcpy (new_log, log, sizeof (BDNVMESmartLog));

    return new_log;
}

/**
 * bd_nvme_error_log_entry_free: (skip)
 * @entry: (allow-none): %BDNVMEErrorLogEntry to free
 *
 * Frees @entry.
 */
void bd_nvme_error_log_entry_free (BDNVMEErrorLogEntry *entry) {
    if (entry == NULL)
        return;

    g_error_free (entry->command_error);
    g_free (entry);
}

/**
 * bd_nvme_error_log_entry_copy: (skip)
 * @entry: (allow-none): %BDNVMEErrorLogEntry to copy
 *
 * Creates a new copy of @entry.
 */
BDNVMEErrorLogEntry * bd_nvme_error_log_entry_copy (BDNVMEErrorLogEntry *entry) {
    BDNVMEErrorLogEntry *new_entry;

    if (entry == NULL)
        return NULL;

    new_entry = g_new0 (BDNVMEErrorLogEntry, 1);
    memcpy (new_entry, entry, sizeof (BDNVMEErrorLogEntry));
    new_entry->command_error = g_error_copy (entry->command_error);

    return new_entry;
}



static guint64 int128_to_guint64 (__u8 *data)
{
    int i;
    guint64 result = 0;

    /* FIXME: would overflow, need to find 128-bit int */
    for (i = 0; i < 16; i++) {
        result *= 256;
        result += data[15 - i];
    }
    return result;
}

static gint open_dev (const gchar *device, GError **error) {
    int fd;

    /* TODO: nvme-cli is checking for file type, if it's a block or char device.
     *       See also nvme_open().
     */
    fd = open (device, O_RDONLY);
    if (fd < 0) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Failed to open device '%s': %m", device);
        return -1;
    }

    return fd;
}

/**
 * bd_nvme_get_controller_info:
 * @device: a NVMe controller device (e.g. /dev/nvme0)
 * @error: (out): place to store error (if any)
 *
 * Retrieves information about the NVMe controller (the Identify Controller command)
 * as specified by the @device block device path.
 *
 * Returns: (transfer full): information about given controller or %NULL in case of an error (with @error set).
 *
 * Tech category: %BD_NVME_TECH_NVME-%BD_NVME_TECH_MODE_INFO
 */
BDNVMEControllerInfo * bd_nvme_get_controller_info (const gchar *device, GError **error) {
    int ret;
    int fd;
    struct nvme_id_ctrl ctrl_id = { };
    BDNVMEControllerInfo *info;

    /* open the block device */
    fd = open_dev (device, error);
    if (fd < 0)
        return NULL;

    /* send the NVME_IDENTIFY_CNS_NS + NVME_IDENTIFY_CNS_CTRL ioctl */
    ret = nvme_identify_ctrl (fd, &ctrl_id);
    if (ret < 0) {
        /* generic errno errors */
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "NVMe Identify Controller command error: %s", strerror_l (errno, c_locale));
        close (fd);
        return NULL;
    }
    close (fd);
    if (ret > 0) {
        /* NVMe status codes */
        _nvme_status_to_error (ret, FALSE, error);
        g_prefix_error (error, "NVMe Identify Controller command error: ");
        return NULL;
    }

    info = g_new0 (BDNVMEControllerInfo, 1);
    if ((ctrl_id.cmic & NVME_CTRL_CMIC_MULTI_PORT) == NVME_CTRL_CMIC_MULTI_PORT)
        info->features |= BD_NVME_CTRL_FEAT_MULTIPORT;
    if ((ctrl_id.cmic & NVME_CTRL_CMIC_MULTI_CTRL) == NVME_CTRL_CMIC_MULTI_CTRL)
        info->features |= BD_NVME_CTRL_FEAT_MULTICTRL;
    if ((ctrl_id.cmic & NVME_CTRL_CMIC_MULTI_SRIOV) == NVME_CTRL_CMIC_MULTI_SRIOV)
        info->features |= BD_NVME_CTRL_FEAT_SRIOV;
    if ((ctrl_id.cmic & NVME_CTRL_CMIC_MULTI_ANA_REPORTING) == NVME_CTRL_CMIC_MULTI_ANA_REPORTING)
        info->features |= BD_NVME_CTRL_FEAT_ANA_REPORTING;
    info->ctrl_id = le16_to_cpu (ctrl_id.cntlid);
    info->fguid = g_strdup_printf ("%-.*s", (int) sizeof (ctrl_id.fguid), ctrl_id.fguid);
    /* TODO: vwci: VPD Write Cycle Information */
    if ((ctrl_id.oacs & NVME_CTRL_OACS_FORMAT) == NVME_CTRL_OACS_FORMAT)
        info->features |= BD_NVME_CTRL_FEAT_FORMAT;
    if ((ctrl_id.oacs & NVME_CTRL_OACS_NS_MGMT) == NVME_CTRL_OACS_NS_MGMT)
        info->features |= BD_NVME_CTRL_FEAT_NS_MGMT;
    if ((ctrl_id.oacs & NVME_CTRL_OACS_SELF_TEST) == NVME_CTRL_OACS_SELF_TEST)
        info->features |= BD_NVME_CTRL_FEAT_SELFTEST;
    info->hmb_pref_size = le32_to_cpu (ctrl_id.hmpre) * 4096LL;
    info->hmb_min_size = le32_to_cpu (ctrl_id.hmmin) * 4096LL;
    info->size_total = int128_to_guint64 (ctrl_id.tnvmcap);
    info->size_unalloc = int128_to_guint64 (ctrl_id.unvmcap);
    info->selftest_ext_time = le16_to_cpu (ctrl_id.edstt);
    /* TODO: lpa: Log Page Attributes - NVME_CTRL_LPA_PERSETENT_EVENT: Persistent Event log */
    if ((ctrl_id.dsto & NVME_CTRL_DSTO_ONE_DST) == NVME_CTRL_DSTO_ONE_DST)
        info->features |= BD_NVME_CTRL_FEAT_SELFTEST_SINGLE;
    if ((ctrl_id.sanicap & NVME_CTRL_SANICAP_CES) == NVME_CTRL_SANICAP_CES)
        info->features |= BD_NVME_CTRL_FEAT_SANITIZE_CRYPTO;
    if ((ctrl_id.sanicap & NVME_CTRL_SANICAP_BES) == NVME_CTRL_SANICAP_BES)
        info->features |= BD_NVME_CTRL_FEAT_SANITIZE_BLOCK;
    if ((ctrl_id.sanicap & NVME_CTRL_SANICAP_OWS) == NVME_CTRL_SANICAP_OWS)
        info->features |= BD_NVME_CTRL_FEAT_SANITIZE_OVERWRITE;
    /* struct nvme_id_ctrl.nn: If the &struct nvme_id_ctrl.mnan field is cleared to 0h,
     * then the struct nvme_id_ctrl.nn field also indicates the maximum number of namespaces
     * supported by the NVM subsystem.
     */
    info->num_namespaces = le32_to_cpu (ctrl_id.mnan) == 0 ? le32_to_cpu (ctrl_id.nn) : le32_to_cpu (ctrl_id.mnan);
    if ((ctrl_id.fna & NVME_CTRL_FNA_FMT_ALL_NAMESPACES) == NVME_CTRL_FNA_FMT_ALL_NAMESPACES)
        info->features |= BD_NVME_CTRL_FEAT_FORMAT_ALL_NS;
    if ((ctrl_id.fna & NVME_CTRL_FNA_SEC_ALL_NAMESPACES) == NVME_CTRL_FNA_SEC_ALL_NAMESPACES)
        info->features |= BD_NVME_CTRL_FEAT_SECURE_ERASE_ALL_NS;
    if ((ctrl_id.fna & NVME_CTRL_FNA_CRYPTO_ERASE) == NVME_CTRL_FNA_CRYPTO_ERASE)
        info->features |= BD_NVME_CTRL_FEAT_SECURE_ERASE_CRYPTO;
    /* TODO: enum nvme_id_ctrl_oncs: NVME_CTRL_ONCS_WRITE_UNCORRECTABLE, NVME_CTRL_ONCS_WRITE_ZEROES... */
    /* TODO: nwpc: Namespace Write Protection Capabilities */
    info->subsysnqn = g_strdup_printf ("%-.*s", (int) sizeof (ctrl_id.subnqn), ctrl_id.subnqn);

    return info;
}


/**
 * bd_nvme_get_namespace_info:
 * @device: a NVMe namespace device (e.g. /dev/nvme0n1)
 * @error: (out): place to store error (if any)
 *
 * Retrieves information about the NVMe namespace (the Identify Namespace command)
 * as specified by the @device block device path.
 *
 * Returns: (transfer full): information about given namespace or %NULL in case of an error (with @error set).
 *
 * Tech category: %BD_NVME_TECH_NVME-%BD_NVME_TECH_MODE_INFO
 */
BDNVMENamespaceInfo *bd_nvme_get_namespace_info (const gchar *device, GError **error) {
    int ret;
    int ret_desc;
    int fd;
    __u32 nsid = 0;
    struct nvme_id_ns ns_info = { };
    uint8_t desc[NVME_IDENTIFY_DATA_SIZE] = { };
    guint flbas;
    guint i;
    guint len;
    BDNVMENamespaceInfo *info;
    GPtrArray *ptr_array;

    /* open the block device */
    fd = open_dev (device, error);
    if (fd < 0)
        return NULL;

    /* get Namespace Identifier (NSID) for the @device (NVME_IOCTL_ID) */
    ret = nvme_get_nsid (fd, &nsid);
    if (ret < 0) {
        /* generic errno errors */
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "Error getting Namespace Identifier (NSID): %s", strerror_l (errno, c_locale));
        close (fd);
        return NULL;
    }
    if (ret > 0) {
        /* NVMe status codes */
        _nvme_status_to_error (ret, FALSE, error);
        g_prefix_error (error, "Error getting Namespace Identifier (NSID): ");
        close (fd);
        return NULL;
    }

    /* send the NVME_IDENTIFY_CNS_NS ioctl */
    ret_desc = nvme_identify_ns_descs (fd, nsid, (struct nvme_ns_id_desc *) &desc);
    ret = nvme_identify_ns (fd, nsid, &ns_info);
    if (ret < 0) {
        /* generic errno errors */
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "NVMe Identify Namespace command error: %s", strerror_l (errno, c_locale));
        close (fd);
        return NULL;
    }
    close (fd);
    if (ret > 0) {
        /* NVMe status codes */
        _nvme_status_to_error (ret, FALSE, error);
        g_prefix_error (error, "NVMe Identify Namespace command error: ");
        return NULL;
    }

    info = g_new0 (BDNVMENamespaceInfo, 1);
    info->nsize = le64_to_cpu (ns_info.nsze);
    info->ncap = le64_to_cpu (ns_info.ncap);
    info->nuse = le64_to_cpu (ns_info.nuse);
    if ((ns_info.nsfeat & NVME_NS_FEAT_THIN) == NVME_NS_FEAT_THIN)
        info->features |= BD_NVME_NS_FEAT_THIN;
    if ((ns_info.nmic & NVME_NS_NMIC_SHARED) == NVME_NS_NMIC_SHARED)
        info->features |= BD_NVME_NS_FEAT_MULTIPATH_SHARED;
    if ((ns_info.fpi & NVME_NS_FPI_SUPPORTED) == NVME_NS_FPI_SUPPORTED)
        info->features |= BD_NVME_NS_FEAT_FORMAT_PROGRESS;
    info->format_progress_remaining = ns_info.fpi & NVME_NS_FPI_REMAINING;
    /* TODO: what the ns_info.nvmcap really stands for? */
    info->write_protected = (ns_info.nsattr & NVME_NS_NSATTR_WRITE_PROTECTED) == NVME_NS_NSATTR_WRITE_PROTECTED;
    info->nguid = g_malloc0 (sizeof (ns_info.nguid) * 2 + 1);
    for (i = 0; i < G_N_ELEMENTS (ns_info.nguid); i++)
        snprintf (info->nguid + i * 2, 3, "%02x", ns_info.nguid[i]);
    info->eui64 = g_malloc0 (sizeof (ns_info.eui64) * 2 + 1);
    for (i = 0; i < G_N_ELEMENTS (ns_info.eui64); i++)
        snprintf (info->eui64 + i * 2, 3, "%02x", ns_info.eui64[i]);
    if (ret_desc == 0) {
        for (i = 0; i < NVME_IDENTIFY_DATA_SIZE; i += len) {
            struct nvme_ns_id_desc *d = (void *) desc + i;
            gchar uuid_buf[37] = { };

            if (!d->nidl)
                break;
            len = d->nidl + sizeof (*d);

            switch (d->nidt) {
                case NVME_NIDT_EUI64:
                case NVME_NIDT_NGUID:
                    /* already have these from nvme_identify_ns() */
                    break;
                case NVME_NIDT_UUID:
                    uuid_unparse (d->nid, uuid_buf);
                    info->uuid = g_strdup (uuid_buf);
                    break;
                case NVME_NIDT_CSI:
                    /* unused */
                    break;
            }
        }
    }

    /* translate the LBA Format array */
    ptr_array = g_ptr_array_new ();
    for (i = 0; i < ns_info.nlbaf; i++) {
        BDNVMELBAFormat *lbaf = g_new0 (BDNVMELBAFormat, 1);
        lbaf->data_size = 1 << ns_info.lbaf[i].ds;
        lbaf->relative_performance = ns_info.lbaf[i].rp + 1;
        g_ptr_array_add (ptr_array, lbaf);
    }
    g_ptr_array_add (ptr_array, NULL);  /* trailing NULL element */
    info->lba_formats = (BDNVMELBAFormat **) g_ptr_array_free (ptr_array, FALSE);

    flbas = ns_info.flbas & NVME_NS_FLBAS_LBA_MASK;
    /* Note that for some devices nlbaf may be zero and flbas is thus invalid */
    if (flbas < ns_info.nlbaf) {
        g_warn_if_fail (info->lba_formats[flbas] != NULL);
        info->current_lba_format.data_size = info->lba_formats[flbas]->data_size;
        info->current_lba_format.relative_performance = info->lba_formats[flbas]->relative_performance;
    }

    return info;
}


/**
 * bd_nvme_get_smart_log:
 * @device: a NVMe controller device (e.g. /dev/nvme0)
 * @error: (out): place to store error (if any)
 *
 * Retrieves drive SMART and general health information (Log Identifier %02h).
 * The information provided is over the life of the controller and is retained across power cycles.
 *
 * Returns: (transfer full): health log data or %NULL in case of an error (with @error set).
 *
 * Tech category: %BD_NVME_TECH_NVME-%BD_NVME_TECH_MODE_INFO
 */
BDNVMESmartLog * bd_nvme_get_smart_log (const gchar *device, GError **error) {
    int ret;
    int ret_identify;
    int fd;
    struct nvme_id_ctrl ctrl_id = { };
    struct nvme_smart_log smart_log = { };
    BDNVMESmartLog *log;
    guint i;

    /* open the block device */
    fd = open_dev (device, error);
    if (fd < 0)
        return NULL;

    /* send the NVME_IDENTIFY_CNS_NS + NVME_IDENTIFY_CNS_CTRL ioctl */
    ret_identify = nvme_identify_ctrl (fd, &ctrl_id);
    if (ret_identify < 0) {
        /* generic errno errors */
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "NVMe Identify Controller command error: %s", strerror_l (errno, c_locale));
        close (fd);
        return NULL;
    }
    if (ret_identify > 0) {
        /* NVMe status codes */
        _nvme_status_to_error (ret_identify, FALSE, error);
        g_prefix_error (error, "NVMe Identify Controller command error: ");
        close (fd);
        return NULL;
    }

    /* send the NVME_LOG_LID_SMART ioctl */
    ret = nvme_get_log_smart (fd, NVME_NSID_ALL, FALSE /* rae */, &smart_log);
    if (ret < 0) {
        /* generic errno errors */
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "NVMe Get Log Page - SMART / Health Information Log command error: %s", strerror_l (errno, c_locale));
        close (fd);
        return NULL;
    }
    close (fd);
    if (ret > 0) {
        /* NVMe status codes */
        _nvme_status_to_error (ret, FALSE, error);
        g_prefix_error (error, "NVMe Get Log Page - SMART / Health Information Log command error: ");
        return NULL;
    }

    log = g_new0 (BDNVMESmartLog, 1);
    if ((smart_log.critical_warning & NVME_SMART_CRIT_SPARE) == NVME_SMART_CRIT_SPARE)
        log->critical_warning |= BD_NVME_SMART_CRITICAL_WARNING_SPARE;
    if ((smart_log.critical_warning & NVME_SMART_CRIT_TEMPERATURE) == NVME_SMART_CRIT_TEMPERATURE)
        log->critical_warning |= BD_NVME_SMART_CRITICAL_WARNING_TEMPERATURE;
    if ((smart_log.critical_warning & NVME_SMART_CRIT_DEGRADED) == NVME_SMART_CRIT_DEGRADED)
        log->critical_warning |= BD_NVME_SMART_CRITICAL_WARNING_DEGRADED;
    if ((smart_log.critical_warning & NVME_SMART_CRIT_MEDIA) == NVME_SMART_CRIT_MEDIA)
        log->critical_warning |= BD_NVME_SMART_CRITICAL_WARNING_READONLY;
    if ((smart_log.critical_warning & NVME_SMART_CRIT_VOLATILE_MEMORY) == NVME_SMART_CRIT_VOLATILE_MEMORY)
        log->critical_warning |= BD_NVME_SMART_CRITICAL_WARNING_VOLATILE_MEM;
    if ((smart_log.critical_warning & NVME_SMART_CRIT_PMR_RO) == NVME_SMART_CRIT_PMR_RO)
        log->critical_warning |= BD_NVME_SMART_CRITICAL_WARNING_PMR_READONLY;
    log->avail_spare = smart_log.avail_spare;
    log->spare_thresh = smart_log.spare_thresh;
    log->percent_used = smart_log.percent_used;
    log->total_data_read = int128_to_guint64 (smart_log.data_units_read) * 1000 * 512;
    log->total_data_written = int128_to_guint64 (smart_log.data_units_written) * 1000 * 512;
    log->ctrl_busy_time = int128_to_guint64 (smart_log.ctrl_busy_time);
    log->power_cycles = int128_to_guint64 (smart_log.power_cycles);
    log->power_on_hours = int128_to_guint64 (smart_log.power_on_hours);
    log->unsafe_shutdowns = int128_to_guint64 (smart_log.unsafe_shutdowns);
    log->media_errors = int128_to_guint64 (smart_log.media_errors);
    log->num_err_log_entries = int128_to_guint64 (smart_log.num_err_log_entries);

    log->temperature = ((smart_log.temperature[1] << 8) | smart_log.temperature[0]) - 273;
    for (i = 0; i < G_N_ELEMENTS (smart_log.temp_sensor); i++)
        log->temp_sensors[i] = le16_to_cpu (smart_log.temp_sensor[i]) - 273;
    log->warning_temp_time = le32_to_cpu (smart_log.warning_temp_time);
    log->critical_temp_time = le32_to_cpu (smart_log.critical_comp_time);

    if (ret_identify == 0) {
        log->wctemp = le16_to_cpu (ctrl_id.wctemp);
        log->cctemp = le16_to_cpu (ctrl_id.cctemp);
    }

    /* FIXME: intentionally not providing Host Controlled Thermal Management attributes
     *        at the moment (an optional NVMe feature), along with intentionally not providing
     *        Power State attributes. Subject to re-evaluation in the future.
     */

    return log;
}


/**
 * bd_nvme_get_error_log_entries:
 * @device: a NVMe controller device (e.g. /dev/nvme0)
 * @error: (out): place to store error (if any)
 *
 * Retrieves Error Information Log (Log Identifier %01h) entries, used to describe
 * extended error information for a command that completed with error or to report
 * an error that is not specific to a particular command. This log is global to the
 * controller. The ordering of the entries is based on the time when the error
 * occurred, with the most recent error being returned as the first log entry.
 * As the number of entries is typically limited by the drive implementation, only
 * most recent entries are provided.
 *
 * Returns: (transfer full) (array zero-terminated=1) (element-type BDNVMEErrorLogEntry):
 *          null-terminated list of error entries or %NULL in case of an error (with @error set).
 *
 * Tech category: %BD_NVME_TECH_NVME-%BD_NVME_TECH_MODE_INFO
 */
BDNVMEErrorLogEntry ** bd_nvme_get_error_log_entries (const gchar *device, GError **error) {
    int ret;
    int fd;
    guint elpe;
    struct nvme_id_ctrl ctrl_id = { };
    struct nvme_error_log_page *err_log;
    GPtrArray *ptr_array;
    guint i;

    /* open the block device */
    fd = open_dev (device, error);
    if (fd < 0)
        return NULL;

    /* find out the maximum number of error log entries as reported by the controller */
    ret = nvme_identify_ctrl (fd, &ctrl_id);
    if (ret < 0) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "NVMe Identify Controller command error: %s", strerror_l (errno, c_locale));
        close (fd);
        return NULL;
    }
    if (ret > 0) {
        _nvme_status_to_error (ret, FALSE, error);
        g_prefix_error (error, "NVMe Identify Controller command error: ");
        close (fd);
        return NULL;
    }

    /* send the NVME_LOG_LID_ERROR ioctl */
    elpe = ctrl_id.elpe + 1;
    err_log = g_new0 (struct nvme_error_log_page, elpe);
    ret = nvme_get_log_error (fd, elpe, FALSE /* rae */, err_log);
    if (ret < 0) {
        g_set_error (error, BD_NVME_ERROR, BD_NVME_ERROR_FAILED,
                     "NVMe Get Log Page - Error Information Log Entry command error: %s", strerror_l (errno, c_locale));
        close (fd);
        g_free (err_log);
        return NULL;
    }
    close (fd);
    if (ret > 0) {
        _nvme_status_to_error (ret, FALSE, error);
        g_prefix_error (error, "NVMe Get Log Page - Error Information Log Entry command error: ");
        g_free (err_log);
        return NULL;
    }

    /* parse the log */
    ptr_array = g_ptr_array_new ();
    for (i = 0; i < elpe; i++) {
        if (le64_to_cpu (err_log[i].error_count) > 0) {
            BDNVMEErrorLogEntry *entry;

            entry = g_new0 (BDNVMEErrorLogEntry, 1);
            entry->error_count = le64_to_cpu (err_log[i].error_count);
            entry->command_id = err_log[i].cmdid;
            entry->command_specific = le64_to_cpu (err_log[i].cs);
            entry->command_status = le16_to_cpu (err_log[i].status_field) >> 1;
            _nvme_status_to_error (le16_to_cpu (err_log[i].status_field) >> 1, FALSE, &entry->command_error);
            entry->lba = le64_to_cpu (err_log[i].lba);
            entry->nsid = err_log[i].nsid;
            entry->transport_type = err_log[i].trtype;
            /* not providing Transport Type Specific Information here on purpose */

            g_ptr_array_add (ptr_array, entry);
        }
    }
    g_ptr_array_add (ptr_array, NULL);  /* trailing NULL element */
    g_free (err_log);

    return (BDNVMEErrorLogEntry **) g_ptr_array_free (ptr_array, FALSE);
}
