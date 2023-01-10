/*
 * Copyright (C) 2014-2023 Red Hat, Inc.
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

#include <json-glib/json-glib.h>

#include <blockdev/utils.h>
#include <check_deps.h>
#include "smart.h"

/**
 * SECTION: smart
 * @short_description: SMART device reporting and management.
 * @title: SMART
 * @include: smart.h
 *
 * A plugin for SMART device reporting and management, based around smartmontools.
 */

#define SMARTCTL_MIN_VERSION "7.0"

/**
 * bd_smart_error_quark: (skip)
 */
GQuark bd_smart_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-smart-error-quark");
}

static volatile guint avail_deps = 0;
static GMutex deps_check_lock;

#define DEPS_SMART 0
#define DEPS_SMART_MASK (1 << DEPS_SMART)
#define DEPS_LAST 1

static const UtilDep deps[DEPS_LAST] = {
    { "smartctl", SMARTCTL_MIN_VERSION, NULL, "smartctl ([\\d\\.]+) .*" },
};

/**
 * bd_smart_check_deps:
 *
 * Returns: whether the plugin's runtime dependencies are satisfied or not
 *
 * Function checking plugin's runtime dependencies.
 */
gboolean bd_smart_check_deps (void) {
    GError *error = NULL;
    guint i = 0;
    gboolean status = FALSE;
    gboolean ret = TRUE;

    for (i = 0; i < DEPS_LAST; i++) {
        status = bd_utils_check_util_version (deps[i].name, deps[i].version,
                                              deps[i].ver_arg, deps[i].ver_regexp, &error);
        if (!status)
            bd_utils_log_format (BD_UTILS_LOG_WARNING, "%s", error->message);
        else
            g_atomic_int_or (&avail_deps, 1 << i);
        g_clear_error (&error);
        ret = ret && status;
    }

    if (!ret)
        bd_utils_log_format (BD_UTILS_LOG_WARNING, "Cannot load the SMART plugin");

    return ret;
}

/**
 * bd_smart_init:
 *
 * Initializes the plugin. **This function is called automatically by the
 * library's initialization functions.**
 *
 */
gboolean bd_smart_init (void) {
    /* nothing to do here */
    return TRUE;
};

/**
 * bd_smart_close:
 *
 * Cleans up after the plugin. **This function is called automatically by the
 * library's functions that unload it.**
 *
 */
void bd_smart_close (void) {
    /* nothing to do here */
}

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
    /* all tech-mode combinations are supported by this implementation of the plugin */
    return check_deps (&avail_deps, DEPS_SMART_MASK, deps, DEPS_LAST, &deps_check_lock, error);
}


/**
 * bd_smart_ata_attribute_free: (skip)
 * @attr: (nullable): %BDSmartATAAttribute to free
 *
 * Frees @attr.
 */
void bd_smart_ata_attribute_free (BDSmartATAAttribute *attr) {
    if (attr == NULL)
        return;
    g_free (attr->name);
    g_free (attr->value_raw_string);
    g_free (attr);
}

/**
 * bd_smart_ata_attribute_copy: (skip)
 * @attr: (nullable): %BDSmartATAAttribute to copy
 *
 * Creates a new copy of @attr.
 */
BDSmartATAAttribute * bd_smart_ata_attribute_copy (BDSmartATAAttribute *attr) {
    BDSmartATAAttribute *new_attr;

    if (attr == NULL)
        return NULL;

    new_attr = g_new0 (BDSmartATAAttribute, 1);
    memcpy (new_attr, attr, sizeof (BDSmartATAAttribute));
    new_attr->name = g_strdup (attr->name);
    new_attr->value_raw_string = g_strdup (attr->value_raw_string);

    return new_attr;
}

/**
 * bd_smart_ata_free: (skip)
 * @data: (nullable): %BDSmartATA to free
 *
 * Frees @data.
 */
void bd_smart_ata_free (BDSmartATA *data) {
    BDSmartATAAttribute **attr;

    if (data == NULL)
        return;

    for (attr = data->attributes; attr && *attr; attr++)
        bd_smart_ata_attribute_free (*attr);
    g_free (data->attributes);
    g_free (data);
}

