// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance: Somfy RTS (Telis) decoder. Ported from the Flipper Zero firmware
// (lib/subghz/protocols/somfy_telis.c, GPL-3.0-or-later, (C) Flipper Devices Inc.
// and contributors). See THIRD_PARTY.md for the full attribution.
//
// Somfy RTS: 56-bit manchester, half-symbol te=640 us (long 1280 us), hardware
// sync 4x2416 us then software sync 4550 us. Frame de-obfuscated by
// (frame ^ (frame>>8)), validated by a 4-bit checksum (XOR of all nibbles == 0).
// Decode only: the code is rolling, so a replay grants no access.
#include "rf_decoder.h"
#include <stdint.h>
#include <vector>

#define SOMFY_TE_SHORT 640
#define SOMFY_TE_LONG 1280
#define SOMFY_TE_DELTA 250
#define SOMFY_SW_SYNC 4550
#define SOMFY_BITS 56

static inline uint32_t somfy_absdur(int v) { return v > 0 ? (uint32_t)v : (uint32_t)(-v); }
static inline uint32_t somfy_diff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

// Decode 56 manchester bits from the half-symbols at a given offset/polarity,
// then de-obfuscate and validate by checksum. Returns true and fills `dataOut` if OK.
static bool somfy_try(const std::vector<uint8_t> &half, size_t off, bool inv, uint64_t &dataOut) {
    uint64_t frame = 0;
    int bits = 0;
    for (size_t p = off; p + 1 < half.size() && bits < SOMFY_BITS; p += 2) {
        uint8_t a = half[p], b = half[p + 1];
        uint8_t bit;
        if (a == 0 && b == 1) bit = inv ? 0 : 1;
        else if (a == 1 && b == 0) bit = inv ? 1 : 0;
        else return false; // not clean manchester at this alignment
        frame = (frame << 1) | bit;
        bits++;
    }
    if (bits < SOMFY_BITS) return false;

    uint64_t data = frame ^ (frame >> 8);       // de-obfuscation
    data &= 0x00FFFFFFFFFFFFFFULL;               // 56 usable bits

    uint8_t ck = 0;
    for (int s = 0; s < SOMFY_BITS; s += 4) ck ^= (uint8_t)((data >> s) & 0xF);
    if ((ck & 0xF) != 0) return false;           // invalid checksum

    dataOut = data;
    return true;
}

bool rf_decode_somfy(const std::vector<int> &durations, RfCodes &out) {
    int n = (int)durations.size();
    if (n < 100) return false;

    // 1) find the software sync (~4550 us, HIGH level): the data follows
    int idx = -1;
    for (int k = 0; k + 2 < n; k++) {
        if (durations[k] > 0 && somfy_diff(somfy_absdur(durations[k]), SOMFY_SW_SYNC) < 900) {
            idx = k + 1;
            break;
        }
    }
    if (idx < 0) return false;

    // 2) rebuild the half-symbols (short = 1 half, long = 2 halves of same level)
    std::vector<uint8_t> half;
    half.reserve(SOMFY_BITS * 2 + 8);
    for (int k = idx; k < n; k++) {
        uint8_t level = durations[k] > 0 ? 1 : 0;
        uint32_t d = somfy_absdur(durations[k]);
        if (somfy_diff(d, SOMFY_TE_SHORT) < SOMFY_TE_DELTA) {
            half.push_back(level);
        } else if (somfy_diff(d, SOMFY_TE_LONG) < SOMFY_TE_DELTA) {
            half.push_back(level);
            half.push_back(level);
        } else {
            break; // end of frame / inter-frame gap
        }
        if (half.size() >= (size_t)(SOMFY_BITS * 2 + 4)) break;
    }
    if (half.size() < (size_t)(SOMFY_BITS * 2)) return false;

    // 3) try both alignments and both polarities, validated by checksum
    uint64_t data = 0;
    if (!(somfy_try(half, 0, false, data) || somfy_try(half, 1, false, data) ||
          somfy_try(half, 0, true, data) || somfy_try(half, 1, true, data))) {
        return false;
    }

    // 4) extract the fields (56-bit frame, MSB first)
    uint8_t ctrl = (uint8_t)((data >> 44) & 0xF);      // command: 1=My 2=Up 4=Down 8=Prog
    uint16_t rolling = (uint16_t)((data >> 24) & 0xFFFF); // 16-bit rolling code
    uint32_t addr = (uint32_t)(data & 0xFFFFFF);       // 24-bit address

    out.protocol = "Somfy Telis";
    out.mf_name = "Somfy RTS";
    out.serial = addr;
    out.cnt = rolling;
    out.btn = ctrl;
    out.key = data;
    out.Bit = SOMFY_BITS;
    out.te = SOMFY_TE_SHORT;
    out.preset = "FuriHalSubGhzPresetOok650Async";
    return true;
}
