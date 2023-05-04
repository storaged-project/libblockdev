#include <glib.h>
#include <glib-object.h>
#include <blockdev/utils.h>

#ifndef BD_NVME
#define BD_NVME

GQuark bd_nvme_error_quark (void);
#define BD_NVME_ERROR bd_nvme_error_quark ()

/**
 * BDNVMEError:
 * @BD_NVME_ERROR_TECH_UNAVAIL: NVMe support not available.
 * @BD_NVME_ERROR_FAILED: General error.
 * @BD_NVME_ERROR_BUSY: The device is temporarily unavailable or in an inconsistent state.
 * @BD_NVME_ERROR_INVALID_ARGUMENT: Invalid argument.
 * @BD_NVME_ERROR_WOULD_FORMAT_ALL_NS: The NVMe controller indicates that it would format all namespaces in the NVM subsystem.
 * @BD_NVME_ERROR_SC_GENERIC: Generic NVMe Command Status Code.
 * @BD_NVME_ERROR_SC_CMD_SPECIFIC: NVMe Command Specific error.
 * @BD_NVME_ERROR_SC_MEDIA: Media and Data Integrity Errors: media specific errors that occur in the NVM or data integrity type errors.
 * @BD_NVME_ERROR_SC_PATH: Path related error.
 * @BD_NVME_ERROR_SC_VENDOR_SPECIFIC: NVMe Vendor specific error.
 * @BD_NVME_ERROR_NO_MATCH: No matching resource found (e.g. a Fabrics Controller).
 * @BD_NVME_ERROR_CONNECT: General connection error.
 * @BD_NVME_ERROR_CONNECT_ALREADY: Already connected.
 * @BD_NVME_ERROR_CONNECT_INVALID: Invalid argument specified.
 * @BD_NVME_ERROR_CONNECT_ADDRINUSE: HostNQN already in use.
 * @BD_NVME_ERROR_CONNECT_NODEV: Invalid interface.
 * @BD_NVME_ERROR_CONNECT_OPNOTSUPP: Operation not supported.
 * @BD_NVME_ERROR_CONNECT_REFUSED: Connection refused.
 */
typedef enum {
    BD_NVME_ERROR_TECH_UNAVAIL,
    BD_NVME_ERROR_FAILED,
    BD_NVME_ERROR_BUSY,
    BD_NVME_ERROR_INVALID_ARGUMENT,
    BD_NVME_ERROR_WOULD_FORMAT_ALL_NS,
    BD_NVME_ERROR_SC_GENERIC,
    BD_NVME_ERROR_SC_CMD_SPECIFIC,
    BD_NVME_ERROR_SC_MEDIA,
    BD_NVME_ERROR_SC_PATH,
    BD_NVME_ERROR_SC_VENDOR_SPECIFIC,
    BD_NVME_ERROR_NO_MATCH,
    BD_NVME_ERROR_CONNECT,
    BD_NVME_ERROR_CONNECT_ALREADY,
    BD_NVME_ERROR_CONNECT_INVALID,
    BD_NVME_ERROR_CONNECT_ADDRINUSE,
    BD_NVME_ERROR_CONNECT_NODEV,
    BD_NVME_ERROR_CONNECT_OPNOTSUPP,
    BD_NVME_ERROR_CONNECT_REFUSED,
} BDNVMEError;

typedef enum {
    BD_NVME_TECH_NVME = 0,
    BD_NVME_TECH_FABRICS,
} BDNVMETech;

typedef enum {
    BD_NVME_TECH_MODE_INFO         = 1 << 0,
    BD_NVME_TECH_MODE_MANAGE       = 1 << 1,
    BD_NVME_TECH_MODE_INITIATOR    = 1 << 2,
} BDNVMETechMode;

