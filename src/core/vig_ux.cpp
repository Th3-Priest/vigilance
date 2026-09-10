// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - Point L: themes (#50), mission profiles (#48), customizable
// dashboard (#49). Themes/profiles ride on Bruce's config API
// (setUiColor / setSoundEnabled / setBright / saveFile) so they persist. Dashboard
// preferences live in a small Vigilance-only file
// (/Vigilance/dash.cfg) to avoid touching bruce.conf serialization.
#include "vig_ux.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include <globals.h>
#include <vector>

#ifdef HAS_RGB_LED
#include "core/led_control.h"
#endif

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// ---------------- #50 Themes ----------------
struct VigTheme {
    const char *name;
    uint16_t pri, sec, bg;
};
static const VigTheme THEMES[] = {
    {"Cyan Watch",    RGB565(0x2F, 0xB7, 0xFF), RGB565(0x1C, 0x7B, 0xA8), RGB565(0x06, 0x0A, 0x12)},
    {"Tactical Amber",RGB565(0xFF, 0xB0, 0x20), RGB565(0x8A, 0x5A, 0x10), RGB565(0x0A, 0x08, 0x00)},
    {"Red Alert",     RGB565(0xFF, 0x3B, 0x3B), RGB565(0x8A, 0x14, 0x14), RGB565(0x0A, 0x03, 0x03)},
    {"Matrix Green",  RGB565(0x33, 0xFF, 0x88), RGB565(0x12, 0x70, 0x3A), RGB565(0x01, 0x11, 0x0A)},
    {"Ice",           RGB565(0xBF, 0xE8, 0xFF), RGB565(0x4A, 0x7A, 0x93), RGB565(0x05, 0x08, 0x0C)},
    {"Neon Violet",   RGB565(0xB9, 0x6B, 0xFF), RGB565(0x5A, 0x2E, 0x8A), RGB565(0x08, 0x04, 0x0F)},
    {"Mono White",    RGB565(0xE6, 0xED, 0xF3), RGB565(0x7A, 0x82, 0x8C), RGB565(0x05, 0x07, 0x0A)},
};
static const int THEME_N = sizeof(THEMES) / sizeof(THEMES[0]);

static void vigApplyTheme(int idx) {
    if (idx < 0 || idx >= THEME_N) return;
    uint16_t sec = THEMES[idx].sec, bg = THEMES[idx].bg;
    bruceConfig.setUiColor(THEMES[idx].pri, &sec, &bg);
    bruceConfig.saveFile();
    displaySuccess(String("Theme: ") + THEMES[idx].name, true);
}

void vigThemesMenu() {
    std::vector<Option> o;
    for (int i = 0; i < THEME_N; i++) {
        int idx = i;
        o.push_back({THEMES[i].name, [idx]() { vigApplyTheme(idx); }});
    }
    o.push_back({"Back", []() {}});
    loopOptions(o, MENU_TYPE_SUBMENU, "Vigilance Themes");
}

// ---------------- #48 Mission profiles ----------------
struct VigProfile {
    const char *name;
    int themeIdx;
    int sound; // 0/1
    int bright; // 0..100
};
static const VigProfile PROFILES[] = {
    {"Watch",           0, 1, 60},  // cyan, sound, medium brightness
    {"Home Audit",      0, 1, 100}, // cyan, sound, full brightness
    {"CTF",             3, 0, 80},  // matrix, low-key
    {"Stealth",         6, 0, 15},  // mono, muted, very dark
    {"Alert",           2, 1, 100}, // red, sound, full
};
static const int PROFILE_N = sizeof(PROFILES) / sizeof(PROFILES[0]);

static void vigApplyProfile(int idx) {
    if (idx < 0 || idx >= PROFILE_N) return;
    const VigProfile &p = PROFILES[idx];
    uint16_t sec = THEMES[p.themeIdx].sec, bg = THEMES[p.themeIdx].bg;
    bruceConfig.setUiColor(THEMES[p.themeIdx].pri, &sec, &bg);
    bruceConfig.setSoundEnabled(p.sound);
    bruceConfig.setBright((uint8_t)p.bright);
    bruceConfig.saveFile();
    displaySuccess(String("Profile: ") + p.name, true);
}

void vigProfilesMenu() {
    std::vector<Option> o;
    for (int i = 0; i < PROFILE_N; i++) {
        int idx = i;
        o.push_back({PROFILES[i].name, [idx]() { vigApplyProfile(idx); }});
    }
    o.push_back({"Back", []() {}});
    loopOptions(o, MENU_TYPE_SUBMENU, "Mission Profiles");
}

// ---------------- #49 Customizable dashboard ----------------
static uint8_t g_dashMask = 0xFF; // everything shown by default
static bool g_dashLoaded = false;

static void vigDashLoad() {
    g_dashLoaded = true;
    FS *fs;
    if (!getFsStorage(fs)) return;
    if (!(*fs).exists("/Vigilance/dash.cfg")) return;
    File f = (*fs).open("/Vigilance/dash.cfg", FILE_READ);
    if (!f) return;
    if (f.available()) g_dashMask = (uint8_t)f.read();
    f.close();
}

static void vigDashSave() {
    FS *fs;
    if (!getFsStorage(fs)) return;
    if (!(*fs).exists("/Vigilance")) (*fs).mkdir("/Vigilance");
    File f = (*fs).open("/Vigilance/dash.cfg", FILE_WRITE);
    if (!f) return;
    f.write(g_dashMask);
    f.close();
}

uint8_t vigDashMask() {
    if (!g_dashLoaded) vigDashLoad();
    return g_dashMask;
}

