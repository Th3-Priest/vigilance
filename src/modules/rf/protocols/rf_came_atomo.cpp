// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance: CAME Atomo detection (DETECTION ONLY). Inspired by the Flipper Zero
// firmware (lib/subghz/protocols/came_atomo.c, GPL-3.0-or-later). See THIRD_PARTY.md.
//
// IMPORTANT: decrypting CAME Atomo needs a key table (rainbow table) that the
// Flipper firmware does NOT ship (an external file supplied by the user). To stay
// self-contained and not embed a set of access keys that are deliberately kept
// out, Vigilance only IDENTIFIES the protocol and shows the raw encrypted frame,
// without decrypting it.
//
// Manchester, te 600/1200 us, header = long LOW ~72 ms. We validate by requiring
// >= 62 clean manchester pairs after the header (the keys are not used).
#include "rf_decoder.h"
#include <stdint.h>
#include <vector>

#define ATOMO_TE_SHORT 600
#define ATOMO_TE_LONG 1200
#define ATOMO_TE_DELTA 300
#define ATOMO_BITS 62

static inline uint32_t atomo_absdur(int v) { return v > 0 ? (uint32_t)v : (uint32_t)(-v); }
static inline bool atomo_near(uint32_t d, uint32_t ref, uint32_t delta) {
    return (d > ref ? d - ref : ref - d) < delta;
}

bool rf_decode_came_atomo(const std::vector<int> &durations, RfCodes &out) {
    int n = (int)durations.size();
    if (n < ATOMO_BITS) return false;

    // 1) header: long LOW ~72 ms (te_long * 60)
    int idx = -1;
    for (int k = 0; k + 2 < n; k++) {
        if (durations[k] < 0 && atomo_absdur(durations[k]) > 50000 &&
            atomo_absdur(durations[k]) < 95000) {
            idx = k + 1;
            break;
        }
    }
    if (idx < 0) return false;

    // 2) rebuild the half-symbols (short = 1, long = 2)
    std::vector<uint8_t> half;
    half.reserve(ATOMO_BITS * 2 + 8);
    for (int k = idx; k < n; k++) {
        uint8_t level = durations[k] > 0 ? 1 : 0;
        uint32_t d = atomo_absdur(durations[k]);
        if (atomo_near(d, ATOMO_TE_SHORT, ATOMO_TE_DELTA)) half.push_back(level);
        else if (atomo_near(d, ATOMO_TE_LONG, ATOMO_TE_DELTA)) {
            half.push_back(level);
            half.push_back(level);
        } else break; // gap / end of frame
        if (half.size() >= (size_t)(64 * 2 + 4)) break;
    }
    if (half.size() < (size_t)(ATOMO_BITS * 2)) return false;

    // 3) decode the manchester (try both alignments): we don't decrypt, we just
    //    require >= 62 clean pairs to confirm it really is CAME Atomo.
    for (int off = 0; off < 2; off++) {
        uint64_t data = 0;
        int bits = 0;
        bool clean = true;
        for (size_t p = (size_t)off; p + 1 < half.size() && bits < 64; p += 2) {
            uint8_t a = half[p], b = half[p + 1];
            uint8_t bit;
            if (a == 0 && b == 1) bit = 1;
            else if (a == 1 && b == 0) bit = 0;
            else {
                clean = false;
                break;
            }
            data = (data << 1) | (uint8_t)(bit ^ 1); // inverted value (cf. Flipper)
            bits++;
        }
        if (clean && bits >= ATOMO_BITS) {
            out.protocol = "CAME Atomo";
            out.mf_name = "CAME Atomo (encrypted)";
            out.key = data;         // raw frame, NOT decrypted
            out.Bit = bits;
            out.te = ATOMO_TE_SHORT;
            out.serial = 0;
            out.cnt = 0;
            out.btn = 0;
            out.preset = "FuriHalSubGhzPresetOok650Async";
            return true;
        }
    }
    return false;
}
