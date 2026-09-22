// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance HUD: the shared rendering engine behind the "Sentinel HUD" look.
// Every HUD screen draws into one full-screen 16-bit sprite, allocated once in
// PSRAM, and pushes it in a single transfer. No flicker, and we can afford the
// gradients, glows and anti-aliased primitives that look poor when drawn
// straight to the panel. The palette derives from the active theme colors.
#include "vig_hud.h"

#include "display.h"
#include "settings.h"
#include "utils.h"
#include "vig_ux.h"
#include <globals.h>
#include <interface.h>
#include <math.h>

static inline uint16_t c565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t vigMix(uint16_t fg, uint16_t bg, uint8_t a) {
    uint32_t fr = (fg >> 11) & 0x1F, fgn = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    uint32_t br = (bg >> 11) & 0x1F, bgn = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    uint32_t ia = 255 - a;
    uint32_t r = (fr * a + br * ia) / 255;
    uint32_t g = (fgn * a + bgn * ia) / 255;
    uint32_t b = (fb * a + bb * ia) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

const VigPal &vigPal() {
    static VigPal p;
    static bool init = false;
    static uint16_t kAcc = 0, kSec = 0, kBg = 0;
    if (!init || kAcc != bruceConfig.priColor || kSec != bruceConfig.secColor || kBg != bruceConfig.bgColor) {
        init = true;
        kAcc = bruceConfig.priColor;
        kSec = bruceConfig.secColor;
        kBg = bruceConfig.bgColor;
        p.acc = kAcc;
        p.accDim = kSec;
        p.bg = kBg;
        p.accHi = vigMix(TFT_WHITE, p.acc, 110);
        p.accMut = vigMix(p.acc, p.bg, 110);
        p.panel = vigMix(p.acc, p.bg, 22);
        p.panelHi = vigMix(p.acc, p.bg, 70);
        p.line = vigMix(p.acc, p.bg, 60);
        p.text = c565(0xEA, 0xF2, 0xF8);
        p.textDim = vigMix(c565(0x6C, 0x93, 0xA8), p.bg, 235);
        p.red = c565(0xFF, 0x4D, 0x4D);
        p.amber = c565(0xFF, 0xB0, 0x20);
        p.green = c565(0x35, 0xE0, 0x8A);
        p.blue = c565(0x4D, 0x7D, 0xFF);
    }
    return p;
}

// ---------------- framebuffer ----------------
static TFT_eSprite *s_spr = nullptr;
static bool s_tried = false;

TFT_eSprite *vigHudSprite() {
    if (s_spr) return s_spr;
    if (s_tried) return nullptr;
    s_tried = true;
    TFT_eSprite *sp = new TFT_eSprite(tft.native());
    sp->setColorDepth(16);
    if (sp->createSprite(tftWidth, tftHeight) == nullptr) {
        delete sp;
        return nullptr;
    }
    s_spr = sp;
    return s_spr;
}

// Dump the framebuffer to SD as a 24-bit BMP (/Vigilance/shots/shot_NNN.bmp).
// Reads the sprite back pixel by pixel, so it captures exactly what is on screen.
static void vigHudSaveShot(TFT_eSprite &s) {
    FS *fs;
    if (!getFsStorage(fs)) return;
    if (!(*fs).exists("/Vigilance")) (*fs).mkdir("/Vigilance");
    if (!(*fs).exists("/Vigilance/shots")) (*fs).mkdir("/Vigilance/shots");

    char path[40];
    int n = 0;
    do {
        snprintf(path, sizeof(path), "/Vigilance/shots/shot_%03d.bmp", n++);
    } while ((*fs).exists(path) && n < 1000);

    File f = (*fs).open(path, FILE_WRITE);
    if (!f) return;

    const int W = s.width(), H = s.height();
    const int rowSize = (W * 3 + 3) & ~3;
    const uint32_t fileSize = 54 + (uint32_t)rowSize * H;

    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    hdr[2] = fileSize;
    hdr[3] = fileSize >> 8;
    hdr[4] = fileSize >> 16;
    hdr[5] = fileSize >> 24;
    hdr[10] = 54; // pixel data offset
    hdr[14] = 40; // DIB header size
    hdr[18] = W;
    hdr[19] = W >> 8;
    hdr[22] = H;
    hdr[23] = H >> 8;
    hdr[26] = 1;  // planes
    hdr[28] = 24; // bits per pixel
    f.write(hdr, 54);

    uint8_t *row = (uint8_t *)malloc(rowSize);
    if (!row) {
        f.close();
        return;
    }
    for (int y = H - 1; y >= 0; y--) { // BMP rows are bottom-up
        int p = 0;
        for (int x = 0; x < W; x++) {
            uint16_t c = s.readPixel(x, y);
            row[p++] = (uint8_t)((c & 0x1F) * 255 / 31);         // B
            row[p++] = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);  // G
            row[p++] = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31); // R
        }
        while (p < rowSize) row[p++] = 0;
        f.write(row, rowSize);
    }
    free(row);
    f.close();
}

