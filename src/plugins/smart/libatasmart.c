/*
 * Copyright (C) 2014-2024 Red Hat, Inc.
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
#include <errno.h>

#include <atasmart.h>

#include <blockdev/utils.h>
#include <check_deps.h>

#include "smart.h"
#include "smart-private.h"

/**
 * SECTION: smart
 * @short_description: SMART device reporting and management.
 * @title: SMART
 * @include: smart.h
 *
 * A plugin for SMART device reporting and management, based on libatasmart.
 */

/**
 * bd_smart_is_tech_avail:
 * @tech: the queried tech
 * @mode: a bit mask of queried modes of operation (#BDSmartTechMode) for @tech
 * @error: (out) (nullable): place to store error (details about why the @tech-@mode combination is not available)
 *
 * Returns: whether the @tech-@mode combination is available -- supported by the
 *          plugin implementation and having all the runtime dependencies available
 */
gboolean bd_smart_is_tech_avail (G_GNUC_UNUSED BDSmartTech tech, G_GNUC_UNUSED guint64 mode, GError **error) {
    switch (tech) {
        case BD_SMART_TECH_ATA:
            return TRUE;
        case BD_SMART_TECH_SCSI:
            g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_TECH_UNAVAIL, "SCSI SMART is unavailable with libatasmart");
            return FALSE;
        default:
            g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_TECH_UNAVAIL, "Unknown technology");
            return FALSE;
    }
}

/* copied from libatasmart/atasmart.c (a non-public symbol) */
static gchar *print_value (uint64_t pretty_value, SkSmartAttributeUnit pretty_unit) {
    switch (pretty_unit) {
        case SK_SMART_ATTRIBUTE_UNIT_MSECONDS:
            if (pretty_value >= 1000LLU*60LLU*60LLU*24LLU*365LLU)
                return g_strdup_printf ("%0.1f years", ((double) pretty_value)/(1000.0*60*60*24*365));
            if (pretty_value >= 1000LLU*60LLU*60LLU*24LLU*30LLU)
                return g_strdup_printf ("%0.1f months", ((double) pretty_value)/(1000.0*60*60*24*30));
            if (pretty_value >= 1000LLU*60LLU*60LLU*24LLU)
                return g_strdup_printf ("%0.1f days", ((double) pretty_value)/(1000.0*60*60*24));
            if (pretty_value >= 1000LLU*60LLU*60LLU)
                return g_strdup_printf ("%0.1f h", ((double) pretty_value)/(1000.0*60*60));
            if (pretty_value >= 1000LLU*60LLU)
                return g_strdup_printf ("%0.1f min", ((double) pretty_value)/(1000.0*60));
            if (pretty_value >= 1000LLU)
                return g_strdup_printf ("%0.1f s", ((double) pretty_value)/(1000.0));
            else
                return g_strdup_printf ("%llu ms", (unsigned long long) pretty_value);
            break;
        case SK_SMART_ATTRIBUTE_UNIT_MKELVIN:
            return g_strdup_printf ("%0.1f C", ((double) pretty_value - 273150) / 1000);
            break;
        case SK_SMART_ATTRIBUTE_UNIT_SECTORS:
            return g_strdup_printf ("%llu sectors", (unsigned long long) pretty_value);
            break;
        case SK_SMART_ATTRIBUTE_UNIT_PERCENT:
            return g_strdup_printf ("%llu%%", (unsigned long long) pretty_value);
            break;
        case SK_SMART_ATTRIBUTE_UNIT_SMALL_PERCENT:
            return g_strdup_printf ("%0.3f%%", (double) pretty_value);
            break;
        case SK_SMART_ATTRIBUTE_UNIT_MB:
            if (pretty_value >= 1000000LLU)
                return g_strdup_printf ("%0.3f TB",  (double) pretty_value / 1000000LLU);
            if (pretty_value >= 1000LLU)
                return g_strdup_printf ("%0.3f GB",  (double) pretty_value / 1000LLU);
            else
                return g_strdup_printf ("%llu MB", (unsigned long long) pretty_value);
            break;
        case SK_SMART_ATTRIBUTE_UNIT_NONE:
            return g_strdup_printf ("%llu", (unsigned long long) pretty_value);
            break;
        case SK_SMART_ATTRIBUTE_UNIT_UNKNOWN:
            return g_strdup_printf ("n/a");
            break;
        case _SK_SMART_ATTRIBUTE_UNIT_MAX:
            g_warn_if_reached ();
            return NULL;
    }
    return NULL;
}

