// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance Swarm - MASTER side (T-Embed). Discovers and coordinates cheap ESP32
// satellites over ESP-NOW. V2 = offloaded tasks:
//   - Scan (#18)        : aggregated remote WiFi scan
//   - Watch (#21)       : satellites report alerts (deauth...) -> journal
//   - Roles (#24)       : assign a role (WiFi/BLE) to satellites
//   - Triangulate (#20) : rank satellites by RSSI toward a target BSSID
//   - Sniff (#19)       : count EAPOL from a target AP (handshake detection, WIDS)
//   - OTA (#23)         : push satellite firmware from the master's SD
// LoRa satellites (#22) are a stub (relay needs the physical module).
#include "swarm.h"

#include "core/display.h"
#include "core/event_log.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include <FS.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <globals.h>
#include <string.h>
#include <vector>

static const uint8_t SW_BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct SatEntry {
    uint8_t mac[6];
    uint8_t id;
    uint8_t battery;
    uint8_t task;
    uint8_t role;
    int8_t rssi; // last RSSI received (#14/#15 mesh map / radar)
    uint32_t lastSeen;
    char name[16];
};

struct SwNet {
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t ch;
    char ssid[17];
    uint8_t satId;
};

// Input queue filled by the ESP-NOW callback (WiFi task context), drained in the
// main loop - no heavy work in the callback.
#define SW_Q 64 // deep enough to absorb a burst of SW_REPORT (16 sats x ~30 AP)
static volatile int qHead = 0, qTail = 0;
static SwarmMsg qMsg[SW_Q];
static uint8_t qMac[SW_Q][6];
static int8_t qRssi[SW_Q];
static std::vector<String> g_chat; // Mesh Whisper (#22): chat lines received/sent

// Shared state (filled by swarmPump, read by the actions and the display).
static std::vector<SatEntry> g_sats;
static std::vector<SwNet> g_nets;
struct MeasRes {
    int8_t rssi;
    uint8_t found;
    bool fresh;
};
static MeasRes g_meas[VIG_SWARM_MAX_SATS + 1];
struct SniffRes {
    uint8_t eapol;
    uint8_t hs;
    bool fresh;
};
static SniffRes g_sniff[VIG_SWARM_MAX_SATS + 1];
static volatile bool g_otaFresh = false;
static uint16_t g_otaSeq = 0;
static uint8_t g_otaStatus = 0;
static uint8_t g_otaFrom = 0;
static String g_lastAlert = "";

static void swarmOnRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != (int)sizeof(SwarmMsg)) return;
    SwarmMsg m;
    memcpy(&m, data, sizeof(m));
    if (m.magic != VIG_SWARM_MAGIC || m.swarmId != VIG_SWARM_ID) return;
    if (m.type != SW_HELLO && m.type != SW_HEARTBEAT && m.type != SW_REPORT && m.type != SW_ALERT &&
        m.type != SW_OTA_ACK && m.type != SW_CHAT)
        return;
    int nt = (qTail + 1) % SW_Q;
    if (nt == qHead) return; // queue full: drop it
    memcpy(&qMsg[qTail], &m, sizeof(m));
    memcpy(qMac[qTail], info->src_addr, 6);
    qRssi[qTail] = (info->rx_ctrl) ? (int8_t)info->rx_ctrl->rssi : 0;
    qTail = nt;
}

static bool swarmAddPeer(const uint8_t *mac) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;
    p.encrypt = false;
    return esp_now_add_peer(&p) == ESP_OK;
}

static void swHeader(SwarmMsg &m, uint8_t type) {
    memset(&m, 0, sizeof(m));
    m.magic = VIG_SWARM_MAGIC;
    m.ver = VIG_SWARM_VER;
    m.type = type;
    m.swarmId = VIG_SWARM_ID;
    m.channel = VIG_SWARM_CHANNEL;
    strncpy(m.name, "VIGILANCE", sizeof(m.name) - 1);
}

static void swSend(const uint8_t *mac, uint8_t type, uint8_t task = 0, uint8_t role = 0) {
    SwarmMsg m;
    swHeader(m, type);
    m.task = task;
    m.role = role;
    esp_now_send(mac, (uint8_t *)&m, sizeof(m));
}

