// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - Watch Mode. Signature "surveillance" feature. Built on the same
// promiscuous model as jam_detect.cpp, extended to spot devices as they appear
// and fire alerts (LED/sound/log).
#if !defined(LITE_VERSION)
#include "watch_mode.h"

#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "core/display.h"
#include "core/event_log.h"
#include "core/vig_env.h"
#include "core/vig_report.h"
#include "core/led_control.h"
#include "core/oui_vendor.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/wifi/wifi_common.h"
#include "modules/others/audio.h"
#include <Arduino.h>
#include <globals.h>
#include <map>
#include <set>
#include <vector>

static const uint8_t WM_CHANNELS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const int WM_NCH = sizeof(WM_CHANNELS) / sizeof(WM_CHANNELS[0]);
static const uint16_t WM_DWELL = 120; // ms per channel

// Deauth counter (current window) + queue of device appearances.
static volatile uint32_t wm_deauth = 0;

struct WEvt {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t kind; // 0 = AP (beacon), 1 = client (probe request)
    uint8_t ch;
    uint8_t ssidLen;  // extracted SSID length (0 if none)
    char ssid[33];    // SSID (beacon / probe), null-terminated
};
#define WM_RING 24
static volatile int wm_head = 0, wm_tail = 0;
static WEvt wm_ring[WM_RING];

static void IRAM_ATTR wm_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (!pkt || pkt->rx_ctrl.sig_len < 24) return;
    const uint8_t *f = pkt->payload;
    uint16_t fc = (uint16_t)f[0] | ((uint16_t)f[1] << 8);
    uint8_t ftype = (fc & 0x0C) >> 2;
    uint8_t fsub = (fc & 0xF0) >> 4;
    if (ftype != 0x00) return; // management only
    if (fsub == 0x0C || fsub == 0x0A) {
        wm_deauth++;
        return;
    }
    const uint8_t *mac;
    uint8_t kind;
    int ssidOff; // offset of the SSID element (tag 0) in the frame, -1 if none
    if (fsub == 0x08) {        // beacon -> BSSID (addr3), body after 24+12 bytes
        mac = f + 16;
        kind = 0;
        ssidOff = 36;
    } else if (fsub == 0x04) { // probe request -> client (addr2), body after 24 bytes
        mac = f + 10;
        kind = 1;
        ssidOff = 24;
    } else {
        return;
    }
    int nt = (wm_tail + 1) % WM_RING;
    if (nt == wm_head) return; // queue full: drop
    for (int i = 0; i < 6; i++) wm_ring[wm_tail].mac[i] = mac[i];
    wm_ring[wm_tail].rssi = pkt->rx_ctrl.rssi;
    wm_ring[wm_tail].kind = kind;
    wm_ring[wm_tail].ch = pkt->rx_ctrl.channel;

    // Extract the SSID (tag 0 element) if present and within frame bounds.
    uint8_t sl = 0;
    int total = pkt->rx_ctrl.sig_len;
    if (ssidOff + 2 <= total && f[ssidOff] == 0x00) {
        sl = f[ssidOff + 1];
        if (sl > 32) sl = 32;
        if (ssidOff + 2 + sl > total) sl = 0; // inconsistent length: ignore
        for (uint8_t i = 0; i < sl; i++) {
            char c = (char)f[ssidOff + 2 + i];
            wm_ring[wm_tail].ssid[i] = (c >= 32 && c < 127) ? c : '.';
        }
    }
    wm_ring[wm_tail].ssid[sl] = 0;
    wm_ring[wm_tail].ssidLen = sl;
    wm_tail = nt;
}

static void wm_start_wifi() {
    ensureWifiPlatform();
    nvs_flash_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_disconnect();
    esp_wifi_set_promiscuous(true);
    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(wm_rx_cb);
}

static void wm_stop_wifi() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    esp_wifi_stop();
    wifiDisconnect();
    vTaskDelay(1 / portTICK_RATE_MS);
}