void vigHudPush() {
    if (!s_spr) return;
    s_spr->pushSprite(0, 0);
    if (ScreenShot) {
        ScreenShot = false;
        vigHudSaveShot(*s_spr);
        const VigPal &p = vigPal();
        tft.fillRoundRect(tftWidth / 2 - 48, tftHeight / 2 - 13, 96, 26, 6, p.bg);
        tft.drawRoundRect(tftWidth / 2 - 48, tftHeight / 2 - 13, 96, 26, 6, p.acc);
        tft.setTextColor(p.acc, p.bg);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("SHOT SAVED", tftWidth / 2, tftHeight / 2, 1);
        tft.setTextDatum(TL_DATUM);
        delay(550);
    }
}

// ---------------- primitives ----------------
void vigHudBackground(TFT_eSprite &s) {
    const VigPal &p = vigPal();
    s.fillSprite(p.bg);
    // soft light falling from the top edge
    for (int y = 0; y < 56; y++) {
        uint8_t a = (uint8_t)(16 * (56 - y) / 56);
        if (a) s.drawFastHLine(0, y, tftWidth, vigMix(p.acc, p.bg, a));
    }
    // faint working grid under the content area
    uint16_t g = vigMix(p.acc, p.bg, 9);
    for (int x = 0; x < tftWidth; x += 16) s.drawFastVLine(x, 20, tftHeight - 20, g);
    for (int y = 20; y < tftHeight; y += 16) s.drawFastHLine(0, y, tftWidth, g);
}

void vigHudHairline(TFT_eSprite &s, int y, uint8_t maxA) {
    const VigPal &p = vigPal();
    int x0 = 6, x1 = tftWidth - 6, half = (x1 - x0) / 2, cx = x0 + half;
    if (half <= 0) return;
    for (int x = x0; x <= x1; x++) {
        int d = abs(x - cx);
        uint8_t a = (uint8_t)((int)maxA * (half - d) / half);
        s.drawPixel(x, y, vigMix(p.acc, p.bg, a));
    }
}

void vigHudSpaced(TFT_eSprite &s, const char *str, int x, int y, uint16_t col, int spacing) {
    s.setTextDatum(TL_DATUM);
    s.setTextColor(col);
    for (const char *c = str; *c; c++) {
        char b[2] = {*c, 0};
        s.drawString(b, x, y, 1);
        x += 6 + spacing;
    }
}

void vigHudRing(TFT_eSprite &s, int x, int y, int r, int ir, uint16_t col, uint16_t bg) {
    // two half arcs make a full anti-aliased ring of thickness r-ir
    s.drawSmoothArc(x, y, r, ir, 0, 180, col, bg, false);
    s.drawSmoothArc(x, y, r, ir, 180, 360, col, bg, false);
}

void vigHudGradRoundRect(TFT_eSprite &s, int x, int y, int w, int h, int r, uint16_t top, uint16_t bot) {
    if (h <= 1) return;
    for (int i = 0; i < h; i++) {
        float dy = 0;
        if (i < r) dy = r - i - 0.5f;
        else if (i >= h - r) dy = i - (h - r) + 0.5f;
        int inset = 0;
        if (dy > 0) inset = (int)(r - sqrtf((float)r * r - dy * dy) + 0.5f);
        uint16_t c = vigMix(bot, top, (uint8_t)(i * 255 / (h - 1)));
        s.drawFastHLine(x + inset, y + i, w - 2 * inset, c);
    }
}