static void swEnroll(const uint8_t *mac, uint8_t id) {
    SwarmMsg m;
    swHeader(m, SW_ENROLL);
    m.satId = id;
    esp_now_send(mac, (uint8_t *)&m, sizeof(m));
}

static void swAssign(uint8_t task, uint8_t role, const uint8_t *bssid, uint8_t ch, uint8_t dur) {
    SwarmMsg m;
    swHeader(m, SW_ASSIGN);
    m.task = task;
    m.role = role;
    if (bssid) memcpy(m.payload, bssid, 6);
    m.payload[6] = ch;
    m.payload[7] = dur;
    esp_now_send(SW_BCAST, (uint8_t *)&m, sizeof(m));
}

// ---- Readable names ----
static const char *roleName(uint8_t r) {
    switch (r) {
        case SW_ROLE_WIFI: return "WiFi";
        case SW_ROLE_BLE: return "BLE";
        case SW_ROLE_SUBGHZ: return "SubG";
        case SW_ROLE_LORA: return "LoRa";
        default: return "any";
    }
}
static const char *taskName(uint8_t t) {
    switch (t) {
        case SW_TASK_SCAN: return "scan";
        case SW_TASK_VEILLE: return "watch";
        case SW_TASK_MEASURE: return "meas";
        case SW_TASK_SNIFF: return "sniff";
        default: return "idle";
    }
}

static int findSat(const uint8_t *mac) {
    for (size_t i = 0; i < g_sats.size(); i++)
        if (memcmp(g_sats[i].mac, mac, 6) == 0) return (int)i;
    return -1;
}

// Assign the smallest free id in [1..MAX] (recycled when a satellite leaves), so
// satId always stays a valid index into g_meas/g_sniff. 0 = none free.
static uint8_t swAllocId() {
    for (uint8_t id = 1; id <= VIG_SWARM_MAX_SATS; id++) {
        bool used = false;
        for (auto &s : g_sats)
            if (s.id == id) {
                used = true;
                break;
            }
        if (!used) return id;
    }
    return 0;
}

// Drain the queue: enroll/update satellites, aggregate networks/measures/sniff/alerts.
static void swarmPump() {
    while (qHead != qTail) {
        SwarmMsg &m = qMsg[qHead];
        uint8_t *mac = qMac[qHead];
        uint32_t now = millis();

        // Refresh presence + RSSI on ANY frame from a known satellite.
        {
            int li = findSat(mac);
            if (li >= 0) {
                g_sats[li].lastSeen = now;
                g_sats[li].rssi = qRssi[qHead];
            }
        }

        if (m.type == SW_CHAT) {
            char txt[201];
            memcpy(txt, m.payload, 200);
            txt[200] = 0;
            String line = String(m.name).length() ? String(m.name) : String("?");
            g_chat.push_back(line + "> " + String(txt));
            if (g_chat.size() > 40) g_chat.erase(g_chat.begin());
            qHead = (qHead + 1) % SW_Q;
            continue;
        }

        if (m.type == SW_REPORT && m.task == SW_TASK_SCAN) {
            int ni = -1;
            for (size_t j = 0; j < g_nets.size(); j++)
                if (memcmp(g_nets[j].bssid, m.payload, 6) == 0) {
                    ni = (int)j;
                    break;
                }
            SwNet net = {};
            memcpy(net.bssid, m.payload, 6);
            net.rssi = (int8_t)m.payload[6];
            net.ch = m.payload[7];
            for (int k = 0; k < 16; k++) net.ssid[k] = (char)m.payload[8 + k];
            net.ssid[16] = 0;
            net.satId = m.satId;
            if (ni < 0) {
                if (g_nets.size() < 64) g_nets.push_back(net);
            } else g_nets[ni] = net;
        } else if (m.type == SW_REPORT && m.task == SW_TASK_MEASURE) {
            uint8_t id = m.satId;
            if (id >= 1 && id <= VIG_SWARM_MAX_SATS) {
                g_meas[id].rssi = (int8_t)m.payload[6];
                g_meas[id].found = m.payload[7];
                g_meas[id].fresh = true;
            }
        } else if (m.type == SW_REPORT && m.task == SW_TASK_SNIFF) {
            uint8_t id = m.satId;
            if (id >= 1 && id <= VIG_SWARM_MAX_SATS) {
                g_sniff[id].eapol = m.payload[6];
                g_sniff[id].hs = m.payload[7];
                g_sniff[id].fresh = true;
            }
        } else if (m.type == SW_ALERT) {
            char txt[24];
            for (int k = 0; k < 20; k++) txt[k] = (char)m.payload[8 + k];
            txt[20] = 0;
            String body = "sat#" + String((int)m.satId) + " " + String(txt);
            const char *et =
                (m.payload[0] == SW_AL_TRACKER) ? "TRACKER" : (m.payload[0] == SW_AL_DEAUTH ? "DEAUTH" : "SWARM");
            vigLogEvent(et, body);
            g_lastAlert = body;
        } else if (m.type == SW_OTA_ACK) {
            g_otaFresh = true;
            g_otaSeq = m.seq;
            g_otaStatus = m.payload[0];
            g_otaFrom = m.satId;
        } else { // HELLO / HEARTBEAT
            int idx = findSat(mac);
            if (idx < 0 && m.type == SW_HELLO && g_sats.size() < VIG_SWARM_MAX_SATS) {
                uint8_t id = swAllocId();
                if (id != 0) {
                    SatEntry s = {};
                    memcpy(s.mac, mac, 6);
                    s.id = id;
                    s.battery = m.battery;
                    s.task = m.task;
                    s.role = m.role;
                    s.rssi = qRssi[qHead];
                    s.lastSeen = now;
                    strncpy(s.name, m.name, sizeof(s.name) - 1);
                    g_sats.push_back(s);
                    swarmAddPeer(mac);
                    swEnroll(mac, s.id);
                }
            } else if (idx >= 0) {
                g_sats[idx].battery = m.battery;
                g_sats[idx].task = m.task;
                g_sats[idx].role = m.role;
                g_sats[idx].lastSeen = now;
                // Satellite rebooted (HELLO while already known): re-assign its
                // existing id, otherwise it stays stuck un-enrolled.
                if (m.type == SW_HELLO) swEnroll(mac, g_sats[idx].id);
            }
        }
        qHead = (qHead + 1) % SW_Q;
    }
}