/**
 * bd_smart_ata_copy: (skip)
 * @data: (nullable): %BDSmartATA to copy
 *
 * Creates a new copy of @data.
 */
BDSmartATA * bd_smart_ata_copy (BDSmartATA *data) {
    BDSmartATA *new_data;
    BDSmartATAAttribute **attr;
    GPtrArray *ptr_array;

    if (data == NULL)
        return NULL;

    new_data = g_new0 (BDSmartATA, 1);
    memcpy (new_data, data, sizeof (BDSmartATA));

    ptr_array = g_ptr_array_new ();
    for (attr = data->attributes; attr && *attr; attr++)
        g_ptr_array_add (ptr_array, bd_smart_ata_attribute_copy (*attr));
    g_ptr_array_add (ptr_array, NULL);
    new_data->attributes = (BDSmartATAAttribute **) g_ptr_array_free (ptr_array, FALSE);

    return new_data;
}


static const gchar * get_error_message_from_exit_code (gint exit_code) {
    /*
     * bit 0: Command line did not parse.
     * bit 1: Device open failed, device did not return an IDENTIFY DEVICE structure, or device is in a low-power mode
     * bit 2: Some SMART or other ATA command to the disk failed, or there was a checksum error in a SMART data structure
     */

    if (exit_code & 0x01)
        return "Command line did not parse.";
    if (exit_code & 0x02)
        return "Device open failed or device did not return an IDENTIFY DEVICE structure.";
    if (exit_code & 0x04)
        return "Some SMART or other ATA command to the disk failed, or there was a checksum error in a SMART data structure.";
    return NULL;
}

/* Returns num elements read or -1 in case of an error. */
static gint parse_int_array (JsonReader *reader, const gchar *key, gint64 *dest, gint max_count, GError **error) {
    gint count;
    int i;

    if (! json_reader_read_member (reader, key)) {
        g_set_error_literal (error, BD_SMART_ERROR, BD_SMART_ERROR_INVALID_ARGUMENT,
                             json_reader_get_error (reader)->message);
        json_reader_end_member (reader);
        return -1;
    }

    count = MIN (max_count, json_reader_count_elements (reader));
    if (count < 0) {
        g_set_error_literal (error, BD_SMART_ERROR, BD_SMART_ERROR_INVALID_ARGUMENT,
                             json_reader_get_error (reader)->message);
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (! json_reader_read_element (reader, i)) {
            g_set_error_literal (error, BD_SMART_ERROR, BD_SMART_ERROR_INVALID_ARGUMENT,
                                 json_reader_get_error (reader)->message);
            json_reader_end_element (reader);
            return -1;
        }
        dest[i] = json_reader_get_int_value (reader);
        json_reader_end_element (reader);
    }

    json_reader_end_member (reader);
    return count;
}

/* Returns null-terminated list of messages marked as severity=error */
static gchar ** parse_error_messages (JsonReader *reader) {
    GPtrArray *a;
    gint count;
    int i;

    if (! json_reader_read_member (reader, "smartctl")) {
        json_reader_end_member (reader);
        return NULL;
    }
    if (! json_reader_read_member (reader, "messages")) {
        json_reader_end_member (reader);
        json_reader_end_member (reader);
        return NULL;
    }

    a = g_ptr_array_new_full (0, g_free);
    count = json_reader_count_elements (reader);

    for (i = 0; count >= 0 && i < count; i++) {
        if (! json_reader_read_element (reader, i)) {
            json_reader_end_element (reader);
            g_ptr_array_free (a, TRUE);
            return NULL;
        }
        if (json_reader_is_object (reader)) {
            gboolean severity_error = FALSE;

            if (json_reader_read_member (reader, "severity"))
                severity_error = g_strcmp0 ("error", json_reader_get_string_value (reader)) == 0;
            json_reader_end_member (reader);

            if (severity_error) {
                if (json_reader_read_member (reader, "string")) {
                    const gchar *val = json_reader_get_string_value (reader);
                    if (val)
                        g_ptr_array_add (a, g_strdup (val));
                }
                json_reader_end_member (reader);
            }
        }
        json_reader_end_element (reader);
    }
    json_reader_end_member (reader);
    json_reader_end_member (reader);

    g_ptr_array_add (a, NULL);
    return (gchar **) g_ptr_array_free (a, FALSE);
}


