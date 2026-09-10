#include "main_menu.h"
#include "display.h"
#include "event_log.h"
#include "module_detect.h"
#include "utils.h"
#include "vig_env.h"
#include "vig_hud.h"
#include "vig_standby.h"
#include "vig_ux.h"
#include <algorithm>
#include <globals.h>

MainMenu::MainMenu() {
    _menuItems = {
        &veilleMenu,
        &wifiMenu,
        &bleMenu,
        &rfMenu,
        &nrf24Menu,
#if !defined(LITE_VERSION)
        &loraMenu,
#endif
#if defined(FM_SI4713) && !defined(LITE_VERSION)
        &fmMenu,
#endif
        &irMenu,
#if !defined(LITE_VERSION)
        &ethernetMenu,
#endif
        &gpsMenu,
        &rfidMenu,
        &fileMenu,
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
        &scriptsMenu,
#endif
        &clockMenu,
        &othersMenu,
        &modulesMenu,
        &configMenu,
    };

    _totalItems = _menuItems.size();
}

MainMenu::~MainMenu() {}

// Vigilance: pointers the capture-less hover callback needs to reach the menu
// and remember which tile is focused between animation frames.
static MainMenu *g_dashMenu = nullptr;
static void *g_homeFocus = nullptr;

// Vigilance home ("Sentinel HUD"): status bar, live radar with the last known
// environment counts, then a 2-column grid of module tiles scrolled around the
// focused one. Everything is drawn into the HUD framebuffer; navigation stays
// in loopOptions, we only render.
static void drawVigilanceHome(TFT_eSprite &s, void *focusPtr) {
    const VigPal &p = vigPal();
    const int W = tftWidth, H = tftHeight;

    vigHudBackground(s);
    vigHudStatusBar(s, "VIGILANCE", p.acc);

    // --- hero: radar + legend ---
    int nW = vigEnvGet(VIG_ENV_WIFI), nB = vigEnvGet(VIG_ENV_BLE), nS = vigEnvGet(VIG_ENV_SUB);
    float sweep = (float)(((uint64_t)millis() * 860ULL / 1000ULL) % 3600ULL) / 10.0f; // 86 deg/s
    vigHudRadar(s, W / 2, 47, 22, sweep, nW, nB, nS);
    vigHudSpaced(s, "ENVIRONMENT", 8, 24, p.accDim, 1);

    auto cnt = [](int n) { return n < 0 ? String("--") : String(n); };
    auto legend = [&](int x, int y, uint16_t col, const char *label, const String &val) {
        s.drawSpot(x + 2.0f, y + 8.0f, 2.0f, col);
        s.setTextDatum(TL_DATUM);
        s.setTextColor(p.textDim);
        s.drawString(label, x + 8, y + 5, 1);
        s.setTextDatum(TR_DATUM);
        s.setTextColor(col);
        s.drawString(val, x + 64, y, 2);
    };
    legend(26, 33, p.acc, "WIFI", cnt(nW));
    legend(26, 51, p.blue, "BLE", cnt(nB));
    legend(240, 33, p.amber, "SUB", cnt(nS));
    uint8_t dm = vigDashMask(); // #49: user-selected vitals
    if (dm & VDASH_AL) {
        unsigned long al = (unsigned long)vigEventsTotal();
        legend(240, 51, al ? p.red : p.green, "ALERTS", String(al));
    } else if (dm & VDASH_MOD) {
        legend(240, 51, p.green, "MODULES", String(g_pluggableModuleCount));
    }
    s.drawFastHLine(6, 72, W - 12, vigMix(p.acc, p.bg, 50));

    // --- module tiles ---
    if (!g_dashMenu) return;
    std::vector<MenuItemInterface *> all = g_dashMenu->getItems();
    std::vector<String> dis = bruceConfig.disabledMenus;
    std::vector<MenuItemInterface *> vis;
    for (auto it : all) {
        if (std::find(dis.begin(), dis.end(), it->getName()) == dis.end()) vis.push_back(it);
    }
    int n = (int)vis.size();
    if (n == 0) return;

    const int cols = 2, gx = 6, gapX = 6, gapY = 4, tileH = 27, top = 76;
    int tileW = (W - 2 * gx - (cols - 1) * gapX) / cols;
    int visRows = (H - top - 2 + gapY) / (tileH + gapY);
    if (visRows < 1) visRows = 1;
    int totalRows = (n + cols - 1) / cols;

    int fidx = 0;
    for (int i = 0; i < n; i++)
        if ((void *)vis[i] == focusPtr) {
            fidx = i;
            break;
        }
    int frow = fidx / cols;
    static int s_scrollRow = 0;
    if (frow < s_scrollRow) s_scrollRow = frow;
    if (frow >= s_scrollRow + visRows) s_scrollRow = frow - visRows + 1;
    if (s_scrollRow > totalRows - visRows) s_scrollRow = totalRows - visRows;
    if (s_scrollRow < 0) s_scrollRow = 0;

    for (int r = 0; r < visRows; r++) {
        int row = s_scrollRow + r;
        for (int c = 0; c < cols; c++) {
            int i = row * cols + c;
            if (i >= n) continue;
            int tx = gx + c * (tileW + gapX);
            int ty = top + r * (tileH + gapY);
            bool focus = ((void *)vis[i] == focusPtr);

            uint16_t bodyTop = vigMix(p.acc, p.bg, focus ? 72 : 24);
            uint16_t bodyBot = vigMix(p.acc, p.bg, focus ? 42 : 12);
            if (focus) vigHudGlow(s, tx, ty, tileW, tileH, 6, p.acc);
            vigHudGradRoundRect(s, tx, ty, tileW, tileH, 6, bodyTop, bodyBot);
            s.drawSmoothRoundRect(tx, ty, 6, 5, tileW, tileH, focus ? p.acc : p.line);
            s.drawFastHLine(
                tx + 6, ty + 1, tileW - 12, focus ? vigMix(p.accHi, bodyTop, 120) : vigMix(TFT_WHITE, bodyTop, 14)
            );

            // icon chip
            const int chs = 21, chx = tx + 4, chy = ty + (tileH - chs) / 2;
            uint16_t chipBg = vigMix(p.acc, bodyTop, focus ? 60 : 24);
            s.fillSmoothRoundRect(chx, chy, chs, chs, 5, chipBg);
            s.drawSmoothRoundRect(chx, chy, 5, 4, chs, chs, focus ? p.acc : vigMix(p.acc, bodyTop, 110));
            vigHudIcon(s, vis[i]->getName(), chx + chs / 2, chy + chs / 2, focus ? p.accHi : p.acc, chipBg);

            // name + category
            String nm = vis[i]->getName();
            int maxW = tileW - 31 - 4;
            while (nm.length() > 1 && s.textWidth(nm, 2) > maxW) nm.remove(nm.length() - 1);
            s.setTextDatum(TL_DATUM);
            s.setTextColor(focus ? p.text : vigMix(p.text, p.bg, 175));
            s.drawString(nm, tx + 31, ty + 2, 2);
            s.setTextColor(focus ? p.acc : p.textDim);
            s.drawString(vigHudCategory(vis[i]->getName()), tx + 31, ty + 18, 1);
        }
    }
    vigHudScrollbar(s, W - 4, top, visRows * (tileH + gapY) - gapY, totalRows, visRows, s_scrollRow);
}

