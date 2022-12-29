#include <glib.h>
#include <glib-object.h>

#ifndef BD_SMART
#define BD_SMART

GQuark bd_smart_error_quark (void);
#define BD_SMART_ERROR bd_smart_error_quark ()

/**
 * BDSmartError:
 * @BD_SMART_ERROR_TECH_UNAVAIL: SMART support not available.
 * @BD_SMART_ERROR_FAILED: General error.
 * @BD_SMART_ERROR_INVALID_ARGUMENT: Invalid argument.
 * @BD_SMART_ERROR_DRIVE_SLEEPING: Device is in a low-power mode.
 */
typedef enum {
    BD_SMART_ERROR_TECH_UNAVAIL,
    BD_SMART_ERROR_FAILED,
    BD_SMART_ERROR_INVALID_ARGUMENT,
    BD_SMART_ERROR_DRIVE_SLEEPING,
} BDSmartError;

typedef enum {
    BD_SMART_TECH_ATA = 0,
    BD_SMART_TECH_SCSI = 1,
} BDSmartTech;

typedef enum {
    BD_SMART_TECH_MODE_INFO         = 1 << 0,
    BD_SMART_TECH_MODE_SELFTEST     = 1 << 1,
} BDSmartTechMode;

/**
 * BDSmartATAOfflineDataCollectionStatus:
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_NEVER_STARTED: Offline data collection activity was never started.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_NO_ERROR: Offline data collection activity was completed without error. Indicates a passed test.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_IN_PROGRESS: Offline data collection activity is in progress.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_SUSPENDED_INTR: Offline data collection activity was suspended by an interrupting command from host.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_ABORTED_INTR: Offline data collection activity was aborted by an interrupting command from host.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_ABORTED_ERROR: Offline data collection activity was aborted by the device with a fatal error. Indicates a failed test.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_VENDOR_SPECIFIC: Offline data collection activity is in a Vendor Specific state.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_RESERVED: Offline data collection activity is in a Reserved state.
 */
typedef enum {
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_NEVER_STARTED   = 0x00,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_NO_ERROR        = 0x02,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_IN_PROGRESS     = 0x03,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_SUSPENDED_INTR  = 0x04,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_ABORTED_INTR    = 0x05,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_ABORTED_ERROR   = 0x06,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_VENDOR_SPECIFIC = 0x40,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_STATUS_RESERVED        = 0x3F,
} BDSmartATAOfflineDataCollectionStatus;

/**
 * BDSmartATAOfflineDataCollectionCapabilities:
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_NOT_SUPPORTED: Offline data collection not supported.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_EXEC_OFFLINE_IMMEDIATE: Execute Offline Immediate function supported.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_OFFLINE_ABORT: Abort Offline collection upon new command.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_OFFLINE_SURFACE_SCAN: Offline surface scan supported.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_SELF_TEST: Self-test supported.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_CONVEYANCE_SELF_TEST: Conveyance Self-test supported.
 * @BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_SELECTIVE_SELF_TEST: Selective Self-test supported.
 */
typedef enum {
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_NOT_SUPPORTED          = 0x00,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_EXEC_OFFLINE_IMMEDIATE = 0x01,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_OFFLINE_ABORT          = 0x04,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_OFFLINE_SURFACE_SCAN   = 0x08,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_SELF_TEST              = 0x10,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_CONVEYANCE_SELF_TEST   = 0x20,
    BD_SMART_ATA_OFFLINE_DATA_COLLECTION_CAP_SELECTIVE_SELF_TEST    = 0x40,
} BDSmartATAOfflineDataCollectionCapabilities;

/**
 * BDSmartATASelfTestStatus:
 * @BD_SMART_ATA_SELF_TEST_STATUS_COMPLETED_NO_ERROR: The previous self-test routine completed without error or no self-test has ever been run.
 * @BD_SMART_ATA_SELF_TEST_STATUS_ABORTED_HOST: The self-test routine was aborted by the host.
 * @BD_SMART_ATA_SELF_TEST_STATUS_INTR_HOST_RESET: The self-test routine was interrupted by the host with a hard or soft reset.
 * @BD_SMART_ATA_SELF_TEST_STATUS_ERROR_FATAL: A fatal error or unknown test error occurred while the device was executing its self-test routine and the device was unable to complete the self-test routine.
 * @BD_SMART_ATA_SELF_TEST_STATUS_ERROR_UNKNOWN: The previous self-test completed having a test element that failed and the test element that failed is not known.
 * @BD_SMART_ATA_SELF_TEST_STATUS_ERROR_ELECTRICAL: The previous self-test completed having the electrical element of the test failed.
 * @BD_SMART_ATA_SELF_TEST_STATUS_ERROR_SERVO: The previous self-test completed having the servo (and/or seek) element of the test failed.
 * @BD_SMART_ATA_SELF_TEST_STATUS_ERROR_READ: The previous self-test completed having the read element of the test failed.
 * @BD_SMART_ATA_SELF_TEST_STATUS_ERROR_HANDLING: The previous self-test completed having a test element that failed and the device is suspected of having handling damage.
 * @BD_SMART_ATA_SELF_TEST_STATUS_IN_PROGRESS: Self-test routine in progress.
 */