// ---- Small in-house selection menu (avoids loopOptions) ----
static int swChoose(const char *title, const String *items, int n) {
    if (n <= 0) return -1;
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    int sel = 0, top = 0;
    bool redraw = true;
    drawMainBorderWithTitle(title);
    for (;;) {
        swarmPump(); // keep the swarm alive during selection
        if (redraw) {
            tft.fillRect(6, 26, tftWidth - 12, tftHeight - 40, bg);
            tft.setTextSize(1);
            int rows = (tftHeight - 44) / 14;
            if (sel < top) top = sel;
            if (sel >= top + rows) top = sel - rows + 1;
            int y = 30;
            for (int i = top; i < n && i < top + rows; i++, y += 14) {
                bool s = (i == sel);
                if (s) {
                    tft.fillRect(8, y - 1, tftWidth - 16, 13, ac);
                    tft.setTextColor(bg, ac);
                } else tft.setTextColor(ac, bg);
                tft.drawString(items[i].substring(0, 34), 12, y, 1);
            }
            tft.setTextColor(dimc, bg);
            tft.drawString("SEL ok   ESC cancel", 10, tftHeight - 12, 1);
            redraw = false;
        }
#ifdef HAS_ENCODER
        int32_t steps = drainRotarySteps();
        if (steps > 0) {
            sel = (sel - 1 + n) % n;
            redraw = true;
        } else if (steps < 0) {
            sel = (sel + 1) % n;
            redraw = true;
        }
#endif
        if (check(UpPress)) {
            sel = (sel - 1 + n) % n;
            redraw = true;
        }
        if (check(DownPress)) {
            sel = (sel + 1) % n;
            redraw = true;
        }
        if (check(EscPress)) return -1;
        if (check(SelPress)) return sel;
        delay(20);
    }
}

static void swInfo(const String &l1, const String &l2, uint32_t ms) {
    const uint16_t ac = bruceConfig.priColor, bg = bruceConfig.bgColor;
    tft.fillRect(6, 40, tftWidth - 12, tftHeight - 56, bg);
    tft.setTextColor(ac, bg);
    tft.drawString(l1, 12, 60, 1);
    if (l2.length()) tft.drawString(l2, 12, 76, 1);
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        swarmPump();
        if (check(EscPress)) break;
        delay(20);
    }
}

