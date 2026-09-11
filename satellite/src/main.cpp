// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance-Satellite - firmware for a cheap ESP32 (swarm satellite). Headless.
// Joins the T-Embed swarm over ESP-NOW, announces itself, sends a heartbeat, and
// runs the master's offloaded tasks (Swarm V2):
//   - SCAN    : WiFi scan -> one SW_REPORT per network
//   - WATCH   : passive monitoring (deauth) -> SW_ALERT (#21)
//   - MEASURE : RSSI of a target BSSID -> SW_REPORT (#20 triangulation)
//   - SNIFF   : count EAPOL of a target AP -> SW_REPORT (#19 WIDS capture)
//   - OTA     : receive and flash a firmware pushed by the master (#23)
// Role (#24): steers the default watch. Purely passive/defensive (the satellite
// LISTENS to public frames; it injects nothing).
//
// Default target: ESP32-C3. Arduino-ESP32 core 3.x (ESP-IDF 5.x).
#include <Arduino.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "swarm_protocol.h"

#ifndef LED_BUILTIN
#define LED_BUILTIN 8 // C3 supermini: LED on GPIO8 (active low)
#endif
#define LED_ON LOW
#define LED_OFF HIGH

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static bool enrolled = false;
static uint8_t masterMac[6];
static uint8_t mySatId = 0;
static uint16_t seq = 0;
static uint8_t myRole = SW_ROLE_ANY;
static uint8_t curTask = SW_TASK_IDLE;
static uint8_t targetBssid[6] = {0};

// Counters fed by the promiscuous callback (WiFi task context).
static volatile uint32_t deauthCount = 0;
static volatile uint32_t eapolCount = 0;
static bool promiscOn = false;

// OTA
static bool otaActive = false;
static uint32_t otaTotal = 0, otaWritten = 0;
static uint16_t otaNextSeq = 0;

// incoming message pending (handled in loop, not in the callback)
static volatile bool pending = false;
static SwarmMsg pendMsg;
static uint8_t pendMac[6];

static bool macEq(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6) == 0; }

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != (int)sizeof(SwarmMsg)) return;
    SwarmMsg m;
    memcpy(&m, data, sizeof(m));
    if (m.magic != VIG_SWARM_MAGIC || m.swarmId != VIG_SWARM_ID) return;
    memcpy(&pendMsg, &m, sizeof(m));
    memcpy(pendMac, info->src_addr, 6);
    pending = true;
}

static bool addPeer(const uint8_t *mac) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;
    p.encrypt = false;
    return esp_now_add_peer(&p) == ESP_OK;
}

static void fillHeader(SwarmMsg &m, uint8_t type) {
    memset(&m, 0, sizeof(m));
    m.magic = VIG_SWARM_MAGIC;
    m.ver = VIG_SWARM_VER;
    m.type = type;
    m.swarmId = VIG_SWARM_ID;
    m.satId = mySatId;
    m.battery = 100; // no gauge on a bare ESP32: nominal value
    m.task = curTask;
    m.role = myRole;
    m.channel = VIG_SWARM_CHANNEL;
    m.seq = seq++;
    strncpy(m.name, "SAT-C3", sizeof(m.name) - 1);
}

static void sendTo(const uint8_t *mac, uint8_t type) {
    SwarmMsg m;
    fillHeader(m, type);
    esp_now_send(mac, (uint8_t *)&m, sizeof(m));
}

