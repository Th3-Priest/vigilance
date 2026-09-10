// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance: detection of pluggable modules (Grove/QWIIC I2C port). Reuses
// check_i2c_address() (core/i2c_finder) which goes through the shared system bus
// (acquireI2CBus/releaseI2CBus): never call Wire.begin() directly.
#include "module_detect.h"

#include "i2c_finder.h"
#include "sd_functions.h"
#include <set>

int g_pluggableModuleCount = 0;
bool g_fmModuleDetected = false;

struct KnownI2C {
    uint8_t addr;
    const char *name;
    ModuleCategory cat;
};

// I2C address -> {name, category} table. Addresses that are actually internal to
// the T-Embed CC1101 Plus are marked SYSTEM/NFC so they aren't counted as
// pluggable. A bare address is sometimes ambiguous (see comments): it's an
// indicative guess, not proof.
static const KnownI2C KNOWN[] = {
    // --- T-Embed internal (not counted as pluggable) ---
    {0x6B, "PMU BQ25896", MODCAT_SYSTEM},
    {0x55, "BQ27220 gauge", MODCAT_SYSTEM},
    {0x51, "RTC PCF85063", MODCAT_SYSTEM},
    {0x24, "PN532 NFC", MODCAT_NFC},
    {0x28, "MFRC522 RFID", MODCAT_NFC},
    // --- recognized pluggable modules ---
    {0x63, "SI4713 FM transmitter", MODCAT_MODULE},
    {0x11, "SI4713 FM transmitter", MODCAT_MODULE},
    {0x42, "GPS u-blox I2C", MODCAT_MODULE},
    {0x70, "TCA9548A multiplexer", MODCAT_MODULE},
    // --- common pluggable sensors / displays (detectable) ---
    {0x76, "BME/BMP280 sensor", MODCAT_SENSOR},
    {0x77, "BME/BMP280 sensor", MODCAT_SENSOR},
    {0x44, "SHT3x sensor", MODCAT_SENSOR},
    {0x45, "SHT3x sensor", MODCAT_SENSOR},
    {0x38, "AHT10/20 sensor", MODCAT_SENSOR},
    {0x40, "INA/HTU21 sensor", MODCAT_SENSOR},
    {0x39, "APDS light sensor", MODCAT_SENSOR},
    {0x23, "BH1750 light sensor", MODCAT_SENSOR},
    {0x29, "ToF VL53L0X", MODCAT_SENSOR},
    {0x68, "IMU MPU6050 / DS3231", MODCAT_SENSOR},
    {0x69, "IMU MPU6050", MODCAT_SENSOR},
    {0x0D, "QMC5883 compass", MODCAT_SENSOR},
    {0x1E, "HMC5883 compass", MODCAT_SENSOR},
    {0x3C, "SSD1306 OLED display", MODCAT_SENSOR},
    {0x3D, "SSD1306 OLED display", MODCAT_SENSOR},
    {0x48, "ADC ADS1115 / temp", MODCAT_SENSOR},
    {0x5A, "CCS811 gas sensor", MODCAT_SENSOR},
};

const char *moduleCategoryName(ModuleCategory c) {
    switch (c) {
        case MODCAT_SYSTEM: return "system";
        case MODCAT_MODULE: return "module";
        case MODCAT_SENSOR: return "sensor";
        case MODCAT_NFC: return "NFC";
        default: return "unknown";
    }
}

std::vector<DetectedI2C> scanI2CModules() {
    std::vector<DetectedI2C> found;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (!check_i2c_address(a)) continue;
        String name = "Unknown";
        ModuleCategory cat = MODCAT_UNKNOWN;
        for (size_t i = 0; i < sizeof(KNOWN) / sizeof(KNOWN[0]); i++) {
            if (KNOWN[i].addr == a) {
                name = KNOWN[i].name;
                cat = KNOWN[i].cat;
                break;
            }
        }
        DetectedI2C d;
        d.addr = a;
        d.name = name;
        d.category = cat;
        found.push_back(d);
    }
    return found;
}

void detectModulesAtBoot() {
    g_pluggableModuleCount = 0;
    g_fmModuleDetected = false;
    std::vector<DetectedI2C> mods = scanI2CModules();
    for (size_t i = 0; i < mods.size(); i++) {
        if (mods[i].category == MODCAT_MODULE || mods[i].category == MODCAT_SENSOR ||
            mods[i].category == MODCAT_UNKNOWN)
            g_pluggableModuleCount++;
        if (mods[i].addr == 0x63 || mods[i].addr == 0x11) g_fmModuleDetected = true;
    }
    vigSaveModuleProfiles(mods); // #16: remember the profiles we've seen
}

// --- Module profiles (#16): persistent registry of pluggable modules seen ---
static String modKey(const DetectedI2C &m) {
    char b[8];
    snprintf(b, sizeof(b), "0x%02X", m.addr);
    return String(b) + " " + m.name;
}

void vigSaveModuleProfiles(const std::vector<DetectedI2C> &mods) {
    FS *fs;
    if (!getFsStorage(fs)) return;
    std::vector<String> lines;
    vigModuleProfileLines(lines); // load what's already there
    std::set<String> seen(lines.begin(), lines.end());
    bool changed = false;
    for (auto &m : mods) {
        if (m.category != MODCAT_MODULE && m.category != MODCAT_SENSOR && m.category != MODCAT_UNKNOWN)
            continue; // pluggable only (not internal)
        String k = modKey(m);
        if (seen.insert(k).second) changed = true;
    }
    if (!changed) return;
    if (!(*fs).exists("/Vigilance")) (*fs).mkdir("/Vigilance");
    File f = (*fs).open("/Vigilance/modules.csv", FILE_WRITE); // rewrite the deduplicated set
    if (!f) return;
    for (auto &s : seen) f.println(s);
    f.close();
}

int vigModuleProfileLines(std::vector<String> &out) {
    FS *fs;
    if (!getFsStorage(fs)) return 0;
    if (!(*fs).exists("/Vigilance/modules.csv")) return 0;
    File f = (*fs).open("/Vigilance/modules.csv", FILE_READ);
    if (!f) return 0;
    while (f.available()) {
        String l = f.readStringUntil('\n');
        l.trim();
        if (l.length()) out.push_back(l);
    }
    f.close();
    return (int)out.size();
}