// Build the list of aggregated networks for a target choice.
static int swPickNet() {
    if (g_nets.empty()) {
        swInfo("No aggregated network.", "Run Scan networks first.", 2000);
        return -1;
    }
    std::vector<String> items;
    for (auto &n : g_nets) {
        String ss = n.ssid[0] ? String(n.ssid) : String("(hidden)");
        items.push_back(ss + "  ch" + String((int)n.ch) + " " + String((int)n.rssi) + "dB");
    }
    return swChoose("TARGET (network)", items.data(), (int)items.size());
}

// ---- Actions ----
static void actScan() {
    swAssign(SW_TASK_SCAN, SW_ROLE_ANY, nullptr, 0, 0);
    swInfo("Scan requested from swarm.", "Networks will aggregate.", 1500);
}

static void actVeille() {
    swAssign(SW_TASK_VEILLE, SW_ROLE_ANY, nullptr, VIG_SWARM_CHANNEL, 0);
    swInfo("Watch armed on swarm.", "Alerts -> Journal.", 1500);
}

static void actStop() {
    swSend(SW_BCAST, SW_STOP);
    swInfo("Stop sent to swarm.", "Satellites idle.", 1200);
}

static void actRoles() {
    String opts[3] = {"All -> WiFi", "All -> BLE", "All -> Any"};
    int c = swChoose("ROLES", opts, 3);
    if (c < 0) return;
    uint8_t role = (c == 0) ? SW_ROLE_WIFI : (c == 1) ? SW_ROLE_BLE : SW_ROLE_ANY;
    swAssign(SW_TASK_IDLE, role, nullptr, 0, 0);
    swInfo(String("Role assigned: ") + roleName(role), "", 1200);
}

static void actTriangulate() {
    int ni = swPickNet();
    if (ni < 0) return;
    uint8_t bssid[6];
    memcpy(bssid, g_nets[ni].bssid, 6);
    for (int i = 0; i <= VIG_SWARM_MAX_SATS; i++) g_meas[i].fresh = false;
    swAssign(SW_TASK_MEASURE, SW_ROLE_ANY, bssid, 0, 0);

    // Collect ~6 s.
    uint32_t t0 = millis();
    while (millis() - t0 < 6000) {
        swarmPump();
        if (check(EscPress)) break;
        delay(30);
    }
    // Show the ranking by RSSI (stronger = closer).
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    drawMainBorderWithTitle("TRIANGULATION");
    tft.setTextColor(ac, bg);
    tft.drawString(g_nets[ni].ssid[0] ? g_nets[ni].ssid : "(hidden)", 12, 30, 1);
    int y = 46;
    int best = -1;
    int8_t bestR = -128;
    for (auto &s : g_sats) {
        if (s.id < 1 || s.id > VIG_SWARM_MAX_SATS) continue;
        MeasRes &r = g_meas[s.id];
        char line[40];
        if (r.fresh && r.found) {
            snprintf(line, sizeof(line), "sat#%d  %ddB", (int)s.id, (int)r.rssi);
            if (r.rssi > bestR) {
                bestR = r.rssi;
                best = s.id;
            }
        } else snprintf(line, sizeof(line), "sat#%d  --", (int)s.id);
        tft.setTextColor(ac, bg);
        tft.drawString(line, 12, y, 1);
        y += 14;
    }
    tft.setTextColor(dimc, bg);
    if (best >= 0) tft.drawString("Closest: sat#" + String(best), 12, y + 4, 1);
    else tft.drawString("Target not heard.", 12, y + 4, 1);
    tft.drawString("SEL/ESC to return", 10, tftHeight - 12, 1);
    while (!check(SelPress) && !check(EscPress)) {
        swarmPump();
        delay(20);
    }
}

