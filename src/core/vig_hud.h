#ifndef __VIGILANCE_HUD_H__
#define __VIGILANCE_HUD_H__

// Vigilance HUD: shared rendering engine behind the "Sentinel HUD" look.
// Screens draw into one full-screen sprite (PSRAM) and push it in one go, which
// removes flicker and makes gradients, glows and anti-aliased shapes affordable.
#include <Arduino.h>
#include <globals.h> // brings in TFT_eSprite via the HAL, in the right include order

// Palette derived from the active theme (bruceConfig colors), so the Vigilance
// themes retint the whole HUD.
struct VigPal {
    uint16_t bg, panel, panelHi, line;
    uint16_t acc, accHi, accDim, accMut;
    uint16_t text, textDim;
    uint16_t red, amber, green, blue;
};
const VigPal &vigPal();

// Blend fg over bg (RGB565), alpha 0..255.
uint16_t vigMix(uint16_t fg, uint16_t bg, uint8_t alpha);

TFT_eSprite *vigHudSprite(); // full-screen framebuffer, nullptr if it cannot be allocated
void vigHudPush();           // send the framebuffer to the panel

void vigHudBackground(TFT_eSprite &s);
void vigHudHairline(TFT_eSprite &s, int y, uint8_t maxAlpha);
void vigHudSpaced(TFT_eSprite &s, const char *str, int x, int y, uint16_t col, int spacing);
void vigHudStatusBar(TFT_eSprite &s, const char *title, uint16_t titleCol);
void vigHudRing(TFT_eSprite &s, int x, int y, int r, int ir, uint16_t col, uint16_t bg);
void vigHudGradRoundRect(TFT_eSprite &s, int x, int y, int w, int h, int r, uint16_t top, uint16_t bottom);
void vigHudGlow(TFT_eSprite &s, int x, int y, int w, int h, int r, uint16_t col);
void vigHudScrollbar(TFT_eSprite &s, int x, int y, int h, int total, int visible, int first);
void vigHudRadar(TFT_eSprite &s, int cx, int cy, int R, float sweepDeg, int nWifi, int nBle, int nSub);

// Module glyphs (about 16 px), picked from the menu name. bg is the flat color
// under the glyph (used to anti-alias arcs).
void vigHudIcon(TFT_eSprite &s, const String &menuName, int cx, int cy, uint16_t col, uint16_t bg);
const char *vigHudCategory(const String &menuName);

#endif // __VIGILANCE_HUD_H__
