#include "ModulesMenu.h"

#include "core/display.h"
#include "core/module_detect.h"
#include "core/scrollableTextArea.h"
#include <globals.h>

void ModulesMenu::optionsMenu(void) {
    ScrollableTextArea area = ScrollableTextArea("MODULES");
    std::vector<DetectedI2C> mods = scanI2CModules();

    int nbPlug = 0;
    for (size_t i = 0; i < mods.size(); i++) {
        ModuleCategory c = mods[i].category;
        if (c == MODCAT_MODULE || c == MODCAT_SENSOR || c == MODCAT_UNKNOWN) nbPlug++;
    }

    area.addLine("Scan Grove I2C port (GPIO8/18)");
    area.addLine("Plug-ins detected: " + String(nbPlug));
    area.addLine("");

    area.addLine("[PLUG-INS]");
    bool any = false;
    for (size_t i = 0; i < mods.size(); i++) {
        ModuleCategory c = mods[i].category;
        if (c == MODCAT_SYSTEM || c == MODCAT_NFC) continue;
        char b[52];
        snprintf(
            b, sizeof(b), "0x%02X %s (%s)", mods[i].addr, mods[i].name.c_str(),
            moduleCategoryName(c)
        );
        area.addLine(String(b));
        any = true;
    }
    if (!any) area.addLine("None. Plug in a Grove module.");
    area.addLine("");

    area.addLine("[INTERNAL / SYSTEM]");
    for (size_t i = 0; i < mods.size(); i++) {
        ModuleCategory c = mods[i].category;
        if (c != MODCAT_SYSTEM && c != MODCAT_NFC) continue;
        char b[52];
        snprintf(b, sizeof(b), "0x%02X %s", mods[i].addr, mods[i].name.c_str());
        area.addLine(String(b));
    }
    area.addLine("");

    area.addLine("[NOTES]");
    if (g_fmModuleDetected) area.addLine("FM SI4713 ready (FM menu).");
    area.addLine("LoRa: plugs into the header");
    area.addLine("CC1101 (SPI+CS preset).");
    area.addLine("LoRa menu. RST/DIO adjustable");
    area.addLine("(Config > Pins > LoRa Pins).");
    area.addLine("");
    area.addLine("Re-plug then reopen to");
    area.addLine("re-scan. ESC to quit.");
    area.addLine("");

    area.addLine("[SAVED PROFILES]");
    std::vector<String> profs;
    if (vigModuleProfileLines(profs) == 0) area.addLine("None yet.");
    else
        for (size_t i = 0; i < profs.size(); i++) area.addLine(profs[i]);

    area.show();
}

void ModulesMenu::drawIcon(float scale) {
    clearIconArea();
    uint16_t c = bruceConfig.priColor;
    int r = (int)(scale * 22);
    int cx = iconCenterX, cy = iconCenterY;
    // chip body
    tft.drawRoundRect(cx - r, cy - r, 2 * r, 2 * r, (int)(scale * 4), c);
    // pins on all 4 sides
    int step = r / 2;
    int len = (int)(scale * 7);
    for (int i = -1; i <= 1; i++) {
        int o = i * step;
        tft.drawLine(cx + o, cy - r, cx + o, cy - r - len, c);
        tft.drawLine(cx + o, cy + r, cx + o, cy + r + len, c);
        tft.drawLine(cx - r, cy + o, cx - r - len, cy + o, c);
        tft.drawLine(cx + r, cy + o, cx + r + len, cy + o, c);
    }
    // core
    tft.fillCircle(cx, cy, (int)(scale * 5), c);
}