void vigDashMenu() {
    if (!g_dashLoaded) vigDashLoad();
    for (;;) {
        auto st = [](uint8_t bit) { return (g_dashMask & bit) ? "[X] " : "[ ] "; };
        std::vector<Option> o = {
            {String(st(VDASH_RF)) + "RF Frequency",  []() { g_dashMask ^= VDASH_RF; vigDashSave(); }  },
            {String(st(VDASH_MOD)) + "Modules (MOD)", []() { g_dashMask ^= VDASH_MOD; vigDashSave(); } },
            {String(st(VDASH_AL)) + "Alert counter",  []() { g_dashMask ^= VDASH_AL; vigDashSave(); } },
            {"Back",                                   []() {}                                          },
        };
        int s = loopOptions(o, MENU_TYPE_SUBMENU, "Dashboard (BAT/SD fixed)");
        if (s == -1 || s == (int)o.size() - 1) return;
    }
}

// ---------------- Sentinel Pulse (#31): persistent toggle ----------------
static bool g_breath = false;
static bool g_breathLoaded = false;

static void vigBreathLoad() {
    g_breathLoaded = true;
    FS *fs;
    if (!getFsStorage(fs)) return;
    if (!(*fs).exists("/Vigilance/breathe.cfg")) return;
    File f = (*fs).open("/Vigilance/breathe.cfg", FILE_READ);
    if (f && f.available()) g_breath = (f.read() == '1');
    if (f) f.close();
}

static void vigBreathSave() {
    FS *fs;
    if (!getFsStorage(fs)) return;
    if (!(*fs).exists("/Vigilance")) (*fs).mkdir("/Vigilance");
    File f = (*fs).open("/Vigilance/breathe.cfg", FILE_WRITE);
    if (!f) return;
    f.write(g_breath ? '1' : '0');
    f.close();
}

bool vigBreathingEnabled() {
    if (!g_breathLoaded) vigBreathLoad();
    return g_breath;
}

// ---------------- Live radar + Standby: persistent toggles ----------------
// Small on/off flags stored one byte per file under /Vigilance, both default on.
static bool vigFlagLoad(const char *path, bool dflt) {
    FS *fs;
    if (!getFsStorage(fs)) return dflt;
    if (!(*fs).exists(path)) return dflt;
    File f = (*fs).open(path, FILE_READ);
    bool v = dflt;
    if (f && f.available()) v = (f.read() == '1');
    if (f) f.close();
    return v;
}
static void vigFlagSave(const char *path, bool v) {
    FS *fs;
    if (!getFsStorage(fs)) return;
    if (!(*fs).exists("/Vigilance")) (*fs).mkdir("/Vigilance");
    File f = (*fs).open(path, FILE_WRITE);
    if (!f) return;
    f.write(v ? '1' : '0');
    f.close();
}

static int8_t g_liveRadar = -1; // -1 = not loaded
static int8_t g_standby = -1;

bool vigLiveRadarEnabled() {
    if (g_liveRadar < 0) g_liveRadar = vigFlagLoad("/Vigilance/radar.cfg", true) ? 1 : 0;
    return g_liveRadar == 1;
}

bool vigStandbyEnabled() {
    if (g_standby < 0) g_standby = vigFlagLoad("/Vigilance/standby.cfg", true) ? 1 : 0;
    return g_standby == 1;
}

// Drive the ambient LED. When on, run Bruce's background "breathe" effect (so the
// LED pulses everywhere, not just in Guardian Eye); when off, turn the LED off.
static void vigApplyBreath(bool on) {
#ifdef HAS_RGB_LED
    if (on) {
        // The LED was likely left at brightness 0 (disabled earlier), so nothing
        // showed. Force a visible brightness and PUSH it to FastLED, re-enable the
        // LED, pick the breathe effect, and start the effect task.
        int b = bruceConfig.ledBright < 10 ? 60 : bruceConfig.ledBright;
        bruceConfig.setLedBright(b);
        bruceConfig.setLedBlinkEnabled(1);
        bruceConfig.setLedEffect(LED_COLOR_BREATHE);
        ledSetup();
        setLedBrightness(b);
        setLedEffect(LED_COLOR_BREATHE);
        ledEffects(true);
    } else {
        bruceConfig.setLedEffect(LED_EFFECT_SOLID);
        ledEffects(false);
        setLedColor(CRGB::Black);
        setLedBrightness(0);
    }
    bruceConfig.saveFile();
#endif
}

// ---------------- Main submenu ----------------
void vigilanceUxMenu() {
    for (;;) {
        std::vector<Option> o = {
            {"Themes",           []() { vigThemesMenu(); }  },
            {"Mission Profiles", []() { vigProfilesMenu(); }},
            {"Dashboard",        []() { vigDashMenu(); }    },
            {String("LED Pulse: ") + (vigBreathingEnabled() ? "ON" : "OFF"),
             []() {
                 g_breath = !g_breath;
                 vigBreathSave();
                 vigApplyBreath(g_breath);
             }},
        };
#ifdef HAS_RGB_LED
        o.push_back({"LED color", []() { setLedColorConfig(); }});
#endif
        o.push_back(
            {String("Live radar: ") + (vigLiveRadarEnabled() ? "ON" : "OFF"),
             []() {
                 g_liveRadar = vigLiveRadarEnabled() ? 0 : 1;
                 vigFlagSave("/Vigilance/radar.cfg", g_liveRadar == 1);
             }}
        );
        o.push_back(
            {String("Standby screen: ") + (vigStandbyEnabled() ? "ON" : "OFF"),
             []() {
                 g_standby = vigStandbyEnabled() ? 0 : 1;
                 vigFlagSave("/Vigilance/standby.cfg", g_standby == 1);
             }}
        );
        o.push_back({"Back", []() {}});
        int s = loopOptions(o, MENU_TYPE_SUBMENU, "Vigilance UX");
        if (s == -1 || s == (int)o.size() - 1) return;
    }
}
