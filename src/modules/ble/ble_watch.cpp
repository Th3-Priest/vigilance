// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - BLE Watch Mode / anti-tracker (#6). Repeated passive BLE scan: on
// each window, we spot tracking devices (Apple FindMy/AirTag, Tile, Samsung
// SmartTag, Chipolo) and count how many windows each one stays present for. A
// tracker seen persistently (>= threshold) fires a "possible tracking" alert:
// the anti-stalking signal (like AirGuard / Tracker Detect). Purely defensive
// and passive: we listen to public advertisements, we emit nothing. Modeled on
// the NimBLE API already used by ble_sniffer.
#if !defined(LITE_VERSION)
#include "ble_watch.h"

#include "core/display.h"
#include "core/event_log.h"
#include "core/vig_env.h"
#include "core/led_control.h"
#include "core/mykeyboard.h"
#include "modules/others/audio.h"
#include <NimBLEDevice.h>
#include <globals.h>
#include <map>

#define BW_SCAN_MS 4000   // scan window duration (ms)
#define BW_PERSIST 3      // consecutive windows before the "possible tracking" alert

struct BwTrk {
    String type;          // "FindMy", "AirTag", "Tile", "SmartTag", "Chipolo"
    int rssi = 0;
    uint16_t windows = 0; // number of windows this tracker was seen in
    uint32_t firstS = 0, lastS = 0;
    bool alerted = false;
    bool seenThis = false;
};

// Returns the tracker type, or "" if the device isn't one.
static String bw_detect(const NimBLEAdvertisedDevice *dev) {
    std::string mfg = dev->getManufacturerData();
    if (mfg.size() >= 3) {
        uint16_t cid = (uint8_t)mfg[0] | ((uint16_t)(uint8_t)mfg[1] << 8);
        uint8_t t = (uint8_t)mfg[2];
        // Apple "offline finding" (FindMy network: AirTag + third-party accessories).
        if (cid == 0x004C && t == 0x12) return "FindMy";
    }
    String nm = String(dev->getName().c_str());
    nm.toLowerCase();
    if (nm.indexOf("airtag") >= 0) return "AirTag";
    if (nm.indexOf("smarttag") >= 0 || nm.indexOf("smart tag") >= 0) return "SmartTag";
    if (nm.indexOf("chipolo") >= 0) return "Chipolo";
    if (nm.indexOf("tile") >= 0) return "Tile";
    return "";
}

static void bw_alert() {
    setLedColor(CRGB::Red);
    _tone(3500, 150);
    setLedColor(CRGB::Black);
}

static void bw_draw(std::map<String, BwTrk> &trk, uint32_t win, bool scanning, const String &lastAlert) {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    tft.fillRect(6, 26, tftWidth - 12, tftHeight - 40, bg);
    tft.setTextSize(FP);
    int nAlert = 0;
    for (auto &kv : trk)
        if (kv.second.alerted) nAlert++;

    int y = 30;
    uint16_t sc = nAlert > 0 ? TFT_RED : ac;
    tft.fillRect(8, y, tftWidth - 16, 16, sc);
    tft.setTextColor(TFT_BLACK, sc);
    tft.drawCentreString(nAlert > 0 ? "! POSSIBLE TRACKING !" : "BLE WATCH", tftWidth / 2, y + 3, 1);
    y += 22;

    tft.setTextColor(ac, bg);
    tft.drawString("Trackers: " + String((int)trk.size()) + (scanning ? "  scan..." : "        "), 10, y, 1);
    y += 14;

    if (trk.empty()) {
        tft.setTextColor(dimc, bg);
        tft.drawString("No tracker detected.", 10, y, 1);
        y += 12;
        tft.drawString("AirTag/Tile/SmartTag/FindMy", 10, y, 1);
    } else {
        for (auto &kv : trk) {
            if (y > tftHeight - 26) break;
            BwTrk &e = kv.second;
            const String &addr = kv.first;
            String tail = addr.length() >= 8 ? addr.substring(addr.length() - 8) : addr;
            char line[42];
            snprintf(
                line, sizeof(line), "%-8s %s %ddB x%d", e.type.c_str(), tail.c_str(), e.rssi, (int)e.windows
            );
            tft.setTextColor(e.alerted ? TFT_RED : ac, bg);
            tft.drawString(line, 10, y, 1);
            y += 12;
        }
    }

    tft.setTextColor(dimc, bg);
    tft.drawString("win:" + String(win) + "  ESC quit", 10, tftHeight - 12, 1);
    if (lastAlert.length()) {
        tft.setTextColor(TFT_RED, bg);
        tft.drawString(lastAlert, 10, tftHeight - 24, 1);
    }
}