#define STANDBY_RET_CODE 255      /* custom return code to indicate sleeping drive */
#define MIN_JSON_FORMAT_VER 1     /* minimal json_format_version */

static gboolean parse_smartctl_error (gint wait_status, const gchar *stdout, const gchar *stderr, gboolean nowakeup, JsonParser *parser, GError **error) {
    gint status = 0;
    gint res;
    JsonReader *reader;
    gint64 ver_info[2] = { 0, 0 };
    GError *l_error = NULL;

#if !GLIB_CHECK_VERSION(2, 69, 0)
#define g_spawn_check_wait_status(x,y) (g_spawn_check_exit_status (x,y))
#endif

    if (!g_spawn_check_wait_status (wait_status, &l_error)) {
        if (g_error_matches (l_error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED)) {
            /* process was terminated abnormally (e.g. using a signal) */
            g_propagate_error (error, l_error);
            return FALSE;
        }
        status = l_error->code;
        g_clear_error (&l_error);
    }

    if (nowakeup && status == STANDBY_RET_CODE) {
        g_set_error_literal (error, BD_SMART_ERROR, BD_SMART_ERROR_DRIVE_SLEEPING,
                             "Device is in a low-power mode");
        return FALSE;
    }
    if ((!stdout || strlen (stdout) == 0) &&
        (!stderr || strlen (stderr) == 0)) {
        g_set_error_literal (error, BD_SMART_ERROR, BD_SMART_ERROR_FAILED,
                             (status & 0x07) ? get_error_message_from_exit_code (status) : "Empty response");
        return FALSE;
    }
    /* Expecting proper JSON output on stdout, take what has been received on stderr */
    if (!stdout || strlen (stdout) == 0) {
        g_set_error_literal (error, BD_SMART_ERROR, BD_SMART_ERROR_FAILED,
                             stderr);
        return FALSE;
    }

    /* Parse the JSON output */
    if (! json_parser_load_from_data (parser, stdout, -1, &l_error) ||
        ! json_parser_get_root (parser)) {
        g_set_error_literal (error, BD_SMART_ERROR, BD_SMART_ERROR_INVALID_ARGUMENT,
                             l_error->message);
        g_error_free (l_error);
        return FALSE;
    }
    reader = json_reader_new (json_parser_get_root (parser));

    /* Verify the JSON output format */
    res = parse_int_array (reader, "json_format_version", ver_info, G_N_ELEMENTS (ver_info), error);
    if (res < 1) {
        g_prefix_error (error, "Error parsing version info: ");
        g_object_unref (reader);
        return FALSE;
    }
    if (ver_info[0] < MIN_JSON_FORMAT_VER) {
        g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_INVALID_ARGUMENT,
                     "Reported smartctl JSON format version too low: %" G_GUINT64_FORMAT " (required: %d)",
                     ver_info[0], MIN_JSON_FORMAT_VER);
        g_object_unref (reader);
        return FALSE;
    }
    if (ver_info[0] > MIN_JSON_FORMAT_VER)
        g_warning ("Reported smartctl JSON format major version higher than expected, expect parse issues");

    /* Find out the return code and associated messages */
    if (status & 0x07) {
        gchar **messages;

        messages = parse_error_messages (reader);
        g_set_error_literal (error, BD_SMART_ERROR, BD_SMART_ERROR_FAILED,
                             messages && messages[0] ? messages[0] : get_error_message_from_exit_code (status));
        g_strfreev (messages);
        g_object_unref (reader);
        return FALSE;
    }

    g_object_unref (reader);
    return TRUE;
}