/**
 * BDNVMEControllerFeature:
 * @BD_NVME_CTRL_FEAT_MULTIPORT: if set, then the NVM subsystem may contain more than one NVM subsystem port, otherwise it's single-port only.
 * @BD_NVME_CTRL_FEAT_MULTICTRL: if set, then the NVM subsystem may contain two or more controllers, otherwise contains only single controller.
 * @BD_NVME_CTRL_FEAT_SRIOV: if set, then the controller is associated with an SR-IOV Virtual Function, otherwise it's associated with a PCI Function or a Fabrics connection.
 * @BD_NVME_CTRL_FEAT_ANA_REPORTING: indicates that the NVM subsystem supports Asymmetric Namespace Access (ANA) Reporting.
 * @BD_NVME_CTRL_FEAT_FORMAT: indicates that the controller supports the Format NVM command.
 * @BD_NVME_CTRL_FEAT_FORMAT_ALL_NS: if set, then a format (excluding secure erase) of any namespace results in a format of all namespaces
 *                                   in an NVM subsystem with all namespaces in an NVM subsystem configured with the same attributes.
 *                                   If not set, then the controller supports format on a per namespace basis.
 * @BD_NVME_CTRL_FEAT_NS_MGMT: indicates that the controller supports the Namespace Management and Attachment capability.
 * @BD_NVME_CTRL_FEAT_SELFTEST: indicates that the controller supports the Device Self-test command.
 * @BD_NVME_CTRL_FEAT_SELFTEST_SINGLE: indicates that the NVM subsystem supports only one device self-test operation in progress at a time.
 * @BD_NVME_CTRL_FEAT_SANITIZE_CRYPTO: indicates that the controller supports the Crypto Erase sanitize operation.
 * @BD_NVME_CTRL_FEAT_SANITIZE_BLOCK: indicates that the controller supports the Block Erase sanitize operation.
 * @BD_NVME_CTRL_FEAT_SANITIZE_OVERWRITE: indicates that the controller supports the Overwrite sanitize operation.
 * @BD_NVME_CTRL_FEAT_SECURE_ERASE_ALL_NS: if set, then any secure erase performed as part of a format operation
 *                                         results in a secure erase of all namespaces in the NVM subsystem. If not set,
 *                                         then any secure erase performed as part of a format results in a secure erase
 *                                         of the particular namespace specified.
 * @BD_NVME_CTRL_FEAT_SECURE_ERASE_CRYPTO: indicates that the cryptographic erase is supported.
 * @BD_NVME_CTRL_FEAT_STORAGE_DEVICE: indicates that the NVM subsystem is part of an NVMe Storage Device.
 * @BD_NVME_CTRL_FEAT_ENCLOSURE: indicates that the NVM subsystem is part of an NVMe Enclosure.
 * @BD_NVME_CTRL_FEAT_MGMT_PCIE: indicates that the NVM subsystem contains a Management Endpoint on a PCIe port.
 * @BD_NVME_CTRL_FEAT_MGMT_SMBUS: indicates that the NVM subsystem contains a Management Endpoint on an SMBus/I2C port.
 */
typedef enum {
    BD_NVME_CTRL_FEAT_MULTIPORT           = 1 << 0,
    BD_NVME_CTRL_FEAT_MULTICTRL           = 1 << 1,
    BD_NVME_CTRL_FEAT_SRIOV               = 1 << 2,
    BD_NVME_CTRL_FEAT_ANA_REPORTING       = 1 << 3,
    BD_NVME_CTRL_FEAT_FORMAT              = 1 << 4,
    BD_NVME_CTRL_FEAT_FORMAT_ALL_NS       = 1 << 5,
    BD_NVME_CTRL_FEAT_NS_MGMT             = 1 << 6,
    BD_NVME_CTRL_FEAT_SELFTEST            = 1 << 7,
    BD_NVME_CTRL_FEAT_SELFTEST_SINGLE     = 1 << 8,
    BD_NVME_CTRL_FEAT_SANITIZE_CRYPTO     = 1 << 9,
    BD_NVME_CTRL_FEAT_SANITIZE_BLOCK      = 1 << 10,
    BD_NVME_CTRL_FEAT_SANITIZE_OVERWRITE  = 1 << 11,
    BD_NVME_CTRL_FEAT_SECURE_ERASE_ALL_NS = 1 << 12,
    BD_NVME_CTRL_FEAT_SECURE_ERASE_CRYPTO = 1 << 13,
    BD_NVME_CTRL_FEAT_STORAGE_DEVICE      = 1 << 14,
    BD_NVME_CTRL_FEAT_ENCLOSURE           = 1 << 15,
    BD_NVME_CTRL_FEAT_MGMT_PCIE           = 1 << 16,
    BD_NVME_CTRL_FEAT_MGMT_SMBUS          = 1 << 17,
} BDNVMEControllerFeature;

/**
 * BDNVMEControllerType:
 * @BD_NVME_CTRL_TYPE_UNKNOWN: Controller type not reported (as reported by older NVMe-compliant devices).
 * @BD_NVME_CTRL_TYPE_IO: I/O controller.
 * @BD_NVME_CTRL_TYPE_DISCOVERY: Discovery controller.
 * @BD_NVME_CTRL_TYPE_ADMIN: Administrative controller.
 */
typedef enum {
    BD_NVME_CTRL_TYPE_UNKNOWN = 0,
    BD_NVME_CTRL_TYPE_IO,
    BD_NVME_CTRL_TYPE_DISCOVERY,
    BD_NVME_CTRL_TYPE_ADMIN,
} BDNVMEControllerType;

