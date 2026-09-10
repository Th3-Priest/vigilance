// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance: FAAC SLH (Serial Learning Hopping / RC) decoder. Ported from the
// Flipper Zero firmware (lib/subghz/protocols/faac_slh.c, GPL-3.0-or-later,
// (C) Flipper Devices Inc. and contributors). See THIRD_PARTY.md.
//
// 64-bit PWM: bit 0 = short HIGH (255 us) + long LOW (595 us), bit 1 = long HIGH
// + short LOW. The 64 bits (MSB first) are reversed then split into fix (28-bit
// serial + 4-bit button) and hop (32-bit rolling code). Decode only (rolling
// code: a replay grants no access).
#include "rf_decoder.h"
#include <stdint.h>
#include <vector>

#define FAAC_TE_SHORT 255
#define FAAC_TE_LONG 595
#define FAAC_BITS 64

static inline uint32_t faac_absdur(int v) { return v > 0 ? (uint32_t)v : (uint32_t)(-v); }
static inline uint32_t faac_diff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

// Read 64 FAAC PWM bits starting at offset `start`. Returns true + `dataOut`
// (64 bits, MSB first) if all 64 (HIGH,LOW) pairs are valid.
static bool faac_read(const std::vector<int> &durations, int start, uint64_t &dataOut) {
    int n = (int)durations.size();
    uint64_t data = 0;
    int bits = 0;
    int k = start;
    for (; bits < FAAC_BITS; k += 2) {
        if (k + 1 >= n) return false;
        int hi = durations[k];
        int lo = durations[k + 1];
        if (hi <= 0 || lo >= 0) return false; // expect HIGH then LOW
        uint32_t hd = (uint32_t)hi, ld = (uint32_t)(-lo);
        uint8_t bit;
        if (faac_diff(hd, FAAC_TE_SHORT) < 150 && faac_diff(ld, FAAC_TE_LONG) < 200) bit = 0;
        else if (faac_diff(hd, FAAC_TE_LONG) < 200 && faac_diff(ld, FAAC_TE_SHORT) < 150) bit = 1;
        else return false;
        data = (data << 1) | bit;
        bits++;
    }
    dataOut = data;
    return true;
}

bool rf_decode_faac_slh(const std::vector<int> &durations, RfCodes &out) {
    int n = (int)durations.size();
    if (n < FAAC_BITS * 2) return false;

    // Robust scan: try every start offset (avoids depending on the exact preamble
    // format). We require 64 perfectly typed PWM pairs.
    uint64_t data = 0;
    bool found = false;
    for (int start = 0; start + FAAC_BITS * 2 <= n; start++) {
        if (durations[start] <= 0) continue; // a bit starts with a HIGH
        if (faac_read(durations, start, data)) {
            found = true;
            break;
        }
    }
    if (!found) return false;

    // Reverse the 64 bits, then split into fix / hop
    uint64_t rev = 0;
    uint64_t tmp = data;
    for (int i = 0; i < 64; i++) {
        rev = (rev << 1) | (tmp & 1);
        tmp >>= 1;
    }
    uint32_t code_fix = (uint32_t)(rev & 0xFFFFFFFFULL);
    uint32_t code_hop = (uint32_t)((rev >> 32) & 0xFFFFFFFFULL);

    uint32_t serial = code_fix & 0x0FFFFFFF; // 28 bits
    uint8_t button = (uint8_t)((code_fix >> 28) & 0xF);

    if (serial == 0 || code_hop == 0) return false; // guard against false positives

    out.protocol = "FAAC SLH";
    out.mf_name = "FAAC SLH";
    out.serial = serial;
    out.btn = button;
    out.hop = code_hop;
    out.key = rev;
    out.Bit = FAAC_BITS;
    out.te = FAAC_TE_SHORT;
    out.preset = "FuriHalSubGhzPresetOok650Async";
    return true;
}