void vigHudGlow(TFT_eSprite &s, int x, int y, int w, int h, int r, uint16_t col) {
    const VigPal &p = vigPal();
    s.drawSmoothRoundRect(x - 3, y - 3, r + 3, r + 2, w + 6, h + 6, vigMix(col, p.bg, 22));
    s.drawSmoothRoundRect(x - 2, y - 2, r + 2, r + 1, w + 4, h + 4, vigMix(col, p.bg, 45));
    s.drawSmoothRoundRect(x - 1, y - 1, r + 1, r, w + 2, h + 2, vigMix(col, p.bg, 90));
}

void vigHudScrollbar(TFT_eSprite &s, int x, int y, int h, int total, int visible, int first) {
    if (total <= visible || h < 8) return;
    const VigPal &p = vigPal();
    s.fillSmoothRoundRect(x, y, 2, h, 1, vigMix(p.acc, p.bg, 40));
    int th = h * visible / total;
    if (th < 8) th = 8;
    int ty = y + (h - th) * first / (total - visible);
    s.fillSmoothRoundRect(x, ty, 2, th, 1, p.acc);
}

// ---------------- status bar ----------------
void vigHudStatusBar(TFT_eSprite &s, const char *title, uint16_t titleCol) {
    const VigPal &p = vigPal();
    const int W = tftWidth;
    uint16_t barBg = vigMix(p.acc, p.bg, 13); // matches the top light at y ~ 10

    // sentinel eye + title
    vigHudRing(s, 11, 10, 5, 4, p.acc, barBg);
    s.drawSpot(11.0f, 10.0f, 1.8f, p.accHi);
    vigHudSpaced(s, title, 20, 6, titleCol, 1);

    // clock (font 2), right aligned
    String t = "--:--";
    if (clock_set) {
#if defined(HAS_RTC)
        updateTimeStr(_rtc.getTimeStruct());
#else
        updateTimeStr(rtc.getTimeStruct());
#endif
        t = String(timeStr).substring(0, 5);
    }
    s.setTextDatum(TR_DATUM);
    s.setTextColor(p.text);
    s.drawString(t, W - 8, 2, 2);

    // battery: body + nub, proportional fill, percentage centered in the body
    uint8_t bat = getBattery();
    const int bw = 28, bh = 12, bx = W - 94, by = 4;
    s.drawSmoothRoundRect(bx, by, 3, 2, bw, bh, p.textDim, barBg);
    s.fillSmoothRoundRect(bx + bw + 1, by + 3, 2, 6, 1, p.textDim, barBg);
    int fw = (bw - 4) * bat / 100;
    if (bat > 0 && fw < 2) fw = 2;
    if (fw > 0) s.fillSmoothRoundRect(bx + 2, by + 2, fw, bh - 4, 2, bat < 20 ? p.red : p.green);
    bool onFill = (bx + 2 + fw) > (bx + bw / 2 + 6);
    s.setTextDatum(MC_DATUM);
    s.setTextColor(onFill ? c565(4, 18, 26) : p.text);
    String bl = bat == 0 ? String("--") : (isCharging() ? String("CHG") : String(bat));
    s.drawString(bl, bx + bw / 2, by + bh / 2 + 1, 1);

    // SD card presence
    s.drawSpot((float)(W - 106), 10.0f, 2.2f, sdcardMounted ? p.green : vigMix(p.textDim, p.bg, 120));

    // Sub-GHz frequency (user can hide it from the dashboard settings)
    if (vigDashMask() & VDASH_RF) {
        s.setTextDatum(TL_DATUM);
        s.setTextColor(p.textDim);
        s.drawString("RF", 150, 6, 1);
        s.setTextColor(p.acc);
        s.drawString(String(bruceConfigPins.rfFreq, 2), 166, 6, 1);
    }

    vigHudHairline(s, 19, 140);
}