/**
 * BDNVMEControllerInfo:
 * @pci_vendor_id: The PCI Vendor ID.
 * @pci_subsys_vendor_id: The PCI Subsystem Vendor ID.
 * @ctrl_id: Controller ID, the NVM subsystem unique controller identifier associated with the controller.
 * @fguid: FRU GUID, a 128-bit value that is globally unique for a given Field Replaceable Unit.
 * @model_number: The model number.
 * @serial_number: The serial number.
 * @firmware_ver: The currently active firmware revision.
 * @nvme_ver: The NVM Express base specification that the controller implementation supports or %NULL when not reported by the device.
 * @features: features and capabilities present for this controller, see #BDNVMEControllerFeature.
 * @controller_type: The controller type.
 * @selftest_ext_time: Extended Device Self-test Time, if #BD_NVME_CTRL_FEAT_SELFTEST is supported then this field
 *                     indicates the nominal amount of time in one minute units that the controller takes
 *                     to complete an extended device self-test operation when in power state 0.
 * @hmb_pref_size: Host Memory Buffer Preferred Size indicates the preferred size that the host
 *                 is requested to allocate for the Host Memory Buffer feature in bytes.
 * @hmb_min_size: Host Memory Buffer Minimum Size indicates the minimum size that the host
 *                is requested to allocate for the Host Memory Buffer feature in bytes.
 * @size_total: Total NVM Capacity in the NVM subsystem in bytes.
 * @size_unalloc: Unallocated NVM Capacity in the NVM subsystem in bytes.
 * @num_namespaces: Maximum Number of Allowed Namespaces supported by the NVM subsystem.
 * @subsysnqn: NVM Subsystem NVMe Qualified Name, UTF-8 null terminated string.
 */
typedef struct BDNVMEControllerInfo {
    guint16 pci_vendor_id;
    guint16 pci_subsys_vendor_id;
    guint16 ctrl_id;
    gchar *fguid;
    gchar *model_number;
    gchar *serial_number;
    gchar *firmware_ver;
    gchar *nvme_ver;
    guint64 features;
    BDNVMEControllerType controller_type;
    gint selftest_ext_time;
    guint64 hmb_pref_size;
    guint64 hmb_min_size;
    guint64 size_total;
    guint64 size_unalloc;
    guint num_namespaces;
    gchar *subsysnqn;
} BDNVMEControllerInfo;

/**
 * BDNVMELBAFormatRelativePerformance:
 * Performance index of the LBA format relative to other LBA formats supported by the controller.
 * @BD_NVME_LBA_FORMAT_RELATIVE_PERFORMANCE_UNKNOWN: Unknown relative performance index.
 * @BD_NVME_LBA_FORMAT_RELATIVE_PERFORMANCE_BEST: Best performance.
 * @BD_NVME_LBA_FORMAT_RELATIVE_PERFORMANCE_BETTER: Better performance.
 * @BD_NVME_LBA_FORMAT_RELATIVE_PERFORMANCE_GOOD: Good performance.
 * @BD_NVME_LBA_FORMAT_RELATIVE_PERFORMANCE_DEGRADED: Degraded performance.
 */
typedef enum {
    BD_NVME_LBA_FORMAT_RELATIVE_PERFORMANCE_UNKNOWN = 0,
    BD_NVME_LBA_FORMAT_RELATIVE_PERFORMANCE_BEST = 1,
    BD_NVME_LBA_FORMAT_RELATIVE_PERFORMANCE_BETTER = 2,
    BD_NVME_LBA_FORMAT_RELATIVE_PERFORMANCE_GOOD = 3,
    BD_NVME_LBA_FORMAT_RELATIVE_PERFORMANCE_DEGRADED = 4
} BDNVMELBAFormatRelativePerformance;

/**
 * BDNVMELBAFormat:
 * Namespace LBA Format Data Structure.
 * @data_size: LBA data size (i.e. a sector size) in bytes.
 * @metadata_size: metadata size in bytes or `0` in case of no metadata support.
 * @relative_performance: Relative Performance index, see #BDNVMELBAFormatRelativePerformance.
 */
typedef struct BDNVMELBAFormat {
    guint16 data_size;
    guint16 metadata_size;
    BDNVMELBAFormatRelativePerformance relative_performance;
} BDNVMELBAFormat;

/**
 * BDNVMENamespaceFeature:
 * @BD_NVME_NS_FEAT_THIN: indicates that the namespace supports thin provisioning. Specifically, the Namespace Capacity
 *                        reported may be less than the Namespace Size.
 * @BD_NVME_NS_FEAT_MULTIPATH_SHARED: indicates the capability to attach the namespace to two or more controllers
 *                                    in the NVM subsystem concurrently.
 * @BD_NVME_NS_FEAT_FORMAT_PROGRESS: indicates the capability to report the percentage of the namespace
 *                                   that remains to be formatted.
 * @BD_NVME_NS_FEAT_ROTATIONAL: indicates a rotational medium.
 */
typedef enum {
    BD_NVME_NS_FEAT_THIN             = 1 << 0,
    BD_NVME_NS_FEAT_MULTIPATH_SHARED = 1 << 1,
    BD_NVME_NS_FEAT_FORMAT_PROGRESS  = 1 << 2,
    BD_NVME_NS_FEAT_ROTATIONAL       = 1 << 3,
} BDNVMENamespaceFeature;