static BDSmartATAAttribute ** parse_ata_smart_attributes (JsonReader *reader, GError **error) {
    GPtrArray *ptr_array;
    gint count;
    int i;

    ptr_array = g_ptr_array_new_full (0, (GDestroyNotify) bd_smart_ata_attribute_free);
    count = json_reader_count_elements (reader);
    for (i = 0; count > 0 && i < count; i++) {
        BDSmartATAAttribute *attr;
        gint64 f;

        if (! json_reader_read_element (reader, i)) {
            g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_INVALID_ARGUMENT,
                         "Error parsing the ata_smart_attributes[%d] element: %s",
                         i, json_reader_get_error (reader)->message);
            g_ptr_array_free (ptr_array, TRUE);
            json_reader_end_element (reader);
            return NULL;
        }

        attr = g_new0 (BDSmartATAAttribute, 1);

#define _READ_AND_CHECK(elem_name) \
        if (! json_reader_read_member (reader, elem_name)) { \
            g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_INVALID_ARGUMENT, \
                         "Error parsing the ata_smart_attributes[%d] element: %s", \
                         i, json_reader_get_error (reader)->message); \
            g_ptr_array_free (ptr_array, TRUE); \
            bd_smart_ata_attribute_free (attr); \
            json_reader_end_member (reader); \
            json_reader_end_element (reader); \
            return NULL; \
        }

        _READ_AND_CHECK ("id");
        attr->id = json_reader_get_int_value (reader);
        json_reader_end_member (reader);

        _READ_AND_CHECK ("name");
        attr->name = g_strdup (json_reader_get_string_value (reader));
        json_reader_end_member (reader);

        _READ_AND_CHECK ("value");
        attr->value = json_reader_get_int_value (reader);
        json_reader_end_member (reader);

        _READ_AND_CHECK ("worst");
        attr->worst = json_reader_get_int_value (reader);
        json_reader_end_member (reader);

        _READ_AND_CHECK ("thresh");
        attr->threshold = json_reader_get_int_value (reader);
        json_reader_end_member (reader);

        _READ_AND_CHECK ("when_failed");
        if (g_strcmp0 (json_reader_get_string_value (reader), "past") == 0)
            attr->failed_past = TRUE;
        else
        if (g_strcmp0 (json_reader_get_string_value (reader), "now") == 0)
            attr->failing_now = TRUE;
        json_reader_end_member (reader);

        _READ_AND_CHECK ("raw");
        _READ_AND_CHECK ("value");
        attr->value_raw = json_reader_get_int_value (reader);
        json_reader_end_member (reader);
        _READ_AND_CHECK ("string");
        attr->value_raw_string = g_strdup (json_reader_get_string_value (reader));
        json_reader_end_member (reader);
        json_reader_end_member (reader);

        _READ_AND_CHECK ("flags");
        _READ_AND_CHECK ("value");
        f = json_reader_get_int_value (reader);
        if (f & 0x01)
            attr->flags |= BD_SMART_ATA_ATTRIBUTE_FLAG_PREFAILURE;
        if (f & 0x02)
            attr->flags |= BD_SMART_ATA_ATTRIBUTE_FLAG_ONLINE;
        if (f & 0x04)
            attr->flags |= BD_SMART_ATA_ATTRIBUTE_FLAG_PERFORMANCE;
        if (f & 0x08)
            attr->flags |= BD_SMART_ATA_ATTRIBUTE_FLAG_ERROR_RATE;
        if (f & 0x10)
            attr->flags |= BD_SMART_ATA_ATTRIBUTE_FLAG_EVENT_COUNT;
        if (f & 0x20)
            attr->flags |= BD_SMART_ATA_ATTRIBUTE_FLAG_SELF_PRESERVING;
        if (f & 0xffc0)
            attr->flags |= BD_SMART_ATA_ATTRIBUTE_FLAG_OTHER;
        json_reader_end_member (reader);
        json_reader_end_member (reader);

#undef _READ_AND_CHECK

        json_reader_end_element (reader);
        g_ptr_array_add (ptr_array, attr);
    }

    g_ptr_array_add (ptr_array, NULL);
    return (BDSmartATAAttribute **) g_ptr_array_free (ptr_array, FALSE);
}

