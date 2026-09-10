// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance standby scene. One sentinel eye, set inside a sweeping radar, that
// watches the live RF environment: contacts drift on the rings, the eye's iris
// shifts from calm cyan to alert red as journal events pile up, and a rotating
// status line gives it a bit of character. Drawn into the HUD framebuffer.
#include "vig_standby.h"

#include "display.h"
#include "event_log.h"
#include "mykeyboard.h"
#include "settings.h" // timeStr
#include "utils.h"    // updateTimeStr
#include "vig_env.h"
#include "vig_hud.h"
#include "vig_ux.h"
#include <globals.h>
#include <math.h>

// Almond eye outline between (cx-ew,cy) and (cx+ew,cy), half-height eh.
static void eyeLid(TFT_eSprite &s, int cx, int cy, int ew, int eh, uint16_t col, uint16_t bg) {
    float px = cx - ew, ptop = cy, pbot = cy;
    for (int i = 1; i <= 16; i++) {
        float xx = cx - ew + 2.0f * ew * i / 16.0f;
        float rel = (xx - cx) / (float)ew;
        float yy = eh * (1.0f - rel * rel);
        s.drawWideLine(px, ptop, xx, cy - yy, 2.0f, col, bg);
        s.drawWideLine(px, pbot, xx, cy + yy, 2.0f, col, bg);
        px = xx;
        ptop = cy - yy;
        pbot = cy + yy;
    }
}

