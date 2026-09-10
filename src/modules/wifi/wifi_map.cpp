// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - WiFi neighborhood map (#30). Inventory of access points in
// range, enriched with the embedded OUI database (vendor) and the encryption
// type. Two views: channel usage and detailed list. Fully passive (standard
// active scan, no injection).
#include "wifi_map.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/oui_vendor.h"
#include "core/wifi/wifi_common.h"
#include <WiFi.h>
#include <algorithm>
#include <globals.h>
#include <vector>

struct WMap {
    String ssid;
    uint8_t bssid[6];
    int32_t rssi;
    int32_t ch;
    uint8_t enc;
};

static const char *wm_encStr(uint8_t e) {
    switch (e) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
        default: return "?";
    }
}

static void wm_scan(std::vector<WMap> &aps) {
    aps.clear();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(80);
    int n = WiFi.scanNetworks(false, true); // sync, hidden networks included
    for (int i = 0; i < n; i++) {
        WMap a;
        a.ssid = WiFi.SSID(i);
        a.rssi = WiFi.RSSI(i);
        a.ch = WiFi.channel(i);
        a.enc = (uint8_t)WiFi.encryptionType(i);
        uint8_t *b = WiFi.BSSID(i);
        for (int k = 0; k < 6; k++) a.bssid[k] = b ? b[k] : 0;
        aps.push_back(a);
    }
    WiFi.scanDelete();
    std::sort(aps.begin(), aps.end(), [](const WMap &x, const WMap &y) { return x.rssi > y.rssi; });
}

// View 0: channel usage histogram, channels 1..13.
static void wm_drawChannels(std::vector<WMap> &aps) {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    tft.fillRect(6, 26, tftWidth - 12, tftHeight - 40, bg);
    tft.setTextSize(FP);
    tft.setTextColor(ac, bg);
    tft.drawString("AP: " + String((int)aps.size()) + "   2.4G channels", 10, 30, 1);

    int cnt[14] = {0};
    for (auto &a : aps)
        if (a.ch >= 1 && a.ch <= 13) cnt[a.ch]++;
    int mx = 1, busiest = 1;
    for (int c = 1; c <= 13; c++)
        if (cnt[c] > mx) {
            mx = cnt[c];
            busiest = c;
        }

    int x0 = 12, top = 48, base = tftHeight - 26;
    int bw = (tftWidth - 24) / 13;
    int h = base - top;
    for (int c = 1; c <= 13; c++) {
        int bh = (mx > 0) ? (cnt[c] * h / mx) : 0;
        int x = x0 + (c - 1) * bw;
        uint16_t col = (c == busiest && mx > 0) ? TFT_RED : ac;
        if (bh > 0) tft.fillRect(x + 1, base - bh, bw - 2, bh, col);
        tft.setTextColor(dimc, bg);
        tft.drawString(String(c), x + 1, base + 2, 1);
    }
    tft.setTextColor(dimc, bg);
    tft.drawString("Busiest: ch" + String(busiest) + "  SEL:list ESC", 10, tftHeight - 12, 1);
}

// View 1: detailed AP list.
static void wm_drawList(std::vector<WMap> &aps, int scroll) {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    tft.fillRect(6, 26, tftWidth - 12, tftHeight - 40, bg);
    tft.setTextSize(FP);
    int y = 30;
    const int rowH = 20;
    int vis = (tftHeight - y - 16) / rowH;
    for (int i = scroll; i < (int)aps.size() && i < scroll + vis; i++) {
        WMap &a = aps[i];
        String ss = a.ssid.length() ? a.ssid : String("(hidden)");
        if (ss.length() > 16) ss = ss.substring(0, 16);
        uint16_t ec = (a.enc == WIFI_AUTH_OPEN) ? TFT_RED : ac;
        tft.setTextColor(ec, bg);
        tft.drawString(ss, 10, y, 1);
        char meta[40];
        snprintf(
            meta, sizeof(meta), "ch%d %ddB %s %s", (int)a.ch, (int)a.rssi, wm_encStr(a.enc),
            ouiLabel(a.bssid).c_str()
        );
        tft.setTextColor(dimc, bg);
        tft.drawString(meta, 14, y + 9, 1);
        y += rowH;
    }
    tft.setTextColor(dimc, bg);
    char foot[40];
    snprintf(foot, sizeof(foot), "%d AP  UP/DN  SEL:channels ESC", (int)aps.size());
    tft.drawString(foot, 10, tftHeight - 12, 1);
}

void wifi_map_setup() {
    returnToMenu = false;
    std::vector<WMap> aps;

    drawMainBorderWithTitle("WIFI MAP");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawString("Scan...", 12, 34, 1);
    wm_scan(aps);

    int view = 0, scroll = 0;
    uint32_t lastScan = millis();
    bool dirty = true;

    for (;;) {
        if (check(EscPress) || returnToMenu) break;

        if (check(SelPress)) {
            view ^= 1;
            scroll = 0;
            dirty = true;
        }
        if (view == 1) {
            const int rowH = 20;
            int vis = (tftHeight - 30 - 16) / rowH;
            int dir = 0;
            if (check(DownPress)) dir = 1;
            if (check(UpPress)) dir = -1;
#ifdef HAS_ENCODER
            int32_t rs = drainRotarySteps();
            if (rs < 0) dir = 1;
            else if (rs > 0) dir = -1;
#endif
            if (dir > 0 && scroll < (int)aps.size() - vis) {
                scroll++;
                dirty = true;
            } else if (dir < 0 && scroll > 0) {
                scroll--;
                dirty = true;
            }
        }

        // Auto refresh every 20 s.
        if (millis() - lastScan > 20000) {
            drawMainBorderWithTitle("WIFI MAP");
            tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
            tft.drawString("Rescan...", 12, 34, 1);
            wm_scan(aps);
            lastScan = millis();
            if (scroll < 0) scroll = 0;
            dirty = true;
        }

        if (dirty) {
            dirty = false;
            if (view == 0) wm_drawChannels(aps);
            else wm_drawList(aps, scroll);
        }
        delay(30);
    }

    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
}