/**
 * BDNVMENamespaceInfo:
 * @nsid: The Namespace Identifier (NSID).
 * @eui64: IEEE Extended Unique Identifier: a 64-bit IEEE Extended Unique Identifier (EUI-64)
 *         that is globally unique and assigned to the namespace when the namespace is created.
 *         Remains fixed throughout the life of the namespace and is preserved across namespace
 *         and controller operations.
 * @nguid: Namespace Globally Unique Identifier: a 128-bit value that is globally unique and
 *         assigned to the namespace when the namespace is created. Remains fixed throughout
 *         the life of the namespace and is preserved across namespace and controller operations.
 * @uuid: Namespace 128-bit Universally Unique Identifier (UUID) as specified in RFC 4122.
 * @nsize: Namespace Size: total size of the namespace in logical blocks. The number of logical blocks
 *         is based on the formatted LBA size (see @current_lba_format).
 * @ncap: Namespace Capacity: maximum number of logical blocks that may be allocated in the namespace
 *        at any point in time. The number of logical blocks is based on the formatted LBA size (see @current_lba_format).
 * @nuse: Namespace Utilization: current number of logical blocks allocated in the namespace.
 *        This field is smaller than or equal to the Namespace Capacity. The number of logical
 *        blocks is based on the formatted LBA size (see @current_lba_format).
 * @features: features and capabilities present for this namespace, see #BDNVMENamespaceFeature.
 * @format_progress_remaining: The percentage value remaining of a format operation in progress.
 * @write_protected: %TRUE if the namespace is currently write protected and all write access to the namespace shall fail.
 * @lba_formats: (array zero-terminated=1) (element-type BDNVMELBAFormat): A list of supported LBA Formats.
 * @current_lba_format: A LBA Format currently used for the namespace. Contains zeroes in case of
 *                      an invalid or no supported LBA Format reported.
 */
typedef struct BDNVMENamespaceInfo {
    guint32 nsid;
    gchar *eui64;
    gchar *uuid;
    gchar *nguid;
    guint64 nsize;
    guint64 ncap;
    guint64 nuse;
    guint64 features;
    guint8 format_progress_remaining;
    gboolean write_protected;
    BDNVMELBAFormat **lba_formats;
    BDNVMELBAFormat current_lba_format;
} BDNVMENamespaceInfo;

/**
 * BDNVMESmartCriticalWarning:
 * @BD_NVME_SMART_CRITICAL_WARNING_SPARE: the available spare capacity has fallen below the threshold.
 * @BD_NVME_SMART_CRITICAL_WARNING_TEMPERATURE: a temperature is either greater than or equal to an over temperature threshold;
 *                                              or less than or equal to an under temperature threshold.
 * @BD_NVME_SMART_CRITICAL_WARNING_DEGRADED: the NVM subsystem reliability has been degraded due to  significant media
 *                                           related errors or any internal error that degrades NVM subsystem reliability.
 * @BD_NVME_SMART_CRITICAL_WARNING_READONLY: all of the media has been placed in read only mode. Unrelated to the write
 *                                           protection state of a namespace.
 * @BD_NVME_SMART_CRITICAL_WARNING_VOLATILE_MEM: the volatile memory backup device has failed. Valid only if the controller
 *                                               has a volatile memory backup solution.
 * @BD_NVME_SMART_CRITICAL_WARNING_PMR_READONLY: Persistent Memory Region has become read-only or unreliable.
 */
typedef enum {
    BD_NVME_SMART_CRITICAL_WARNING_SPARE        = 1 << 0,
    BD_NVME_SMART_CRITICAL_WARNING_TEMPERATURE  = 1 << 1,
    BD_NVME_SMART_CRITICAL_WARNING_DEGRADED     = 1 << 2,
    BD_NVME_SMART_CRITICAL_WARNING_READONLY     = 1 << 3,
    BD_NVME_SMART_CRITICAL_WARNING_VOLATILE_MEM = 1 << 4,
    BD_NVME_SMART_CRITICAL_WARNING_PMR_READONLY = 1 << 5,
} BDNVMESmartCriticalWarning;