static BDSmartATA * parse_ata_smart (JsonParser *parser, GError **error) {
    BDSmartATA *data;
    JsonReader *reader;

    data = g_new0 (BDSmartATA, 1);
    reader = json_reader_new (json_parser_get_root (parser));

    /* smart_support section */
    if (json_reader_read_member (reader, "smart_support")) {
        if (json_reader_read_member (reader, "available"))
            data->smart_supported = json_reader_get_boolean_value (reader);
        json_reader_end_member (reader);
        if (json_reader_read_member (reader, "enabled"))
            data->smart_enabled = json_reader_get_boolean_value (reader);
        json_reader_end_member (reader);
    }
    json_reader_end_member (reader);

    /* smart_status section */
    if (json_reader_read_member (reader, "smart_status")) {
        if (json_reader_read_member (reader, "passed"))
            data->overall_status_passed = json_reader_get_boolean_value (reader);
        json_reader_end_member (reader);
    }
    json_reader_end_member (reader);

    /* ata_smart_data section */
    if (! json_reader_read_member (reader, "ata_smart_data")) {
        g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_INVALID_ARGUMENT,
                     "Missing 'ata_smart_data' section: %s",
                     json_reader_get_error (reader)->message);
        g_object_unref (reader);
        bd_smart_ata_free (data);
        return NULL;
    }
    if (json_reader_read_member (reader, "offline_data_collection")) {
        if (json_reader_read_member (reader, "status")) {
            if (json_reader_read_member (reader, "value")) {
                gint64 val = json_reader_get_int_value (reader);

                switch (val & 0x7f) {
                    case 0x00:
                        data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_NEVER_STARTED;
                        break;
                    case 0x02:
                        data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_NO_ERROR;
                        break;
                    case 0x03:
                        if (val == 0x03)
                            data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_IN_PROGRESS;
                        else
                            data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_RESERVED;
                        break;
                    case 0x04:
                        data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_SUSPENDED_INTR;
                        break;
                    case 0x05:
                        data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_ABORTED_INTR;
                        break;
                    case 0x06:
                        data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_ABORTED_ERROR;
                        break;
                    default:
                        if ((val & 0x7f) >= 0x40)
                            data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_VENDOR_SPECIFIC;
                        else
                            data->offline_data_collection_status = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_RESERVED;
                        break;
                }
                data->auto_offline_data_collection_enabled = val & 0x80;
            }
            json_reader_end_member (reader);
        }
        json_reader_end_member (reader);
        if (json_reader_read_member (reader, "completion_seconds"))
            data->offline_data_collection_completion = json_reader_get_int_value (reader);
        json_reader_end_member (reader);
    }
    json_reader_end_member (reader);  /* offline_data_collection */

    if (json_reader_read_member (reader, "self_test")) {
        if (json_reader_read_member (reader, "status")) {
            if (json_reader_read_member (reader, "value")) {
                gint64 val = json_reader_get_int_value (reader);

                switch (val >> 4) {
                    case 0x00:
                        data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_COMPLETED_NO_ERROR;
                        break;
                    case 0x01:
                        data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ABORTED_HOST;
                        break;
                    case 0x02:
                        data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_INTR_HOST_RESET;
                        break;
                    case 0x03:
                        data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ERROR_FATAL;
                        break;
                    case 0x04:
                        data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ERROR_UNKNOWN;
                        break;
                    case 0x05:
                        data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ERROR_ELECTRICAL;
                        break;
                    case 0x06:
                        data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ERROR_SERVO;
                        break;
                    case 0x07:
                        data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ERROR_READ;
                        break;
                    case 0x08:
                        data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_ERROR_HANDLING;
                        break;
                    case 0x0f:
                        data->self_test_status = BD_SMART_ATA_SELF_TEST_STATUS_IN_PROGRESS;
                        data->self_test_percent_remaining = (val & 0x0f) * 10;
                        break;
                }
            }
            json_reader_end_member (reader);
        }
        json_reader_end_member (reader);
        if (json_reader_read_member (reader, "polling_minutes")) {
            if (json_reader_read_member (reader, "short"))
                data->self_test_polling_short = json_reader_get_int_value (reader);
            json_reader_end_member (reader);
            if (json_reader_read_member (reader, "extended"))
                data->self_test_polling_extended = json_reader_get_int_value (reader);
            json_reader_end_member (reader);
            if (json_reader_read_member (reader, "conveyance"))
                data->self_test_polling_conveyance = json_reader_get_int_value (reader);
            json_reader_end_member (reader);
        }
        json_reader_end_member (reader);
    }
    json_reader_end_member (reader);  /* self_test */

    if (json_reader_read_member (reader, "capabilities")) {
        gint64 val[2] = { 0, 0 };

        if (parse_int_array (reader, "values", val, G_N_ELEMENTS (val), NULL) == G_N_ELEMENTS (val)) {
            if (val[0] == 0x00)
                data->offline_data_collection_capabilities = BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_NOT_SUPPORTED;
            else {
                if (val[0] & 0x01)
                    data->offline_data_collection_capabilities |= BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_EXEC_OFFLINE_IMMEDIATE;
                /* 0x02 is deprecated - SupportAutomaticTimer */
                if (val[0] & 0x04)
                    data->offline_data_collection_capabilities |= BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_OFFLINE_ABORT;
                if (val[0] & 0x08)
                    data->offline_data_collection_capabilities |= BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_OFFLINE_SURFACE_SCAN;
                if (val[0] & 0x10)
                    data->offline_data_collection_capabilities |= BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_SELF_TEST;
                if (val[0] & 0x20)
                    data->offline_data_collection_capabilities |= BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_CONVEYANCE_SELF_TEST;
                if (val[0] & 0x40)
                    data->offline_data_collection_capabilities |= BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_SELECTIVE_SELF_TEST;
            }
            if (val[1] & 0x01)
                data->smart_capabilities |= BD_SMART_ATA_CAP_ATTRIBUTE_AUTOSAVE;
            if (val[1] & 0x02)
                data->smart_capabilities |= BD_SMART_ATA_CAP_AUTOSAVE_TIMER;
        }
        if (json_reader_read_member (reader, "error_logging_supported"))
            if (json_reader_get_boolean_value (reader))
                data->smart_capabilities |= BD_SMART_ATA_CAP_ERROR_LOGGING;
        json_reader_end_member (reader);
        if (json_reader_read_member (reader, "gp_logging_supported"))
            if (json_reader_get_boolean_value (reader))
                data->smart_capabilities |= BD_SMART_ATA_CAP_GP_LOGGING;
        json_reader_end_member (reader);
    }
    json_reader_end_member (reader);  /* capabilities */
    json_reader_end_member (reader);  /* ata_smart_data */

    /* ata_smart_attributes section */
    if (! json_reader_read_member (reader, "ata_smart_attributes") ||
        ! json_reader_read_member (reader, "table") ||
        ! json_reader_is_array (reader)) {
        g_set_error (error, BD_SMART_ERROR, BD_SMART_ERROR_INVALID_ARGUMENT,
                     "Error parsing the 'ata_smart_attributes' section: %s",
                     json_reader_get_error (reader)->message);
        g_object_unref (reader);
        bd_smart_ata_free (data);
        return NULL;
    }
    data->attributes = parse_ata_smart_attributes (reader, error);
    if (! data->attributes) {
        g_object_unref (reader);
        bd_smart_ata_free (data);
        return NULL;
    }
    json_reader_end_member (reader);  /* table */
    json_reader_end_member (reader);  /* ata_smart_attributes */

    /* power_on_time section */
    if (json_reader_read_member (reader, "power_on_time")) {
        if (json_reader_read_member (reader, "hours"))
            data->power_on_time += json_reader_get_int_value (reader) * 60;
        json_reader_end_member (reader);
        if (json_reader_read_member (reader, "minutes"))
            data->power_on_time += json_reader_get_int_value (reader);
        json_reader_end_member (reader);
    }
    json_reader_end_member (reader);

    /* power_cycle_count section */
    if (json_reader_read_member (reader, "power_cycle_count"))
        data->power_cycle_count = json_reader_get_int_value (reader);
    json_reader_end_member (reader);

    /* temperature section */
    if (json_reader_read_member (reader, "temperature")) {
        if (json_reader_read_member (reader, "current"))
            data->temperature = json_reader_get_int_value (reader) + 273;
        json_reader_end_member (reader);
    }
    json_reader_end_member (reader);

    g_object_unref (reader);
    return data;
}