typedef enum {
    BD_SMART_ATA_SELF_TEST_STATUS_COMPLETED_NO_ERROR = 0x00,
    BD_SMART_ATA_SELF_TEST_STATUS_ABORTED_HOST       = 0x01,
    BD_SMART_ATA_SELF_TEST_STATUS_INTR_HOST_RESET    = 0x02,
    BD_SMART_ATA_SELF_TEST_STATUS_ERROR_FATAL        = 0x03,
    BD_SMART_ATA_SELF_TEST_STATUS_ERROR_UNKNOWN      = 0x04,
    BD_SMART_ATA_SELF_TEST_STATUS_ERROR_ELECTRICAL   = 0x05,
    BD_SMART_ATA_SELF_TEST_STATUS_ERROR_SERVO        = 0x06,
    BD_SMART_ATA_SELF_TEST_STATUS_ERROR_READ         = 0x07,
    BD_SMART_ATA_SELF_TEST_STATUS_ERROR_HANDLING     = 0x08,
    BD_SMART_ATA_SELF_TEST_STATUS_IN_PROGRESS        = 0x0F,
} BDSmartATASelfTestStatus;

/**
 * BDSmartATACapabilities:
 * @BD_SMART_ATA_CAP_ATTRIBUTE_AUTOSAVE: Saves SMART data before entering power-saving mode.
 * @BD_SMART_ATA_CAP_AUTOSAVE_TIMER: Supports SMART auto save timer.
 * @BD_SMART_ATA_CAP_ERROR_LOGGING: Error logging supported.
 * @BD_SMART_ATA_CAP_GP_LOGGING: General Purpose Logging supported.
 */
typedef enum {
    BD_SMART_ATA_CAP_ATTRIBUTE_AUTOSAVE = 1 << 0,
    BD_SMART_ATA_CAP_AUTOSAVE_TIMER     = 1 << 1,
    BD_SMART_ATA_CAP_ERROR_LOGGING      = 1 << 2,
    BD_SMART_ATA_CAP_GP_LOGGING         = 1 << 3,
} BDSmartATACapabilities;

/**
 * BDSmartATAAttributeFlag:
 * @BD_SMART_ATA_ATTRIBUTE_FLAG_PREFAILURE: Pre-failure/advisory bit: If the value of this bit equals zero, an attribute value less than or equal to its corresponding attribute threshold indicates an advisory condition where the usage or age of the device has exceeded its intended design life period. If the value of this bit equals one, an attribute value less than or equal to its corresponding attribute threshold indicates a prefailure condition where imminent loss of data is being predicted.
 * @BD_SMART_ATA_ATTRIBUTE_FLAG_ONLINE: On-line data collection bit: If the value of this bit equals zero, then the attribute value is updated only during off-line data collection activities. If the value of this bit equals one, then the attribute value is updated during normal operation of the device or during both normal operation and off-line testing.
 * @BD_SMART_ATA_ATTRIBUTE_FLAG_PERFORMANCE: Performance type bit (vendor specific).
 * @BD_SMART_ATA_ATTRIBUTE_FLAG_ERROR_RATE: Errorrate type bit (vendor specific).
 * @BD_SMART_ATA_ATTRIBUTE_FLAG_EVENT_COUNT: Eventcount bit (vendor specific).
 * @BD_SMART_ATA_ATTRIBUTE_FLAG_SELF_PRESERVING: Selfpereserving bit (vendor specific).
 * @BD_SMART_ATA_ATTRIBUTE_FLAG_OTHER: Reserved.
 */
typedef enum {
    BD_SMART_ATA_ATTRIBUTE_FLAG_PREFAILURE      = 0x0001,
    BD_SMART_ATA_ATTRIBUTE_FLAG_ONLINE          = 0x0002,
    BD_SMART_ATA_ATTRIBUTE_FLAG_PERFORMANCE     = 0x0004,
    BD_SMART_ATA_ATTRIBUTE_FLAG_ERROR_RATE      = 0x0008,
    BD_SMART_ATA_ATTRIBUTE_FLAG_EVENT_COUNT     = 0x0010,
    BD_SMART_ATA_ATTRIBUTE_FLAG_SELF_PRESERVING = 0x0020,
    BD_SMART_ATA_ATTRIBUTE_FLAG_OTHER           = 0xffc0,
} BDSmartATAAttributeFlag;

