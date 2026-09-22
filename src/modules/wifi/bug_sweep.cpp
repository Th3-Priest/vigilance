// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - Bug Sweep (#5): flags likely WiFi cameras / recorders.
// Many consumer cameras broadcast a very recognizable setup SSID
// (Wyze, Tapo, EZVIZ, Reolink, "IPC-xxxx", "ESP32-CAM"...) or carry an OUI
// from a surveillance-gear vendor. We scan, classify, and show the suspects
// with a hint. Passive: no transmission.
#include "bug_sweep.h"

#include "core/display.h"
#include "core/event_log.h"
#include "core/mykeyboard.h"
#include "core/oui_vendor.h"
#include "core/vig_report.h"
#include <WiFi.h>
#include <globals.h>
#include <vector>

// Typical camera / NVR / DVR SSID patterns (compared in UPPERCASE).
static const char *CAM_SSID[] = {
    "IPCAM",    "IPC-",    "IP-CAM",   "IPCAMERA", "CAMERA",   "-CAM-",     "NETCAM",   "WEBCAM",
    "SPYCAM",   "MINICAM", "SMARTCAM", "SECURITYCAM", "DOORBELL", "NVR",    "DVR-",
    "HIKVI",    "DS-",     "DAHUA",    "REOLINK",  "WYZE",     "TAPO",      "EZVIZ",    "V380",
    "SRICAM",   "FOSCAM",  "XMEYE",    "YCC365",   "YCC",      "ESP32-CAM", "ESPCAM",   "GOOGLECAST",
    "ANRAN",    "LOROX",   "IMOU",     "ANNKE",    "SWANN",    "ARLO",      "BLINK",    "NEST-CAM",
    "VSTARCAM", "EYE4",    "JOOAN",    "ZOSI",     "AMCREST",  "LOREX",     "VIVOTEK",  "WISENET",
    "MOBOTIX",  "GEENI",   "MERKURY",  "WANSVIEW", "CLOUDEDGE","XIONGMAI",  "HISEEU",   "CTRONICS",
    "LITTLELF", "IEGEEK",  "TOPODOME", "GOKE",
};

// OUIs of surveillance-focused vendors (extra hint, catches hidden-SSID cams).
static const uint32_t CAM_OUI[] = {
    0x4419B6, 0x4CBD8F, 0x5803FB, 0xC056E3, 0xBCAD28, 0x2857BE, // Hikvision
    0x3CEF8C, 0x9002A9, 0x14A78B, 0x08EDED,                     // Dahua
    0xEC71DB, 0x7C4B4D,                                         // Reolink / misc
    0x00408C, 0xACCC8E, 0xB8A44F,                               // Axis
    0x2CAA8E,                                                   // Wyze
    0x00626E,                                                   // Foscam
};

static bool camByOui(const uint8_t *b) {
    uint32_t o = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    for (uint32_t x : CAM_OUI)
        if (x == o) return true;
    return false;
}

static bool looksLikeCam(const String &ssid, const uint8_t *bssid, String &why) {
    String s = ssid;
    s.toUpperCase();
    for (const char *p : CAM_SSID)
        if (s.indexOf(p) >= 0) {
            why = "SSID~" + String(p);
            return true;
        }
    if (camByOui(bssid)) {
        const char *v = ouiVendor(bssid);
        why = String("vendor ") + (v ? v : "video");
        return true;
    }
    return false;
}

void bug_sweep_setup() {
    returnToMenu = false;
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    bool reportSaved = false;
    String reportPath = "";

    for (;;) {
        drawMainBorderWithTitle("BUG SWEEP");
        tft.setTextColor(ac, bg);
        tft.drawString("Scanning for WiFi cameras...", 10, 40, 1);

        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(80);
        int n = WiFi.scanNetworks(false, true);

        struct Hit {
            String ssid, why;
            int rssi;
        };
        std::vector<Hit> hits;
        for (int i = 0; i < n; i++) {
            uint8_t *b = WiFi.BSSID(i);
            String ssid = WiFi.SSID(i);
            String why;
            if (b && looksLikeCam(ssid, b, why))
                hits.push_back({ssid.length() ? ssid : String("(hidden)"), why, (int)WiFi.RSSI(i)});
        }
        WiFi.scanDelete();

        tft.fillRect(6, 26, tftWidth - 12, tftHeight - 40, bg);
        tft.setTextColor(hits.empty() ? TFT_GREEN : TFT_RED, bg);
        char hdr[40];
        snprintf(hdr, sizeof(hdr), "%d likely cameras / %d AP", (int)hits.size(), n);
        tft.drawString(hdr, 10, 30, 1);

        int y = 46;
        if (hits.empty()) {
            tft.setTextColor(dimc, bg);
            tft.drawString("Nothing obvious here.", 10, y, 1);
            tft.drawString("(some cams hide well:", 10, y + 14, 1);
            tft.drawString(" check visually too)", 10, y + 28, 1);
        } else {
            for (auto &h : hits) {
                if (y > tftHeight - 30) break;
                tft.setTextColor(TFT_RED, bg);
                tft.drawString(h.ssid.substring(0, 20), 10, y, 1);
                tft.setTextColor(dimc, bg);
                tft.drawString(String(h.rssi) + "dB  " + h.why, 14, y + 9, 1);
                y += 20;
            }
            vigLogEvent("BUGSWEEP", String((int)hits.size()) + " cam?");
            if (!reportSaved) {
                String body = "Vigilance Bug Sweep\n";
                body += String((int)hits.size()) + " likely cameras / " + String(n) + " APs\n\n";
                for (auto &h : hits) body += h.ssid + "  " + String(h.rssi) + "dB  " + h.why + "\n";
                reportPath = vigSaveReport("bugsweep", body);
                reportSaved = true;
            }
        }
        tft.setTextColor(dimc, bg);
        if (!hits.empty()) tft.drawString("IP cams often expose RTSP :554", 10, tftHeight - 34, 1);
        if (reportPath.length()) {
            int sl = reportPath.lastIndexOf('/');
            tft.drawString("Report: " + reportPath.substring(sl + 1), 10, tftHeight - 23, 1);
        }
        tft.drawString("SEL: rescan   ESC: quit", 10, tftHeight - 12, 1);

        for (;;) {
            if (check(EscPress) || returnToMenu) {
                WiFi.mode(WIFI_OFF);
                return;
            }
            if (check(SelPress)) break;
            delay(20);
        }
    }
}