static void actSniff() {
    int ni = swPickNet();
    if (ni < 0) return;
    uint8_t bssid[6];
    memcpy(bssid, g_nets[ni].bssid, 6);
    uint8_t ch = g_nets[ni].ch;
    uint8_t dur = 8;
    for (int i = 0; i <= VIG_SWARM_MAX_SATS; i++) g_sniff[i].fresh = false;
    swAssign(SW_TASK_SNIFF, SW_ROLE_ANY, bssid, ch, dur);

    uint32_t t0 = millis();
    while (millis() - t0 < (uint32_t)(dur + 3) * 1000) {
        swarmPump();
        if (check(EscPress)) break;
        delay(30);
    }
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    drawMainBorderWithTitle("SNIFF EAPOL");
    tft.setTextColor(ac, bg);
    tft.drawString(String(g_nets[ni].ssid[0] ? g_nets[ni].ssid : "(hidden)") + " ch" + String((int)ch), 12, 30, 1);
    int y = 46;
    for (auto &s : g_sats) {
        if (s.id < 1 || s.id > VIG_SWARM_MAX_SATS) continue;
        SniffRes &r = g_sniff[s.id];
        char line[40];
        if (r.fresh) {
            snprintf(line, sizeof(line), "sat#%d  %d EAPOL %s", (int)s.id, (int)r.eapol, r.hs ? "[HS]" : "");
            if (r.hs) vigLogEvent("SWARM", "handshake sat#" + String((int)s.id));
        } else snprintf(line, sizeof(line), "sat#%d  --", (int)s.id);
        tft.setTextColor(ac, bg);
        tft.drawString(line, 12, y, 1);
        y += 14;
    }
    tft.setTextColor(dimc, bg);
    tft.drawString("SEL/ESC to return", 10, tftHeight - 12, 1);
    while (!check(SelPress) && !check(EscPress)) {
        swarmPump();
        delay(20);
    }
}

// Wait for the OTA ACK of block `wantSeq` (timeout ms). Returns: 1 ok, 0 timeout, -1 err.
static int swOtaWait(uint16_t wantSeq, uint32_t ms) {
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        swarmPump();
        if (g_otaFresh) {
            g_otaFresh = false;
            if (g_otaStatus == 1) return -1;
            if (g_otaSeq == wantSeq) return 1;
        }
        delay(2);
    }
    return 0;
}

static int swListBins(FS *fs, String *out, int maxN) {
    int c = 0;
    if (!fs->exists("/VigilanceSat")) return 0;
    File dir = fs->open("/VigilanceSat");
    if (!dir) return 0;
    File e = dir.openNextFile();
    while (e && c < maxN) {
        String n = String(e.name());
        int slash = n.lastIndexOf('/');
        if (slash >= 0) n = n.substring(slash + 1);
        if (n.endsWith(".bin")) out[c++] = n;
        e = dir.openNextFile();
    }
    dir.close();
    return c;
}

