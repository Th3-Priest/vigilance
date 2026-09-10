// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - Hunt Mode (#8): proximity direction finder. Pick a WiFi target,
// then measure its RSSI in a loop (fast single-channel scan) and turn proximity
// into an on-screen bar + green LED intensity + beep rate.
#include "hunt_mode.h"

#include "core/display.h"
#include "core/led_control.h"
#include "core/mykeyboard.h"
#include "core/oui_vendor.h"
#include "modules/others/audio.h"
#include <WiFi.h>
#include <globals.h>
#include <vector>

struct HuntAP {
    uint8_t bssid[6];
    String ssid;
    int rssi;
    uint8_t ch;
};

// Small homemade vertical selector (Up/Down/Sel/Esc).
static int huntChoose(const std::vector<String> &items, const char *title) {
    if (items.empty()) return -1;
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    int sel = 0, top = 0;
    bool redraw = true;
    drawMainBorderWithTitle(title);
    for (;;) {
        if (redraw) {
            tft.fillRect(6, 26, tftWidth - 12, tftHeight - 40, bg);
            tft.setTextSize(1);
            int rows = (tftHeight - 44) / 14;
            if (sel < top) top = sel;
            if (sel >= top + rows) top = sel - rows + 1;
            int y = 30;
            for (int i = top; i < (int)items.size() && i < top + rows; i++, y += 14) {
                bool s = (i == sel);
                if (s) {
                    tft.fillRect(8, y - 1, tftWidth - 16, 13, ac);
                    tft.setTextColor(bg, ac);
                } else tft.setTextColor(ac, bg);
                tft.drawString(items[i].substring(0, 34), 12, y, 1);
            }
            tft.setTextColor(dimc, bg);
            tft.drawString("SEL: track   ESC: back", 10, tftHeight - 12, 1);
            redraw = false;
        }
        int cnt = (int)items.size();
#ifdef HAS_ENCODER
        int32_t steps = drainRotarySteps();
        if (steps > 0) {
            sel = (sel - 1 + cnt) % cnt;
            redraw = true;
        } else if (steps < 0) {
            sel = (sel + 1) % cnt;
            redraw = true;
        }
#endif
        if (check(UpPress)) {
            sel = (sel - 1 + cnt) % cnt;
            redraw = true;
        }
        if (check(DownPress)) {
            sel = (sel + 1) % cnt;
            redraw = true;
        }
        if (check(EscPress)) return -1;
        if (check(SelPress)) return sel;
        delay(20);
    }
}

static void huntLoop(const HuntAP &t) {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    ledEffects(false);
    drawMainBorderWithTitle("HUNT MODE");
    tft.setTextColor(ac, bg);
    tft.drawString(t.ssid.substring(0, 24), 10, 30, 1);
    tft.setTextColor(dimc, bg);
    tft.drawString(String("ch") + t.ch + "  " + ouiLabel(t.bssid), 10, 44, 1);

    int smooth = -100;
    uint32_t lastBeep = 0;
    for (;;) {
        if (check(EscPress)) break;
        // Fast single-channel scan on the target's channel.
        int m = WiFi.scanNetworks(false, true, false, 150, t.ch);
        int rssi = -100;
        for (int i = 0; i < m; i++) {
            uint8_t *b = WiFi.BSSID(i);
            if (b && memcmp(b, t.bssid, 6) == 0) {
                rssi = (int)WiFi.RSSI(i);
                break;
            }
        }
        WiFi.scanDelete();
        smooth = (smooth * 2 + rssi) / 3; // smoothing

        // Proximity 0..255 (stronger = closer).
        int level = map(smooth, -90, -35, 0, 255);
        if (level < 0) level = 0;
        if (level > 255) level = 255;

        // Graduated green LED (red if lost).
        if (rssi <= -100) setLedColor(CRGB(40, 0, 0));
        else setLedColor(CRGB(0, level, level / 6));

        // Beep faster and faster.
        uint32_t now = millis();
        uint32_t interval = 900 - (uint32_t)level * 3; // 900ms (far) -> ~135ms (near)
        if (rssi > -100 && now - lastBeep > interval) {
            lastBeep = now;
            _tone(1600 + level * 4, 25);
        }

        // Display.
        tft.fillRect(6, 60, tftWidth - 12, tftHeight - 76, bg);
        tft.setTextColor(rssi <= -100 ? TFT_RED : ac, bg);
        tft.drawString(rssi <= -100 ? "-- lost --" : (String(smooth) + " dBm"), 10, 64, 1);
        int bw = (tftWidth - 24) * level / 255;
        tft.drawRect(12, 84, tftWidth - 24, 20, dimc);
        uint16_t barCol = level > 200 ? TFT_GREEN : (level > 90 ? ac : dimc);
        tft.fillRect(13, 85, bw, 18, barCol);
        tft.setTextColor(dimc, bg);
        tft.drawString(level > 200 ? "VERY CLOSE!" : (level > 120 ? "hot" : (level > 50 ? "warm" : "cold")), 12, 110, 1);
        tft.drawString("ESC: quit", 10, tftHeight - 12, 1);
    }
    setLedColor(CRGB::Black);
    ledSetup();
}

void hunt_mode_setup() {
    returnToMenu = false;
    drawMainBorderWithTitle("HUNT MODE");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawString("Scanning targets...", 10, 40, 1);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(80);
    int n = WiFi.scanNetworks(false, true);
    std::vector<HuntAP> aps;
    std::vector<String> items;
    for (int i = 0; i < n && i < 40; i++) {
        uint8_t *b = WiFi.BSSID(i);
        if (!b) continue;
        HuntAP a;
        memcpy(a.bssid, b, 6);
        a.ssid = WiFi.SSID(i).length() ? WiFi.SSID(i) : String("(hidden)");
        a.rssi = (int)WiFi.RSSI(i);
        a.ch = (uint8_t)WiFi.channel(i);
        aps.push_back(a);
        items.push_back(a.ssid.substring(0, 18) + " " + String(a.rssi) + "dB " + ouiLabel(a.bssid));
    }
    WiFi.scanDelete();

    if (aps.empty()) {
        displayError("No targets", true);
        WiFi.mode(WIFI_OFF);
        return;
    }
    for (;;) {
        int sel = huntChoose(items, "Choose target");
        if (sel < 0) break;
        huntLoop(aps[sel]);
    }
    WiFi.mode(WIFI_OFF);
}