// Hover callback used by loopOptions. shouldRender=true on a selection change,
// false on the periodic tick we use to animate the radar.
static bool vigHomeHover(void *menuItem, bool shouldRender) {
    static uint32_t lastFrame = 0, lastActivity = 0;
    if (lastActivity == 0) lastActivity = millis();
    TFT_eSprite *sp = vigHudSprite();
    if (!sp) return false; // no framebuffer: fall back to the stock menu drawing
    if (shouldRender) {
        g_homeFocus = menuItem;
        lastActivity = millis();
    } else {
        if (vigLiveRadarEnabled()) vigScanTick(); // live WiFi detection for the radar
        // Inactivity -> Vigilance standby (screensaver). Returns on any input.
        if (vigStandbyEnabled() && millis() - lastActivity > 20000) {
            vigStandbyRun(false);
            lastActivity = millis();
            check(SelPress); // swallow the wake press so it doesn't select a tile
            check(EscPress);
            check(AnyKeyPress);
#ifdef HAS_ENCODER
            drainRotarySteps();
#endif
        } else if (millis() - lastFrame < 66) return true; // ~15 fps otherwise
    }
    lastFrame = millis();
    drawVigilanceHome(*sp, g_homeFocus ? g_homeFocus : menuItem);
    vigHudPush();
#if defined(HAS_TOUCH)
    TouchFooter();
#endif
    return true;
}

void MainMenu::begin(void) {
    returnToMenu = false;
    g_dashMenu = this;
    options = {};

    std::vector<String> l = bruceConfig.disabledMenus;
    for (int i = 0; i < _totalItems; i++) {
        String itemName = _menuItems[i]->getName();
        if (find(l.begin(), l.end(), itemName) == l.end()) { // If menu item is not disabled
            options.push_back(
                {itemName,
                 [this, i]() {
                     vigScanStop(); // turn the radio off before handing over to the module
                     _menuItems[i]->optionsMenu();
                 },
                 false,        // selected = false
                 vigHomeHover, // Vigilance dashboard render
                 _menuItems[i]}
            );
        }
    }
    _currentIndex = loopOptions(options, MENU_TYPE_MAIN, "Main Menu", _currentIndex);
    vigScanStop(); // safety: never leave the radio scanning after the home screen
};

/*********************************************************************
**  Function: hideAppsMenu
**  Menu to Hide or show menus
**********************************************************************/

void MainMenu::hideAppsMenu() {
    auto items = this->getItems();
    int index = 0;
RESTART: // using gotos to avoid stackoverflow after many choices
    options.clear();
    for (auto item : items) {
        String label = item->getName();
        std::vector<String> l = bruceConfig.disabledMenus;
        bool enabled = find(l.begin(), l.end(), label) == l.end();
        options.push_back(
            {label,
             [this, label, enabled]() {
                 if (enabled) bruceConfig.addDisabledMenu(label);
                 else bruceConfig.removeDisabledMenu(label);
             },
             enabled}
        );
    }
    options.push_back({"Show All", [=]() { bruceConfig.disabledMenus.clear(); }, true});
    addOptionToMainMenu();
    index = loopOptions(options, index);
    bruceConfig.saveFile();
    if (!returnToMenu) goto RESTART;
}