static void parse_attr_cb (G_GNUC_UNUSED SkDisk             *d,
                           const SkSmartAttributeParsedData *a,
                           void                             *user_data) {
    GPtrArray *ptr_array = user_data;
    BDSmartATAAttribute *attr;

    attr = g_new0 (BDSmartATAAttribute, 1);
    attr->id = a->id;
    attr->name = g_strdup (a->name);
    attr->well_known_name = g_strdup (a->name);
    attr->value = a->current_value_valid ? a->current_value : -1;
    attr->worst = a->worst_value_valid ? a->worst_value : -1;
    attr->threshold = a->threshold_valid ? a->threshold : -1;
    attr->failed_past = attr->worst > 0 && attr->threshold > 0 && attr->worst <= attr->threshold;
    attr->failing_now = attr->value > 0 && attr->threshold > 0 && attr->value <= attr->threshold;
    attr->value_raw = ((uint64_t) a->raw[0]) |
                      (((uint64_t) a->raw[1]) << 8) |
                      (((uint64_t) a->raw[2]) << 16) |
                      (((uint64_t) a->raw[3]) << 24) |
                      (((uint64_t) a->raw[4]) << 32) |
                      (((uint64_t) a->raw[5]) << 40);
    attr->flags = a->flags;
    attr->pretty_value = a->pretty_value;
    switch (a->pretty_unit) {
        case SK_SMART_ATTRIBUTE_UNIT_UNKNOWN:
            attr->pretty_value_unit = BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN;
            break;
        case SK_SMART_ATTRIBUTE_UNIT_NONE:
            attr->pretty_value_unit = BD_SMART_ATA_ATTRIBUTE_UNIT_NONE;
            break;
        case SK_SMART_ATTRIBUTE_UNIT_MSECONDS:
            attr->pretty_value_unit = BD_SMART_ATA_ATTRIBUTE_UNIT_MSECONDS;
            break;
        case SK_SMART_ATTRIBUTE_UNIT_SECTORS:
            attr->pretty_value_unit = BD_SMART_ATA_ATTRIBUTE_UNIT_SECTORS;
            break;
        case SK_SMART_ATTRIBUTE_UNIT_MKELVIN:
            attr->pretty_value_unit = BD_SMART_ATA_ATTRIBUTE_UNIT_MKELVIN;
            break;
        case SK_SMART_ATTRIBUTE_UNIT_SMALL_PERCENT:
            attr->pretty_value_unit = BD_SMART_ATA_ATTRIBUTE_UNIT_SMALL_PERCENT;
            break;
        case SK_SMART_ATTRIBUTE_UNIT_PERCENT:
            attr->pretty_value_unit = BD_SMART_ATA_ATTRIBUTE_UNIT_PERCENT;
            break;
        case SK_SMART_ATTRIBUTE_UNIT_MB:
            attr->pretty_value_unit = BD_SMART_ATA_ATTRIBUTE_UNIT_MB;
            break;
        default:
            attr->pretty_value_unit = BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN;
            g_warn_if_reached ();
    }
    attr->pretty_value_string = print_value (a->pretty_value, a->pretty_unit);

    g_ptr_array_add (ptr_array, attr);
}