static uint64_t wm_macKey(const uint8_t *m) {
    uint64_t k = 0;
    for (int i = 0; i < 6; i++) k = (k << 8) | m[i];
    return k;
}
static String wm_macStr(const uint8_t *m) {
    char b[18];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(b);
}

// Device tracking (#27): one entry per MAC seen, with first/last seen and
// frequency. Used for fixed/random counting (#29), recurring-device detection
// (presence #7) and the devices.csv export.
struct WmDev {
    uint8_t mac[6];
    uint32_t firstS; // first seen (s since boot)
    uint32_t lastS;  // last seen
    uint16_t count;  // number of appearances
    int8_t rssi;     // last RSSI
    uint8_t kind;    // 0 = AP, 1 = client
    bool isRandom;   // locally administered MAC
    bool recurLogged;
    // Tail Watch (#3): track co-movement across RF zones.
    uint8_t zonesSeen; // number of distinct zones the device was seen again in
    uint8_t lastZone;  // last zone (epoch) it was seen in
    bool tailLogged;
};
#define WM_MAXDEV 200 // table cap (RAM)
#define WM_RECUR 8    // appearances before flagging a recurring device

// Write a per-device summary to /VigilanceVeille/devices.csv (overwritten).
static void wm_exportDevices(std::vector<WmDev> &devs) {
    FS *fs;
    if (!getFsStorage(fs)) return;
    if (!(*fs).exists("/VigilanceVeille")) (*fs).mkdir("/VigilanceVeille");
    File f = (*fs).open("/VigilanceVeille/devices.csv", FILE_WRITE);
    if (!f) return;
    f.println("first_s,last_s,count,kind,rand,vendor,cat,mac");
    for (auto &d : devs) {
        String macs = wm_macStr(d.mac);
        f.println(
            String(d.firstS) + "," + String(d.lastS) + "," + String(d.count) + "," +
            (d.kind == 0 ? "AP" : "STA") + "," + (d.isRandom ? "1" : "0") + "," + ouiLabel(d.mac) + "," +
            ouiCategory(d.mac) + "," + macs
        );
    }
    f.close();
}

static std::set<String> wm_loadWatchlist() {
    std::set<String> wl;
    FS *fs;
    if (!getFsStorage(fs)) return wl;
    // First run: create a commented template so the alert rules (by MAC or by
    // SSID) are discoverable without external documentation.
    if (!(*fs).exists("/VigilanceVeille/watch.txt")) {
        if (!(*fs).exists("/VigilanceVeille")) (*fs).mkdir("/VigilanceVeille");
        File t = (*fs).open("/VigilanceVeille/watch.txt", FILE_WRITE);
        if (t) {
            t.println("# Vigilance - watchlist (Watch Mode)");
            t.println("# One entry per line. '#' = comment.");
            t.println("# An entry can be a MAC or an SSID (case-insensitive).");
            t.println("# If the entry appears, TARGET alert (cyan LED + beep + log).");
            t.println("#");
            t.println("# Examples:");
            t.println("# AA:BB:CC:DD:EE:FF");
            t.println("# MyWiFiNetwork");
            t.close();
        }
        return wl;
    }
    File f = (*fs).open("/VigilanceVeille/watch.txt", FILE_READ);
    while (f && f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;
        line.toLowerCase();
        wl.insert(line);
    }
    if (f) f.close();
    return wl;
}

static File wm_openLog() {
    FS *fs;
    File none;
    if (!getFsStorage(fs)) return none;
    if (!(*fs).exists("/VigilanceVeille")) (*fs).mkdir("/VigilanceVeille");
    File f = (*fs).open("/VigilanceVeille/veille_log.csv", FILE_APPEND);
    return f;
}