static void actOTA() {
    if (g_sats.empty()) {
        swInfo("No satellite enrolled.", "", 1500);
        return;
    }
    // 1) Pick the target satellite
    std::vector<String> satItems;
    for (auto &s : g_sats)
        satItems.push_back("sat#" + String((int)s.id) + " " + String(s.name) + " " + String((int)s.battery) + "%");
    int si = swChoose("OTA: satellite", satItems.data(), (int)satItems.size());
    if (si < 0) return;
    SatEntry target = g_sats[si];

    // 2) Pick the firmware (/VigilanceSat/*.bin on the SD)
    FS *fs;
    if (!getFsStorage(fs)) {
        swInfo("SD unavailable.", "", 1500);
        return;
    }
    String bins[16];
    int nb = swListBins(fs, bins, 16);
    if (nb == 0) {
        swInfo("No .bin found.", "Drop it in /VigilanceSat/", 2500);
        return;
    }
    int bi = swChoose("OTA: firmware", bins, nb);
    if (bi < 0) return;
    String path = "/VigilanceSat/" + bins[bi];
    File f = (*fs).open(path, FILE_READ);
    if (!f) {
        swInfo("Open failed.", path, 2000);
        return;
    }
    uint32_t total = (uint32_t)f.size();
    if (total < 1024 || total > (uint32_t)SW_OTA_CHUNK * 60000UL) {
        swInfo("Invalid size.", String(total) + " B", 2000);
        f.close();
        return;
    }

    // 3) BEGIN (payload: size LE)
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    drawMainBorderWithTitle("OTA SWARM");
    tft.setTextColor(ac, bg);
    tft.drawString("sat#" + String((int)target.id) + "  " + bins[bi], 12, 30, 1);
    tft.setTextColor(dimc, bg);
    tft.drawString(String(total) + " bytes", 12, 44, 1);

    g_otaFresh = false;
    {
        SwarmMsg m;
        swHeader(m, SW_OTA_BEGIN);
        m.payload[0] = total & 0xFF;
        m.payload[1] = (total >> 8) & 0xFF;
        m.payload[2] = (total >> 16) & 0xFF;
        m.payload[3] = (total >> 24) & 0xFF;
        esp_now_send(target.mac, (uint8_t *)&m, sizeof(m));
    }
    if (swOtaWait(0, 1500) != 1) {
        swInfo("Satellite not ready (BEGIN).", "Aborting.", 2000);
        f.close();
        return;
    }

    // 4) DATA in stop-and-wait with retry
    uint16_t seq = 0;
    uint32_t sent = 0;
    bool ok = true;
    uint8_t buf[SW_OTA_CHUNK];
    while (sent < total) {
        uint32_t len = total - sent;
        if (len > SW_OTA_CHUNK) len = SW_OTA_CHUNK;
        f.read(buf, len);
        int tries = 0, res = 0;
        do {
            SwarmMsg m;
            swHeader(m, SW_OTA_DATA);
            m.seq = seq;
            memcpy(m.payload, buf, len);
            esp_now_send(target.mac, (uint8_t *)&m, sizeof(m));
            res = swOtaWait((uint16_t)(seq + 1), 400);
            tries++;
        } while (res == 0 && tries < 25);
        if (res != 1) {
            ok = false;
            break;
        }
        sent += len;
        seq++;
        if ((seq % 8) == 0 || sent == total) {
            int pct = (int)((uint64_t)sent * 100 / total);
            tft.fillRect(12, 62, tftWidth - 24, 10, bg);
            tft.setTextColor(ac, bg);
            tft.drawString("Transfer: " + String(pct) + "%", 12, 62, 1);
            tft.fillRect(12, 78, (tftWidth - 24) * pct / 100, 6, ac);
        }
        if (check(EscPress)) {
            ok = false;
            break;
        }
    }
    f.close();

    if (!ok) {
        swInfo("OTA interrupted.", "Satellite unchanged (safe).", 2500);
        return;
    }
    // 5) END with retry (like DATA) -> the satellite validates and reboots.
    int endRes = 0, etries = 0;
    do {
        SwarmMsg m;
        swHeader(m, SW_OTA_END);
        m.seq = seq;
        g_otaFresh = false;
        esp_now_send(target.mac, (uint8_t *)&m, sizeof(m));
        endRes = swOtaWait(seq, 600);
        etries++;
    } while (!(endRes == 1 && g_otaStatus == 2) && etries < 8);
    if (endRes == 1 && g_otaStatus == 2) swInfo("OTA OK. Satellite rebooting.", "", 2500);
    else swInfo("OTA end uncertain.", "Check the satellite.", 2500);
    vigLogEvent("SWARM", "OTA sat#" + String((int)target.id) + " " + bins[bi]);
}

static void swarmDrawList(uint32_t now) {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    tft.fillRect(6, 26, tftWidth - 12, tftHeight - 40, bg);
    tft.setTextSize(1);
    tft.setTextColor(ac, bg);
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "Sat:%d  Nets:%d", (int)g_sats.size(), (int)g_nets.size());
    tft.drawString(hdr, 10, 30, 1);

    int y = 46;
    if (g_sats.empty()) {
        tft.setTextColor(dimc, bg);
        tft.drawString("Waiting for satellites...", 10, y, 1);
        y += 14;
    } else {
        for (auto &s : g_sats) {
            if (y > tftHeight - 40) break;
            uint32_t age = (now - s.lastSeen) / 1000;
            char line[42];
            snprintf(
                line, sizeof(line), "#%d %s %s %d%% %lus", (int)s.id, roleName(s.role), taskName(s.task),
                (int)s.battery, (unsigned long)age
            );
            tft.setTextColor(ac, bg);
            tft.drawString(line, 10, y, 1);
            y += 13;
        }
    }
    if (g_lastAlert.length()) {
        tft.setTextColor(TFT_RED, bg);
        tft.drawString(("! " + g_lastAlert).substring(0, 34), 10, tftHeight - 26, 1);
    }
    tft.setTextColor(dimc, bg);
    tft.drawString("SEL: actions   ESC: quit", 10, tftHeight - 12, 1);
}