static BDSmartATA * parse_sk_data (SkDisk *d, GError **error) {
    SkBool good = FALSE;
    SkBool available = FALSE;
    SkSmartOverall overall = SK_SMART_OVERALL_GOOD;
    uint64_t temp_mkelvin = 0;
    uint64_t power_on_msec = 0;
    const SkSmartParsedData *parsed_data;
    BDSmartATA *data;
    GPtrArray *ptr_array;

    if (sk_disk_smart_read_data (d) != 0) {
        g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_FAILED,
                     "Error reading SMART data from device: %s",
                     strerror_l (errno, _C_LOCALE));
        return NULL;
    }

    if (sk_disk_smart_status (d, &good) != 0) {
        g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_FAILED,
                     "Error checking SMART data status: %s",
                     strerror_l (errno, _C_LOCALE));
        return NULL;
    }

    if (sk_disk_smart_parse (d, &parsed_data) != 0) {
        g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_FAILED,
                     "Error parsing SMART data: %s",
                     strerror_l (errno, _C_LOCALE));
        return NULL;
    }

    data = g_new0 (BDSmartATA, 1);

    sk_disk_smart_is_available (d, &available);
    data->smart_supported = available;
    /* At this point when SMART is not and cannot be enabled,
     * sk_disk_smart_read_data() would've already returned an error.
     */
    data->smart_enabled = TRUE;

    sk_disk_smart_get_overall (d, &overall);
    data->overall_status_passed = overall == SK_SMART_OVERALL_GOOD;

    switch (parsed_data->offline_data_collection_status) {
        case SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_NEVER:
            data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_NEVER_STARTED;
            break;
        case SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_SUCCESS:
            data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_NO_ERROR;
            break;
        case SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_INPROGRESS:
            data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_IN_PROGRESS;
            break;
        case SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_SUSPENDED:
            data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_SUSPENDED_INTR;
            break;
        case SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_ABORTED:
            data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_ABORTED_INTR;
            break;
        case SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_FATAL:
            data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_ABORTED_ERROR;
            break;
        case SK_SMART_OFFLINE_DATA_COLLECTION_STATUS_UNKNOWN:
            data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_VENDOR_SPECIFIC;
            break;
        default:
            g_warn_if_reached ();
    }

    data->auto_offline_data_collection_enabled = FALSE;   /* TODO */
    data->offline_data_collection_completion = parsed_data->total_offline_data_collection_seconds;
    data->offline_data_collection_capabilities = 0;       /* TODO */

    switch (parsed_data->self_test_execution_status) {
        case SK_SMART_SELF_TEST_EXECUTION_STATUS_SUCCESS_OR_NEVER:
            data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_COMPLETED_NO_ERROR;
            break;
        case SK_SMART_SELF_TEST_EXECUTION_STATUS_ABORTED:
            data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ABORTED_HOST;
            break;
        case SK_SMART_SELF_TEST_EXECUTION_STATUS_INTERRUPTED:
            data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_INTR_HOST_RESET;
            break;
        case SK_SMART_SELF_TEST_EXECUTION_STATUS_FATAL:
            data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ERROR_FATAL;
            break;
        case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_UNKNOWN:
            data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ERROR_UNKNOWN;
            break;
        case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_ELECTRICAL:
            data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ERROR_ELECTRICAL;
            break;
        case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_SERVO:
            data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ERROR_SERVO;
            break;
        case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_READ:
            data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ERROR_READ;
            break;
        case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_HANDLING:
            data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ERROR_HANDLING;
            break;
        case SK_SMART_SELF_TEST_EXECUTION_STATUS_INPROGRESS:
            data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_IN_PROGRESS;
            break;
        default:
            g_warn_if_reached ();
    }

    data->self_test_percent_remaining = parsed_data->self_test_execution_percent_remaining;
    data->self_test_polling_short = parsed_data->short_test_polling_minutes;
    data->self_test_polling_extended = parsed_data->extended_test_polling_minutes;
    data->self_test_polling_conveyance = parsed_data->conveyance_test_polling_minutes;

    data->smart_capabilities = 0;    /* TODO */

    sk_disk_smart_get_power_on (d, &power_on_msec);
    data->power_on_time = power_on_msec / 1000 / 60;

    sk_disk_smart_get_power_cycle (d, &data->power_cycle_count);

    sk_disk_smart_get_temperature (d, &temp_mkelvin);
    data->temperature = temp_mkelvin / 1000;

    ptr_array = g_ptr_array_new_full (0, (GDestroyNotify) bd_smart_ata_attribute_free);
    if (sk_disk_smart_parse_attributes (d, parse_attr_cb, ptr_array) != 0) {
        g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_FAILED,
                     "Error parsing SMART data: %s",
                     strerror_l (errno, _C_LOCALE));
        g_ptr_array_free (ptr_array, TRUE);
        bd_smart_ata_free (data);
        return NULL;
    }
    g_ptr_array_add (ptr_array, NULL);
    data->attributes = (BDSmartATAAttribute **) g_ptr_array_free (ptr_array, FALSE);

    return data;
}

/**
 * bd_smart_ata_get_info:
 * @device: device to check.
 * @error: (out) (optional): place to store error (if any).
 *
 * Retrieve SMART information from the drive.
 *
 * Returns: (transfer full): ATA SMART log or %NULL in case of an error (with @error set).
 *
 * Tech category: %BD_SMART_TECH_ATA-%BD_SMART_TECH_MODE_INFO
 */
BDSmartATA * bd_smart_ata_get_info (const gchar *device, GError **error) {
    SkDisk *d;
    BDSmartATA *data;

    g_warn_if_fail (device != NULL);

    if (sk_disk_open (device, &d) != 0) {
        g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_FAILED,
                     "Error opening device %s: %s",
                     device,
                     strerror_l (errno, _C_LOCALE));
        return NULL;
    }

    data = parse_sk_data (d, error);
    sk_disk_free (d);

    return data;
}


