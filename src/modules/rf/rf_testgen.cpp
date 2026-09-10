// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - RF test signal generator (#42). Emits a short identifiable OOK
// pattern (24 bits, RcSwitch/Princeton preset) at a given frequency, or sweeps
// 433.92 / 315 / 868.35 MHz. Use it to audit your own receivers. Everything goes
// through sendRfCommand() (CC1101 init + OOK config + TX + deinit), so we never
// touch the driver directly; we just change the frequency step by step.
#include "rf_testgen.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/utils.h"
#include "rf_send.h"
#include <globals.h>
#include <vector>

// Build the test packet (recognizable pattern 0x155555, 24 bits, OOK).
static RfCodes vigTestCode(float fMHz) {
    RfCodes t;
    t.frequency = (uint32_t)(fMHz * 1000000.0);
    t.protocol = "RcSwitch"; // dispatched to legacy protocol 1 (Princeton/PT2262)
    t.preset = "1";          // numeric preset -> OOK (ASK modulation by default)
    t.key = 0x155555;        // test pattern 0101... over 24 bits
    t.Bit = 24;
    t.te = 350; // typical Princeton element duration (us)
    t.data = "";
    return t;
}

// Emit `bursts` bursts at fMHz. Returns false if the user aborted (ESC).
static bool vigTxAt(float fMHz, int bursts) {
    RfCodes t = vigTestCode(fMHz);
    const uint16_t ac = bruceConfig.priColor, bg = bruceConfig.bgColor;
    for (int i = 0; i < bursts; i++) {
        if (check(EscPress)) return false;
        tft.fillRect(6, 58, tftWidth - 12, 22, bg);
        tft.setTextColor(ac, bg);
        tft.drawString("TX " + String(fMHz, 2) + " MHz  (" + String(i + 1) + "/" + String(bursts) + ")", 12, 62, 1);
        sendRfCommand(t, true); // hideDefaultUI = we handle the display ourselves
        delay(120);
    }
    return true;
}

void rf_testgen_setup() {
    // Warning (RF transmission).
    drawMainBorderWithTitle("RF TEST GEN");
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    tft.setTextColor(ac, bg);
    tft.drawString("Emits a test OOK pattern.", 10, 40, 1);
    tft.setTextColor(dimc, bg);
    tft.drawString("Use only on YOUR own hardware", 10, 56, 1);
    tft.drawString("and an allowed ISM band.", 10, 68, 1);
    tft.setTextColor(ac, bg);
    tft.drawString("SEL = continue   ESC = cancel", 10, 90, 1);
    while (true) {
        if (check(SelPress)) break;
        if (check(EscPress)) return;
        delay(20);
    }

    for (;;) {
        std::vector<Option> o = {
            {"Current (" + String(bruceConfigPins.rfFreq, 2) + ")", []() {}},
            {"433.92 MHz",   []() {}},
            {"315.00 MHz",   []() {}},
            {"868.35 MHz",   []() {}},
            {"ISM sweep",    []() {}},
            {"Back",         []() {}},
        };
        int s = loopOptions(o, MENU_TYPE_SUBMENU, "Test frequency");
        if (s < 0 || s == 5) return;

        drawMainBorderWithTitle("RF TEST GEN");
        tft.setTextColor(dimc, bg);
        tft.drawString("ESC to stop", 10, tftHeight - 14, 1);

        if (s == 0) vigTxAt(bruceConfigPins.rfFreq, 20);
        else if (s == 1) vigTxAt(433.92, 20);
        else if (s == 2) vigTxAt(315.00, 20);
        else if (s == 3) vigTxAt(868.35, 20);
        else if (s == 4) { // ISM sweep looping until ESC
            const float fs[3] = {433.92, 315.00, 868.35};
            bool run = true;
            while (run) {
                for (int k = 0; k < 3 && run; k++) {
                    if (!vigTxAt(fs[k], 5)) run = false;
                    delay(150);
                }
                if (check(EscPress)) run = false;
            }
        }

        tft.fillRect(6, 58, tftWidth - 12, 22, bg);
        tft.setTextColor(ac, bg);
        tft.drawString("Done.", 12, 62, 1);
        delay(500);
    }
}