// #14 Living Mesh Map: master at the center, satellites in a circle, links colored
// by RSSI with a pulse running along each link.
static void actMeshMap() {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    drawMainBorderWithTitle("MESH MAP");
    uint32_t t0 = millis();
    for (;;) {
        if (check(EscPress) || check(SelPress)) break;
        swarmPump();
        tft.fillRect(6, 26, tftWidth - 12, tftHeight - 40, bg);
        int cx = tftWidth / 2, cy = tftHeight / 2 + 4;
        int n = (int)g_sats.size();
        float phase = (millis() - t0) / 600.0f;
        for (int i = 0; i < n; i++) {
            float ang = (2 * PI * i) / (n ? n : 1);
            int x = cx + (int)(cos(ang) * 62);
            int y = cy + (int)(sin(ang) * 44);
            int rssi = g_sats[i].rssi;
            uint16_t lc = rssi > -55 ? TFT_GREEN : (rssi > -75 ? ac : dimc);
            int q = rssi > -55 ? 3 : (rssi > -75 ? 2 : 1);
            for (int w = 0; w < q; w++) tft.drawLine(cx, cy + w, x, y + w, lc);
            float pp = fmod(phase + i * 0.2f, 1.0f);
            tft.fillCircle(cx + (int)((x - cx) * pp), cy + (int)((y - cy) * pp), 2, TFT_WHITE);
            tft.fillCircle(x, y, 7, g_sats[i].task != SW_TASK_IDLE ? TFT_ORANGE : ac);
            tft.setTextColor(bg, g_sats[i].task != SW_TASK_IDLE ? TFT_ORANGE : ac);
            tft.drawCentreString(String((int)g_sats[i].id), x, y - 3, 1);
            tft.setTextColor(dimc, bg);
            tft.drawString(String(rssi) + "dB", x - 12, y + 9, 1);
        }
        tft.fillCircle(cx, cy, 8, ac);
        tft.setTextColor(bg, ac);
        tft.drawCentreString("M", cx, cy - 3, 1);
        tft.setTextColor(dimc, bg);
        tft.drawString("Sat:" + String(n) + "  SEL/ESC", 10, tftHeight - 12, 1);
        delay(110);
    }
}

// #15 Swarm Radar: radar sweep, satellites as blips (proximity = RSSI).
static void actRadar() {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    drawMainBorderWithTitle("SWARM RADAR");
    int cx = tftWidth / 2, cy = tftHeight / 2 + 4;
    int Rmax = (tftHeight - 44) / 2;
    float sweep = 0;
    for (;;) {
        if (check(EscPress) || check(SelPress)) break;
        swarmPump();
        tft.fillRect(6, 26, tftWidth - 12, tftHeight - 40, bg);
        for (int r = Rmax / 3; r <= Rmax; r += Rmax / 3) tft.drawCircle(cx, cy, r, dimc);
        tft.drawLine(cx - Rmax, cy, cx + Rmax, cy, dimc);
        tft.drawLine(cx, cy - Rmax, cx, cy + Rmax, dimc);
        sweep += 0.18f;
        if (sweep > 2 * PI) sweep -= 2 * PI;
        tft.drawLine(cx, cy, cx + (int)(cos(sweep) * Rmax), cy + (int)(sin(sweep) * Rmax), TFT_GREEN);
        for (int i = 0; i < (int)g_sats.size(); i++) {
            float ang = (2 * PI * g_sats[i].id) / 16.0f;
            int prox = map(g_sats[i].rssi, -90, -40, Rmax, 12);
            if (prox < 12) prox = 12;
            if (prox > Rmax) prox = Rmax;
            int x = cx + (int)(cos(ang) * prox), y = cy + (int)(sin(ang) * prox);
            tft.fillCircle(x, y, 4, g_sats[i].task != SW_TASK_IDLE ? TFT_ORANGE : ac);
            tft.setTextColor(dimc, bg);
            tft.drawString(String((int)g_sats[i].id), x + 5, y - 3, 1);
        }
        tft.setTextColor(dimc, bg);
        tft.drawString("close=strong  SEL/ESC", 10, tftHeight - 12, 1);
        delay(55);
    }
}

