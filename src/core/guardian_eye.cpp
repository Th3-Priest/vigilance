// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - Guardian Eye (#30) + Sentinel Pulse (#31). An animated eye that
// mirrors the threat level, worked out from how many new journal events have
// arrived since the screen opened. Optional breathing LED (#31).
#include "guardian_eye.h"

#include "core/display.h"
#include "core/event_log.h"
#include "core/led_control.h"
#include "core/mykeyboard.h"
#include "core/vig_ux.h"
#include <globals.h>
#include <math.h>

void guardian_eye_setup() {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    uint32_t baseline = vigEventsTotal();
    int cx = tftWidth / 2, cy = tftHeight / 2 + 4;
    int lookx = 0, looky = 0, tgx = 0, tgy = 0;
    uint32_t lastLook = 0, lastBlink = 0, blinkStart = 0, nextBlink = 3000;
    bool blinking = false;

    ledEffects(false);
    drawMainBorderWithTitle("GUARDIAN EYE");

    for (;;) {
        if (check(EscPress) || check(SelPress)) break;
        uint32_t now = millis();
        uint32_t newEv = vigEventsTotal() - baseline;
        int threat = (newEv == 0) ? 0 : (newEv < 3 ? 1 : 2);
        uint16_t iris = threat >= 2 ? TFT_RED : (threat == 1 ? TFT_ORANGE : ac);

        // Gaze drifts around slowly.
        if (now - lastLook > 1400) {
            lastLook = now;
            tgx = (int)random(-10, 11);
            tgy = (int)random(-6, 7);
        }
        if (lookx < tgx) lookx++;
        else if (lookx > tgx) lookx--;
        if (looky < tgy) looky++;
        else if (looky > tgy) looky--;

        // Blinks (not while alerting: the eye stares).
        if (!blinking && threat < 2 && now - lastBlink > nextBlink) {
            blinking = true;
            blinkStart = now;
        }
        if (blinking && now - blinkStart > 170) {
            blinking = false;
            lastBlink = now;
            nextBlink = (uint32_t)random(2500, 4800);
        }

        // Draw.
        tft.fillRect(6, 26, tftWidth - 12, tftHeight - 42, bg);
        tft.drawCircle(cx, cy, 46, dimc);
        tft.drawCircle(cx, cy, 45, dimc);
        int ix = cx + lookx, iy = cy + looky;
        tft.fillCircle(ix, iy, 22, iris);
        tft.fillCircle(ix, iy, threat >= 2 ? 12 : 9, TFT_BLACK); // pupil dilated when alerting
        tft.fillCircle(ix - 6, iy - 6, 3, TFT_WHITE);            // catch light
        if (threat >= 1) {                                       // furrowed brow
            tft.drawLine(cx - 32, cy - 30, cx + 6, cy - 20, iris);
            tft.drawLine(cx - 32, cy - 29, cx + 6, cy - 19, iris);
        }
        if (blinking) {
            float t = (now - blinkStart) / 170.0f;
            float c = 1.0f - fabsf(1.0f - 2.0f * t); // 0 -> 1 -> 0
            int cover = (int)(48 * c);
            tft.fillRect(cx - 48, cy - 48, 96, cover, bg);
            tft.fillRect(cx - 48, cy + 48 - cover, 96, cover, bg);
        }

        // State label.
        tft.setTextColor(iris, bg);
        tft.drawCentreString(threat >= 2 ? "! ALERT !" : (threat == 1 ? "WATCHING" : "calm"), cx, tftHeight - 26, 1);
        tft.setTextColor(dimc, bg);
        tft.drawString("SEL/ESC", 10, tftHeight - 12, 1);

        // Sentinel Pulse (#31): LED breathes with the threat level, if enabled.
        if (vigBreathingEnabled()) {
            int b = (int)((sinf((now % 3000) / 3000.0f * 2 * PI) * 0.5f + 0.5f) * 170);
            if (threat >= 2) setLedColor(((now / 150) % 2) ? CRGB(180, 0, 0) : CRGB::Black); // red strobe
            else if (threat == 1) setLedColor(CRGB(b, (b * 2) / 3, 0));                       // amber
            else setLedColor(CRGB(0, b, b / 4));                                              // cyan
        } else setLedColor(CRGB::Black);

        delay(40);
    }
    setLedColor(CRGB::Black);
    ledSetup();
    // Resume the ambient breathe if the user left the LED Pulse on.
    if (vigBreathingEnabled()) {
        setLedEffect(LED_COLOR_BREATHE);
        ledEffects(true);
    }
}