/**
 * BDNVMESmartLog:
 * @critical_warning: critical warnings for the state of the controller, see #BDNVMESmartCriticalWarning.
 * @avail_spare: Available Spare: a normalized percentage (0% to 100%) of the remaining spare capacity available.
 * @spare_thresh: Available Spare Threshold: a normalized percentage (0% to 100%) of the available spare threshold.
 * @percent_used: Percentage Used: a vendor specific estimate of the percentage drive life used based on the
 *                actual usage and the manufacturer's prediction. A value of 100 indicates that the estimated
 *                endurance has been consumed, but may not indicate an NVM subsystem failure.
 *                The value is allowed to exceed 100.
 * @total_data_read: An estimated calculation of total data read in bytes based on calculation of data
 *                   units read from the host. A value of 0 indicates that the number of Data Units Read
 *                   is not reported.
 * @total_data_written: An estimated calculation of total data written in bytes based on calculation
 *                      of data units written by the host. A value of 0 indicates that the number
 *                      of Data Units Written is not reported.
 * @ctrl_busy_time: Amount of time the controller is busy with I/O commands, reported in minutes.
 * @power_cycles: The number of power cycles.
 * @power_on_hours: The number of power-on hours, excluding a non-operational power state.
 * @unsafe_shutdowns: The number of unsafe shutdowns as a result of a Shutdown Notification not received prior to loss of power.
 * @media_errors: Media and Data Integrity Errors: the number of occurrences where the controller detected
 *                an unrecovered data integrity error (e.g. uncorrectable ECC, CRC checksum failure, or LBA tag mismatch).
 * @num_err_log_entries: Number of Error Information Log Entries: the number of Error Information log
 *                       entries over the life of the controller.
 * @temperature: Composite Temperature: temperature in Kelvins that represents the current composite
 *               temperature of the controller and associated namespaces or 0 when not applicable.
 * @temp_sensors: Temperature Sensor 1-8: array of the current temperature reported by temperature sensors
 *                1-8 in Kelvins or 0 when the particular sensor is not available.
 * @wctemp: Warning Composite Temperature Threshold (WCTEMP): indicates the minimum Composite Temperature (@temperature)
 *          value that indicates an overheating condition during which controller operation continues.
 *          A value of 0 indicates that no warning temperature threshold value is reported by the controller.
 * @cctemp: Critical Composite Temperature Threshold (CCTEMP): indicates the minimum Composite Temperature (@temperature)
 *          value that indicates a critical overheating condition (e.g., may prevent continued normal operation,
 *          possibility of data loss, automatic device shutdown, extreme performance throttling, or permanent damage).
 *          A value of 0 indicates that no critical temperature threshold value is reported by the controller.
 * @warning_temp_time: Warning Composite Temperature Time: the amount of time in minutes that the Composite Temperature (@temperature)
 *                     is greater than or equal to the Warning Composite Temperature Threshold (@wctemp) and less than the
 *                     Critical Composite Temperature Threshold (@cctemp).
 * @critical_temp_time: Critical Composite Temperature Time: the amount of time in minutes that the Composite Temperature (@temperature)
 *                      is greater than or equal to the Critical Composite Temperature Threshold (@cctemp).
 */
typedef struct BDNVMESmartLog {
    guint critical_warning;
    guint8 avail_spare;
    guint8 spare_thresh;
    guint8 percent_used;
    guint64 total_data_read;
    guint64 total_data_written;
    guint64 ctrl_busy_time;
    guint64 power_cycles;
    guint64 power_on_hours;
    guint64 unsafe_shutdowns;
    guint64 media_errors;
    guint64 num_err_log_entries;
    guint16 temperature;
    guint16 temp_sensors[8];
    guint16 wctemp;
    guint16 cctemp;
    guint warning_temp_time;
    guint critical_temp_time;
} BDNVMESmartLog;

/**
 * BDNVMETransportType:
 * Transport Type.
 * @BD_NVME_TRANSPORT_TYPE_UNSPECIFIED: Not indicated
 * @BD_NVME_TRANSPORT_TYPE_RDMA: RDMA Transport
 * @BD_NVME_TRANSPORT_TYPE_FC: Fibre Channel Transport
 * @BD_NVME_TRANSPORT_TYPE_TCP: TCP Transport
 * @BD_NVME_TRANSPORT_TYPE_LOOP: Intra-host Transport (loopback)
 */
typedef enum {
    BD_NVME_TRANSPORT_TYPE_UNSPECIFIED = 0,
    BD_NVME_TRANSPORT_TYPE_RDMA        = 1,
    BD_NVME_TRANSPORT_TYPE_FC          = 2,
    BD_NVME_TRANSPORT_TYPE_TCP         = 3,
    BD_NVME_TRANSPORT_TYPE_LOOP        = 254
} BDNVMETransportType;

/**
 * BDNVMEErrorLogEntry:
 * @error_count: internal error counter, a unique identifier for the error.
 * @command_id: the Command Identifier of the command that the error is associated with or `0xffff` if the error is not specific to a particular command.
 * @command_specific: Command Specific Information specific to @command_id.
 * @command_status: the Status code for the command that completed.
 * @command_error: translated command error in the BD_NVME_ERROR domain or %NULL in case @command_status indicates success.
 * @lba: the first LBA that experienced the error condition.
 * @nsid: the NSID of the namespace that the error is associated with.
 * @transport_type: type of the transport associated with the error.
 */
typedef struct BDNVMEErrorLogEntry {
    guint64 error_count;
    guint16 command_id;
    guint64 command_specific;
    guint16 command_status;
    GError *command_error;
    guint64 lba;
    guint32 nsid;
    BDNVMETransportType transport_type;
} BDNVMEErrorLogEntry;

/**
 * BDNVMESelfTestAction:
 * Action taken by the Device Self-test command.
 * @BD_NVME_SELF_TEST_ACTION_NOT_RUNNING: No device self-test operation in progress. Not a valid argument for bd_nvme_device_self_test().
 * @BD_NVME_SELF_TEST_ACTION_SHORT: Start a short device self-test operation.
 * @BD_NVME_SELF_TEST_ACTION_EXTENDED: Start an extended device self-test operation.
 * @BD_NVME_SELF_TEST_ACTION_VENDOR_SPECIFIC: Start a vendor specific device self-test operation.
 * @BD_NVME_SELF_TEST_ACTION_ABORT: Abort the device self-test operation. Only valid for bd_nvme_device_self_test().
 */