// Return to the control channel (required for the master to hear).
static void backToControlChannel() {
    esp_wifi_set_channel(VIG_SWARM_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

// ---- Promiscuous callback: count deauth (WATCH) and EAPOL (SNIFF) ----
static void IRAM_ATTR promiscCb(void *buf, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (!pkt) return;
    int sl = pkt->rx_ctrl.sig_len;
    if (sl < 24) return;
    const uint8_t *f = pkt->payload;
    uint16_t fc = (uint16_t)f[0] | ((uint16_t)f[1] << 8);
    uint8_t ftype = (fc & 0x0C) >> 2;
    uint8_t fsub = (fc & 0xF0) >> 4;

    if (curTask == SW_TASK_VEILLE && ftype == 0x00) { // management
        if (fsub == 0x0C || fsub == 0x0A) deauthCount++; // deauth / disassoc
        return;
    }
    if (curTask == SW_TASK_SNIFF && ftype == 0x02) { // data
        if (f[1] & 0x40) return;             // protected frame (encrypted): no cleartext EAPOL
        int hdr = 24;
        if ((f[1] & 0x03) == 0x03) hdr += 6; // ToDS+FromDS -> addr4 present
        if (fsub & 0x08) hdr += 2;           // QoS data -> QoS control (2 B)
        // target BSSID in addr1/2/3?
        bool match = macEq(f + 4, targetBssid) || macEq(f + 10, targetBssid) || macEq(f + 16, targetBssid);
        if (match && sl >= hdr + 8) {
            const uint8_t *llc = f + hdr;
            // LLC/SNAP + ethertype EAPOL (0x888E)
            if (llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03 && llc[6] == 0x88 && llc[7] == 0x8E)
                eapolCount++;
        }
    }
}

static void promiscStart() {
    if (promiscOn) return;
    esp_wifi_set_promiscuous(true);
    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(promiscCb);
    promiscOn = true;
}

static void promiscStop() {
    if (!promiscOn) return;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    promiscOn = false;
}

// ---- SCAN (already in V2): one SW_REPORT per network ----
static void doScanAndReport() {
    promiscStop();
    int n = WiFi.scanNetworks(false, true);
    backToControlChannel();
    for (int i = 0; i < n && i < 30; i++) {
        SwarmMsg m;
        fillHeader(m, SW_REPORT);
        m.task = SW_TASK_SCAN;
        m.channel = (uint8_t)WiFi.channel(i);
        uint8_t *b = WiFi.BSSID(i);
        if (b)
            for (int k = 0; k < 6; k++) m.payload[k] = b[k];
        m.payload[6] = (uint8_t)(int8_t)WiFi.RSSI(i);
        m.payload[7] = (uint8_t)WiFi.channel(i);
        String ssid = WiFi.SSID(i);
        for (int k = 0; k < 16; k++) m.payload[8 + k] = (k < (int)ssid.length()) ? (uint8_t)ssid[k] : 0;
        esp_now_send(masterMac, (uint8_t *)&m, sizeof(m));
        delay(15);
    }
    WiFi.scanDelete();
    curTask = SW_TASK_IDLE;
}

// ---- MEASURE (#20): RSSI of a target BSSID ----
static void doMeasureAndReport() {
    promiscStop();
    int n = WiFi.scanNetworks(false, true);
    backToControlChannel();
    int8_t rssi = 0;
    uint8_t found = 0;
    for (int i = 0; i < n; i++) {
        uint8_t *b = WiFi.BSSID(i);
        if (b && macEq(b, targetBssid)) {
            rssi = (int8_t)WiFi.RSSI(i);
            found = 1;
            break;
        }
    }
    WiFi.scanDelete();
    SwarmMsg m;
    fillHeader(m, SW_REPORT);
    m.task = SW_TASK_MEASURE;
    memcpy(m.payload, targetBssid, 6);
    m.payload[6] = (uint8_t)rssi;
    m.payload[7] = found;
    delay((uint32_t)mySatId * 8); // stagger responses to limit ch1 collisions
    esp_now_send(masterMac, (uint8_t *)&m, sizeof(m));
    curTask = SW_TASK_IDLE;
}

// ---- SNIFF (#19): count EAPOL of a target AP for `durS` s ----
static void doSniffAndReport(uint8_t ch, uint8_t durS) {
    if (durS == 0) durS = 8;
    if (durS > 30) durS = 30;
    eapolCount = 0;
    curTask = SW_TASK_SNIFF; // the callback now counts target EAPOL
    promiscStart();
    esp_wifi_set_channel(ch ? ch : 1, WIFI_SECOND_CHAN_NONE);
    uint32_t t0 = millis();
    while (millis() - t0 < (uint32_t)durS * 1000) {
        delay(50);
        // heartbeat impossible here (other channel): stay focused on listening
    }
    promiscStop();
    backToControlChannel();
    uint32_t cnt = eapolCount;
    SwarmMsg m;
    fillHeader(m, SW_REPORT);
    m.task = SW_TASK_SNIFF;
    memcpy(m.payload, targetBssid, 6);
    m.payload[6] = (uint8_t)(cnt > 255 ? 255 : cnt);
    m.payload[7] = (cnt >= 4) ? 1 : 0; // heuristic: >=4 EAPOL ~ full handshake
    esp_now_send(masterMac, (uint8_t *)&m, sizeof(m));
    curTask = SW_TASK_IDLE;
}

// ---- WATCH (#21): monitor deauth on the control channel ----
static void veilleTick() {
    static uint32_t last = 0, lastAlert = 0;
    if (curTask != SW_TASK_VEILLE) return;
    uint32_t now = millis();
    if (now - last < 1000) return;
    last = now;
    uint32_t rate = deauthCount;
    deauthCount = 0;
    if (rate >= 5 && now - lastAlert > 30000) { // flood threshold + anti-spam cooldown
        lastAlert = now;
        SwarmMsg m;
        fillHeader(m, SW_ALERT);
        m.payload[0] = SW_AL_DEAUTH;
        m.payload[1] = 0;
        // short text starting at payload[8]
        char txt[24];
        snprintf(txt, sizeof(txt), "deauth %lu/s", (unsigned long)rate);
        for (int i = 0; i < 20 && txt[i]; i++) m.payload[8 + i] = (uint8_t)txt[i];
        esp_now_send(masterMac, (uint8_t *)&m, sizeof(m));
    }
}

// ---- OTA (#23): receive + flash via Update ----
static void otaAck(uint8_t status, uint16_t nextSeq) {
    SwarmMsg m;
    fillHeader(m, SW_OTA_ACK);
    m.seq = nextSeq;
    m.payload[0] = status;
    esp_now_send(masterMac, (uint8_t *)&m, sizeof(m));
}

static void otaBegin(uint32_t total) {
    promiscStop();
    backToControlChannel(); // OTA runs on the control channel
    curTask = SW_TASK_IDLE;
    // Abort any previous OTA session left open (interrupted transfer), otherwise
    // Update.begin() fails and the satellite refuses any OTA until reboot.
    if (otaActive || Update.isRunning()) {
        Update.abort();
        otaActive = false;
    }
    otaTotal = total;
    otaWritten = 0;
    otaNextSeq = 0;
    otaActive = Update.begin(total);
    otaAck(otaActive ? 0 : 1, 0);
}

static void otaData(uint16_t s, const uint8_t *data) {
    if (!otaActive) {
        otaAck(1, otaNextSeq);
        return;
    }
    if (s == otaNextSeq) {
        uint32_t remain = otaTotal - otaWritten;
        uint32_t len = remain < SW_OTA_CHUNK ? remain : SW_OTA_CHUNK;
        if (Update.write((uint8_t *)data, len) == len) {
            otaWritten += len;
            otaNextSeq++;
        } else {
            Update.abort();
            otaActive = false;
            otaAck(1, otaNextSeq);
            return;
        }
    }
    // ACK the next expected block (dedup + recovery on loss)
    otaAck(0, otaNextSeq);
}

static void otaEnd() {
    if (!otaActive) {
        otaAck(1, otaNextSeq);
        return;
    }
    bool ok = Update.end(true); // true = set size to total written
    otaActive = false;
    otaAck(ok ? 2 : 1, otaNextSeq);
    if (ok) {
        delay(300);
        ESP.restart();
    }
}

static void handleMsg() {
    SwarmMsg &m = pendMsg;
    switch (m.type) {
        case SW_DISCOVER:
            if (!enrolled) {
                addPeer(pendMac);
                sendTo(pendMac, SW_HELLO);
            }
            break;
        case SW_ENROLL:
            memcpy(masterMac, pendMac, 6);
            mySatId = m.satId;
            enrolled = true;
            addPeer(masterMac);
            break;
        case SW_ASSIGN:
            if (!enrolled) break;
            if (m.role != SW_ROLE_ANY) myRole = m.role;
            memcpy(targetBssid, m.payload, 6);
            if (m.task == SW_TASK_SCAN) {
                curTask = SW_TASK_SCAN;
                doScanAndReport();
            } else if (m.task == SW_TASK_MEASURE) {
                curTask = SW_TASK_MEASURE;
                doMeasureAndReport();
            } else if (m.task == SW_TASK_SNIFF) {
                doSniffAndReport(m.payload[6], m.payload[7]);
            } else if (m.task == SW_TASK_VEILLE) {
                curTask = SW_TASK_VEILLE;
                deauthCount = 0;
                backToControlChannel();
                promiscStart();
            } else {
                curTask = SW_TASK_IDLE;
                promiscStop();
            }
            break;
        case SW_STOP:
            curTask = SW_TASK_IDLE;
            promiscStop();
            backToControlChannel();
            if (otaActive) { // abort an in-progress OTA at the master's request
                Update.abort();
                otaActive = false;
            }
            break;
        case SW_OTA_BEGIN: {
            uint32_t total = (uint32_t)m.payload[0] | ((uint32_t)m.payload[1] << 8) |
                             ((uint32_t)m.payload[2] << 16) | ((uint32_t)m.payload[3] << 24);
            otaBegin(total);
            break;
        }
        case SW_OTA_DATA: otaData(m.seq, m.payload); break;
        case SW_OTA_END: otaEnd(); break;
        default: break;
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LED_OFF);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    backToControlChannel();

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    addPeer(BCAST);
    esp_now_register_recv_cb(onRecv);
    Serial.println("Vigilance-Satellite V2 ready, waiting for a master...");
}

void loop() {
    static uint32_t lastHb = 0, lastBlink = 0;
    static bool led = false;
    uint32_t now = millis();

    if (pending) {
        pending = false;
        handleMsg();
    }

    veilleTick();

    // Heartbeat (not during OTA to avoid disturbing the flow).
    if (enrolled && !otaActive && now - lastHb > 2000) {
        lastHb = now;
        sendTo(masterMac, SW_HEARTBEAT);
    }

    // LED: solid once enrolled (fast blink during OTA), otherwise slow blink.
    uint32_t blinkT = otaActive ? 100 : (enrolled ? 0 : 400);
    if (blinkT == 0) {
        digitalWrite(LED_BUILTIN, LED_ON);
    } else if (now - lastBlink > blinkT) {
        lastBlink = now;
        led = !led;
        digitalWrite(LED_BUILTIN, led ? LED_ON : LED_OFF);
    }

    delay(5);
}