// #22 Mesh Whisper: text chat broadcast over ESP-NOW between Vigilance devices.
static void actChat() {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    for (;;) {
        drawMainBorderWithTitle("MESH CHAT");
        size_t lastSize = g_chat.size();
        auto drawLog = [&]() {
            tft.fillRect(6, 26, tftWidth - 12, tftHeight - 40, bg);
            int rows = (tftHeight - 44) / 12;
            int start = (int)g_chat.size() - rows;
            if (start < 0) start = 0;
            int y = 30;
            for (int i = start; i < (int)g_chat.size(); i++, y += 12) {
                tft.setTextColor(g_chat[i].startsWith("me>") ? ac : TFT_GREEN, bg);
                tft.drawString(g_chat[i].substring(0, 40), 10, y, 1);
            }
            tft.setTextColor(dimc, bg);
            tft.drawString("SEL: write   ESC: quit", 10, tftHeight - 12, 1);
        };
        drawLog();
        bool compose = false;
        for (;;) {
            swarmPump();
            if (g_chat.size() != lastSize) {
                lastSize = g_chat.size();
                drawLog();
            }
            if (check(EscPress)) return;
            if (check(SelPress)) {
                compose = true;
                break;
            }
            delay(20);
        }
        if (compose) {
            String msg = keyboard("", 180, "Message:");
            if (msg.length() && msg != "\x1B") {
                SwarmMsg m;
                swHeader(m, SW_CHAT);
                int L = msg.length();
                if (L > 199) L = 199;
                memcpy(m.payload, msg.c_str(), L);
                m.payload[L] = 0;
                esp_now_send(SW_BCAST, (uint8_t *)&m, sizeof(m));
                g_chat.push_back("me> " + msg);
                if (g_chat.size() > 40) g_chat.erase(g_chat.begin());
            }
        }
    }
}

static void swarmActionMenu() {
    String opts[11] = {"Scan networks", "Watch (alerts)", "Roles",  "Triangulate", "Sniff EAPOL", "OTA satellite",
                       "Mesh map",     "Radar",          "Chat",   "Stop",        "Back"};
    int c = swChoose("SWARM ACTIONS", opts, 11);
    switch (c) {
        case 0: actScan(); break;
        case 1: actVeille(); break;
        case 2: actRoles(); break;
        case 3: actTriangulate(); break;
        case 4: actSniff(); break;
        case 5: actOTA(); break;
        case 6: actMeshMap(); break;
        case 7: actRadar(); break;
        case 8: actChat(); break;
        case 9: actStop(); break;
        default: break;
    }
}

void swarm_setup() {
    g_sats.clear();
    g_nets.clear();
    g_lastAlert = "";
    qHead = qTail = 0;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(VIG_SWARM_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        tft.fillScreen(bruceConfig.bgColor);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.drawCentreString("ESP-NOW init failed", tftWidth / 2, tftHeight / 2 - 4, 1);
        delay(1500);
        WiFi.mode(WIFI_OFF);
        return;
    }
    swarmAddPeer(SW_BCAST);
    esp_now_register_recv_cb(swarmOnRecv);

    drawMainBorderWithTitle("ESP-NOW SWARM");

    uint32_t lastBeacon = 0, lastDraw = 0;
    for (;;) {
        uint32_t now = millis();
        swarmPump();

        if (now - lastBeacon > 1500) {
            lastBeacon = now;
            swSend(SW_BCAST, SW_DISCOVER);
        }
        for (size_t i = 0; i < g_sats.size();) {
            if (now - g_sats[i].lastSeen > 12000) g_sats.erase(g_sats.begin() + i);
            else i++;
        }
        if (now - lastDraw > 1000) {
            lastDraw = now;
            swarmDrawList(now);
        }
        if (check(SelPress)) {
            swarmActionMenu();
            drawMainBorderWithTitle("ESP-NOW SWARM");
            lastDraw = 0;
        }
        if (check(EscPress)) break;
        delay(20);
    }

    esp_now_unregister_recv_cb();
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
}
