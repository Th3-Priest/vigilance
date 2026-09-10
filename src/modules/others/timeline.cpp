// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance: on-device Timeline / Journal screen. Reads the global event log
// (event_log), fed by all the monitoring modules, and shows it on the device:
// most recent on top, relative age, scrolling, color coded by event type.
// Complements the web companion (/events endpoint).
#include "timeline.h"

#include "core/display.h"
#include "core/event_log.h"
#include "core/mykeyboard.h"
#include <Arduino.h>
#include <globals.h>

// Color by event type (alert = red, target = amber, rest = accent).
static uint16_t tl_color(const char *type) {
    if (strcmp(type, "DEAUTH") == 0 || strcmp(type, "ATTAQUE") == 0 || strcmp(type, "TRACKER") == 0 ||
        strcmp(type, "TAIL") == 0)
        return TFT_RED;
    if (strcmp(type, "TARGET") == 0 || strcmp(type, "SSIDDUP") == 0 || strcmp(type, "RECURRING") == 0 ||
        strcmp(type, "BUGSWEEP") == 0)
        return TFT_ORANGE;
    return bruceConfig.priColor;
}

// Compact relative age ("3s", "5m", "2h").
static String tl_ago(uint32_t nowS, uint32_t tsS) {
    uint32_t d = (nowS >= tsS) ? (nowS - tsS) : 0;
    if (d < 60) return String(d) + "s";
    if (d < 3600) return String(d / 60) + "m";
    return String(d / 3600) + "h";
}

void timeline_setup() {
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;

    VigEventView evs[32];
    int scroll = 0;
    uint32_t lastFetch = 0;
    int n = 0;
    bool dirty = true;

    drawMainBorderWithTitle("JOURNAL");

    const int rowH = 20;         // 2 lines per event
    const int top = 30;
    int visRows = (tftHeight - top - 16) / rowH;
    if (visRows < 1) visRows = 1;

    for (;;) {
        uint32_t now = millis();

        if (now - lastFetch > 1000) {
            lastFetch = now;
            n = vigEventsSnapshot(evs, 32);
            dirty = true;
        }

        if (check(EscPress)) break;
        if (check(UpPress)) {
            if (scroll > 0) scroll--;
            dirty = true;
        }
        if (check(DownPress)) {
            if (scroll < n - visRows) scroll++;
            dirty = true;
        }
#ifdef HAS_ENCODER
        int32_t rs = drainRotarySteps();
        if (rs > 0) {
            if (scroll > 0) scroll--;
            dirty = true;
        } else if (rs < 0) {
            if (scroll < n - visRows) scroll++;
            dirty = true;
        }
#endif
        if (scroll > n - visRows) scroll = (n > visRows) ? n - visRows : 0;
        if (scroll < 0) scroll = 0;

        if (dirty) {
            dirty = false;
            uint32_t nowS = now / 1000;
            tft.fillRect(6, top - 2, tftWidth - 12, tftHeight - top - 12, bg);
            tft.setTextSize(1);

            if (n == 0) {
                tft.setTextColor(dimc, bg);
                tft.drawCentreString("No events", tftWidth / 2, top + 12, 1);
                tft.drawCentreString("(Watch Mode, swarm...)", tftWidth / 2, top + 28, 1);
            } else {
                int y = top;
                for (int i = scroll; i < n && i < scroll + visRows; i++) {
                    uint16_t c = tl_color(evs[i].type);
                    char head[28];
                    snprintf(head, sizeof(head), "%-6s %4s", evs[i].type, tl_ago(nowS, evs[i].ts_s).c_str());
                    tft.setTextColor(c, bg);
                    tft.drawString(head, 10, y, 1);
                    tft.setTextColor(dimc, bg);
                    tft.drawString(String(evs[i].msg).substring(0, 30), 14, y + 9, 1);
                    y += rowH;
                }
            }
            // footer: count + cumulative total
            tft.setTextColor(dimc, bg);
            char foot[36];
            snprintf(foot, sizeof(foot), "%d/%lu  UP/DN  ESC", n, (unsigned long)vigEventsTotal());
            tft.drawString(foot, 10, tftHeight - 12, 1);
        }

        delay(30);
    }
}