/**
 * bd_smart_ata_get_info_from_data:
 * @data: (array length=data_len): binary data to parse.
 * @data_len: length of the data supplied.
 * @error: (out) (optional): place to store error (if any).
 *
 * Retrieve SMART information from the supplied data.
 *
 * Returns: (transfer full): ATA SMART log or %NULL in case of an error (with @error set).
 *
 * Tech category: %BD_SMART_TECH_ATA-%BD_SMART_TECH_MODE_INFO
 */
BDSmartATA * bd_smart_ata_get_info_from_data (const guint8 *data, gsize data_len, GError **error) {
    SkDisk *d;
    BDSmartATA *ata_data;

    g_warn_if_fail (data != NULL);
    g_warn_if_fail (data_len > 0);

    if (sk_disk_open (NULL, &d) != 0) {
        g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_FAILED,
                     "Error parsing blob data: %s",
                     strerror_l (errno, _C_LOCALE));
        return NULL;
    }

    if (sk_disk_set_blob (d, data, data_len) != 0) {
        g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_FAILED,
                     "Error parsing blob data: %s",
                     strerror_l (errno, _C_LOCALE));
        return NULL;
    }

    ata_data = parse_sk_data (d, error);
    sk_disk_free (d);

    return ata_data;
}


/**
 * bd_smart_scsi_get_info:
 * @device: device to check.
 * @error: (out) (optional): place to store error (if any).
 *
 * Retrieve SMART information from SCSI or SAS-compliant drive.
 *
 * Returns: (transfer full): SCSI SMART log or %NULL in case of an error (with @error set).
 *
 * Tech category: %BD_SMART_TECH_SCSI-%BD_SMART_TECH_MODE_INFO
 */
BDSmartSCSI * bd_smart_scsi_get_info (G_GNUC_UNUSED const gchar *device, GError **error) {
    g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_TECH_UNAVAIL, "SCSI SMART is unavailable with libatasmart");
    return FALSE;
}


/**
 * bd_smart_set_enabled:
 * @device: SMART-capable device.
 * @enabled: whether to enable or disable the SMART functionality
 * @error: (out) (optional): place to store error (if any).
 *
 * Enables or disables SMART functionality on device.
 *
 * Returns: %TRUE when the functionality was set successfully or %FALSE in case of an error (with @error set).
 *
 * Tech category: %BD_SMART_TECH_ATA-%BD_SMART_TECH_MODE_INFO
 */
gboolean bd_smart_set_enabled (G_GNUC_UNUSED const gchar *device, G_GNUC_UNUSED gboolean enabled, GError **error) {
    g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_TECH_UNAVAIL, "Enabling/disabling ATA SMART functionality is unavailable with libatasmart");
    return FALSE;
}


/**
 * bd_smart_device_self_test:
 * @device: device to trigger the test on.
 * @operation: #BDSmartSelfTestOp self-test operation.
 * @error: (out) (optional): place to store error (if any).
 *
 * Executes or aborts device self-test.
 *
 * Returns: %TRUE when the self-test was trigerred successfully or %FALSE in case of an error (with @error set).
 *
 * Tech category: %BD_SMART_TECH_ATA-%BD_SMART_TECH_MODE_SELFTEST
 */
gboolean bd_smart_device_self_test (const gchar *device, BDSmartSelfTestOp operation, GError **error) {
    SkDisk *d;
    SkSmartSelfTest op;
    gboolean ret;

    switch (operation) {
        case BD_SMART_SELF_TEST_OP_ABORT:
            op = SK_SMART_SELF_TEST_ABORT;
            break;
        case BD_SMART_SELF_TEST_OP_SHORT:
            op = SK_SMART_SELF_TEST_SHORT;
            break;
        case BD_SMART_SELF_TEST_OP_LONG:
        case BD_SMART_SELF_TEST_OP_OFFLINE:
            op = SK_SMART_SELF_TEST_EXTENDED;
            break;
        case BD_SMART_SELF_TEST_OP_CONVEYANCE:
            op = SK_SMART_SELF_TEST_CONVEYANCE;
            break;
        default:
            g_set_error_literal (error, BD_SMART_ERROR, BD_SMART_ERROR_INVALID_ARGUMENT,
                                 "Invalid self-test operation.");
            return FALSE;
    }

    if (sk_disk_open (device, &d) != 0) {
        g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_FAILED,
                     "Error opening device %s: %s",
                     device,
                     strerror_l (errno, _C_LOCALE));
        return FALSE;
    }

    ret = sk_disk_smart_self_test (d, op) == 0;
    sk_disk_free (d);
    if (!ret) {
        g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_FAILED,
                     "Error triggering device self-test: %s",
                     strerror_l (errno, _C_LOCALE));
        return FALSE;
    }

    return TRUE;
}
