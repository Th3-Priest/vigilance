#ifndef __VIGILANCE_OUI_VENDOR_H__
#define __VIGILANCE_OUI_VENDOR_H__

#include <Arduino.h>

// Vigilance: embedded OUI database (self-contained, no external resource).
// Gives a vendor name for a WiFi/BLE MAC from its prefix (first 3 bytes = OUI).
// Curated best-effort table: it covers the most common consumer vendors (Apple,
// Samsung, Espressif, Google, Amazon, Intel, Xiaomi, Huawei, TP-Link, Microsoft,
// Raspberry Pi...), not the whole IEEE registry. An unknown OUI returns
// nullptr / "?".

// Short vendor name, or nullptr if the OUI is not in the table.
const char *ouiVendor(const uint8_t *mac);

// Locally administered MAC (bit 0x02 of the first byte): typically a random or
// private MAC (modern iOS/Android). These addresses cannot be tracked reliably
// over time.
bool ouiIsRandom(const uint8_t *mac);

// Compact label for display / log:
//   - a vendor name if the OUI is known ("Apple", "Espressif"...)
//   - "random" if the MAC is locally administered
//   - "?" otherwise (fixed MAC but vendor not listed)
String ouiLabel(const uint8_t *mac);

// Rough category (#28) inferred from the vendor alone (heuristic, never certain
// from the OUI): "Mobile", "PC", "Net", "IoT", "Print", "Dev", or "?" if
// unknown. Handy for classifying the devices seen.
const char *ouiCategory(const uint8_t *mac);

#endif // __VIGILANCE_OUI_VENDOR_H__
