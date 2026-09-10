#ifndef __MODULES_MENU_H__
#define __MODULES_MENU_H__

#include <MenuItemInterface.h>

// Vigilance: "Modules" menu. Scans the expansion connector and lists the
// plug-in modules detected (I2C), alongside the internal peripherals.
class ModulesMenu : public MenuItemInterface {
public:
    ModulesMenu() : MenuItemInterface("Modules") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return false; }
    const String &themePath() override {
        static const String empty = "";
        return empty;
    }
};

#endif
