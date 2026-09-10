#ifndef __VEILLE_MENU_H__
#define __VEILLE_MENU_H__

#include <MenuItemInterface.h>

// Vigilance: the "Watch" hub, entry point to the surveillance platform.
// Pulls together the watch features scattered across the other menus:
// WiFi Watch Mode, BLE Watch (anti-tracker), WiFi Map, Journal, Swarm.
class VeilleMenu : public MenuItemInterface {
public:
    VeilleMenu() : MenuItemInterface("Watch") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return false; }
    const String &themePath() override {
        static const String empty = "";
        return empty;
    }
};

#endif
