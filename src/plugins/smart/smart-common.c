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

#include <blockdev/utils.h>
#include <check_deps.h>

#include "smart.h"
#include "smart-private.h"


/**
 * bd_smart_error_quark: (skip)
 */
GQuark bd_smart_error_quark (void)
{
    return g_quark_from_static_string ("g-bd-smart-error-quark");
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
 * bd_smart_ata_attribute_free: (skip)
 * @attr: (nullable): %BDSmartATAAttribute to free
 *
 * Frees @attr.
 */
void bd_smart_ata_attribute_free (BDSmartATAAttribute *attr) {
    if (attr == NULL)
        return;
    g_free (attr->name);
    g_free (attr->well_known_name);
    g_free (attr->pretty_value_string);
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
    new_attr->well_known_name = g_strdup (attr->well_known_name);
    new_attr->pretty_value_string = g_strdup (attr->pretty_value_string);

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

/**
 * bd_smart_scsi_free: (skip)
 * @data: (nullable): %BDSmartSCSI to free
 *
 * Frees @data.
 */
void bd_smart_scsi_free (BDSmartSCSI *data) {
    if (data == NULL)
        return;

    g_free (data->scsi_ie_string);
    g_free (data);
}

/**
 * bd_smart_scsi_copy: (skip)
 * @data: (nullable): %BDSmartSCSI to copy
 *
 * Creates a new copy of @data.
 */
BDSmartSCSI * bd_smart_scsi_copy (BDSmartSCSI *data) {
    BDSmartSCSI *new_data;

    if (data == NULL)
        return NULL;

    new_data = g_new0 (BDSmartSCSI, 1);
    memcpy (new_data, data, sizeof (BDSmartSCSI));
    new_data->scsi_ie_string = g_strdup (data->scsi_ie_string);

    return new_data;
}