// ---------------- radar ----------------
static uint32_t blipHash(int kind, int i) {
    uint32_t h = (uint32_t)(kind + 1) * 2654435761u ^ (uint32_t)(i + 1) * 40503u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

void vigHudRadar(TFT_eSprite &s, int cx, int cy, int R, float sweepDeg, int nWifi, int nBle, int nSub) {
    const VigPal &p = vigPal();
    uint16_t ring = vigMix(p.acc, p.bg, 46), cross = vigMix(p.acc, p.bg, 30);
    vigHudRing(s, cx, cy, (int)(R * 0.4f), (int)(R * 0.4f) - 1, ring, p.bg);
    vigHudRing(s, cx, cy, (int)(R * 0.7f), (int)(R * 0.7f) - 1, ring, p.bg);
    vigHudRing(s, cx, cy, R, R - 1, ring, p.bg);
    s.drawFastHLine(cx - R, cy, 2 * R, cross);
    s.drawFastVLine(cx, cy - R, 2 * R, cross);

    // sweep with a fading trail
    float a = sweepDeg * DEG_TO_RAD;
    for (int i = 25; i >= 1; i--) {
        float ai = a - i * 0.045f;
        uint8_t al = (uint8_t)((1.0f - i / 26.0f) * 0.42f * 255);
        s.drawLine(cx, cy, cx + (int)(cosf(ai) * R), cy + (int)(sinf(ai) * R), vigMix(p.green, p.bg, al));
    }
    s.drawWideLine(cx, cy, cx + cosf(a) * R, cy + sinf(a) * R, 1.5f, p.green);

    // blips light up when the sweep passes over them
    const int counts[3] = {nWifi, nBle, nSub};
    const uint16_t cols[3] = {p.acc, p.blue, p.amber};
    for (int k = 0; k < 3; k++) {
        int m = counts[k];
        if (m < 0) m = 0;
        if (m > 6) m = 6;
        for (int i = 0; i < m; i++) {
            uint32_t h = blipHash(k, i);
            float ang = (float)(h % 360);
            float rad = R * (0.35f + (float)((h >> 9) % 60) / 100.0f);
            float d = fabsf(fmodf(sweepDeg - ang + 540.0f, 360.0f) - 180.0f);
            float lit = 1.0f - d / 36.0f;
            if (lit < 0) lit = 0;
            float bx = cx + cosf(ang * DEG_TO_RAD) * rad, by = cy + sinf(ang * DEG_TO_RAD) * rad;
            if (lit > 0.2f) s.drawSpot(bx, by, 1.5f + lit * 1.6f + 2.5f, vigMix(cols[k], p.bg, (uint8_t)(lit * 50)));
            s.drawSpot(bx, by, 1.5f + lit * 1.6f, vigMix(cols[k], p.bg, (uint8_t)(82 + lit * 173)));
        }
    }
    s.drawSpot((float)cx, (float)cy, 1.6f, p.accHi);
}

// ---------------- module glyphs ----------------
static bool nameHas(const String &lower, const char *k) { return lower.indexOf(k) >= 0; }

void vigHudIcon(TFT_eSprite &s, const String &menuName, int cx, int cy, uint16_t col, uint16_t bg) {
    String n = menuName;
    n.toLowerCase();
    const float x = cx, y = cy, W1 = 1.5f;

    if (nameHas(n, "rfid") || nameHas(n, "nfc")) { // contactless card
        s.drawSmoothRoundRect(cx - 8, cy - 5, 2, 1, 15, 11, col, bg);
        s.fillRect(cx - 5, cy - 2, 4, 3, col);
        s.drawSmoothArc(cx + 3, cy, 3, 2, 225, 315, col, bg, true);
        s.drawSmoothArc(cx + 3, cy, 6, 5, 225, 315, col, bg, true);
    } else if (nameHas(n, "nrf")) { // radio chip with pins
        s.drawSmoothRoundRect(cx - 5, cy - 5, 2, 1, 11, 11, col, bg);
        for (int o = -2; o <= 2; o += 4) {
            s.drawFastHLine(cx - 8, cy + o, 3, col);
            s.drawFastHLine(cx + 6, cy + o, 3, col);
            s.drawFastVLine(cx + o, cy - 8, 3, col);
            s.drawFastVLine(cx + o, cy + 6, 3, col);
        }
        s.drawSpot(x - 2, y - 2, 1.0f, col, bg);
    } else if (nameHas(n, "wifi")) { // dot + arcs
        s.drawSpot(x, y + 5, 1.5f, col, bg);
        s.drawSmoothArc(cx, cy + 5, 4, 3, 135, 225, col, bg, true);
        s.drawSmoothArc(cx, cy + 5, 7, 6, 135, 225, col, bg, true);
    } else if (nameHas(n, "ble") || nameHas(n, "blue")) { // bluetooth rune, logo proportions
        s.drawWideLine(x, y - 7.5f, x, y + 7.5f, 1.7f, col, bg);
        s.drawWideLine(x - 5.4f, y - 4.0f, x + 4.4f, y + 3.3f, 1.7f, col, bg);
        s.drawWideLine(x + 4.4f, y + 3.3f, x, y + 7.5f, 1.7f, col, bg);
        s.drawWideLine(x - 5.4f, y + 4.0f, x + 4.4f, y - 3.3f, 1.7f, col, bg);
        s.drawWideLine(x + 4.4f, y - 3.3f, x, y - 7.5f, 1.7f, col, bg);
    } else if (nameHas(n, "lora")) { // mast with waves from the top
        s.drawWideLine(x - 4, y + 7, x, y - 2, W1, col, bg);
        s.drawWideLine(x + 4, y + 7, x, y - 2, W1, col, bg);
        s.drawWideLine(x - 2.4f, y + 3.5f, x + 2.4f, y + 3.5f, W1, col, bg);
        s.drawSpot(x, y - 3, 1.3f, col, bg);
        s.drawSmoothArc(cx, cy - 3, 4, 3, 125, 235, col, bg, true);
        s.drawSmoothArc(cx, cy - 3, 7, 6, 125, 235, col, bg, true);
    } else if (n == "fm" || nameHas(n, "radio")) { // small radio set
        s.drawSmoothRoundRect(cx - 7, cy - 2, 2, 1, 15, 9, col, bg);
        s.drawSpot(x + 3, y + 2, 1.5f, col, bg);
        s.drawFastHLine(cx - 5, cy + 2, 5, col);
        s.drawWideLine(x - 3, y - 2, x + 2, y - 8, W1, col, bg);
    } else if (nameHas(n, "ether")) { // network jack
        s.drawSmoothRoundRect(cx - 6, cy - 6, 2, 1, 13, 9, col, bg);
        for (int o = -3; o <= 3; o += 3) s.drawFastVLine(cx + o, cy - 1, 2, col);
        s.drawWideLine(x, y + 3, x, y + 7, W1, col, bg);
    } else if (nameHas(n, "gps")) { // map pin
        vigHudRing(s, cx, cy - 2, 4, 3, col, bg);
        s.drawWideLine(x - 3, y + 1, x, y + 7, W1, col, bg);
        s.drawWideLine(x + 3, y + 1, x, y + 7, W1, col, bg);
        s.drawSpot(x, y - 2, 1.2f, col, bg);
    } else if (nameHas(n, "file")) { // folder
        s.drawSmoothRoundRect(cx - 7, cy - 4, 2, 1, 15, 11, col, bg);
        s.drawWideLine(x - 7, y - 6, x - 1, y - 6, 1.2f, col, bg);
        s.drawWideLine(x - 1, y - 6, x - 1, y - 4, 1.2f, col, bg);
    } else if (nameHas(n, "script") || nameHas(n, "interp") || n == "js") { // < >
        s.drawWideLine(x - 2, y - 5, x - 6, y, W1, col, bg);
        s.drawWideLine(x - 6, y, x - 2, y + 5, W1, col, bg);
        s.drawWideLine(x + 2, y - 5, x + 6, y, W1, col, bg);
        s.drawWideLine(x + 6, y, x + 2, y + 5, W1, col, bg);
    } else if (nameHas(n, "clock")) {
        vigHudRing(s, cx, cy, 7, 6, col, bg);
        s.drawWideLine(x, y, x, y - 4, W1, col, bg);
        s.drawWideLine(x, y, x + 3, y, W1, col, bg);
        s.drawSpot(x, y, 1.0f, col, bg);
    } else if (nameHas(n, "other")) { // 3x3 dots
        for (int dx = -4; dx <= 4; dx += 4)
            for (int dy = -4; dy <= 4; dy += 4) s.drawSpot(x + dx, y + dy, 1.2f, col, bg);
    } else if (nameHas(n, "module")) { // pluggable board
        s.drawSmoothRoundRect(cx - 6, cy - 6, 2, 1, 13, 13, col, bg);
        s.drawRect(cx - 3, cy - 3, 7, 7, col);
        s.drawFastHLine(cx - 8, cy, 2, col);
        s.drawFastHLine(cx + 7, cy, 2, col);
        s.drawFastVLine(cx, cy - 8, 2, col);
        s.drawFastVLine(cx, cy + 7, 2, col);
    } else if (nameHas(n, "config") || nameHas(n, "setting")) { // gear
        vigHudRing(s, cx, cy, 5, 4, col, bg);
        for (int k = 0; k < 8; k++) {
            float a = k * 45.0f * DEG_TO_RAD;
            s.drawWideLine(x + cosf(a) * 5, y + sinf(a) * 5, x + cosf(a) * 7.5f, y + sinf(a) * 7.5f, 1.8f, col, bg);
        }
        s.drawSpot(x, y, 1.5f, col, bg);
    } else if (nameHas(n, "connect")) { // two chain links
        s.drawSmoothRoundRect(cx - 9, cy - 3, 3, 2, 11, 7, col, bg);
        s.drawSmoothRoundRect(cx - 2, cy - 3, 3, 2, 11, 7, col, bg);
    } else if (n == "ir" || nameHas(n, "infra")) { // remote control
        s.drawSmoothRoundRect(cx - 3, cy - 7, 2, 1, 7, 15, col, bg);
        s.drawSpot(x, y - 4, 1.2f, col, bg);
        s.drawFastHLine(cx - 1, cy, 3, col);
        s.drawFastHLine(cx - 1, cy + 3, 3, col);
    } else if (nameHas(n, "watch") || nameHas(n, "veille")) { // sentinel eye
        float px = x - 7.5f, pyt = y, pyb = y;
        for (int i = 1; i <= 8; i++) {
            float xx = x - 7.5f + 15.0f * i / 8.0f;
            float rel = (xx - x) / 7.5f;
            float yy = 4.0f * (1.0f - rel * rel);
            s.drawWideLine(px, pyt, xx, y - yy, W1, col, bg);
            s.drawWideLine(px, pyb, xx, y + yy, W1, col, bg);
            px = xx;
            pyt = y - yy;
            pyb = y + yy;
        }
        vigHudRing(s, cx, cy, 3, 2, col, bg);
        s.drawSpot(x, y, 1.1f, col, bg);
    } else if (n == "rf" || nameHas(n, "sub")) { // one clean sine wave
        float px = x - 8, py = y;
        for (int i = 1; i <= 16; i++) {
            float xx = x - 8 + i;
            float yy = y - 5.0f * sinf((float)i / 16.0f * 6.2832f);
            s.drawWideLine(px, py, xx, yy, W1, col, bg);
            px = xx;
            py = yy;
        }
    } else {
        s.drawSpot(x, y, 2.0f, col, bg);
    }
}

const char *vigHudCategory(const String &menuName) {
    String n = menuName;
    n.toLowerCase();
    if (nameHas(n, "rfid") || nameHas(n, "nfc")) return "NFC / cards";
    if (nameHas(n, "nrf")) return "2.4 GHz";
    if (nameHas(n, "wifi")) return "scan / attack";
    if (nameHas(n, "ble") || nameHas(n, "blue")) return "scan / trackers";
    if (nameHas(n, "lora")) return "long range";
    if (n == "fm" || nameHas(n, "radio")) return "broadcast";
    if (nameHas(n, "ether")) return "wired";
    if (nameHas(n, "gps")) return "position";
    if (nameHas(n, "file")) return "SD / flash";
    if (nameHas(n, "script") || nameHas(n, "interp") || n == "js") return "scripts";
    if (nameHas(n, "clock")) return "time";
    if (nameHas(n, "other")) return "tools";
    if (nameHas(n, "module")) return "hardware";
    if (nameHas(n, "config") || nameHas(n, "setting")) return "settings";
    if (nameHas(n, "connect")) return "link";
    if (n == "ir" || nameHas(n, "infra")) return "remotes";
    if (nameHas(n, "watch") || nameHas(n, "veille")) return "sentinel";
    if (n == "rf" || nameHas(n, "sub")) return "433 / 315 / 868";
    return "";
}
