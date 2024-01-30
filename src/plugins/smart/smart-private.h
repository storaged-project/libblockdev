#include <glib.h>
#include <glib-object.h>
#include <blockdev/utils.h>

#include "smart.h"

#ifndef BD_SMART_PRIVATE
#define BD_SMART_PRIVATE

/* "C" locale to get the locale-agnostic error messages */
#define _C_LOCALE (locale_t) 0

typedef struct DriveDBAttr {
    gint id;
    gchar *name;
} DriveDBAttr;

struct WellKnownAttrInfo {
    const gchar *libatasmart_name;
    BDSmartATAAttributeUnit unit;
    const gchar *smartmontools_names[7];  /* NULL-terminated */
};

/* This table was initially stolen from libatasmart, including the comment below: */
/* This data is stolen from smartmontools */
static const struct WellKnownAttrInfo well_known_attrs[256] = {
    [1]   = { "raw-read-error-rate",         BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Raw_Read_Error_Count", "Raw_Read_Error_Rate", NULL }},
    [2]   = { "throughput-performance",      BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { "Throughput_Performance", NULL }},
    [3]   = { "spin-up-time",                BD_SMART_ATA_ATTRIBUTE_UNIT_MSECONDS,  { "Spin_Up_Time", NULL }},
    [4]   = { "start-stop-count",            BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Start_Stop_Count", NULL }},
    [5]   = { "reallocated-sector-count",    BD_SMART_ATA_ATTRIBUTE_UNIT_SECTORS,   { "Reallocated_Block_Count", "Reallocated_Sector_Ct", NULL }},
    [6]   = { "read-channel-margin",         BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { "Read_Channel_Margin", NULL }},
    [7]   = { "seek-error-rate",             BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Seek_Error_Rate", NULL }},
    [8]   = { "seek-time-performance",       BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { "Seek_Time_Performance", NULL }},
    [9]   = { "power-on-hours",              BD_SMART_ATA_ATTRIBUTE_UNIT_MSECONDS,  { "Power_On_Hours", "Power_On_Hours_and_Msec", NULL }},
    [10]  = { "spin-retry-count",            BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Spin_Retry_Count", NULL }},
    [11]  = { "calibration-retry-count",     BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Calibration_Retry_Count", NULL }},
    [12]  = { "power-cycle-count",           BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Power_Cycle_Count", "Device_Power_Cycle_Cnt", NULL }},
    [13]  = { "read-soft-error-rate",        BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Read_Soft_Error_Rate", NULL }},
    [170] = { "available-reserved-space",    BD_SMART_ATA_ATTRIBUTE_UNIT_PERCENT,   { "Available_Reservd_Space", "Reserved_Block_Pct", NULL }},
    [171] = { "program-fail-count",          BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Program_Fail_Cnt", "Program_Fail_Count", "Program_Fail_Ct", NULL }},
    [172] = { "erase-fail-count",            BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Erase_Fail_Cnt", "Erase_Fail_Ct", "Erase_Fail_Count", "Block_Erase_Failure", NULL }},
    [175] = { "program-fail-count-chip",     BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Program_Fail_Count_Chip", NULL }},
    [176] = { "erase-fail-count-chip",       BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Erase_Fail_Count_Chip", NULL }},
    [177] = { "wear-leveling-count",         BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Wear_Leveling_Count", NULL }},
    [178] = { "used-reserved-blocks-chip",   BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Used_Rsvd_Blk_Cnt_Chip", "Used_Rsrvd_Blk_Cnt_Wrst", NULL }},
    [179] = { "used-reserved-blocks-total",  BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Used_Rsvd_Blk_Cnt_Tot", "Used_Rsrvd_Blk_Cnt_Tot", NULL }},
    [180] = { "unused-reserved-blocks",      BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Unused_Rsvd_Blk_Cnt_Tot", NULL }},
    [181] = { "program-fail-count-total",    BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Program_Fail_Cnt_Total", NULL }},
    [182] = { "erase-fail-count-total",      BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Erase_Fail_Count_Total", NULL }},
    [183] = { "runtime-bad-block-total",     BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Runtime_Bad_Block", NULL }},
    [184] = { "end-to-end-error",            BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "End-to-End_Error", "End-to-End_Error_Count", NULL }},
    [187] = { "reported-uncorrect",          BD_SMART_ATA_ATTRIBUTE_UNIT_SECTORS,   { "Reported_Uncorrect", "Reported_UE_Counts", NULL }},
    [188] = { "command-timeout",             BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Command_Timeout", "Command_Timeouts", NULL }},
    [189] = { "high-fly-writes",             BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "High_Fly_Writes", NULL }},
    [190] = { "airflow-temperature-celsius", BD_SMART_ATA_ATTRIBUTE_UNIT_MKELVIN,   { "Airflow_Temperature_Cel", "Case_Temperature", "Drive_Temperature", "Temperature_Case", "Drive_Temp_Warning", "Temperature_Celsius", NULL }},
    [191] = { "g-sense-error-rate",          BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "G-Sense_Error_Rate", NULL }},
    [192] = { "power-off-retract-count",     BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Power-Off_Retract_Count", "Power-off_Retract_Count", NULL }},
    [193] = { "load-cycle-count",            BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Load_Cycle_Count", NULL }},
    [194] = { "temperature-celsius-2",       BD_SMART_ATA_ATTRIBUTE_UNIT_MKELVIN,   { "Temperature_Celsius", "Device_Temperature", "Drive_Temperature", "Temperature_Internal", NULL }},
    [195] = { "hardware-ecc-recovered",      BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Hardware_ECC_Recovered", "Cumulativ_Corrected_ECC", "ECC_Error_Rate", NULL }},
    [196] = { "reallocated-event-count",     BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Reallocated_Event_Count", NULL }},
    [197] = { "current-pending-sector",      BD_SMART_ATA_ATTRIBUTE_UNIT_SECTORS,   { "Current_Pending_Sector", "Pending_Sector_Count", NULL }},
    [198] = { "offline-uncorrectable",       BD_SMART_ATA_ATTRIBUTE_UNIT_SECTORS,   { "Offline_Uncorrectable", "Uncor_Read_Error_Ct", "Uncorrectable_Sector_Ct", NULL }},
    [199] = { "udma-crc-error-count",        BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "CRC_Error_Count", "SATA_CRC_Error", "SATA_CRC_Error_Count", "UDMA_CRC_Error_Count", NULL }},
    [200] = { "multi-zone-error-rate",       BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Multi_Zone_Error_Rate", NULL }},
    [201] = { "soft-read-error-rate",        BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Soft_Read_Error_Rate", "Read_Error_Rate", "Uncorr_Soft_Read_Err_Rt", "Unc_Read_Error_Rate", "Unc_Soft_Read_Err_Rate", NULL }},
    [202] = { "ta-increase-count",           BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Data_Address_Mark_Errs", NULL }},
    [203] = { "run-out-cancel",              BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { "Run_Out_Cancel", NULL }},
    [204] = { "shock-count-write-open",      BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Soft_ECC_Correction", "Soft_ECC_Correction_Rt", "Soft_ECC_Correct_Rate", NULL }},
    [205] = { "shock-rate-write-open",       BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Thermal_Asperity_Rate", NULL }},
    [206] = { "flying-height",               BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { "Flying_Height", NULL }},
    [207] = { "spin-high-current",           BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { "Spin_High_Current", NULL }},
    [208] = { "spin-buzz",                   BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { "Spin_Buzz", NULL }},
    [209] = { "offline-seek-performance",    BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { "Offline_Seek_Performnce", NULL }},
    [220] = { "disk-shift",                  BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { "Disk_Shift", NULL }},
    [221] = { "g-sense-error-rate-2",        BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "G-Sense_Error_Rate", NULL }},
    [222] = { "loaded-hours",                BD_SMART_ATA_ATTRIBUTE_UNIT_MSECONDS,  { "Loaded_Hours", NULL }},
    [223] = { "load-retry-count",            BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Load_Retry_Count", NULL }},
    [224] = { "load-friction",               BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { "Load_Friction", NULL }},
    [225] = { "load-cycle-count-2",          BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Load_Cycle_Count", NULL }},
    [226] = { "load-in-time",                BD_SMART_ATA_ATTRIBUTE_UNIT_MSECONDS,  { "Load-in_Time", NULL }},
    [227] = { "torq-amp-count",              BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Torq-amp_Count", NULL }},
    [228] = { "power-off-retract-count-2",   BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Power-Off_Retract_Count", "Power-off_Retract_Count", NULL }},
    [230] = { "head-amplitude",              BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { "Head_Amplitude", NULL }},
    [231] = { "temperature-celsius",         BD_SMART_ATA_ATTRIBUTE_UNIT_MKELVIN,   { "Temperature_Celsius", "Controller_Temperature", NULL }},
    [232] = { "endurance-remaining",         BD_SMART_ATA_ATTRIBUTE_UNIT_PERCENT,   { "Spares_Remaining_Perc", "Perc_Avail_Resrvd_Space", "Available_Reservd_Space", NULL }},
    [233] = { "power-on-seconds-2",          BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { /* TODO */ NULL }},
    [234] = { "uncorrectable-ecc-count",     BD_SMART_ATA_ATTRIBUTE_UNIT_SECTORS,   { /* TODO */ NULL }},
    [235] = { "good-block-rate",             BD_SMART_ATA_ATTRIBUTE_UNIT_UNKNOWN,   { "Good/Sys_Block_Count", NULL }},
    [240] = { "head-flying-hours",           BD_SMART_ATA_ATTRIBUTE_UNIT_MSECONDS,  { "Head_Flying_Hours", NULL }},
    [241] = { "total-lbas-written",          BD_SMART_ATA_ATTRIBUTE_UNIT_MB,        { /* TODO: implement size calculation logic */ NULL }},
    [242] = { "total-lbas-read",             BD_SMART_ATA_ATTRIBUTE_UNIT_MB,        { /* TODO: implement size calculation logic */ NULL }},
    [250] = { "read-error-retry-rate",       BD_SMART_ATA_ATTRIBUTE_UNIT_NONE,      { "Read_Error_Retry_Rate", "Read_Retry_Count", NULL }},
};


G_GNUC_INTERNAL
DriveDBAttr** drivedb_lookup_drive (const gchar *model, const gchar *fw, gboolean include_defaults);

G_GNUC_INTERNAL
void free_drivedb_attrs (DriveDBAttr **attrs);

#endif  /* BD_SMART_PRIVATE */
