// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance: Hormann HSM decoder. Ported from the Flipper Zero firmware
// (lib/subghz/protocols/hormann.c, GPL-3.0-or-later, (C) Flipper Devices Inc.
// and contributors). See THIRD_PARTY.md.
//
// 44-bit PWM, te 500/1000 us. Header = ~12 ms HIGH then ~500 us LOW. bit 0 =
// short HIGH + long LOW, bit 1 = long HIGH + short LOW. Fixed validation pattern
// (data & 0xFF000000003) == 0xFF000000003. Hormann HSM is a fixed code: decode
// only here (replay/encode may be added later).
#include "rf_decoder.h"
#include <stdint.h>
#include <vector>

#define HORM_TE_SHORT 500
#define HORM_TE_LONG 1000
#define HORM_BITS 44
#define HORM_PATTERN 0xFF000000003ULL

static inline uint32_t horm_absdur(int v) { return v > 0 ? (uint32_t)v : (uint32_t)(-v); }
static inline bool horm_near(uint32_t d, uint32_t ref, uint32_t delta) {
    return (d > ref ? d - ref : ref - d) < delta;
}

bool rf_decode_hormann(const std::vector<int> &durations, RfCodes &out) {
    int n = (int)durations.size();
    if (n < HORM_BITS * 2) return false;

    // 1) header: long HIGH pulse (~12 ms)
    int idx = -1;
    for (int k = 0; k + 2 < n; k++) {
        if (durations[k] > 0 && horm_absdur(durations[k]) > 9000 && horm_absdur(durations[k]) < 15000) {
            idx = k + 1; // after the high header
            break;
        }
    }
    if (idx < 0) return false;

    // 2) line up on the first HIGH edge (start of the first bit)
    int s = idx;
    while (s < n && durations[s] <= 0) s++;

    // 3) read 44 PWM bits
    uint64_t data = 0;
    int bits = 0;
    for (int k = s; k + 1 < n && bits < HORM_BITS; k += 2) {
        int hi = durations[k], lo = durations[k + 1];
        if (hi <= 0 || lo >= 0) break;
        uint32_t hd = (uint32_t)hi, ld = (uint32_t)(-lo);
        uint8_t bit;
        if (horm_near(hd, HORM_TE_SHORT, 250) && horm_near(ld, HORM_TE_LONG, 350)) bit = 0;
        else if (horm_near(hd, HORM_TE_LONG, 350) && horm_near(ld, HORM_TE_SHORT, 250)) bit = 1;
        else break;
        data = (data << 1) | bit;
        bits++;
    }
    if (bits < HORM_BITS) return false;

    // 4) fixed validation pattern
    if ((data & HORM_PATTERN) != HORM_PATTERN) return false;

    uint8_t button = (uint8_t)((data >> 8) & 0xF);
    uint32_t serial = (uint32_t)((data >> 12) & 0xFFFFFF); // device code ~24 bits

    out.protocol = "Hormann HSM";
    out.mf_name = "Hormann";
    out.serial = serial;
    out.btn = button;
    out.key = data;
    out.Bit = HORM_BITS;
    out.te = HORM_TE_SHORT;
    out.preset = "FuriHalSubGhzPresetOok650Async";
    return true;
}