void vigStandbyRun(bool fromStartup) {
    TFT_eSprite *sp = vigHudSprite();
    const VigPal &p = vigPal();

    // No framebuffer: keep it graceful, just wait for a key on a dark screen.
    if (!sp) {
        tft.fillScreen(p.bg);
        tft.setTextColor(p.acc, p.bg);
        tft.drawCentreString("VIGILANCE", tftWidth / 2, tftHeight / 2 - 8, 1);
        for (;;) {
            if (check(SelPress) || check(EscPress)) break;
#ifdef HAS_ENCODER
            if (drainRotarySteps() != 0) break;
#endif
            delay(30);
        }
        if (fromStartup) tft.fillScreen(p.bg);
        return;
    }
    TFT_eSprite &s = *sp;

    const int W = tftWidth, H = tftHeight;
    const int cx = W / 2, cy = H / 2 + 8;
    uint32_t baseline = vigEventsTotal();
    int lookx = 0, looky = 0, tgx = 0, tgy = 0;
    uint32_t lastLook = 0, lastBlink = 0, blinkStart = 0, nextBlink = 3200, lastPhrase = 0;
    bool blinking = false;
    int phraseIdx = 0;

    static const char *CALM[] = {"AREA SECURE", "ALL QUIET", "STANDING WATCH", "SCANNING"};

    // Exit on a real input only (short press, long press, or wheel). AnyKeyPress
    // is unreliable here: the background input task can leave it set, which would
    // make the screensaver quit the instant it opened.
    for (;;) {
        uint32_t now = millis();
        if (check(SelPress) || check(EscPress)) break;
#ifdef HAS_ENCODER
        if (drainRotarySteps() != 0) break;
#endif
        if (vigLiveRadarEnabled()) vigScanTick();

        int nW = vigEnvGet(VIG_ENV_WIFI), nB = vigEnvGet(VIG_ENV_BLE), nS = vigEnvGet(VIG_ENV_SUB);
        uint32_t newEv = vigEventsTotal() - baseline;
        int threat = (newEv == 0) ? 0 : (newEv < 3 ? 1 : 2);
        uint16_t iris = threat >= 2 ? p.red : (threat == 1 ? p.amber : p.acc);

        // gaze drift
        if (now - lastLook > 1500) {
            lastLook = now;
            tgx = (int)random(-12, 13);
            tgy = (int)random(-6, 7);
        }
        lookx += (lookx < tgx) - (lookx > tgx);
        looky += (looky < tgy) - (looky > tgy);
        // blink (never while alerting: the eye stares)
        if (!blinking && threat < 2 && now - lastBlink > nextBlink) {
            blinking = true;
            blinkStart = now;
        }
        if (blinking && now - blinkStart > 180) {
            blinking = false;
            lastBlink = now;
            nextBlink = (uint32_t)random(2600, 5200);
        }
        if (now - lastPhrase > 2600) {
            lastPhrase = now;
            phraseIdx++;
        }

        vigHudBackground(s);

        // radar behind the eye
        float sweep = (float)((now * 80UL / 1000UL) % 360UL);
        int rr0 = 52, rr1 = 66, rr2 = 80;
        vigHudRing(s, cx, cy, rr0, rr0 - 1, vigMix(p.acc, p.bg, 40), p.bg);
        vigHudRing(s, cx, cy, rr1, rr1 - 1, vigMix(p.acc, p.bg, 30), p.bg);
        vigHudRing(s, cx, cy, rr2, rr2 - 1, vigMix(p.acc, p.bg, 22), p.bg);
        float a = sweep * DEG_TO_RAD;
        for (int i = 20; i >= 1; i--) {
            float ai = a - i * 0.05f;
            uint8_t al = (uint8_t)((1.0f - i / 21.0f) * 0.30f * 255);
            s.drawLine(cx, cy, cx + (int)(cosf(ai) * rr2), cy + (int)(sinf(ai) * rr2), vigMix(p.green, p.bg, al));
        }
        s.drawWideLine(cx, cy, cx + cosf(a) * rr2, cy + sinf(a) * rr2, 1.4f, vigMix(p.green, p.bg, 150));

        // contacts drift on the outer rings, colored by band, lit as the sweep passes
        int contacts[3] = {nW < 0 ? 0 : nW, nB < 0 ? 0 : nB, nS < 0 ? 0 : nS};
        uint16_t ccol[3] = {p.acc, p.blue, p.amber};
        for (int k = 0; k < 3; k++) {
            int m = contacts[k] > 5 ? 5 : contacts[k];
            for (int i = 0; i < m; i++) {
                float base = (float)((k * 613 + i * 3779) % 360);
                float ang = fmodf(base + now * (0.004f + 0.001f * k), 360.0f);
                float rad = (k == 2 ? rr2 : (k == 1 ? rr1 : rr0)) - 3;
                float d = fabsf(fmodf(sweep - ang + 540.0f, 360.0f) - 180.0f);
                float lit = 1.0f - d / 40.0f;
                if (lit < 0) lit = 0;
                float bx = cx + cosf(ang * DEG_TO_RAD) * rad, by = cy + sinf(ang * DEG_TO_RAD) * rad;
                s.drawSpot(bx, by, 1.4f + lit * 1.8f, vigMix(ccol[k], p.bg, (uint8_t)(70 + lit * 185)));
            }
        }

        // the sentinel eye
        int ew = 40, eh = 24;
        eyeLid(s, cx, cy, ew, eh, vigMix(iris, p.bg, 150), p.bg);
        int ix = cx + lookx, iy = cy + looky;
        s.fillSmoothCircle(ix, iy, 19, iris);
        s.fillSmoothCircle(ix, iy, threat >= 2 ? 12 : 8, p.bg); // pupil dilates on alert
        s.drawSpot(ix - 6, iy - 6, 2.4f, TFT_WHITE);            // catch light
        if (threat >= 1) { // furrowed brow
            s.drawWideLine(cx - 30, cy - 22, cx + 6, cy - 15, 2.0f, iris, p.bg);
        }
        if (blinking) {
            float t = (now - blinkStart) / 180.0f;
            float c = 1.0f - fabsf(1.0f - 2.0f * t); // 0 -> 1 -> 0
            int cover = (int)((eh + 4) * c);
            s.fillRect(cx - ew, cy - eh - 2, 2 * ew, cover, p.bg);
            s.fillRect(cx - ew, cy + eh + 2 - cover, 2 * ew, cover, p.bg);
        }

        // clock top-left
        if (clock_set) {
#if defined(HAS_RTC)
            updateTimeStr(_rtc.getTimeStruct());
#else
            updateTimeStr(rtc.getTimeStruct());
#endif
            s.setTextDatum(TL_DATUM);
            s.setTextColor(p.text);
            s.setTextSize(2);
            s.drawString(String(timeStr).substring(0, 5), 8, 4, 2);
            s.setTextSize(1);
        } else {
            vigHudSpaced(s, "VIGILANCE", 8, 8, p.acc, 1);
        }
        // vitals top-right
        s.setTextDatum(TR_DATUM);
        s.setTextColor(p.textDim);
        s.drawString(String(getBattery()) + "%", W - 8, 8, 2);

        // status line
        const char *phrase;
        if (threat >= 2) phrase = "! CONTACT !";
        else if (threat == 1) phrase = "MOVEMENT";
        else phrase = CALM[phraseIdx % 4];
        s.setTextDatum(TC_DATUM);
        s.setTextColor(iris);
        s.drawString(phrase, cx, H - 26, 2);
        int seen = contacts[0] + contacts[1] + contacts[2];
        s.setTextColor(p.textDim);
        s.drawString(
            (seen > 0 ? String(seen) + " contacts tracked" : String("no contacts")), cx, H - 12, 1
        );

        vigHudPush();
        delay(33);
    }

    vigScanStop();
    if (fromStartup) tft.fillScreen(p.bg);
}
