// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance: global event log. Thread-safe ring buffer (spinlock) fed by the
// modules and exposed to the web companion via /events. Each event is also
// appended to a persistent log on SD (durable and exportable).
#include "event_log.h"

#include "core/sd_functions.h"
#include "freertos/FreeRTOS.h"

struct VigEvent {
    uint32_t ts_s;
    char type[12];
    char msg[48];
};

#define EV_N 32
static VigEvent evBuf[EV_N];
static uint32_t evCount = 0;
static portMUX_TYPE evMux = portMUX_INITIALIZER_UNLOCKED;

// Append the line to the persistent log /Vigilance/journal.csv (best-effort).
// Called OUTSIDE the critical section (no SD I/O under a spinlock).
static void vigPersist(uint32_t ts, const char *type, const char *msg) {
    FS *fs;
    if (!getFsStorage(fs)) return;
    if (!(*fs).exists("/Vigilance")) (*fs).mkdir("/Vigilance");
    File f = (*fs).open("/Vigilance/journal.csv", FILE_APPEND);
    if (!f) return;
    f.println(String(ts) + "," + type + "," + msg);
    f.close();
}

void vigLogEvent(const char *type, const String &msg) {
    uint32_t ts = millis() / 1000;
    const char *m = msg.c_str();
    portENTER_CRITICAL(&evMux);
    int i = evCount % EV_N;
    evBuf[i].ts_s = ts;
    strncpy(evBuf[i].type, type, sizeof(evBuf[i].type) - 1);
    evBuf[i].type[sizeof(evBuf[i].type) - 1] = 0;
    strncpy(evBuf[i].msg, m, sizeof(evBuf[i].msg) - 1);
    evBuf[i].msg[sizeof(evBuf[i].msg) - 1] = 0;
    evCount++;
    portEXIT_CRITICAL(&evMux);
    // Persist to SD outside the lock.
    vigPersist(ts, type, m);
}

static String jsonEsc(const char *s) {
    String o;
    for (const char *p = s; *p; p++) {
        if (*p == '"' || *p == '\\') o += '\\';
        if ((uint8_t)*p >= 32) o += *p;
    }
    return o;
}

String vigEventsJson() {
    VigEvent snap[EV_N];
    int n, total;
    portENTER_CRITICAL(&evMux);
    total = (int)evCount;
    n = total < EV_N ? total : EV_N;
    int start = total - n;
    for (int k = 0; k < n; k++) snap[k] = evBuf[(start + k) % EV_N];
    portEXIT_CRITICAL(&evMux);

    String out = "[";
    bool first = true;
    for (int k = n - 1; k >= 0; k--) { // most recent first
        if (!first) out += ",";
        first = false;
        out += "{\"t\":" + String(snap[k].ts_s) + ",\"type\":\"" + jsonEsc(snap[k].type) +
               "\",\"msg\":\"" + jsonEsc(snap[k].msg) + "\"}";
    }
    out += "]";
    return out;
}

int vigEventsSnapshot(VigEventView *out, int maxN) {
    if (!out || maxN <= 0) return 0;
    portENTER_CRITICAL(&evMux);
    int total = (int)evCount;
    int n = total < EV_N ? total : EV_N;
    if (n > maxN) n = maxN;
    int start = total - n;
    // most recent first
    for (int k = 0; k < n; k++) {
        int srcIdx = (start + (n - 1 - k)) % EV_N;
        out[k].ts_s = evBuf[srcIdx].ts_s;
        memcpy(out[k].type, evBuf[srcIdx].type, sizeof(out[k].type));
        memcpy(out[k].msg, evBuf[srcIdx].msg, sizeof(out[k].msg));
    }
    portEXIT_CRITICAL(&evMux);
    return n;
}

uint32_t vigEventsTotal() {
    portENTER_CRITICAL(&evMux);
    uint32_t c = evCount;
    portEXIT_CRITICAL(&evMux);
    return c;
}