/**
 * BDSmartATAAttribute:
 * @id: Attribute Identifier.
 * @name: The identifier as a string.
 * @value: The normalized value or -1 if unknown.
 * @worst: The worst normalized value of -1 if unknown.
 * @threshold: The threshold of a normalized value or -1 if unknown.
 * @failed_past: Indicates a failure that happened in the past (the normalized worst value is below the threshold).
 * @failing_now: Indicates a failure that is happening now (the normalized value is below the threshold).
 * @value_raw: The raw value of the attribute.
 * @value_raw_string: String representation of the raw value.
 * @flags: Bitmask of attribute flags. See #BDSmartATAAttributeFlag.
 */
typedef struct BDSmartATAAttribute {
    guint8 id;
    gchar *name;
    gint value;
    gint worst;
    gint threshold;
    gboolean failed_past;
    gboolean failing_now;
    guint64 value_raw;
    gchar *value_raw_string;
    guint16 flags;
} BDSmartATAAttribute;

/**
 * BDSmartATA:
 * @smart_supported: Indicates that the device has SMART capability.
 * @smart_enabled: Indicates that the SMART support is enabled.
 * @overall_status_passed: %TRUE if the device SMART overall-health self-assessment test result has passed.
 * @offline_data_collection_status: The offline data collection status. See #BDSmartATAOfflineDataCollectionStatus.
 * @auto_offline_data_collection_enabled: %TRUE if Automatic Offline Data Collection is enabled.
 * @offline_data_collection_completion: Total time in seconds to complete Offline data collection.
 * @offline_data_collection_capabilities: Bitmask of offline data collection capabilities, see #BDSmartATAOfflineDataCollectionCapabilities.
 * @self_test_status: Self-test execution status. See #BDSmartATASelfTestStatus.
 * @self_test_percent_remaining: The percentage remaining of a running self-test.
 * @self_test_polling_short: Short self-test routine recommended polling time in minutes or 0 if not supported.
 * @self_test_polling_extended: Extended self-test routine recommended polling time in minutes or 0 if not supported.
 * @self_test_polling_conveyance: Conveyance self-test routine recommended polling time in minutes or 0 if not supported.
 * @smart_capabilities: Bitmask of device misc. SMART capabilities. See #BDSmartATACapabilities.
 * @attributes: (array zero-terminated=1) (element-type BDSmartATAAttribute): A list of reported SMART attributes.
 * @power_on_time: The count of minutes in power-on state.
 * @power_cycle_count: The count of full hard disk power on/off cycles.
 * @temperature: The current drive temperature in Kelvin or 0 when temperature is not reported.
 */
typedef struct BDSmartATA {
    gboolean smart_supported;
    gboolean smart_enabled;
    gboolean overall_status_passed;
    BDSmartATAOfflineDataCollectionStatus offline_data_collection_status;
    gboolean auto_offline_data_collection_enabled;
    gint offline_data_collection_completion;
    guint offline_data_collection_capabilities;
    BDSmartATASelfTestStatus self_test_status;
    gint self_test_percent_remaining;
    gint self_test_polling_short;
    gint self_test_polling_extended;
    gint self_test_polling_conveyance;
    guint smart_capabilities;
    BDSmartATAAttribute **attributes;
    guint power_on_time;
    guint power_cycle_count;
    guint temperature;
} BDSmartATA;


void bd_smart_ata_free (BDSmartATA *data);
BDSmartATA * bd_smart_ata_copy (BDSmartATA *data);

void bd_smart_ata_attribute_free (BDSmartATAAttribute *attr);
BDSmartATAAttribute * bd_smart_ata_attribute_copy (BDSmartATAAttribute *attr);

/*
 * If using the plugin as a standalone library, the following functions should
 * be called to:
 *
 * check_deps() - check plugin's dependencies, returning TRUE if satisfied
 * init()       - initialize the plugin, returning TRUE on success
 * close()      - clean after the plugin at the end or if no longer used
 *
 */
gboolean bd_smart_check_deps (void);
gboolean bd_smart_init (void);
void     bd_smart_close (void);

gboolean bd_smart_is_tech_avail (BDSmartTech tech, guint64 mode, GError **error);


BDSmartATA *   bd_smart_ata_get_info               (const gchar  *device,
                                                    gboolean      nowakeup,
                                                    GError      **error);

#endif  /* BD_SMART */
