// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - 433 MHz weather sensor decoder (#48). Targets the widespread OOK
// PPM protocol of cheap thermo-hygrometers ("Nexus-TH" type: Nexus, Auriol,
// Sencor, Digoo... format documented by rtl_433). 36 bits, short HIGH pulse
// (~500 us) followed by a LOW gap: ~1000 us = 0, ~2000 us = 1, separated by a
// ~4000 us reset gap.
//
// Decode only. BEST-EFFORT written without a hardware capture: validate against
// a real sensor (the bit order / the 0xF constant act as a guard against false
// positives). Car TPMS are usually FSK and do NOT go through this OOK capture
// path.
#include "rf_decoder.h"
#include <Arduino.h>
#include <stdint.h>
#include <vector>

#define WTH_BITS 36

bool rf_decode_weather(const std::vector<int> &durations, RfCodes &out) {
    int n = (int)durations.size();
    if (n < WTH_BITS * 2) return false;

    // 1) line up right after a reset gap (~4 ms LOW).
    int start = -1;
    for (int k = 0; k + 1 < n; k++) {
        if (durations[k] < -3400 && durations[k] > -5200) {
            start = k + 1;
            break;
        }
    }
    if (start < 0) return false;

    // 2) read 36 PPM bits (short pulse + gap that encodes the bit).
    uint64_t data = 0;
    int bits = 0;
    for (int k = start; k + 1 < n && bits < WTH_BITS; k += 2) {
        int hi = durations[k], lo = durations[k + 1];
        if (hi <= 0 || lo >= 0) break;
        uint32_t hd = (uint32_t)hi, gap = (uint32_t)(-lo);
        if (hd < 150 || hd > 1100) break; // expect a short pulse
        uint8_t bit;
        if (gap > 1500 && gap < 3000) bit = 1;
        else if (gap >= 600 && gap <= 1500) bit = 0;
        else break;
        data = (data << 1) | bit;
        bits++;
    }
    if (bits < WTH_BITS) return false;

    // 3) fields (MSB first within the 36 bits) + Nexus guard (0xF nibble).
    uint8_t id = (uint8_t)((data >> 28) & 0xFF);
    uint8_t battery = (uint8_t)((data >> 27) & 0x1);
    uint8_t channel = (uint8_t)(((data >> 24) & 0x3) + 1);
    int16_t traw = (int16_t)((data >> 12) & 0xFFF);
    if (traw & 0x800) traw |= 0xF000; // sign-extend 12 -> 16 bits
    uint8_t constNib = (uint8_t)((data >> 8) & 0xF);
    uint8_t hum = (uint8_t)(data & 0xFF);
    if (constNib != 0xF) return false; // Nexus-TH sanity check
    float tempC = traw / 10.0f;
    if (hum > 100 || tempC < -40.0f || tempC > 70.0f) return false;

    char buf[44];
    snprintf(
        buf, sizeof(buf), "T:%.1fC H:%d%% id:%02X ch:%d bat:%d", (double)tempC, (int)hum, (int)id, (int)channel,
        (int)battery
    );
    out.protocol = "Weather-TH";
    out.mf_name = "Nexus/Auriol?";
    out.key = data;
    out.Bit = WTH_BITS;
    out.te = 500;
    out.data = String(buf);
    out.preset = "FuriHalSubGhzPresetOok650Async";
    return true;
}
