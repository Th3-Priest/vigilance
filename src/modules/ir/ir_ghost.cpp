// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - IR Ghost-Hunter (#13): an IR activity monitor. It turns on the IR
// receiver and bumps a level on every captured frame (the level decays on its
// own), shown as a bar plus a red LED. Handy to spot an active IR emitter
// (sensor, some cameras). Best-effort: the TSOP demodulates 38 kHz, so steady
// unmodulated IR can go unnoticed.
#include "ir_ghost.h"

#include "core/display.h"
#include "core/led_control.h"
#include "core/mykeyboard.h"
#include <IRrecv.h>
#include <globals.h>

void ir_ghost_setup() {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    IRrecv irrecv(bruceConfigPins.irRx, 512, 15, false);
    decode_results res;
    irrecv.enableIRIn();
    ledEffects(false);
    drawMainBorderWithTitle("IR GHOST");

    int level = 0;
    uint32_t hits = 0, lastDecay = millis();
    for (;;) {
        if (check(EscPress) || check(SelPress)) break;
        if (irrecv.decode(&res)) {
            level += 45;
            if (level > 100) level = 100;
            hits++;
            irrecv.resume();
        }
        uint32_t now = millis();
        if (now - lastDecay > 120) {
            lastDecay = now;
            level = (level > 3) ? level - 3 : 0;
        }
        setLedColor(CRGB(level * 2, 0, 0)); // red scales with activity

        tft.fillRect(6, 40, tftWidth - 12, tftHeight - 56, bg);
        tft.setTextColor(level > 30 ? TFT_RED : ac, bg);
        tft.drawString(level > 30 ? "! IR ACTIVITY !" : "scanning IR...", 12, 46, 1);
        tft.drawRect(12, 66, tftWidth - 24, 18, dimc);
        tft.fillRect(13, 67, (tftWidth - 24) * level / 100, 16, level > 60 ? TFT_RED : ac);
        tft.setTextColor(dimc, bg);
        tft.drawString("captures: " + String((unsigned long)hits), 12, 92, 1);
        tft.drawString("catches modulated IR (cams/sensors/remotes)", 10, tftHeight - 26, 1);
        tft.drawString("SEL/ESC to quit", 10, tftHeight - 12, 1);
        delay(20);
    }

    irrecv.disableIRIn();
    setLedColor(CRGB::Black);
    ledSetup();
}
