// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - environment snapshot shared between the monitoring modules and the
// home screen radar. Plain globals: writers and reader all run on the main task.
#include "vig_env.h"

#include <Arduino.h>
#include <WiFi.h>

static int s_count[VIG_ENV_N] = {-1, -1, -1};
static uint32_t s_stampMs[VIG_ENV_N] = {0, 0, 0};

void vigEnvSet(uint8_t kind, int n) {
    if (kind >= VIG_ENV_N) return;
    s_count[kind] = n < 0 ? 0 : n;
    s_stampMs[kind] = millis();
}

int vigEnvGet(uint8_t kind) {
    if (kind >= VIG_ENV_N) return -1;
    return s_count[kind];
}

uint32_t vigEnvAgeS(uint8_t kind) {
    if (kind >= VIG_ENV_N || s_count[kind] < 0) return UINT32_MAX;
    return (millis() - s_stampMs[kind]) / 1000;
}

// ---------------- light async WiFi scan ----------------
static bool s_scanning = false;
static uint32_t s_nextScan = 0;

void vigScanTick() {
    // Never fight an active connection or a soft-AP (WebUI, evil portal...).
    wifi_mode_t m = WiFi.getMode();
    if ((m & WIFI_MODE_AP) || WiFi.status() == WL_CONNECTED) return;

    int st = WiFi.scanComplete();
    if (st >= 0) { // results ready
        vigEnvSet(VIG_ENV_WIFI, st);
        WiFi.scanDelete();
        s_scanning = false;
        s_nextScan = millis() + 5000; // refresh every ~5 s
    } else if (st == WIFI_SCAN_FAILED && !s_scanning && millis() >= s_nextScan) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false, true);
        WiFi.scanNetworks(true, true); // async, include hidden
        s_scanning = true;
    }
}

void vigScanStop() {
    if (WiFi.scanComplete() >= 0) WiFi.scanDelete();
    s_scanning = false;
    s_nextScan = 0;
    if (WiFi.status() != WL_CONNECTED && !(WiFi.getMode() & WIFI_MODE_AP)) WiFi.mode(WIFI_OFF);
}