static void wm_alert(uint32_t &lastAlertMs) {
    uint32_t now = millis();
    if (now - lastAlertMs < 1500) return; // anti-spam throttle
    lastAlertMs = now;
    setLedColor(CRGB::Cyan);
    _tone(3000, 120);
    setLedColor(CRGB::Black);
}

static void wm_draw(
    uint32_t dev, uint32_t rate, uint32_t thr, uint32_t alerts, const String &last, const String &lastDev,
    uint32_t fixedN, uint32_t randN, uint8_t ch, bool attack
) {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    tft.fillRect(6, 26, tftWidth - 12, tftHeight - 40, bg);
    tft.setTextSize(FP);
    int y = 30;
    uint16_t sc = attack ? TFT_RED : (alerts > 0 ? ac : TFT_GREEN);
    tft.fillRect(8, y, tftWidth - 16, 16, sc);
    tft.setTextColor(TFT_BLACK, sc);
    tft.drawCentreString(attack ? "! DEAUTH ATTACK !" : "WATCH ACTIVE", tftWidth / 2, y + 3, 1);
    y += 22;
    tft.setTextColor(ac, bg);
    tft.drawString("Devices seen: " + String(dev), 10, y, 1);
    y += 14;
    tft.setTextColor(dimc, bg);
    tft.drawString("Fixed:" + String(fixedN) + "  Rand:" + String(randN), 10, y, 1);
    y += 14;
    tft.setTextColor(rate >= thr && rate > 0 ? TFT_RED : ac, bg);
    tft.drawString("Deauth/s: " + String(rate) + "  (thr " + String(thr) + ")", 10, y, 1);
    y += 14;
    tft.setTextColor(ac, bg);
    tft.drawString("Alerts: " + String(alerts), 10, y, 1);
    y += 14;
    if (last.length()) {
        tft.setTextColor(ac, bg);
        tft.drawString(">" + last, 10, y, 1);
        y += 14;
    }
    if (lastDev.length()) {
        tft.setTextColor(dimc, bg);
        tft.drawString(lastDev, 10, y, 1);
    }
    tft.setTextColor(dimc, bg);
    tft.drawString("ch" + String(ch) + " UP/DN thr SEL beep ESC", 10, tftHeight - 12, 1);
}

