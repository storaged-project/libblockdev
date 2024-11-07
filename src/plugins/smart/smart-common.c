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
 * SECTION: smart
 * @short_description: S.M.A.R.T. device reporting and management.
 * @title: SMART
 * @include: smart.h
 *
 * Plugin for ATA and SCSI/SAS S.M.A.R.T. device reporting and management. For NVMe
 * health reporting please use the native [`nvme`][libblockdev-NVMe] plugin.
 *
 * This libblockdev plugin strives to provide good enough abstraction on top of vastly
 * different backend implementations. Two plugin implementations are available:
 * `libatasmart` (default) and `smartmontools` (experimental).
 *
 * Not all plugin implementations provide full functionality and it is advised
 * to use standard libblockdev tech query functions for feature availability testing.
 * For example, the `libatasmart` plugin only provides ATA functionality and error
 * is returned when any SCSI function is called.
 *
 * ## libatasmart plugin #
 *
 * An implementation proven for over a decade, being essentially a port of UDisks code.
 * The `libatasmart` library is reasonably lightweight with minimal dependencies,
 * light on I/O and consuming C API directly with clearly defined data types.
 * However essentially no quirks or any drive database is present in the library
 * (apart from a couple of very old laptop drives).
 *
 * ## smartmontools plugin #
 *
 * In contrast to libatasmart, the smartmontools project is a feature-rich
 * implementation supporting specialties like vendor-specific data blocks. It is
 * a considerably heavier implementation I/O-wise due to device type detection and
 * retrieval of more data blocks from the drive.
 *
 * There's no C API at the moment and the plugin resorts to executing the `smartctl`
 * command and parsing its JSON output, that is by nature loosely-defined. This
 * presents challenges in data type conversions, interpretation of printed values
 * and volatile JSON key presence. Besides, executing external commands always brings
 * certain performance overhead and caution is advised when retrieving SMART data
 * from multiple drives in parallel.
 *
 * ## Attribute naming and value interpretation #
 *
 * Check #BDSmartATAAttribute for the struct members overview first. The plugin
 * public API provides both the implementation-specific attribute names/values
 * as well as unified ('well-known', translated) interpretation that is preferred
 * for general use.
 *
 * The `well_known_name` property follows the libatasmart-style naming -
 * e.g. `'power-on-hours'`. Unknown or untrusted attributes are either provided
 * in the form of `'attribute-123'` or by %NULL.
 *
 * Similarly, value of an attribute is provided in variety of interpretations,
 * subject to availability:
 * - the `value`, `worst` and `threshold` are normalized values in typical S.M.A.R.T. fashion
 * - the `value_raw` as a 64-bit untranslated value with no further context
 *   of which bits are actually valid for a particular attribute
 * - the `pretty_value_string` as an implementation-specific string representation,
 *   intended for end-user display
 * - the `pretty_value` and `pretty_value_unit` as a libatasmart-style of unified value/type pair
 *
 * Both libblockdev plugins strive for best effort of providing accurate values,
 * however there are often challenges ranging from string-to-number conversion,
 * multiple values being unpacked from a single raw number or not having enough
 * context provided by the underlying library for a trusted value interpretation.
 *
 * ## Attribute validation #
 *
 * It may seem obvious to use numerical attribute ID as an authoritative attribute
 * identifier, however in reality drive vendors tend not to stick with public
 * specifications. Attributes are often reused for vendor-specific values and this
 * differs from model to model even for a single vendor. This is more often the case
 * with SSD drives than traditional HDDs.
 *
 * Historically it brought confusion and false alarms on user's end and eventually
 * led to some form of quirk database in most projects. Maintaining such database
 * is a lifetime task and the only successful effort is the smartmontools' `drivedb.h`
 * collection. Quirks are needed for about everything - meaning of a particular
 * attribute (i.e. a 'well-known' name), interpretation of a raw value, all this
 * filtered by drive model string and firmware revision.
 *
 * However even there not everything is consistent and slight variations in
 * a 'well-known' name can be found. Besides, the attribute naming syntax differs
 * from our chosen libatasmart-style form.
 *
 * For this reason an internal libblockdev translation table has been introduced
 * to ensure a bit of consistency. The translation table is kept conservative,
 * is by no means complete and may get extended in future libblockdev releases.
 * As a result, some attributes may be reported as 'untrusted' or 'unknown'.
 *
 * The translation table at this point doesn't handle 'moves' where a different
 * attribute ID has been assigned for otherwise well defined attribute.
 *
 * An experimental `drivedb.h` parser is provided for the libatasmart plugin
 * as an additional tier of validation based on actual drive model + firmware match.
 * Being a C header file, the `drivedb.h` definitions are compiled in the plugin.
 * There's no support for loading external `drivedb.h` file though. This however
 * only serves for validation. Providing backwards mapping to libatasmart-style
 * of attributes is considered as a TODO.
 *
 * ## Device type detection, multipath #
 *
 * There's a big difference in how a drive is accessed. While `libatasmart` performs
 * only very basic device type detection based on parent subsystem as retrieved from
 * the udev database, `smartctl` implements logic to determine which protocol to use,
 * supporting variety of passthrough mechanisms and interface bridges. Such detection
 * is not always reliable though, having known issues with `dm-multipath` for example.
 *
 * For this case most plugin functions consume the `extra` argument allowing
 * callers to specify arguments such as `--device=` for device type override. This
 * is only supported by the smartmontools plugin and ignored by the libatasmart
 * plugin.
 *
 * As a well kept secret libatasmart has historically supported device type override
 * via the `ID_ATA_SMART_ACCESS` udev property. There's no public C API for this and
 * libblockdev generally tends to avoid any udev interaction, leaving the burden
 * to callers.
 *
 * Valid values for this property include:
 * - `'sat16'`: 16 Byte SCSI ATA SAT Passthru
 * - `'sat12'`: 12 Byte SCSI ATA SAT Passthru
 * - `'linux-ide'`: Native Linux IDE
 * - `'sunplus'`: SunPlus USB/ATA bridges
 * - `'jmicron'`: JMicron USB/ATA bridges
 * - `'none'`: No access method, avoid any I/O and ignore the device
 * - `'auto'`: Autodetection based on parent subsystem (default)
 *
 * A common example to override QEMU ATA device type, which often requires legacy
 * IDE protocol:
 * |[
 * KERNEL=="sd*", ENV{ID_VENDOR}=="ATA", ENV{ID_MODEL}=="QEMU_HARDDISK", ENV{ID_ATA}=="1", ENV{ID_ATA_SMART_ACCESS}="linux-ide"
 * ]|
 *
 * An example of blacklisting a USB device, in case of errors caused by reading SMART data:
 * |[
 * ATTRS{idVendor}=="152d", ATTRS{idProduct}=="2329", ENV{ID_ATA_SMART_ACCESS}="none"
 * ]|
 */

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