void ble_watch_setup() {
    returnToMenu = false;
    std::map<String, BwTrk> trk;

    ledEffects(false);
    drawMainBorderWithTitle("BLE WATCH");
    bw_draw(trk, 0, true, "");

    NimBLEDevice::init("Vigilance");
    vTaskDelay(10 / portTICK_PERIOD_MS);
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (!scan) {
        displayError("BLE init failed");
        NimBLEDevice::deinit(true);
        ledSetup();
        return;
    }
    scan->setActiveScan(true); // active: also fetches names (Tile/SmartTag/Chipolo)
    scan->setInterval(160);
    scan->setWindow(160);
    scan->setDuplicateFilter(false);

    uint32_t win = 0;
    String lastAlert = "";

    for (;;) {
        if (check(EscPress) || returnToMenu) break;

        // Scan the window in short slices so a back-press (EscPress) is caught
        // within ~300 ms instead of only after the full 4 s window.
        // NOTE: getResults(0, ...) means "scan forever" in NimBLE and blocks the
        // task indefinitely -- never pass 0. getResults() with no args returns the
        // last results without scanning; getResults(ms, true) scans one slice.
        NimBLEScanResults res = scan->getResults();
        bool quit = false;
        uint32_t winStart = millis();
        while (millis() - winStart < BW_SCAN_MS) {
            if (check(EscPress) || returnToMenu) {
                quit = true;
                break;
            }
            res = scan->getResults(300, true);
        }
        if (quit) break;
        win++;
        uint32_t nowS = millis() / 1000;

        for (auto &kv : trk) kv.second.seenThis = false;

        for (int i = 0; i < res.getCount(); i++) {
            const NimBLEAdvertisedDevice *dev = res.getDevice(i);
            if (!dev) continue;
            String type = bw_detect(dev);
            if (type.length() == 0) continue;
            String addr = String(dev->getAddress().toString().c_str());
            BwTrk &e = trk[addr];
            if (e.windows == 0) {
                e.type = type;
                e.firstS = nowS;
            }
            e.rssi = dev->getRSSI();
            e.lastS = nowS;
            e.seenThis = true;
            if (e.windows < 0xFFFF) e.windows++;
        }
        vigEnvSet(VIG_ENV_BLE, res.getCount()); // feed the home screen radar

        // Anti-stalking alert: tracker present for >= BW_PERSIST windows.
        for (auto &kv : trk) {
            BwTrk &e = kv.second;
            if (e.seenThis && !e.alerted && e.windows >= BW_PERSIST) {
                e.alerted = true;
                String tail = kv.first.length() >= 8 ? kv.first.substring(kv.first.length() - 8) : kv.first;
                lastAlert = "TAIL " + e.type + " " + tail;
                vigLogEvent("TRACKER", e.type + " " + kv.first);
                bw_alert();
            }
        }

        // Purge trackers gone for a while (>90 s) and not alerted.
        for (auto it = trk.begin(); it != trk.end();) {
            if (!it->second.alerted && (nowS - it->second.lastS) > 90) it = trk.erase(it);
            else ++it;
        }

        bw_draw(trk, win, false, lastAlert);
        if (check(EscPress)) break;
    }

    scan->stop();
    scan->clearResults();
    NimBLEDevice::deinit(true);
    setLedColor(CRGB::Black);
    ledSetup();
}

#endif