typedef enum {
    BD_NVME_SELF_TEST_ACTION_NOT_RUNNING     = 0,
    BD_NVME_SELF_TEST_ACTION_SHORT           = 1,
    BD_NVME_SELF_TEST_ACTION_EXTENDED        = 2,
    BD_NVME_SELF_TEST_ACTION_VENDOR_SPECIFIC = 3,
    BD_NVME_SELF_TEST_ACTION_ABORT           = 4,
} BDNVMESelfTestAction;

/**
 * BDNVMESelfTestResult:
 * @BD_NVME_SELF_TEST_RESULT_NO_ERROR: Operation completed without error.
 * @BD_NVME_SELF_TEST_RESULT_ABORTED: Operation was aborted by a Device Self-test command.
 * @BD_NVME_SELF_TEST_RESULT_CTRL_RESET: Operation was aborted by a Controller Level Reset.
 * @BD_NVME_SELF_TEST_RESULT_NS_REMOVED: Operation was aborted due to a removal of a namespace from the namespace inventory.
 * @BD_NVME_SELF_TEST_RESULT_ABORTED_FORMAT: Operation was aborted due to the processing of a Format NVM command.
 * @BD_NVME_SELF_TEST_RESULT_FATAL_ERROR: A fatal error or unknown test error occurred while the controller was executing the device self-test operation and the operation did not complete.
 * @BD_NVME_SELF_TEST_RESULT_UNKNOWN_SEG_FAIL: Operation completed with a segment that failed and the segment that failed is not known.
 * @BD_NVME_SELF_TEST_RESULT_KNOWN_SEG_FAIL: Operation completed with one or more failed segments and the first segment that failed is indicated in the Segment Number field.
 * @BD_NVME_SELF_TEST_RESULT_ABORTED_UNKNOWN: Operation was aborted for unknown reason.
 * @BD_NVME_SELF_TEST_RESULT_ABORTED_SANITIZE: Operation was aborted due to a sanitize operation.
 */
typedef enum {
    BD_NVME_SELF_TEST_RESULT_NO_ERROR         = 0,
    BD_NVME_SELF_TEST_RESULT_ABORTED          = 1,
    BD_NVME_SELF_TEST_RESULT_CTRL_RESET       = 2,
    BD_NVME_SELF_TEST_RESULT_NS_REMOVED       = 3,
    BD_NVME_SELF_TEST_RESULT_ABORTED_FORMAT   = 4,
    BD_NVME_SELF_TEST_RESULT_FATAL_ERROR      = 5,
    BD_NVME_SELF_TEST_RESULT_UNKNOWN_SEG_FAIL = 6,
    BD_NVME_SELF_TEST_RESULT_KNOWN_SEG_FAIL   = 7,
    BD_NVME_SELF_TEST_RESULT_ABORTED_UNKNOWN  = 8,
    BD_NVME_SELF_TEST_RESULT_ABORTED_SANITIZE = 9,
} BDNVMESelfTestResult;

/**
 * BDNVMESelfTestLogEntry:
 * @result: Result of the device self-test operation.
 * @action: The Self-test Code value (action) that was specified in the Device Self-test command that started this device self-test operation.
 * @segment: Segment number where the first self-test failure occurred. Valid only when @result is set to #BD_NVME_SELF_TEST_RESULT_KNOWN_SEG_FAIL.
 * @power_on_hours: Number of power-on hours at the time the device self-test operation was completed or aborted. Does not include time that the controller was powered and in a low power state condition.
 * @nsid: Namespace ID that the Failing LBA occurred on.
 * @failing_lba: LBA of the logical block that caused the test to fail. If the device encountered more than one failed logical block during the test, then this field only indicates one of those failed logical blocks.
 * @status_code_error: Translated NVMe Command Status Code representing additional information related to errors or conditions.
 */
typedef struct BDNVMESelfTestLogEntry {
    BDNVMESelfTestResult result;
    BDNVMESelfTestAction action;
    guint8 segment;
    guint64 power_on_hours;
    guint32 nsid;
    guint64 failing_lba;
    GError *status_code_error;
} BDNVMESelfTestLogEntry;

/**
 * BDNVMESelfTestLog:
 * @current_operation: Current running device self-test operation. There's no corresponding record in @entries for a device self-test operation that is in progress.
 * @current_operation_completion: Percentage of the currently running device self-test operation. Only valid when @current_operation is other than #BD_NVME_SELF_TEST_ACTION_NOT_RUNNING.
 * @entries: (array zero-terminated=1) (element-type BDNVMESelfTestLogEntry): Self-test log entries for the last 20 operations, sorted from newest (first element) to oldest.
 */
typedef struct BDNVMESelfTestLog {
    BDNVMESelfTestAction current_operation;
    guint8 current_operation_completion;
    BDNVMESelfTestLogEntry **entries;
} BDNVMESelfTestLog;

/**
 * BDNVMEFormatSecureErase:
 * Optional Format NVM secure erase action.
 * @BD_NVME_FORMAT_SECURE_ERASE_NONE: No secure erase operation requested.
 * @BD_NVME_FORMAT_SECURE_ERASE_USER_DATA: User Data Erase: All user data shall be erased, contents of the user data after the erase is indeterminate
 *                                         (e.g., the user data may be zero filled, one filled, etc.). If a User Data Erase is requested and all affected
 *                                         user data is encrypted, then the controller is allowed to use a cryptographic erase to perform the requested User Data Erase.
 * @BD_NVME_FORMAT_SECURE_ERASE_CRYPTO: Cryptographic Erase: All user data shall be erased cryptographically. This is accomplished by deleting the encryption key.
 */