/**
 * bd_smart_ata_get_info:
 * @device: device to check.
 * @nowakeup: prevent drive waking up if in a low-power mode.
 * @error: (out) (optional): place to store error (if any).
 *
 * Retrieve SMART information from the drive.
 *
 * Specify @nowakeup to prevent drive spinning up when in a low-power mode,
 * #BD_SMART_ERROR_DRIVE_SLEEPING will be returned in such case. Note that
 * smartctl may actually return this error on non-ATA devices or when
 * device identification fails.
 *
 * Returns: (transfer full): ATA SMART log or %NULL in case of an error (with @error set).
 *
 * Tech category: %BD_SMART_TECH_ATA-%BD_SMART_TECH_MODE_INFO
 */
BDSmartATA * bd_smart_ata_get_info (const gchar *device, gboolean nowakeup, GError **error) {
    const gchar *args[11] = { "smartctl", "--info", "--health", "--capabilities", "--attributes", "--json", "--nocheck=never", "--device=ata" /* TODO */, "--badsum=ignore", device, NULL };
    gint wait_status = 0;
    gchar *stdout = NULL;
    gchar *stderr = NULL;
    JsonParser *parser;
    BDSmartATA *data = NULL;
    gboolean ret;

    if (nowakeup)
        args[6] = "--nocheck=standby," G_STRINGIFY (STANDBY_RET_CODE);

    /* TODO: set UTF-8 locale for JSON? */
    if (!g_spawn_sync (NULL /* working_directory */,
                       (gchar **) args,
                       NULL /* envp */,
                       G_SPAWN_SEARCH_PATH,
                       NULL /* child_setup */,
                       NULL /* user_data */,
                       &stdout,
                       &stderr,
                       &wait_status,
                       error)) {
        g_prefix_error (error, "Error getting ATA SMART info: ");
        return NULL;
    }

    if (stdout)
        g_strstrip (stdout);
    if (stderr)
        g_strstrip (stderr);

    parser = json_parser_new ();
    ret = parse_smartctl_error (wait_status, stdout, stderr, nowakeup, parser, error);
    g_free (stdout);
    g_free (stderr);
    if (! ret) {
        g_prefix_error (error, "Error getting ATA SMART info: ");
        g_object_unref (parser);
        return NULL;
    }

    data = parse_ata_smart (parser, error);
    g_object_unref (parser);

    return data;
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
gboolean bd_smart_set_enabled (const gchar *device, gboolean enabled, GError **error) {
    const gchar *args[5] = { "smartctl", "--json", "--smart=on", device, NULL };
    gint wait_status = 0;
    gchar *stdout = NULL;
    gchar *stderr = NULL;
    JsonParser *parser;
    gboolean ret;

    if (!enabled)
        args[2] = "--smart=off";

    /* TODO: set UTF-8 locale for JSON? */
    if (!g_spawn_sync (NULL /* working_directory */,
                       (gchar **) args,
                       NULL /* envp */,
                       G_SPAWN_SEARCH_PATH,
                       NULL /* child_setup */,
                       NULL /* user_data */,
                       &stdout,
                       &stderr,
                       &wait_status,
                       error)) {
        g_prefix_error (error, "Error setting SMART functionality: ");
        return FALSE;
    }

    if (stdout)
        g_strstrip (stdout);
    if (stderr)
        g_strstrip (stderr);

    parser = json_parser_new ();
    ret = parse_smartctl_error (wait_status, stdout, stderr, FALSE, parser, error);
    g_free (stdout);
    g_free (stderr);
    g_object_unref (parser);
    if (! ret) {
        g_prefix_error (error, "Error setting SMART functionality: ");
        return FALSE;
    }

    return TRUE;
}
