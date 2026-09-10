// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - Ambient Sub-GHz Census (#46). Uses a standalone RMT RX session to
// capture OOK bursts on the configured frequency, runs the same decoder chain as
// the RF scanner, and keeps a de-duplicated list of the devices heard nearby with
// a hit count. Every new device is logged to the shared journal, so sub-GHz
// activity shows up in the timeline and companion just like WiFi/BLE events.
#include "rf_census.h"

#include "core/display.h"
#include "core/event_log.h"
#include "core/vig_env.h"
#include "core/mykeyboard.h"
#include "protocols/rf_decoder.h"
#include "rf_utils.h"
#include "structs.h"
#include <globals.h>
#include <vector>

struct CensusEntry {
    String label;
    uint64_t key;
    uint32_t count;
    uint32_t lastS;
};

static bool census_decode(const std::vector<int> &durations, RfCodes &r) {
    r.mf_name = "Unknown";
    r.fix = 0;
    r.hop = 0;
    r.btn = 0;
    r.cnt = 0;
    r.encrypted = 0;
    r.data = "";
    return rf_decode_keeloq(durations, r) || rf_decode_somfy(durations, r) ||
           rf_decode_faac_slh(durations, r) || rf_decode_hormann(durations, r) ||
           rf_decode_came_atomo(durations, r) || rf_decode_weather(durations, r) ||
           rf_decode_ook(durations, r);
}

static void census_draw(std::vector<CensusEntry> &seen, float freq) {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    tft.fillRect(6, 26, tftWidth - 12, tftHeight - 40, bg);
    tft.setTextColor(ac, bg);
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "%.2f MHz   %d devices", (double)freq, (int)seen.size());
    tft.drawString(hdr, 10, 30, 1);

    int y = 46;
    if (seen.empty()) {
        tft.setTextColor(dimc, bg);
        tft.drawString("Listening for sub-GHz devices...", 10, y, 1);
    } else {
        for (int i = (int)seen.size() - 1; i >= 0 && y < tftHeight - 26; i--) {
            tft.setTextColor(ac, bg);
            tft.drawString(seen[i].label.substring(0, 30), 10, y, 1);
            tft.setTextColor(dimc, bg);
            tft.drawString("x" + String((unsigned long)seen[i].count), tftWidth - 44, y, 1);
            y += 13;
        }
    }
    tft.setTextColor(dimc, bg);
    tft.drawString("RX only   ESC to quit", 10, tftHeight - 12, 1);
}

void rf_census_setup() {
    returnToMenu = false;
    float freq = bruceConfigPins.rfFreq;
    if (freq < 280 || freq > 960) freq = 433.92;

    if (!initRfModule("rx", freq)) {
        displayError("RF init failed", true);
        return;
    }
    RfRxSession rx;
    if (!rx.begin()) {
        deinitRfModule();
        displayError("RX start failed", true);
        return;
    }

    std::vector<CensusEntry> seen;
    drawMainBorderWithTitle("SUB-GHZ CENSUS");
    uint32_t lastDraw = 0;

    for (;;) {
        if (check(EscPress) || returnToMenu) break;

        std::vector<int> durations;
        if (rx.poll(durations)) {
            RfCodes r;
            if (census_decode(durations, r)) {
                String label = r.protocol + (r.data.length() ? (" " + r.data) : "");
                int idx = -1;
                for (size_t i = 0; i < seen.size(); i++)
                    if (seen[i].key == r.key && r.key != 0) {
                        idx = (int)i;
                        break;
                    }
                if (idx < 0) {
                    if (seen.size() < 64) seen.push_back({label, r.key, 1, millis() / 1000});
                    vigLogEvent("SUBGHZ", r.protocol);
                } else {
                    seen[idx].count++;
                    seen[idx].lastS = millis() / 1000;
                    seen[idx].label = label;
                }
            }
        }

        if (millis() - lastDraw > 400) {
            lastDraw = millis();
            vigEnvSet(VIG_ENV_SUB, (int)seen.size()); // feed the home screen radar
            census_draw(seen, freq);
        }
        delay(2);
    }

    rx.end();
    deinitRfModule();
}