typedef enum {
    BD_NVME_FORMAT_SECURE_ERASE_NONE      = 0,
    BD_NVME_FORMAT_SECURE_ERASE_USER_DATA = 1,
    BD_NVME_FORMAT_SECURE_ERASE_CRYPTO    = 2,
} BDNVMEFormatSecureErase;

/**
 * BDNVMESanitizeStatus:
 * @BD_NVME_SANITIZE_STATUS_NEVER_SANITIZED: The NVM subsystem has never been sanitized.
 * @BD_NVME_SANITIZE_STATUS_IN_PROGESS: A sanitize operation is currently in progress.
 * @BD_NVME_SANITIZE_STATUS_SUCCESS: The most recent sanitize operation completed successfully including any additional media modification.
 * @BD_NVME_SANITIZE_STATUS_SUCCESS_NO_DEALLOC: The most recent sanitize operation for which No-Deallocate After Sanitize was requested has completed successfully with deallocation of all user data.
 * @BD_NVME_SANITIZE_STATUS_FAILED: The most recent sanitize operation failed.
 */
typedef enum {
    BD_NVME_SANITIZE_STATUS_NEVER_SANITIZED = 0,
    BD_NVME_SANITIZE_STATUS_IN_PROGESS = 1,
    BD_NVME_SANITIZE_STATUS_SUCCESS = 2,
    BD_NVME_SANITIZE_STATUS_SUCCESS_NO_DEALLOC = 3,
    BD_NVME_SANITIZE_STATUS_FAILED = 4,
} BDNVMESanitizeStatus;

/**
 * BDNVMESanitizeLog:
 * @sanitize_progress: The percentage complete of the sanitize operation.
 * @sanitize_status: The status of the most recent sanitize operation.
 * @global_data_erased: Indicates that no user data has been written either since the drive was manufactured and
 *                      has never been sanitized or since the most recent successful sanitize operation.
 * @overwrite_passes: Number of completed passes if the most recent sanitize operation was an Overwrite.
 * @time_for_overwrite: Estimated time in seconds needed to complete an Overwrite sanitize operation with 16 passes in the background.
 *                      A value of -1 means that no time estimate is reported. A value of 0 means that the operation is expected
 *                      to be completed in the background when the Sanitize command is completed.
 * @time_for_block_erase: Estimated time in seconds needed to complete a Block Erase sanitize operation in the background.
 *                        A value of -1 means that no time estimate is reported. A value of 0 means that the operation is expected
 *                        to be completed in the background when the Sanitize command is completed.
 * @time_for_crypto_erase: Estimated time in seconds needed to complete a Crypto Erase sanitize operation in the background.
 *                         A value of -1 means that no time estimate is reported. A value of 0 means that the operation is expected
 *                         to be completed in the background when the Sanitize command is completed.
 * @time_for_overwrite_nd: Estimated time in seconds needed to complete an Overwrite sanitize operation and the associated
 *                         additional media modification in the background when the No-Deallocate After Sanitize or
 *                         the No-Deallocate Modifies Media After Sanitize features have been requested.
 * @time_for_block_erase_nd: Estimated time in seconds needed to complete a Block Erase sanitize operation and the associated
 *                           additional media modification in the background when the No-Deallocate After Sanitize or
 *                           the No-Deallocate Modifies Media After Sanitize features have been requested.
 * @time_for_crypto_erase_nd: Estimated time in seconds needed to complete a Crypto Erase sanitize operation and the associated
 *                            additional media modification in the background when the No-Deallocate After Sanitize or
 *                            the No-Deallocate Modifies Media After Sanitize features have been requested.
 */
typedef struct BDNVMESanitizeLog {
    gdouble sanitize_progress;
    BDNVMESanitizeStatus sanitize_status;
    gboolean global_data_erased;
    guint8 overwrite_passes;
    gint64 time_for_overwrite;
    gint64 time_for_block_erase;
    gint64 time_for_crypto_erase;
    gint64 time_for_overwrite_nd;
    gint64 time_for_block_erase_nd;
    gint64 time_for_crypto_erase_nd;
} BDNVMESanitizeLog;

/**
 * BDNVMESanitizeAction:
 * @BD_NVME_SANITIZE_ACTION_EXIT_FAILURE: Exit Failure Mode.
 * @BD_NVME_SANITIZE_ACTION_BLOCK_ERASE: Start a Block Erase sanitize operation - a low-level block erase method that is specific to the media.
 * @BD_NVME_SANITIZE_ACTION_OVERWRITE: Start an Overwrite sanitize operation - writing a fixed data pattern or related patterns in multiple passes.
 * @BD_NVME_SANITIZE_ACTION_CRYPTO_ERASE: Start a Crypto Erase sanitize operation - changing the media encryption keys for all locations on the media.
 */
