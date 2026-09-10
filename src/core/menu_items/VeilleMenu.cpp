#include "VeilleMenu.h"

#include "core/display.h"
#include "core/guardian_eye.h"
#include "core/utils.h"
#include "core/vig_standby.h"
#include "modules/others/timeline.h"
#include "modules/others/swarm.h"
#include "modules/rf/rf_census.h"
#include "modules/wifi/bug_sweep.h"
#include "modules/wifi/hunt_mode.h"
#include "modules/wifi/wifi_map.h"
#include <globals.h>

#if !defined(LITE_VERSION)
#include "modules/ble/ble_watch.h"
#include "modules/wifi/watch_mode.h"
#endif

void VeilleMenu::optionsMenu(void) {
    options = {
#if !defined(LITE_VERSION)
        {"WiFi Watch Mode",  watch_mode_setup},
        {"BLE Watch",        ble_watch_setup },
#endif
        {"WiFi Map",         wifi_map_setup  },
        {"Sub-GHz Census",   rf_census_setup },
        {"Bug Sweep",        bug_sweep_setup },
        {"Hunt Mode",        hunt_mode_setup },
        {"Journal",          timeline_setup  },
        {"Guardian Eye",     guardian_eye_setup},
        {"Standby",          []() { vigStandbyRun(false); }},
        {"Swarm",            swarm_setup     },
    };

    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Watch");
}

void VeilleMenu::drawIcon(float scale) {
    clearIconArea();
    uint16_t c = bruceConfig.priColor;
    int cx = iconCenterX, cy = iconCenterY;
    int r = (int)(scale * 20);

    // Watch radar / eye: concentric circles + iris + sweep.
    tft.drawCircle(cx, cy, r, c);
    tft.drawCircle(cx, cy, (int)(r * 0.6), c);
    tft.fillCircle(cx, cy, (int)(scale * 4), c);
    // sweep line
    tft.drawLine(cx, cy, cx + (int)(r * 0.85), cy - (int)(r * 0.6), c);
    // cardinal ticks
    tft.drawLine(cx - r, cy, cx - r + (int)(scale * 5), cy, c);
    tft.drawLine(cx + r - (int)(scale * 5), cy, cx + r, cy, c);
    tft.drawLine(cx, cy - r, cx, cy - r + (int)(scale * 5), c);
    tft.drawLine(cx, cy + r - (int)(scale * 5), cx, cy + r, c);
}
