#ifndef __VIGILANCE_UX_H__
#define __VIGILANCE_UX_H__

#include <Arduino.h>

// Vigilance - Point L (UX / dashboard / identity).
//  #50 Themes      : Vigilance palette gallery (beyond the cyan Watch).
//  #48 Profiles    : one-tap setting presets (Watch, Audit, CTF, Stealth...).
//  #49 Dashboard   : choose which vitals show on the home screen.

// Main submenu (wired into Config).
void vigilanceUxMenu();

// About screen: identity, version, credits, repo.
void vigAboutScreen();

// Individual menus (also reachable directly).
void vigThemesMenu();
void vigProfilesMenu();
void vigDashMenu();

// Dashboard vitals mask (BAT and SD always shown).
#define VDASH_RF 0x01
#define VDASH_MOD 0x02
#define VDASH_AL 0x04
uint8_t vigDashMask(); // lazy-loaded from /Vigilance/dash.cfg

// Sentinel Pulse (#31): the LED "breathes" with the threat level on the
// Guardian Eye screen. Can be toggled on/off (persisted to /Vigilance/breathe.cfg).
bool vigBreathingEnabled();

// Home screen radar runs a light async WiFi scan so it shows live activity
// (persisted to /Vigilance/radar.cfg, default on).
bool vigLiveRadarEnabled();

// After a while idle on the home screen, drop into the Vigilance standby scene
// (persisted to /Vigilance/standby.cfg, default on).
bool vigStandbyEnabled();

#endif // __VIGILANCE_UX_H__
