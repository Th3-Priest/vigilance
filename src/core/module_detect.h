#ifndef __VIGILANCE_MODULE_DETECT_H__
#define __VIGILANCE_MODULE_DETECT_H__

#include <Arduino.h>
#include <vector>

// Vigilance: detection of pluggable modules on the expansion connector
// (Grove/QWIIC I2C port, GPIO8/GPIO18 on the T-Embed CC1101 Plus). Built on
// check_i2c_address() (core/i2c_finder) and the shared system bus.

enum ModuleCategory : uint8_t {
    MODCAT_SYSTEM = 0,  // internal peripheral (PMU, gauge, RTC...)
    MODCAT_MODULE = 1,  // recognized pluggable module (FM transmitter...)
    MODCAT_SENSOR = 2,  // pluggable sensor / display
    MODCAT_NFC = 3,     // internal NFC reader
    MODCAT_UNKNOWN = 4, // address that responds, not catalogued
};

struct DetectedI2C {
    uint8_t addr;
    String name;
    ModuleCategory category;
};

// Scan the I2C bus (0x08-0x77) and return the peripherals present.
std::vector<DetectedI2C> scanI2CModules();

// Boot-time detection: fills in the globals below.
void detectModulesAtBoot();

// Human-readable category name.
const char *moduleCategoryName(ModuleCategory c);

// Module profiles (#16): remember pluggable modules already seen across boots
// in /Vigilance/modules.csv (address,name), to keep a record of what was
// plugged in even after it's unplugged.
void vigSaveModuleProfiles(const std::vector<DetectedI2C> &mods);
int vigModuleProfileLines(std::vector<String> &out); // "0xNN name", returns the count

// Runtime state (modeled on gpsConnected/sdcardMounted).
extern int g_pluggableModuleCount; // pluggable module count at last scan
extern bool g_fmModuleDetected;     // SI4713 FM transmitter present

#endif // __VIGILANCE_MODULE_DETECT_H__