typedef enum {
    BD_NVME_SANITIZE_ACTION_EXIT_FAILURE = 0,
    BD_NVME_SANITIZE_ACTION_BLOCK_ERASE = 1,
    BD_NVME_SANITIZE_ACTION_OVERWRITE = 2,
    BD_NVME_SANITIZE_ACTION_CRYPTO_ERASE = 3,
} BDNVMESanitizeAction;


void bd_nvme_controller_info_free (BDNVMEControllerInfo *info);
BDNVMEControllerInfo * bd_nvme_controller_info_copy (BDNVMEControllerInfo *info);

void bd_nvme_lba_format_free (BDNVMELBAFormat *fmt);
BDNVMELBAFormat * bd_nvme_lba_format_copy (BDNVMELBAFormat *fmt);

void bd_nvme_namespace_info_free (BDNVMENamespaceInfo *info);
BDNVMENamespaceInfo * bd_nvme_namespace_info_copy (BDNVMENamespaceInfo *info);

void bd_nvme_smart_log_free (BDNVMESmartLog *log);
BDNVMESmartLog * bd_nvme_smart_log_copy (BDNVMESmartLog *log);

void bd_nvme_error_log_entry_free (BDNVMEErrorLogEntry *entry);
BDNVMEErrorLogEntry * bd_nvme_error_log_entry_copy (BDNVMEErrorLogEntry *entry);

void bd_nvme_self_test_log_entry_free (BDNVMESelfTestLogEntry *entry);
BDNVMESelfTestLogEntry * bd_nvme_self_test_log_entry_copy (BDNVMESelfTestLogEntry *entry);
const gchar * bd_nvme_self_test_result_to_string (BDNVMESelfTestResult result, GError **error);

void bd_nvme_self_test_log_free (BDNVMESelfTestLog *log);
BDNVMESelfTestLog * bd_nvme_self_test_log_copy (BDNVMESelfTestLog *log);

void bd_nvme_sanitize_log_free (BDNVMESanitizeLog *log);
BDNVMESanitizeLog * bd_nvme_sanitize_log_copy (BDNVMESanitizeLog *log);

/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_nvme_init (void);
void     bd_nvme_close (void);

gboolean bd_nvme_is_tech_avail (BDNVMETech tech, guint64 mode, GError **error);


BDNVMEControllerInfo * bd_nvme_get_controller_info   (const gchar *device, GError **error);
BDNVMENamespaceInfo *  bd_nvme_get_namespace_info    (const gchar *device, GError **error);
BDNVMESmartLog *       bd_nvme_get_smart_log         (const gchar *device, GError **error);
BDNVMEErrorLogEntry ** bd_nvme_get_error_log_entries (const gchar *device, GError **error);
BDNVMESelfTestLog *    bd_nvme_get_self_test_log     (const gchar *device, GError **error);
BDNVMESanitizeLog *    bd_nvme_get_sanitize_log      (const gchar *device, GError **error);

gboolean               bd_nvme_device_self_test      (const gchar                  *device,
                                                      BDNVMESelfTestAction          action,
                                                      GError                      **error);

gboolean               bd_nvme_format                (const gchar                  *device,
                                                      guint16                       lba_data_size,
                                                      guint16                       metadata_size,
                                                      BDNVMEFormatSecureErase       secure_erase,
                                                      GError                      **error);
gboolean               bd_nvme_sanitize              (const gchar                  *device,
                                                      BDNVMESanitizeAction          action,
                                                      gboolean                      no_dealloc,
                                                      gint                          overwrite_pass_count,
                                                      guint32                       overwrite_pattern,
                                                      gboolean                      overwrite_invert_pattern,
                                                      GError                      **error);

gchar *                bd_nvme_get_host_nqn          (GError           **error);
gchar *                bd_nvme_generate_host_nqn     (GError           **error);
gchar *                bd_nvme_get_host_id           (GError           **error);
gboolean               bd_nvme_set_host_nqn          (const gchar       *host_nqn,
                                                      GError           **error);
gboolean               bd_nvme_set_host_id           (const gchar       *host_id,
                                                      GError           **error);

gboolean               bd_nvme_connect               (const gchar       *subsysnqn,
                                                      const gchar       *transport,
                                                      const gchar       *transport_addr,
                                                      const gchar       *transport_svcid,
                                                      const gchar       *host_traddr,
                                                      const gchar       *host_iface,
                                                      const gchar       *host_nqn,
                                                      const gchar       *host_id,
                                                      const BDExtraArg **extra,
                                                      GError           **error);
gboolean               bd_nvme_disconnect            (const gchar       *subsysnqn,
                                                      GError           **error);
gboolean               bd_nvme_disconnect_by_path    (const gchar       *path,
                                                      GError           **error);

gchar **               bd_nvme_find_ctrls_for_ns     (const gchar       *ns_sysfs_path,
                                                      const gchar       *subsysnqn,
                                                      const gchar       *host_nqn,
                                                      const gchar       *host_id,
                                                      GError           **error);


#endif  /* BD_NVME */