void watch_mode_setup() {
    returnToMenu = false;

    std::set<String> watchlist = wm_loadWatchlist();
    std::vector<WmDev> devs;
    std::map<uint64_t, int> devIdx; // MAC -> index into devs (fast lookup)
    std::map<String, std::set<uint64_t>> ssidBssids; // SSID -> distinct BSSIDs (evil-twin)
    std::set<String> ssidDupLogged;                  // SSIDs already flagged as duplicates
    devs.reserve(64);
    File logF = wm_openLog();

    uint32_t devicesSeen = 0, alertsCount = 0, deauthRate = 0, threshold = 10;
    uint32_t fixedN = 0, randN = 0; // #29: fixed vs random MACs
    uint32_t lastAlertMs = 0, lastDeauthCalc = millis(), lastDraw = 0;
    String lastAlert = "", lastDev = "";
    bool dirty = true, attack = false;
    int idx = 0;
    // Tail Watch (#3): "RF zone" signature (set of APs visible over 20 s).
    std::set<uint64_t> curZone, prevZone;
    uint8_t zoneEpoch = 0;
    uint32_t lastZoneCalc = millis();

    ledEffects(false); // keep the effect task from overwriting the alert LED

    wm_start_wifi();
    drawMainBorderWithTitle("WATCH MODE");
    wm_deauth = 0;
    wm_head = wm_tail = 0;

    for (;;) {
        if (returnToMenu) break;

        uint8_t ch = WM_CHANNELS[idx];
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        idx = (idx + 1) % WM_NCH;

        uint32_t t0 = millis();
        while (millis() - t0 < WM_DWELL) {
            if (check(EscPress)) {
                returnToMenu = true;
                break;
            }
            if (check(SelPress)) {
                setLedColor(CRGB::Cyan);
                _tone(2500, 100);
                setLedColor(CRGB::Black);
            }
            if (check(UpPress) && threshold < 250) {
                threshold += 5;
                dirty = true;
            }
            if (check(DownPress) && threshold > 5) {
                threshold -= 5;
                dirty = true;
            }
#ifdef HAS_ENCODER
            int32_t rs = drainRotarySteps();
            if (rs > 0 && threshold < 250) {
                threshold += 5;
                dirty = true;
            } else if (rs < 0 && threshold > 5) {
                threshold -= 5;
                dirty = true;
            }
#endif

            while (wm_head != wm_tail) {
                WEvt e = wm_ring[wm_head];
                wm_head = (wm_head + 1) % WM_RING;
                uint64_t key = wm_macKey(e.mac);
                uint32_t nowS = millis() / 1000;
                if (e.kind == 0) curZone.insert(key); // Tail Watch: AP of the current zone

                // Known device: update its tracking.
                auto it = devIdx.find(key);
                if (it != devIdx.end()) {
                    WmDev &d = devs[it->second];
                    d.lastS = nowS;
                    d.rssi = e.rssi;
                    if (d.count < 0xFFFF) d.count++;
                    // Presence (#7): non-target device seen often -> log (once).
                    if (!d.recurLogged && d.count >= WM_RECUR) {
                        d.recurLogged = true;
                        String v = ouiLabel(d.mac);
                        vigLogEvent("RECURRING", v + " " + wm_macStr(d.mac));
                        lastAlert = "RECURRING " + v;
                        dirty = true;
                    }
                    // Tail Watch (#3): fixed-MAC device seen again across zones -> it's following you.
                    if (!d.isRandom && d.lastZone != zoneEpoch) {
                        d.lastZone = zoneEpoch;
                        if (d.zonesSeen < 255) d.zonesSeen++;
                        if (!d.tailLogged && d.zonesSeen >= 3) {
                            d.tailLogged = true;
                            alertsCount++;
                            String v = ouiLabel(d.mac);
                            lastAlert = "TAIL? " + v + " " + wm_macStr(d.mac).substring(9);
                            wm_alert(lastAlertMs);
                            vigLogEvent("TAIL", v + " " + wm_macStr(d.mac));
                            dirty = true;
                        }
                    }
                    continue;
                }
                if (devs.size() >= WM_MAXDEV) continue; // table full: stop tracking new devices

                // New device.
                WmDev d = {};
                memcpy(d.mac, e.mac, 6);
                d.firstS = d.lastS = nowS;
                d.count = 1;
                d.rssi = e.rssi;
                d.kind = e.kind;
                d.isRandom = ouiIsRandom(e.mac);
                d.lastZone = zoneEpoch; // Tail Watch: zone of first sighting
                d.zonesSeen = 1;
                devIdx[key] = (int)devs.size();
                devs.push_back(d);
                if (d.isRandom) randN++;
                else fixedN++;
                devicesSeen++;
                dirty = true;

                String macs = wm_macStr(e.mac);
                String macl = macs;
                macl.toLowerCase();
                String ssid = String(e.ssid); // SSID (beacon/probe) or ""
                String ssidl = ssid;
                ssidl.toLowerCase();
                // Alert rule (#2): MAC OR SSID present in the watchlist.
                bool target = watchlist.count(macl) > 0 || (ssid.length() && watchlist.count(ssidl) > 0);
                String vendor = ouiLabel(e.mac); // vendor ("Apple"), "random" or "?"
                String cat = ouiCategory(e.mac); // coarse category (#28)
                const char *tag = target ? "TARGET" : (e.kind == 0 ? "AP" : "CLIENT");
                lastDev = String(e.kind == 0 ? "AP " : "STA ") + vendor;
                if (cat != "?") lastDev += "/" + cat;
                lastDev += " " + macs.substring(9);
                if (ssid.length()) lastDev += " \"" + ssid.substring(0, 12) + "\"";

                // Evil-twin / duplicate SSID (#3): same SSID on multiple BSSIDs.
                // Heuristic (mesh/roaming = false positives), so log without beep.
                if (e.kind == 0 && ssid.length()) {
                    std::set<uint64_t> &bs = ssidBssids[ssid];
                    bs.insert(key);
                    if (bs.size() >= 2 && ssidDupLogged.count(ssid) == 0) {
                        ssidDupLogged.insert(ssid);
                        vigLogEvent("SSIDDUP", ssid + " x" + String((int)bs.size()));
                        lastAlert = "SSID DUP " + ssid;
                    }
                }
                if (logF)
                    logF.println(
                        String(nowS) + "," + tag + "," + macs + "," + String(e.rssi) + ",ch" + String(e.ch) +
                        "," + vendor + "," + ssid
                    );
                if (target) {
                    alertsCount++;
                    String what = ssid.length() ? ssid : (vendor + " " + macs);
                    lastAlert = "TARGET " + what;
                    wm_alert(lastAlertMs);
                    vigLogEvent("TARGET", what);
                }
            }
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
        if (returnToMenu) break;

        uint32_t now = millis();
        if (now - lastDeauthCalc >= 1000) {
            uint32_t dt = now - lastDeauthCalc;
            deauthRate = (uint32_t)wm_deauth * 1000UL / dt;
            wm_deauth = 0;
            lastDeauthCalc = now;
            dirty = true;
            attack = (deauthRate >= threshold && deauthRate > 0);
            if (attack) {
                alertsCount++;
                lastAlert = "DEAUTH " + String(deauthRate) + "/s ch" + String(ch);
                wm_alert(lastAlertMs);
                vigLogEvent("DEAUTH", lastAlert);
                if (logF)
                    logF.println(String(millis() / 1000) + ",DEAUTH," + String(deauthRate) + ",,ch" + String(ch));
            }
        }

        // Tail Watch (#3): every 20 s, compare the set of visible APs to the previous one.
        // If the RF environment changed markedly (low similarity), we moved into a new
        // "zone" -> a device seen again across zones is following you.
        if (now - lastZoneCalc >= 20000) {
            lastZoneCalc = now;
            if (!prevZone.empty() && !curZone.empty()) {
                int inter = 0;
                for (auto k : curZone)
                    if (prevZone.count(k)) inter++;
                int uni = (int)curZone.size() + (int)prevZone.size() - inter;
                if (uni > 0 && (float)inter / (float)uni < 0.4f) zoneEpoch++;
            }
            vigEnvSet(VIG_ENV_WIFI, (int)curZone.size()); // feed the home screen radar
            prevZone = curZone;
            curZone.clear();
        }

        if (dirty && now - lastDraw >= 300) {
            wm_draw(
                devicesSeen, deauthRate, threshold, alertsCount, lastAlert, lastDev, fixedN, randN, ch, attack
            );
            dirty = false;
            lastDraw = now;
        }
    }

    // Session report to SD, a shareable summary of the run.
    {
        String body = "Vigilance Watch Mode\n";
        body += "Devices seen: " + String(devicesSeen) + "\n";
        body += "Fixed: " + String(fixedN) + "  Random: " + String(randN) + "\n";
        body += "Alerts: " + String(alertsCount) + "\n";
        body += "Last deauth/s: " + String(deauthRate) + "  (threshold " + String(threshold) + ")\n";
        if (lastAlert.length()) body += "Last alert: " + lastAlert + "\n";
        vigSaveReport("watch", body);
    }

    wm_stop_wifi();
    if (logF) logF.close();
    wm_exportDevices(devs); // per-device summary -> /VigilanceVeille/devices.csv
    setLedColor(CRGB::Black);
    ledSetup(); // restore normal LED behavior
}

#endif
